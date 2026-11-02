#include "ServeRun.h"

#include "AppLog.h"
#include "Config.h"
#include "FleetSource.h"
#include "FleetStatus.h"
#include "FocusProviderFactory.h"
#include "FocusedWindowProvider.h"
#include "MetricsServer.h"
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
    const Config config = loadConfig(defaultConfigPath(), &configError);
    if (!configError.isEmpty()) {
        std::printf("serve: config %s: %s\n",
                    configError.toUtf8().constData(),
                    defaultConfigPath().toUtf8().constData());
    }
    if (!config.metrics.enabled && !config.fleet.share) {
        std::printf(
            "serve: nothing to serve - both [metrics] enabled and "
            "[fleet] share are off\n");
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
    std::printf("serve: ctrl+c to stop\n");
    std::fflush(stdout);

    worker->moveToThread(workerThread);
    QObject::connect(workerThread, &QThread::started, worker,
                     &SamplerWorker::start);
    QObject::connect(workerThread, &QThread::finished, worker,
                     &QObject::deleteLater);
    workerThread->start();
    fleetThread->start();

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
