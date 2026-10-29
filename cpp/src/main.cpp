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
#include "MetricsServer.h"
#include "MetricsEndpoint.h"
#include "DumpReport.h"
#include "HistoryLog.h"
#include "AppLog.h"
#include "ProbeRun.h"
#include "ExportRun.h"
#include "CloudPricing.h"
#include "DashboardPanel.h"
#include "TlsSource.h"
#include "FimSource.h"
#include "platform/PlatformFactory.h"

#include <QApplication>
#include <QSettings>
#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QByteArray>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMetaType>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <tlhelp32.h>
#endif

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

// Per-user instance-key suffix for the single-instance gate (Everything
// 1.4.1 grounds the model: default single instance, with the
// allow_multiple_instances setting and the -first-instance flag in its
// shipped strings).
QString instanceUserKey() {
#if defined(Q_OS_WIN)
    char name[256] = {};
    DWORD size = sizeof(name);
    if (::GetUserNameA(name, &size)) {
        return QString::fromLocal8Bit(name);
    }
    return QStringLiteral("user");
#else
    return QString::number(static_cast<qulonglong>(::getuid()));
#endif
}

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

#if defined(Q_OS_WIN)
static bool g_windowsCtrlHandlerInstalled = false;

// Windows console control handler: Ctrl+C, Ctrl+Break, and close events.
// Posts a quit to the QApplication event loop so the HUD exits cleanly
// instead of leaving the terminal hanging and requiring Task Manager.
static BOOL WINAPI windowsConsoleCtrlHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_CLOSE_EVENT ||
        dwCtrlType == CTRL_BREAK_EVENT) {
        QCoreApplication::quit();
        return TRUE;
    }
    return FALSE;
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

// Autostart state for the Settings view, expressed per installer (M52
// verbs + M50g HKCU Run + M47 LaunchAgent). Reads only: registry value /
// unit file presence.
QString settingsAutostartLine() {
#if defined(Q_OS_WIN)
    QSettings run(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft"
                       "\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    const QString value = run.value(QStringLiteral("Syspeek")).toString();
    return value.isEmpty()
        ? QStringLiteral("off - syspeek --autostart=on enables it")
        : QStringLiteral("on (HKCU Run)");
#elif defined(Q_OS_MACOS)
    const QString plist = QDir::homePath()
        + QStringLiteral("/Library/LaunchAgents/dev.syspeek.hud.plist");
    return QFile::exists(plist)
        ? QStringLiteral("on (LaunchAgent dev.syspeek.hud)")
        : QStringLiteral("off - install_macos.sh enable enables it");
#else
    const QString unit = QDir::homePath()
        + QStringLiteral("/.config/systemd/user/syspeek.service");
    return QFile::exists(unit)
        ? QStringLiteral("on (systemd user unit syspeek.service)")
        : QStringLiteral("off - install.sh enable enables it");
#endif
}

QString settingsVerbsLine() {
#if defined(Q_OS_WIN)
    return QStringLiteral("syspeek start|stop|restart|status|disable"
                          "|uninstall|logs|--dump|--probe|--record");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("install_macos.sh run|start|stop|restart|status"
                          "|enable|disable|update|uninstall|logs");
#else
    return QStringLiteral("install.sh run|start|stop|restart|status|enable"
                          "|disable|update|uninstall|logs|vers");
#endif
}

#if defined(Q_OS_WIN)
// Own executable path via the M49-grounded API
// (QueryFullProcessImageNameW); works before QApplication exists.
QString windowsSelfExePath() {
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    if (::QueryFullProcessImageNameW(::GetCurrentProcess(), 0,
                                     path, &size)) {
        return QString::fromWCharArray(path, static_cast<int>(size));
    }
    return QString();
}

// GUI-subsystem processes have no console of their own; verb output typed
// in a terminal would vanish. Attach to the calling console then re-open
// CONOUT$ for stdio. Grounded: SDK psdk wincon.h (AttachConsole,
// ATTACH_PARENT_PROCESS "Special PID for parent process for AttachConsole
// API"); SystemInformer ships the console-API family (AllocConsole/
// FreeConsole in si.strings); the Datadog Windows agent embeds CONOUT$
// plus GetStdHandle/getStdHandle - the stdout-reattach model. Skipped
// when stdout is already redirected (CI capture, pipes, files).
void attachParentConsole() {
    const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out != nullptr && out != INVALID_HANDLE_VALUE
            && ::GetConsoleMode(out, &mode)) {
        return;
    }
    if (out != nullptr && out != INVALID_HANDLE_VALUE) {
        return; // redirected to a file or pipe: leave the handles alone
    }
    if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
}

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
                              .arg(windowsSelfExePath()));
        run.sync();
        std::fprintf(stdout, "autostart: enabled (HKCU Run)\n");
        std::fflush(stdout);
    } else if (mode == QStringLiteral("off")) {
        run.remove(name);
        run.sync();
        std::fprintf(stdout, "autostart: disabled\n");
        std::fflush(stdout);
    } else {
        const QString value = run.value(name).toString();
        std::fprintf(stdout, "autostart: %s\n",
                     value.isEmpty() ? "disabled" : "enabled");
        std::fflush(stdout);
    }
}

// Windows verbs (M56): stop/disable/restart/status/uninstall. Same
// API family grounded in the kept artifact dumps (windows_exporter,
// SystemInformer, Datadog Windows agent strings: Toolhelp enumeration,
// OpenProcess/TerminateProcess, GetCurrentProcessId) and the shipped
// taskkill self-avoidance precedent (Wine taskkill.c skips
// pid == self_pid) - we never kill our own PID.
// A terminated process object is signaled the moment the kernel retires it, so
// a zero-timeout wait is the cheapest "is this really still running" test.
// Windows verb grounding: everything.exe 1.4.1.1032 x64 import table
// (KERNEL32) carries `OpenProcess`, `TerminateProcess` and
// `WaitForSingleObject` side by side - the same triple this verb uses.
bool processAlreadyExited(DWORD pid) {
    HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    const bool exited = ::WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    ::CloseHandle(process);
    return exited;
}

QList<DWORD> syspeekPidsExcludingSelf() {
    QList<DWORD> out;
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return out;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const DWORD selfId = ::GetCurrentProcessId();
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"syspeek.exe") == 0
                    && entry.th32ProcessID != selfId
                    && !processAlreadyExited(entry.th32ProcessID)) {
                out.append(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    ::CloseHandle(snapshot);
    return out;
}

int windowsStopOthers() {
    int stopped = 0;
    for (DWORD pid : syspeekPidsExcludingSelf()) {
        HANDLE process = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                                       pid);
        if (process == nullptr) {
            // An instance that exited between the snapshot and this call is
            // already stopped; anything else (another user's instance) stays.
            if (processAlreadyExited(pid)) {
                ++stopped;
            }
            continue;
        }
        if (::TerminateProcess(process, 0)) {
            ++stopped;
            // "stop" has to mean stopped: Toolhelp still lists a process until
            // the kernel retires it, so an immediate `start` used to answer
            // "already running" for an instance that was already dead.
            ::WaitForSingleObject(process, 2000);
        }
        ::CloseHandle(process);
    }
    return stopped;
}

bool windowsVerb(const QString &verb) {
    if (verb == QStringLiteral("start")) {
        const QList<DWORD> others = syspeekPidsExcludingSelf();
        if (!others.isEmpty()) {
            std::fprintf(stdout, "already running: %d instance(s)\n",
                         others.size());
            std::fflush(stdout);
            return true;
        }
        const QString exe = windowsSelfExePath();
        if (exe.isEmpty()
                || !QProcess::startDetached(exe, QStringList())) {
            std::fprintf(stderr, "start failed: could not launch %s\n",
                         qUtf8Printable(exe));
            return true;
        }
        std::fprintf(stdout, "started: HUD launched\n");
        std::fflush(stdout);
        return true;
    }
    if (verb == QStringLiteral("status")) {
        autostartCommand(QStringLiteral("status"));
        const QList<DWORD> others = syspeekPidsExcludingSelf();
        std::fprintf(stdout, "other instances: %d\n", others.size());
        std::fflush(stdout);
        return true;
    }
    if (verb == QStringLiteral("stop")
            || verb == QStringLiteral("disable")
            || verb == QStringLiteral("uninstall")
            || verb == QStringLiteral("restart")) {
        const int stopped = windowsStopOthers();
        if (verb == QStringLiteral("disable")
                || verb == QStringLiteral("uninstall")) {
            autostartCommand(QStringLiteral("off"));
        }
        if (verb == QStringLiteral("uninstall")) {
            const QString shimDir = QDir::fromNativeSeparators(
                qEnvironmentVariable("LOCALAPPDATA")
                + QStringLiteral("/Microsoft/WindowsApps"));
            QFile::remove(shimDir + QStringLiteral("/syspeek.cmd"));
            QFile::remove(shimDir + QStringLiteral("/syspeek"));
            const QString appDir = QFileInfo(
                    windowsSelfExePath()).absolutePath();
            std::fprintf(stdout,
                         "teardown done: HUD stopped, autostart removed, "
                         "shims removed\n");
            std::fflush(stdout);
            std::fprintf(stdout,
                         "the running exe locks its folder - delete it "
                         "after this command exits:\n  "
                         "Remove-Item -Recurse -Force \"%s\"\n",
                         qUtf8Printable(appDir));
            std::fflush(stdout);
        }
        if (verb == QStringLiteral("restart")) {
            const QString exe = windowsSelfExePath();
            if (!exe.isEmpty()) {
                QProcess::startDetached(exe, QStringList());
            }
        }
        std::fprintf(stdout, "stopped: %d other instance(s)\n", stopped);
        std::fflush(stdout);
        return true;
    }
    return false;
}
#endif

} // namespace

int main(int argc, char *argv[]) {
#if defined(Q_OS_WIN)
    attachParentConsole();
#endif
    QCoreApplication::setApplicationName(QStringLiteral("syspeek"));
    QCoreApplication::setOrganizationName(QStringLiteral("dev.syspeek"));
    installAppMessageLogger();

    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == QByteArrayLiteral("--dump")) {
            const QByteArray report = dumpReportJson().toUtf8();
            std::fwrite(report.constData(), 1, report.size(), stdout);
            return 0;
        }
        if (arg.startsWith(QByteArrayLiteral("--probe"))) {
            return probeRun(argc, argv);
        }
        if (arg == QByteArrayLiteral("--record")
            || arg.startsWith(QByteArrayLiteral("--record="))) {
            return exportRun(argc, argv);
        }
        if (arg == QByteArrayLiteral("--cloud-prices")
            || arg.startsWith(QByteArrayLiteral("--cloud-prices="))) {
            return cloudPriceRun(argc, argv);
        }
        if (arg == QByteArrayLiteral("logs")) {
            const int status = printAppLogTail(40) ? 0 : 2;
            if (status != 0) {
                std::printf("no hud.log yet on this user account\n");
            }
            return status;
        }
#if defined(Q_OS_WIN)
        if (arg.startsWith(QByteArrayLiteral("--autostart="))) {
            autostartCommand(QString::fromLocal8Bit(
                arg.mid(QByteArrayLiteral("--autostart=").size())));
            return 0;
        }
#endif
    }

#if defined(Q_OS_WIN)
    {
        const QString verb = argc == 2
            ? QString::fromLocal8Bit(argv[1])
            : QString();
        if (!verb.isEmpty()
                && (verb == QStringLiteral("start")
                    || verb == QStringLiteral("stop")
                    || verb == QStringLiteral("disable")
                    || verb == QStringLiteral("uninstall")
                    || verb == QStringLiteral("restart")
                    || verb == QStringLiteral("status"))) {
            if (windowsVerb(verb)) {
                return 0;
            }
        }
    }
#endif

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("xcb"));
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Syspeek"));
    QApplication::setApplicationVersion(
        QString::fromLatin1(SYSPEEK_APP_VERSION));

#if defined(Q_OS_WIN)
    if (!g_windowsCtrlHandlerInstalled) {
        SetConsoleCtrlHandler(windowsConsoleCtrlHandler, TRUE);
        g_windowsCtrlHandlerInstalled = true;
    }
#endif

    // Single-instance gate: a second HUD would stack two overlays.
    const QString instanceKey = QStringLiteral("syspeek-hud-")
        + instanceUserKey();
    QLocalSocket instanceProbe;
    instanceProbe.connectToServer(instanceKey);
    if (instanceProbe.waitForConnected(150)) {
        std::fprintf(stdout, "syspeek: HUD already running\n");
        return 0;
    }
    instanceProbe.abort();
    QLocalServer::removeServer(instanceKey);
    auto *instanceLock = new QLocalServer(&app);
    if (!instanceLock->listen(instanceKey)) {
        instanceLock->deleteLater();
    }

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

    {
        SettingsInfo settingsInfo;
        settingsInfo.version = QApplication::applicationVersion();
        settingsInfo.configPath = defaultConfigPath();
        settingsInfo.configError = configError;
        settingsInfo.logPath = appLogFilePath();
        settingsInfo.autostartLine = settingsAutostartLine();
        settingsInfo.verbsLine = settingsVerbsLine();
        settingsInfo.metricsLine =
            config.metrics.enabled
                ? metricsEndpointText(resolveMetricsEndpoint(config.metrics))
                : QStringLiteral("disabled ([metrics] enabled=false)");
        settingsInfo.webhookLine = webhookConfigLine(config.webhook);
        settingsInfo.historyLine =
            config.history.enabled
                ? QStringLiteral("%1 samples max - %2")
                      .arg(config.history.samples)
                      .arg(historyDefaultPath())
                : QStringLiteral("disabled ([history] enabled=false)");
        {
            const QString ratesPath = config.cloud.ratesPath.isEmpty()
                ? defaultCloudRatePath()
                : config.cloud.ratesPath;
            CloudRateTable rates;
            QFile ratesFile(ratesPath);
            if (ratesFile.open(QIODevice::ReadOnly)) {
                rates = parseCloudRateFile(
                    QString::fromUtf8(ratesFile.readAll()), ratesPath);
                ratesFile.close();
            }
            settingsInfo.cloudLine = cloudSettingsLine(
                config.cloud.enabled, ratesPath, rates,
                QDateTime::currentMSecsSinceEpoch());
        }
        settingsInfo.sampleIntervalMs = config.sampleIntervalMs;
        settingsInfo.cpuWarnPercent = config.health.cpuWarnPercent;
        settingsInfo.cpuHighPercent = config.health.cpuHighPercent;
        settingsInfo.rssWarnGb =
            double(config.health.rssWarnKb) / (1024.0 * 1024.0);
        hud.setSettingsInfo(settingsInfo);
        hud.setMonitors(config.alertRules.isEmpty()
                            ? defaultAlertRules()
                            : config.alertRules);
        hud.setSilences(config.silences.rules);
        hud.setWebhookConfig(config.webhook);
        hud.setActionsEnabled(config.actions.enabled);
        hud.setExportIntervalMs(config.sampleIntervalMs);
        hud.setHistoryStore(historyDefaultPath(),
                            config.history.enabled ? config.history.samples : 0);
        hud.setOverlayConfig(config.overlay, defaultConfigPath());
    }

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

    auto *metricsServer =
        config.metrics.enabled ? new MetricsServer(config.metrics, &app)
                               : nullptr;
    if (metricsServer != nullptr) {
        metricsServer->start();
        QObject::connect(worker, &SamplerWorker::snapshotReady, metricsServer,
                         &MetricsServer::setSnapshot);
    }

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
    QObject::connect(&hud, &HudWindow::containerActionRequested, docker,
                     &DockerSource::containerAction);
    QObject::connect(docker, &DockerSource::containerActionFinished, &hud,
                     [&hud](const QString &verb, const QString &id, bool ok,
                            const QString &error) {
                         const QString text =
                             ok ? QStringLiteral("docker %1 %2 ok")
                                      .arg(verb, id.left(12))
                                : QStringLiteral("docker %1 %2 failed: %3")
                                      .arg(verb, id.left(12), error);
                         hud.setContainerActionStatus(text, ok);
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
    QObject::connect(&hud, &HudWindow::serviceRestartRequested, services,
                     &ServicesSource::restartUnit);
    QObject::connect(services, &ServicesSource::unitActionFinished, &hud,
                     [&hud](const QString &verb, const QString &unit, bool ok,
                            const QString &error) {
                         const QString text =
                             ok ? QStringLiteral("%1 %2 ok").arg(verb, unit)
                                : QStringLiteral("%1 %2 failed: %3")
                                      .arg(verb, unit, error);
                         hud.setServiceActionStatus(text, ok);
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

    // Remove the single-instance named pipe so a subsequent launch (even
    // after an abnormal exit) can bind to it cleanly. On Windows the OS
    // reclaims named pipes on process exit, but an explicit removeServer
    // avoids stale-pipe warnings on the next launch.
    QLocalServer::removeServer(instanceKey);

    logLine(QStringLiteral("stopped"));

    return rc;
}
