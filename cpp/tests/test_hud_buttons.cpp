#include "HudWindow.h"
#include "DashboardPanel.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <cstdio>
#include <memory>

namespace {

class FakeNotifier : public Notifier {
public:
    int count = 0;
    QList<Alert> alerts;
    bool send(const Alert &alert) override {
        ++count;
        alerts.append(alert);
        return true;
    }
};

} // namespace

static DisplaySnapshot makeSnapshot(const char *app,
                                    const char *pid,
                                    const char *cpu,
                                    const char *mem,
                                    const char *status) {
    DisplaySnapshot s;
    s.appName = QString::fromUtf8(app);
    s.pid = QString::fromUtf8(pid);
    s.cpu = QString::fromUtf8(cpu);
    s.memory = QString::fromUtf8(mem);
    s.status = QString::fromUtf8(status);
    return s;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    HudWindow hud;

    auto fake = std::make_unique<FakeNotifier>();
    FakeNotifier *fakePtr = fake.get();
    hud.setNotifier(std::move(fake));

    int failures = 0;
    auto check = [&failures](bool condition, const char *message) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition) {
            ++failures;
        }
    };

    auto *moreButton = hud.findChild<QPushButton *>(QStringLiteral("moreButton"));
    auto *showHideButton =
        hud.findChild<QPushButton *>(QStringLiteral("showHideButton"));
    auto *compactLabel = hud.findChild<QLabel *>(QStringLiteral("compact"));
    auto *appLabel = hud.findChild<QLabel *>(QStringLiteral("appValue"));
    auto *pidLabel = hud.findChild<QLabel *>(QStringLiteral("pidValue"));
    auto *cpuLabel = hud.findChild<QLabel *>(QStringLiteral("cpuValue"));
    auto *memLabel = hud.findChild<QLabel *>(QStringLiteral("memoryValue"));
    auto *statusLabel = hud.findChild<QLabel *>(QStringLiteral("status"));

    check(moreButton != nullptr, "MORE/LESS button exists");
    check(showHideButton != nullptr, "HIDE/SHOW button exists");
    check(hud.findChild<QPushButton *>(QStringLiteral("freezeButton")) == nullptr,
          "FRZ button removed");
    check(compactLabel != nullptr, "compact summary label exists");
    check(appLabel != nullptr && pidLabel != nullptr && cpuLabel != nullptr
              && memLabel != nullptr && statusLabel != nullptr,
          "all metric labels exist");
    check(hud.findChild<QLabel *>(QStringLiteral("tick")) == nullptr,
          "temporary tick label removed");

    bool hasMemCaption = false;
    for (auto *label : hud.findChildren<QLabel *>()) {
        if (label->text() == QStringLiteral("MEM (RSS)")) {
            hasMemCaption = true;
        }
    }
    check(hasMemCaption, "metrics caption reads MEM (RSS)");
    check(compactLabel != nullptr
              && compactLabel->text().contains(QStringLiteral("MEM(RSS)")),
          "compact line labels memory as MEM(RSS)");

    if (appLabel && pidLabel && cpuLabel && memLabel && statusLabel) {
        hud.setSnapshot(makeSnapshot("alpha", "123", "17%", "8 MB", "WARN"));
        check(appLabel->text() == QStringLiteral("alpha"), "snapshot app rendered");
        check(pidLabel->text() == QStringLiteral("123"), "snapshot pid rendered");
        check(cpuLabel->text() == QStringLiteral("17%"), "snapshot cpu rendered");
        check(memLabel->text() == QStringLiteral("8 MB"), "snapshot mem rendered");
        check(statusLabel->text() == QStringLiteral("WARN"),
              "snapshot status rendered");
        check(compactLabel != nullptr
                  && compactLabel->text().contains(QStringLiteral("alpha")),
              "compact line reflects snapshot");

        QLabel *appPtrBefore = appLabel;
        hud.setSnapshot(makeSnapshot("beta", "999", "3%", "1 MB", "OK"));
        QLabel *appPtrAfter = hud.findChild<QLabel *>(QStringLiteral("appValue"));
        check(appPtrBefore == appPtrAfter,
              "widget not recreated on snapshot update");
        check(appLabel->text() == QStringLiteral("beta"),
              "snapshot update applied");
    }

    if (showHideButton && compactLabel) {
        check(showHideButton->text() == QStringLiteral("HIDE"),
              "HIDE/SHOW starts as HIDE");
        showHideButton->click();
        check(showHideButton->text() == QStringLiteral("SHOW"),
              "HIDE click -> SHOW");
        check(compactLabel->isVisibleTo(&hud), "compact summary shown");
        showHideButton->click();
        check(!compactLabel->isVisibleTo(&hud), "compact summary hidden again");
    }

    if (moreButton) {
        check(moreButton->text() == QStringLiteral("MORE"),
              "MORE/LESS starts as MORE");
        moreButton->click();
        check(moreButton->text() == QStringLiteral("LESS"),
              "MORE click -> LESS");
        moreButton->click();
    }

    auto *moreButton2 = hud.findChild<QPushButton *>(QStringLiteral("moreButton"));
    auto *panel = hud.findChild<DashboardPanel *>();
    check(panel != nullptr, "dashboard panel exists inside the overlay");
    check(!hud.isExpanded() && panel->isHidden(),
          "overlay starts collapsed with panel hidden");
    check(hud.findChild<QLabel *>(QStringLiteral("details")) == nullptr,
          "inline DIAGNOSIS panel removed");

    DisplaySnapshot withProcs = makeSnapshot("alpha", "123", "17%", "8 MB", "OK");
    withProcs.rssKb = 8192;
    withProcs.vmsKb = 9216;
    withProcs.swapKb = 0;
    withProcs.sharedKb = 1024;
    ProcessRow root;
    root.pid = 123;
    root.depth = 0;
    root.threads = 5;
    root.fds = 12;
    root.cpuPercent = 12.5;
    root.name = QStringLiteral("alpha");
    root.memory = QStringLiteral("8 MB");
    ProcessRow child;
    child.pid = 124;
    child.depth = 1;
    child.threads = 3;
    child.fds = 7;
    child.name = QStringLiteral("alpha-child");
    child.memory = QStringLiteral("4 MB");
    withProcs.processes = {root, child};
    ConnectionRow conn;
    conn.proto = QStringLiteral("tcp");
    conn.local = QStringLiteral("127.0.0.1:40014");
    conn.remote = QStringLiteral("127.0.0.1:53");
    conn.state = QStringLiteral("ESTABLISHED");
    withProcs.connections = {conn};
    withProcs.cgroupAvailable = true;
    withProcs.cgroupVersion = QStringLiteral("v2");
    withProcs.cgroupPath = QStringLiteral("/user.slice/app.scope");
    withProcs.cgroupMemUsage = 100ULL * 1024 * 1024;
    withProcs.cgroupMemLimit = 512LL * 1024 * 1024;
    withProcs.hostAvailable = true;
    withProcs.hostCpuPercent = 23.0;
    withProcs.hostMemAvailableKb = 4ULL * 1024 * 1024;
    withProcs.hostDiskFreeBytes = 100ULL * 1024 * 1024 * 1024;
    withProcs.hostNetRxPerSec = 2048.0;
    withProcs.hostNetTxPerSec = 1024.0;
    withProcs.hostLoad1 = 0.42;
    withProcs.focusThreads = 5;
    withProcs.focusFds = 12;
    withProcs.focusVolCtx = 100;
    withProcs.focusInvolCtx = 3;
    withProcs.focusOpenFiles = {QStringLiteral("/dev/null"),
                                QStringLiteral("socket:[999]")};
    withProcs.listeningPorts = {QStringLiteral("tcp 0.0.0.0:631")};
    withProcs.dependencies = {QStringLiteral("/usr/lib/libc.so.6")};
    withProcs.instrumentation = {QStringLiteral("datadog"),
                                 QStringLiteral("opentelemetry")};
    EndpointRow endpoint;
    endpoint.proto = QStringLiteral("tcp");
    endpoint.addr = QStringLiteral("0.0.0.0");
    endpoint.port = 5432;
    endpoint.exposure = QStringLiteral("public");
    endpoint.service = QStringLiteral("postgres");
    endpoint.database = true;
    withProcs.endpoints = {endpoint};
    withProcs.dbConnections = {
        QStringLiteral("127.0.0.1:5000 -> 127.0.0.1:5432 (postgres)")};
    withProcs.socketStateCounts = {qMakePair(QStringLiteral("ESTABLISHED"), 12)};
    InterfaceRow ifaceRow;
    ifaceRow.name = QStringLiteral("eth0");
    ifaceRow.rxPerSec = 2048.0;
    ifaceRow.txPerSec = 1024.0;
    withProcs.interfaces = {ifaceRow};
    hud.setSnapshot(withProcs);

    auto *dashApp = hud.findChild<QLabel *>(QStringLiteral("ddApp"));
    auto *dashCpu = hud.findChild<QLabel *>(QStringLiteral("ddCpu"));
    auto *dashMem = hud.findChild<QLabel *>(QStringLiteral("ddMem"));
    auto *dashStatus = hud.findChild<QLabel *>(QStringLiteral("ddStatus"));
    auto *dashMemDetail = hud.findChild<QLabel *>(QStringLiteral("ddMemDetail"));
    auto *dashProcs = hud.findChild<QTreeWidget *>(QStringLiteral("ddProcs"));

    check(dashApp != nullptr && dashApp->text() == QStringLiteral("alpha"),
          "panel shows app name");
    check(dashCpu != nullptr && dashCpu->text() == QStringLiteral("17%"),
          "panel shows cpu");
    check(dashMem != nullptr && dashMem->text() == QStringLiteral("8 MB"),
          "panel shows memory");
    check(dashStatus != nullptr && dashStatus->text() == QStringLiteral("OK"),
          "panel shows status");
    check(dashMemDetail != nullptr
              && dashMemDetail->text().contains(QStringLiteral("RSS")),
          "panel shows memory breakdown");
    check(dashProcs != nullptr && dashProcs->topLevelItemCount() == 1,
          "panel process tree has one root");
    check(dashProcs != nullptr && dashProcs->topLevelItem(0) != nullptr
              && dashProcs->topLevelItem(0)->childCount() == 1
              && dashProcs->topLevelItem(0)->child(0)->text(0)
                     == QStringLiteral("alpha-child"),
          "panel process tree nests child");
    check(dashProcs != nullptr && dashProcs->topLevelItem(0) != nullptr
              && dashProcs->topLevelItem(0)->text(3) == QStringLiteral("5")
              && dashProcs->topLevelItem(0)->text(4) == QStringLiteral("12"),
          "panel process tree shows threads and fds");
    check(dashProcs != nullptr && dashProcs->topLevelItem(0) != nullptr
              && dashProcs->topLevelItem(0)->text(2) == QStringLiteral("12.5%"),
          "panel process tree shows cpu percent");

    auto *focusDetail = hud.findChild<QLabel *>(QStringLiteral("ddFocusDetail"));
    check(focusDetail != nullptr
              && focusDetail->text().contains(QStringLiteral("ctx sw")),
          "panel shows focused process detail");
    auto *openFiles = hud.findChild<QLabel *>(QStringLiteral("ddOpenFiles"));
    check(openFiles != nullptr
              && openFiles->text().contains(QStringLiteral("socket:[999]")),
          "panel shows open files");

    auto *dashNet = hud.findChild<QLabel *>(QStringLiteral("ddNet"));
    check(dashNet != nullptr
              && dashNet->text().contains(QStringLiteral("ESTABLISHED")),
          "panel shows network connection");

    auto *dashListen = hud.findChild<QLabel *>(QStringLiteral("ddListen"));
    check(dashListen != nullptr
              && dashListen->text().contains(QStringLiteral("0.0.0.0:631")),
          "panel shows listening endpoints");
    auto *dashStates = hud.findChild<QLabel *>(QStringLiteral("ddStates"));
    check(dashStates != nullptr
              && dashStates->text().contains(QStringLiteral("ESTABLISHED")),
          "panel shows socket states");
    auto *dashIfaces = hud.findChild<QLabel *>(QStringLiteral("ddIfaces"));
    check(dashIfaces != nullptr
              && dashIfaces->text().contains(QStringLiteral("eth0")),
          "panel shows interfaces");

    auto *dashDeps = hud.findChild<QLabel *>(QStringLiteral("ddDeps"));
    check(dashDeps != nullptr
              && dashDeps->text().contains(QStringLiteral("libc.so.6")),
          "panel shows shared libraries");

    auto *dashEndpoints = hud.findChild<QLabel *>(QStringLiteral("ddEndpoints"));
    check(dashEndpoints != nullptr
              && dashEndpoints->text().contains(QStringLiteral("0.0.0.0:5432")),
          "panel shows endpoints with exposure");
    auto *dashInstr =
        hud.findChild<QLabel *>(QStringLiteral("ddInstrumentation"));
    check(dashInstr != nullptr
              && dashInstr->text().contains(QStringLiteral("datadog")),
          "panel shows instrumentation agents");
    auto *dashDatabases = hud.findChild<QLabel *>(QStringLiteral("ddDatabases"));
    check(dashDatabases != nullptr
              && dashDatabases->text().contains(QStringLiteral("postgres")),
          "panel shows databases");

    ServicesSnapshot services;
    services.available = true;
    services.runningCount = 2;
    services.failedCount = 1;
    UnitRow unit;
    unit.name = QStringLiteral("db.service");
    unit.active = QStringLiteral("failed");
    unit.sub = QStringLiteral("failed");
    services.units = {unit};
    ServiceChange svcChange;
    svcChange.unit = QStringLiteral("db.service");
    svcChange.summary = QStringLiteral("appeared (active/running)");
    services.changes = {svcChange};
    hud.setServicesSnapshot(services);
    auto *svcSummary =
        hud.findChild<QLabel *>(QStringLiteral("ddServicesSummary"));
    check(svcSummary != nullptr
              && svcSummary->text().contains(QStringLiteral("1 failed")),
          "panel shows services summary");
    auto *svcList = hud.findChild<QLabel *>(QStringLiteral("ddServices"));
    check(svcList != nullptr
              && svcList->text().contains(QStringLiteral("db.service")),
          "panel lists failed service");

    JournalSnapshot journal;
    journal.available = true;
    JournalEntry je;
    je.timestampUsec = 1791907430214266LL;
    je.priority = 3;
    je.comm = QStringLiteral("nginx");
    je.message = QStringLiteral("boom");
    journal.entries = {je};
    journal.errorCount = 1;
    ErrorGroup group;
    group.comm = QStringLiteral("nginx");
    group.sample = QStringLiteral("boom");
    group.count = 3;
    journal.errorGroups = {group};
    hud.setJournalSnapshot(journal);
    auto *logsSummary =
        hud.findChild<QLabel *>(QStringLiteral("ddLogsSummary"));
    check(logsSummary != nullptr
              && logsSummary->text().contains(QStringLiteral("1 errors")),
          "panel shows journal summary");
    auto *errors = hud.findChild<QLabel *>(QStringLiteral("ddErrors"));
    check(errors != nullptr && errors->text().contains(QStringLiteral("boom"))
              && errors->text().contains(QStringLiteral("x3")),
          "panel shows grouped errors");

    auto *svcChanges =
        hud.findChild<QLabel *>(QStringLiteral("ddServicesChanges"));
    check(svcChanges != nullptr
              && svcChanges->text().contains(QStringLiteral("db.service")),
          "panel shows service changes");

    auto *dashCgroup = hud.findChild<QLabel *>(QStringLiteral("ddCgroup"));
    check(dashCgroup != nullptr
              && dashCgroup->text().contains(QStringLiteral("512 MB")),
          "panel shows container memory limit");

    auto *hostCpu = hud.findChild<QLabel *>(QStringLiteral("ddHostCpu"));
    auto *hostLoad = hud.findChild<QLabel *>(QStringLiteral("ddHostLoad"));
    check(hostCpu != nullptr && hostCpu->text() == QStringLiteral("23%"),
          "panel shows host cpu");
    check(hostLoad != nullptr && hostLoad->text() == QStringLiteral("0.42"),
          "panel shows host load");

    // The tree is rebuilt every second; a user collapse must survive that.
    if (dashProcs != nullptr && dashProcs->topLevelItem(0) != nullptr) {
        dashProcs->topLevelItem(0)->setExpanded(false);
        hud.setSnapshot(withProcs);
        check(!dashProcs->topLevelItem(0)->isExpanded(),
              "collapsed process stays collapsed across refresh");
        dashProcs->topLevelItem(0)->setExpanded(true);
        hud.setSnapshot(withProcs);
        check(dashProcs->topLevelItem(0)->isExpanded(),
              "expanded process stays expanded across refresh");
    }

    DockerSnapshot dockerSnap;
    dockerSnap.state = DockerState::Ok;
    dockerSnap.runningCount = 1;
    dockerSnap.issueCount = 1;
    hud.setDockerSnapshot(dockerSnap);
    KubeSnapshot kubeSnap;
    kubeSnap.state = KubeState::Ok;
    kubeSnap.totalPods = 3;
    kubeSnap.issueCount = 2;
    KubePod kp;
    kp.ns = QStringLiteral("default");
    kp.name = QStringLiteral("web-1");
    kp.phase = QStringLiteral("Running");
    kp.restarts = 7;
    kubeSnap.pods = {kp};
    KubeDeployment kd;
    kd.ns = QStringLiteral("default");
    kd.name = QStringLiteral("nginx");
    kd.desired = 3;
    kd.available = 1;
    kubeSnap.deployments = {kd};
    KubeEvent ke;
    ke.ns = QStringLiteral("default");
    ke.involvedName = QStringLiteral("web-1");
    ke.type = QStringLiteral("Warning");
    ke.reason = QStringLiteral("BackOff");
    ke.message = QStringLiteral("Back-off restarting");
    ke.count = 4;
    kubeSnap.events = {ke};
    KubeDiagnosis diag;
    diag.ns = QStringLiteral("default");
    diag.pod = QStringLiteral("web-1");
    diag.severity = QStringLiteral("critical");
    diag.title = QStringLiteral("crash loop");
    diag.detail = QStringLiteral("container exits repeatedly");
    kubeSnap.diagnoses = {diag};
    KubeChange change;
    change.atMs = 1791907430214LL;
    change.ns = QStringLiteral("default");
    change.object = QStringLiteral("nginx");
    change.kind = QStringLiteral("Deployment");
    change.summary = QStringLiteral("rollout (gen 2)");
    kubeSnap.timeline = {change};
    hud.setKubeSnapshot(kubeSnap);
    auto *dashDocker = hud.findChild<QLabel *>(QStringLiteral("ddDocker"));
    auto *dashKubeSummary =
        hud.findChild<QLabel *>(QStringLiteral("ddKubeSummary"));
    auto *dashKubePods =
        hud.findChild<QTreeWidget *>(QStringLiteral("ddKubePods"));
    auto *dashKubeEvents =
        hud.findChild<QLabel *>(QStringLiteral("ddKubeEvents"));
    check(dashDocker != nullptr
              && dashDocker->text().contains(QStringLiteral("docker:")),
          "panel shows docker");
    check(dashKubeSummary != nullptr && dashKubePods != nullptr
              && dashKubePods->topLevelItemCount() == 1
              && dashKubePods->topLevelItem(0)->text(0) == QStringLiteral("web-1"),
          "panel lists kubernetes pod");
    check(dashKubeEvents != nullptr
              && dashKubeEvents->text().contains(QStringLiteral("Back-off")),
          "panel lists warning events");
    auto *dashKubeDiagnosis =
        hud.findChild<QLabel *>(QStringLiteral("ddKubeDiagnosis"));
    check(dashKubeDiagnosis != nullptr
              && dashKubeDiagnosis->text().contains(QStringLiteral("crash loop")),
          "panel shows pod diagnosis");
    auto *dashKubeTimeline =
        hud.findChild<QLabel *>(QStringLiteral("ddKubeTimeline"));
    check(dashKubeTimeline != nullptr
              && dashKubeTimeline->text().contains(QStringLiteral("rollout")),
          "panel shows change timeline");

    DisplaySnapshot hot = makeSnapshot("hot", "9", "95%", "10 MB", "WARN");
    hot.cpuValid = true;
    hot.cpuPercent = 95.0;
    hud.setSnapshot(hot);
    hud.setSnapshot(hot);
    hud.setSnapshot(hot);
    check(fakePtr->count >= 1, "notification fired for a breached rule");
    auto *dashAlerts = hud.findChild<QLabel *>(QStringLiteral("ddAlerts"));
    check(dashAlerts != nullptr
              && dashAlerts->text().contains(QStringLiteral("CPU")),
          "panel shows active alert");

    SyntheticSnapshot synthetic;
    ProbeResult probe;
    probe.url = QStringLiteral("https://example.com");
    probe.success = true;
    probe.status = 200;
    probe.totalMs = 12.0;
    synthetic.results = {probe};
    synthetic.upCount = 1;
    hud.setSyntheticSnapshot(synthetic);
    auto *dashSynthetic = hud.findChild<QLabel *>(QStringLiteral("ddSynthetic"));
    check(dashSynthetic != nullptr
              && dashSynthetic->text().contains(QStringLiteral("example.com")),
          "panel shows synthetic check");

    if (moreButton2 && panel) {
        moreButton2->click();
        check(hud.isExpanded() && !panel->isHidden(),
              "MORE expands the overlay in place");
        check(moreButton2->text() == QStringLiteral("LESS"), "MORE -> LESS");

        auto *rail = hud.findChild<QListWidget *>(QStringLiteral("ddRail"));
        if (rail != nullptr) {
            constexpr int kRailCount = 27;
            bool allPagesReachable = rail->count() == kRailCount;
            const int settingsRow = kRailCount - 1;
            rail->setCurrentRow(settingsRow);
            const auto *stack =
                hud.findChild<QStackedWidget *>(QStringLiteral("ddViews"));
            allPagesReachable = allPagesReachable && stack != nullptr
                && stack->currentIndex() == settingsRow;
            auto *software =
                hud.findChild<QLabel *>(QStringLiteral("ddSettingsInfo"));
            allPagesReachable = allPagesReachable && software != nullptr
                && software->text().contains(QStringLiteral("version"));
            rail->setCurrentRow(0);
            check(allPagesReachable,
                  "rail switches pages and Settings view carries info");
        } else {
            check(false, "rail exists");
        }

        SettingsInfo info;
        info.version = QStringLiteral("9.9.9-test");
        info.configPath = QStringLiteral("/tmp/syspeek.conf");
        info.configError = QStringLiteral("bad key");
        info.logPath = QStringLiteral("/tmp/hud.log");
        info.autostartLine = QStringLiteral("on");
        hud.setSettingsInfo(info);
        auto *softwareInfo =
            hud.findChild<QLabel *>(QStringLiteral("ddSettingsInfo"));
        check(softwareInfo != nullptr
                  && softwareInfo->text().contains(QStringLiteral("9.9.9-test"))
                  && softwareInfo->text().contains(QStringLiteral("bad key")),
              "settings info applied to the SOFTWARE card");

        const QList<Alert> richAlerts = {Alert{
            QStringLiteral("id-1"), QStringLiteral("host cpu"),
            QStringLiteral("95%"), QStringLiteral("high"), 95.0, 0}};
        panel->setAlerts(richAlerts);
        auto *alertsText = hud.findChild<QLabel *>(QStringLiteral("ddAlerts"));
        check(alertsText != nullptr
                  && alertsText->text().contains(QStringLiteral("#d1685f")),
              "alert severity marker colorized");


        QString generalText;
        QList<QPushButton *> exportButtons;
        for (QLabel *label :
             hud.findChildren<QLabel *>(QStringLiteral("ddSettingsInfo"))) {
            if (label->text().contains(QStringLiteral("export&nbsp;"))) {
                generalText = label->text();
            }
        }
        for (QPushButton *button : hud.findChildren<QPushButton *>()) {
            if (button->text().startsWith(QStringLiteral("EXPORT"))) {
                exportButtons.append(button);
            }
        }
        check(generalText.contains(QStringLiteral("export&nbsp;")),
              "settings GENERAL card carries the export row");
        check(exportButtons.size() == 2,
              "settings DIAGNOSTICS card carries both export buttons");

        auto exportPathFrom = [](const QString &text) {
            const int arrow = text.lastIndexOf(QStringLiteral("-> "));
            if (arrow < 0) {
                return QString();
            }
            const int stop = text.indexOf(QLatin1Char('<'), arrow);
            return text.mid(arrow + 3,
                            stop < 0 ? -1 : stop - arrow - 3).trimmed();
        };

        for (QPushButton *button : exportButtons) {
            const QString format =
                button->text().endsWith(QStringLiteral("JSON"))
                    ? QStringLiteral("json")
                    : QStringLiteral("csv");
            button->click();
            QString updated;
            for (QLabel *label : hud.findChildren<QLabel *>(
                     QStringLiteral("ddSettingsInfo"))) {
                if (label->text().contains(QStringLiteral("export&nbsp;"))) {
                    updated = label->text();
                }
            }
            const QString path = exportPathFrom(updated);
            QFile history(path);
            const bool opened =
                !path.isEmpty() && path.endsWith(QLatin1Char('.') + format)
                && history.open(QIODevice::ReadOnly);
            const QByteArray body = opened ? history.readAll() : QByteArray();
            if (opened) {
                history.close();
            }
            if (format == QStringLiteral("csv")) {
                const QString text = QString::fromUtf8(body);
                check(opened && text.startsWith(
                                    QStringLiteral("# hostname;interval;"
                                                   "timestamp;APP;app_pid;")),
                      "HUD export writes the CSV header");
                check(text.count(QLatin1Char('\n')) >= 2,
                      "HUD export writes one row per sampled tick");
            } else {
                const QJsonObject root =
                    QJsonDocument::fromJson(body).object();
                const QJsonObject envelope =
                    root.value(QStringLiteral("syspeek")).toObject();
                check(opened
                          && envelope.value(QStringLiteral("samples"))
                                 .toArray()
                                 .size()
                          >= 1,
                      "HUD export writes the JSON envelope");
            }
            check(QFile::remove(path), "HUD export leaves a real file behind");
        }
        moreButton2->click();
        check(!hud.isExpanded() && panel->isHidden(),
              "LESS collapses the overlay in place");
        check(moreButton2->text() == QStringLiteral("MORE"), "LESS -> MORE");
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
