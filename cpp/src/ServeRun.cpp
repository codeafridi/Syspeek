#include "ServeRun.h"

#include "AppLog.h"
#include "CloudProbe.h"
#include "Config.h"
#include "FleetSource.h"
#include "FleetStatus.h"
#include "FocusProviderFactory.h"
#include "FocusedWindowProvider.h"
#include "MetricsServer.h"
#include "RemoteSource.h"
#include "RemoteStatus.h"
#include "SamplerWorker.h"
#include "platform/PlatformFactory.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaType>
#include <QThread>
#include <QTimer>

#include <csignal>
#include <cstdio>

namespace {

volatile std::sig_atomic_t g_stop = 0;

void onSignal(int) { g_stop = 1; }

} // namespace

int serveRun(int argc, char **argv) {
    qputenv("QT_LOGGING_RULES", QByteArrayLiteral("qt.*=false"));
    QCoreApplication app(argc, argv);
    installAppMessageLogger();

    QString configError;
    Config config = loadConfig(defaultConfigPath(), &configError);
    if (!configError.isEmpty()) {
        std::printf("serve: config %s: %s\n",
                    configError.toUtf8().constData(),
                    defaultConfigPath().toUtf8().constData());
    }
    if (!config.metrics.enabled && !config.fleet.share
        && !config.remote.enabled) {
        std::printf(
            "serve: nothing to serve - [metrics] enabled, [fleet] share and "
            "[remote] enabled are all off\n");
        std::fflush(stdout);
        return 2;
    }

    Config samplerConfig = config;
    samplerConfig.sampleIntervalMs = config.sampleIntervalMs > 0
                                         ? config.sampleIntervalMs
                                         : 1000;

    auto provider = createFocusProvider(config);
    auto *worker = new SamplerWorker(provider.get(), samplerConfig);
    auto *workerThread = new QThread;

    auto *server = new MetricsServer(config.metrics, &app);
    server->start();
    const MetricsEndpoint endpoint = server->endpoint();
    const quint16 port = server->port() > 0 ? server->port()
                                            : quint16(endpoint.port);

    const FleetSelfInfo self = fleetSelfInfo(config.fleet.name);
    server->setFleetInfo(self, config.fleet.share);

    QObject::connect(worker, &SamplerWorker::snapshotReady, server,
                     &MetricsServer::setSnapshot);

    FleetPollOptions fleetOptions;
    fleetOptions.enabled = config.fleet.enabled;
    fleetOptions.share = config.fleet.share;
    fleetOptions.shareAddress = endpoint.bindAddress;
    fleetOptions.sharePort = int(port);
    fleetOptions.token = config.fleet.token.isEmpty() ? config.metrics.token
                                                      : config.fleet.token;
    fleetOptions.selfName = self.name;
    fleetOptions.hostname = self.hostname;
    fleetOptions.version = self.version;
    fleetOptions.platform = self.platform;
    fleetOptions.peers = config.fleet.peers;
    fleetOptions.pollMs = config.fleet.pollMs;
    fleetOptions.timeoutMs = config.fleet.timeoutMs;

    qRegisterMetaType<FleetStatus>("FleetStatus");
    auto *fleetThread = new QThread;
    auto *fleetRunner = new FleetRunner;
    fleetRunner->moveToThread(fleetThread);
    fleetRunner->setOptions(fleetOptions);
    QObject::connect(fleetThread, &QThread::started, fleetRunner,
                     &FleetRunner::start);
    QObject::connect(fleetThread, &QThread::finished, fleetRunner,
                     &QObject::deleteLater);
    QObject::connect(fleetRunner, &FleetRunner::fleetUpdated, server,
                     &MetricsServer::setFleetStatus);
    QObject::connect(fleetRunner, &FleetRunner::fleetUpdated, &app,
                     [](const FleetStatus &status) {
                         // One line per completed poll: the runner also
                         // announces the poll in progress, which would double
                         // every line here.
                         if (status.polling) {
                             return;
                         }
                         const qint64 now =
                             QDateTime::currentMSecsSinceEpoch();
                         std::printf("serve: %s\n",
                                     fleetSummaryText(status, now)
                                         .toUtf8().constData());
                         for (const FleetPeer &peer : status.peers) {
                             std::printf("serve:   %-24s %s\n",
                                         (peer.name.isEmpty() ? peer.address
                                                              : peer.name)
                                             .toUtf8().constData(),
                                         fleetPeerStateText(peer, now,
                                                            status.pollMs)
                                             .toUtf8().constData());
                         }
                         std::fflush(stdout);
                     });

    std::printf("serve: %s\n",
                metricsEndpointText(endpoint).toUtf8().constData());
    if (config.fleet.share) {
        std::printf("serve: sharing http://%s:%d/api/v1/snapshot\n",
                    endpoint.bindAddress.toUtf8().constData(), int(port));
    } else {
        std::printf(
            "serve: snapshot sharing off ([fleet] share=false) - /metrics "
            "only\n");
    }
    std::printf("serve: %d peer(s) from [fleet] peers, polling every %d ms\n",
                int(config.fleet.peers.size()), config.fleet.pollMs);
    // M100: the same durable id the console uses, so a headless member and a
    // console on the same box are one machine to the relay, not two.
    const RemoteIdentity remoteIdentity = remoteLoadIdentity(config.remote);
    if (!remoteIdentity.id.isEmpty()) {
        config.remote.machineId = remoteIdentity.id;
    }
    // M97: a headless member reports the same envelope the console does, only
    // without the event ring - there is no console here to collect one.
    QThread *remoteThread = nullptr;
    RemoteRunner *remoteRunner = nullptr;
    if (config.remote.enabled) {
        qRegisterMetaType<RemoteEnvelope>("RemoteEnvelope");
        qRegisterMetaType<RemoteClass>("RemoteClass");
        qRegisterMetaType<RemoteReportState>("RemoteReportState");
        RemoteReportOptions reportOptions;
        reportOptions.enabled = config.remote.enabled;
        reportOptions.url = remoteIngestUrl(config.remote.url, nullptr);
        reportOptions.token = config.remote.token;
        reportOptions.caFile = config.remote.caFile;
        reportOptions.intervalMs = config.remote.intervalMs;
        reportOptions.timeoutMs = config.remote.timeoutMs;
        reportOptions.spoolBytes =
            qint64(config.remote.spoolMb) * 1024 * 1024;
        reportOptions.spoolPath =
            reportOptions.spoolBytes > 0 ? remoteSpoolPath() : QString();
        remoteThread = new QThread;
        remoteRunner = new RemoteRunner;
        remoteRunner->moveToThread(remoteThread);
        remoteRunner->setOptions(reportOptions);
        QObject::connect(remoteThread, &QThread::started, remoteRunner,
                         &RemoteRunner::start);
        QObject::connect(remoteThread, &QThread::finished, remoteRunner,
                         &QObject::deleteLater);
        QObject::connect(
            worker, &SamplerWorker::snapshotReady, remoteRunner,
            [remoteRunner, config, self](const DisplaySnapshot &snapshot) {
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                const RemoteMachine machine =
                    remoteMachine(config.remote, self.version, self.platform,
                                  now);
                remoteRunner->setEnvelope(remoteBuildEnvelope(
                    machine, snapshot, {}, now - config.remote.intervalMs,
                    RemoteCounts{}, cloudHostUptimeSeconds()));
            });
        QObject::connect(remoteRunner, &RemoteRunner::reportSent, &app,
                         [](bool, RemoteClass, int, const QString &note) {
                             std::printf("serve: %s\n",
                                         note.toUtf8().constData());
                             std::fflush(stdout);
                         });
        std::printf("serve: reporting to %s\n",
                    reportOptions.url.toUtf8().constData());
    } else {
        std::printf("serve: reporting off ([remote] enabled=false)\n");
    }
    std::printf("serve: ctrl+c to stop\n");
    std::fflush(stdout);

    worker->moveToThread(workerThread);
    QObject::connect(workerThread, &QThread::started, worker,
                     &SamplerWorker::start);
    QObject::connect(workerThread, &QThread::finished, worker,
                     &QObject::deleteLater);
    workerThread->start();
    fleetThread->start();
    if (remoteThread != nullptr) {
        remoteThread->start();
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    auto *stopTimer = new QTimer(&app);
    stopTimer->setInterval(250);
    QObject::connect(stopTimer, &QTimer::timeout, &app, []() {
        if (g_stop != 0) {
            QCoreApplication::quit();
        }
    });
    stopTimer->start();

    const int rc = app.exec();

    QMetaObject::invokeMethod(fleetRunner, "stop", Qt::BlockingQueuedConnection);
    fleetThread->quit();
    fleetThread->wait();
    delete fleetThread;

    if (remoteRunner != nullptr) {
        QMetaObject::invokeMethod(remoteRunner, "stop",
                                  Qt::BlockingQueuedConnection);
        remoteThread->quit();
        remoteThread->wait();
        delete remoteThread;
    }

    QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
    workerThread->quit();
    workerThread->wait();
    delete workerThread;
    provider.reset();

    server->stop();
    std::printf("serve: stopped\n");
    std::fflush(stdout);
    return rc;
}
