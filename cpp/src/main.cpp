#include "HudWindow.h"

#include "Config.h"
#include "DockerSource.h"
#include "FocusProviderFactory.h"
#include "KubeSource.h"
#include "GnomePidFileProvider.h"
#include "KdeFocusService.h"
#include "SamplerWorker.h"
#include "ServicesSource.h"
#include "JournalSource.h"
#include "SyntheticSource.h"
#include "TraceSource.h"
#include "PathSource.h"
#include "StatsdSource.h"
#include "DbmSource.h"
#include "DumpReport.h"
#include "TlsSource.h"
#include "FimSource.h"
#include "platform/PlatformFactory.h"

#include <QApplication>
#include <QSettings>
#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMetaType>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <QtGlobal>

#include <csignal>
#include <cstdio>
#include <memory>
#if !defined(Q_OS_WIN)
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

int g_signalFd[2] = {-1, -1};

bool g_latencyProbe = false;
qint64 g_focusRequestedAtMs = 0;
qint64 g_guiLastTickMs = 0;
qint64 g_guiMaxLateMs = 0;

void latencyLog(const char *name, qint64 value) {
    std::fprintf(stderr, "[latency] %s=%lld\n", name,
                 static_cast<long long>(value));
}

#if !defined(Q_OS_WIN)
void writeSignalByte(int) {
    const char byte = 0;
    const ssize_t written = ::write(g_signalFd[0], &byte, sizeof(byte));
    static_cast<void>(written);
}
#endif

void logLine(const QString &message) {
    const QString path =
        QProcessEnvironment::systemEnvironment().value(
            QStringLiteral("SYSPEEK_LOG"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        file.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        file.write(" ");
        file.write(message.toUtf8());
        file.write("\n");
    }
}

#if defined(Q_OS_WIN)
// HKCU Run key autostart (SystemInformer quotes
// Software\\Microsoft\\Windows\\CurrentVersion\\Run).
void autostartCommand(const QString &mode) {
    QSettings run(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft"
                       "\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    const QString name = QStringLiteral("Syspeek");
    if (mode == QStringLiteral("on")) {
        run.setValue(name, QStringLiteral("\"%1\"")
                              .arg(QCoreApplication::applicationFilePath()));
        run.sync();
        std::fprintf(stdout, "autostart: enabled (HKCU Run)\n");
    } else if (mode == QStringLiteral("off")) {
        run.remove(name);
        run.sync();
        std::fprintf(stdout, "autostart: disabled\n");
    } else {
        const QString value = run.value(name).toString();
        std::fprintf(stdout, "autostart: %s\n",
                     value.isEmpty() ? "disabled" : "enabled");
    }
}
#endif

} // namespace

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == QByteArrayLiteral("--dump")) {
            const QByteArray report = dumpReportJson().toUtf8();
            std::fwrite(report.constData(), 1, report.size(), stdout);
            return 0;
        }
    }

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("xcb"));
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Syspeek"));

#if defined(Q_OS_WIN)
    for (const QString &arg : app.arguments()) {
        if (!arg.startsWith(QStringLiteral("--autostart="))) {
            continue;
        }
        autostartCommand(arg.mid(QStringLiteral("--autostart=").size()));
        return 0;
    }
#endif

    QString configError;
    const Config config = loadConfig(defaultConfigPath(), &configError);
    if (!configError.isEmpty()) {
        std::fprintf(stderr, "[syspeek] config: %s\n",
                     qUtf8Printable(configError));
    }
    logLine(QStringLiteral("config path=%1 error=%2")
                .arg(defaultConfigPath(), configError));

    qRegisterMetaType<DisplaySnapshot>("DisplaySnapshot");
    qRegisterMetaType<DockerSnapshot>("DockerSnapshot");
    qRegisterMetaType<KubeSnapshot>("KubeSnapshot");
    qRegisterMetaType<ServicesSnapshot>("ServicesSnapshot");
    qRegisterMetaType<JournalSnapshot>("JournalSnapshot");
    qRegisterMetaType<SyntheticSnapshot>("SyntheticSnapshot");
    qRegisterMetaType<TraceSnapshot>("TraceSnapshot");
    qRegisterMetaType<PathSnapshot>("PathSnapshot");
    qRegisterMetaType<StatsdSnapshot>("StatsdSnapshot");
    qRegisterMetaType<DbmSnapshot>("DbmSnapshot");
    qRegisterMetaType<TlsSnapshot>("TlsSnapshot");
    qRegisterMetaType<FimSnapshot>("FimSnapshot");

    g_latencyProbe = qEnvironmentVariableIsSet("SYSPEEK_LATENCY");

#if !defined(Q_OS_WIN)
    struct sigaction ignoreAction;
    ignoreAction.sa_handler = SIG_IGN;
    sigemptyset(&ignoreAction.sa_mask);
    ignoreAction.sa_flags = 0;
    ::sigaction(SIGTSTP, &ignoreAction, nullptr);
    ::sigaction(SIGTTIN, &ignoreAction, nullptr);
    ::sigaction(SIGTTOU, &ignoreAction, nullptr);

    QSocketNotifier *signalNotifier = nullptr;
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFd) == 0) {
        signalNotifier =
            new QSocketNotifier(g_signalFd[1], QSocketNotifier::Read, &app);

        QObject::connect(signalNotifier, &QSocketNotifier::activated, &app,
                         [&app]() {
                             char byte = 0;
                             const ssize_t got =
                                 ::read(g_signalFd[1], &byte, sizeof(byte));
                             static_cast<void>(got);
                             app.quit();
                         });

        struct sigaction action;
        action.sa_handler = writeSignalByte;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        ::sigaction(SIGINT, &action, nullptr);
        ::sigaction(SIGTERM, &action, nullptr);
        ::sigaction(SIGHUP, &action, nullptr);
    }
#else
    Q_UNUSED(app);
#endif

    HudWindow hud;
    hud.setSnapshot(DisplaySnapshot{});

    std::unique_ptr<FocusedWindowProvider> provider = createFocusProvider(config);
    logLine(QStringLiteral("focus provider=%1").arg(provider->name()));

    std::unique_ptr<KdeFocusService> kdeService;
    if (provider->name() == QStringLiteral("kde-dbus")) {
        kdeService = std::make_unique<KdeFocusService>();
        kdeService->registerService();
    }

    if (qEnvironmentVariableIsSet("SYSPEEK_PROBE")) {
        int pid = provider->focusedPid();
        if (pid <= 0 && provider->name() == QStringLiteral("kde-dbus")) {
            QEventLoop loop;
            QTimer::singleShot(2000, &loop, &QEventLoop::quit);
            loop.exec();
            pid = provider->focusedPid();
        }

        std::printf("provider=%s\n", qUtf8Printable(provider->name()));
        if (pid > 0) {
            std::printf("focused_pid=%d\n", pid);
            std::printf("focused_comm=%s\n",
                        qUtf8Printable(systemBackend().comm(pid)));
        } else {
            std::printf("focused_pid=none\n");
        }
        return 0;
    }

    auto *thread = new QThread;
    auto *worker = new SamplerWorker(provider.get(), config);
    worker->moveToThread(thread);
    QObject::connect(thread, &QThread::started, worker, &SamplerWorker::start);
    QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);

    quint64 lastSequence = 0;
    QObject::connect(worker, &SamplerWorker::snapshotReady, &hud,
                     [&hud, &lastSequence](const DisplaySnapshot &snapshot) {
                         if (snapshot.sequence <= lastSequence) {
                             return;
                         }
                         lastSequence = snapshot.sequence;
                         if (g_latencyProbe && g_focusRequestedAtMs > 0) {
                             latencyLog("focus_request_to_apply_ms",
                                        QDateTime::currentMSecsSinceEpoch()
                                            - g_focusRequestedAtMs);
                             g_focusRequestedAtMs = 0;
                         }
                         hud.setSnapshot(snapshot);
                     });

    thread->start();

    auto *dockerThread = new QThread;
    auto *docker = new DockerSource(config.docker);
    docker->moveToThread(dockerThread);
    QObject::connect(dockerThread, &QThread::started, docker, &DockerSource::start);
    QObject::connect(dockerThread, &QThread::finished, docker,
                     &QObject::deleteLater);
    QObject::connect(docker, &DockerSource::dockerReady, &hud,
                     [&hud](const DockerSnapshot &snapshot) {
                         logLine(dockerSummaryText(snapshot));
                         hud.setDockerSnapshot(snapshot);
                     });
    dockerThread->start();

    auto *kubeThread = new QThread;
    auto *kube = new KubeSource(config.kube, config.cost);
    kube->moveToThread(kubeThread);
    QObject::connect(kubeThread, &QThread::started, kube, &KubeSource::start);
    QObject::connect(kubeThread, &QThread::finished, kube,
                     &QObject::deleteLater);
    QObject::connect(kube, &KubeSource::kubeReady, &hud,
                     [&hud](const KubeSnapshot &snapshot) {
                         logLine(kubeSummaryText(snapshot));
                         hud.setKubeSnapshot(snapshot);
                     });
    kubeThread->start();

    auto *servicesThread = new QThread;
    auto *services = new ServicesSource();
    services->moveToThread(servicesThread);
    QObject::connect(servicesThread, &QThread::started, services,
                     &ServicesSource::start);
    QObject::connect(servicesThread, &QThread::finished, services,
                     &QObject::deleteLater);
    QObject::connect(services, &ServicesSource::servicesReady, &hud,
                     [&hud](const ServicesSnapshot &snapshot) {
                         logLine(servicesSummaryText(snapshot));
                         hud.setServicesSnapshot(snapshot);
                     });
    servicesThread->start();

    auto *journalThread = new QThread;
    auto *journal = new JournalSource();
    journal->moveToThread(journalThread);
    QObject::connect(journalThread, &QThread::started, journal,
                     &JournalSource::start);
    QObject::connect(journalThread, &QThread::finished, journal,
                     &QObject::deleteLater);
    QObject::connect(journal, &JournalSource::journalReady, &hud,
                     [&hud](const JournalSnapshot &snapshot) {
                         hud.setJournalSnapshot(snapshot);
                     });
    journalThread->start();

    auto *syntheticThread = new QThread;
    auto *synthetic = new SyntheticSource(config.synthetic);
    synthetic->moveToThread(syntheticThread);
    QObject::connect(syntheticThread, &QThread::started, synthetic,
                     &SyntheticSource::start);
    QObject::connect(syntheticThread, &QThread::finished, synthetic,
                     &QObject::deleteLater);
    QObject::connect(synthetic, &SyntheticSource::syntheticReady, &hud,
                     [&hud](const SyntheticSnapshot &snapshot) {
                         hud.setSyntheticSnapshot(snapshot);
                     });
    syntheticThread->start();

    auto *traceThread = new QThread;
    auto *trace = new TraceSource(config.traces);
    trace->moveToThread(traceThread);
    QObject::connect(traceThread, &QThread::started, trace,
                     &TraceSource::start);
    QObject::connect(traceThread, &QThread::finished, trace,
                     &QObject::deleteLater);
    QObject::connect(trace, &TraceSource::tracesReady, &hud,
                     [&hud](const TraceSnapshot &snapshot) {
                         hud.setTraceSnapshot(snapshot);
                     });
    traceThread->start();

    auto *pathThread = new QThread;
    auto *pathSource = new PathSource(config.path);
    pathSource->moveToThread(pathThread);
    QObject::connect(pathThread, &QThread::started, pathSource,
                     &PathSource::start);
    QObject::connect(pathThread, &QThread::finished, pathSource,
                     &QObject::deleteLater);
    QObject::connect(pathSource, &PathSource::pathReady, &hud,
                     [&hud](const PathSnapshot &snapshot) {
                         hud.setPathSnapshot(snapshot);
                     });
    pathThread->start();

    auto *statsdThread = new QThread;
    auto *statsd = new StatsdSource(config.dogstatsd);
    statsd->moveToThread(statsdThread);
    QObject::connect(statsdThread, &QThread::started, statsd,
                     &StatsdSource::start);
    QObject::connect(statsdThread, &QThread::finished, statsd,
                     &QObject::deleteLater);
    QObject::connect(statsd, &StatsdSource::statsdReady, &hud,
                     [&hud](const StatsdSnapshot &snapshot) {
                         hud.setStatsdSnapshot(snapshot);
                     });
    statsdThread->start();

    auto *dbmThread = new QThread;
    auto *dbm = new DbmSource(config.database);
    dbm->moveToThread(dbmThread);
    QObject::connect(dbmThread, &QThread::started, dbm, &DbmSource::start);
    QObject::connect(dbmThread, &QThread::finished, dbm,
                     &QObject::deleteLater);
    QObject::connect(dbm, &DbmSource::dbmReady, &hud,
                     [&hud](const DbmSnapshot &snapshot) {
                         hud.setDbmSnapshot(snapshot);
                     });
    dbmThread->start();

    auto *tlsThread = new QThread;
    auto *tls = new TlsSource(config.tls);
    tls->moveToThread(tlsThread);
    QObject::connect(tlsThread, &QThread::started, tls, &TlsSource::start);
    QObject::connect(tlsThread, &QThread::finished, tls,
                     &QObject::deleteLater);
    QObject::connect(tls, &TlsSource::tlsReady, &hud,
                     [&hud](const TlsSnapshot &snapshot) {
                         hud.setTlsSnapshot(snapshot);
                     });
    tlsThread->start();

    auto *fimThread = new QThread;
    auto *fim = new FimSource(config.fim);
    fim->moveToThread(fimThread);
    QObject::connect(fimThread, &QThread::started, fim, &FimSource::start);
    QObject::connect(fimThread, &QThread::finished, fim,
                     &QObject::deleteLater);
    QObject::connect(fim, &FimSource::fimReady, &hud,
                     [&hud](const FimSnapshot &snapshot) {
                         hud.setFimSnapshot(snapshot);
                     });
    fimThread->start();

    const QString pidFilePath = GnomePidFileProvider::defaultPath();
    QString lastRaw;
    auto *watcher = new QFileSystemWatcher(&app);

    auto requestSample = [&worker]() {
        if (g_latencyProbe) {
            g_focusRequestedAtMs = QDateTime::currentMSecsSinceEpoch();
        }
        QMetaObject::invokeMethod(worker, "sampleNow", Qt::QueuedConnection);
    };

    auto refreshIfChanged = [&]() {
        QFile file(pidFilePath);
        QString raw;
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            raw = QString::fromUtf8(file.readAll()).trimmed();
        }
        if (raw == lastRaw) {
            return;
        }
        lastRaw = raw;
        requestSample();
    };

    auto onChange = [&]() {
        refreshIfChanged();
        if (!pidFilePath.isEmpty() && QFileInfo::exists(pidFilePath)
            && !watcher->files().contains(pidFilePath)) {
            watcher->addPath(pidFilePath);
        }
    };

    if (provider->name() == QStringLiteral("gnome-pidfile")
        && !pidFilePath.isEmpty()) {
        const QFileInfo info(pidFilePath);
        const QString dirPath = info.absolutePath();
        if (QFileInfo::exists(dirPath)) {
            watcher->addPath(dirPath);
        }
        if (QFileInfo::exists(pidFilePath)) {
            watcher->addPath(pidFilePath);
        }

        QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, &app,
                         [&](const QString &) { onChange(); });
        QObject::connect(watcher, &QFileSystemWatcher::fileChanged, &app,
                         [&](const QString &) { onChange(); });
    }

    if (g_latencyProbe) {
        g_guiLastTickMs = QDateTime::currentMSecsSinceEpoch();
        auto *jitterTimer = new QTimer(&app);
        jitterTimer->setInterval(1000);
        QObject::connect(jitterTimer, &QTimer::timeout, &app, []() {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const qint64 late = (now - g_guiLastTickMs) - 1000;
            g_guiLastTickMs = now;
            if (late > g_guiMaxLateMs) {
                g_guiMaxLateMs = late;
            }
            latencyLog("gui_tick_late_ms", late);
            latencyLog("gui_max_late_ms", g_guiMaxLateMs);
        });
        jitterTimer->start();
    }

    hud.show();

    const int rc = app.exec();

    QMetaObject::invokeMethod(docker, "stop", Qt::BlockingQueuedConnection);
    dockerThread->quit();
    dockerThread->wait();
    delete dockerThread;

    QMetaObject::invokeMethod(kube, "stop", Qt::BlockingQueuedConnection);
    kubeThread->quit();
    kubeThread->wait();
    delete kubeThread;

    thread->quit();
    thread->wait();
    delete thread;

    logLine(QStringLiteral("stopped"));

    return rc;
}
