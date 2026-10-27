#include "HudWindow.h"
#include "DashboardPanel.h"
#include "HistoryLog.h"
#include "TimeseriesChart.h"
#include "platform/PlatformFactory.h"

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QSlider>
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
    root.netRxPerSec = 2048.0;
    root.diskReadPerSec = 4096.0;
    root.diskWritePerSec = 1048576.0;
    root.listenPorts = {8080};
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
    withProcs.processNetAvailable = true;
    withProcs.processNetTracked = 11;
    withProcs.processNetUnattributed = 35;
    withProcs.focusNetRxPerSec = 2048.0;
    withProcs.focusNetTxPerSec = 512.0;
    ProcessNetTalker talker;
    talker.pid = 123;
    talker.sockets = 2;
    talker.name = QStringLiteral("alpha");
    talker.rxPerSec = 2048.0;
    talker.txPerSec = 512.0;
    talker.listenPorts = {8080};
    withProcs.processNetTalkers = {talker};
    withProcs.processDiskAvailable = true;
    withProcs.processDiskScanning = true;
    withProcs.processDiskTracked = 9;
    withProcs.processDiskUnreadable = 41;
    withProcs.processDiskReadPerSec = 8192.0;
    withProcs.processDiskWritePerSec = 2097152.0;
    ProcessDiskConsumer diskConsumer;
    diskConsumer.pid = 321;
    diskConsumer.name = QStringLiteral("dd");
    diskConsumer.readPerSec = 0.0;
    diskConsumer.writePerSec = 2097152.0;
    withProcs.processDiskTop = {diskConsumer};

    GpuDevice gpuDev;
    gpuDev.index = 0;
    gpuDev.vendor = QStringLiteral("intel");
    gpuDev.name = QStringLiteral("Iris Xe");
    gpuDev.driver = QStringLiteral("i915");
    gpuDev.utilPercent = 42.0;
    gpuDev.memUsed = 2ULL * 1024 * 1024 * 1024;
    gpuDev.memTotal = 8ULL * 1024 * 1024 * 1024;
    gpuDev.tempC = 61.0;
    gpuDev.clockMhz = 1100;
    GpuProcess gpuProc;
    gpuProc.pid = 4242;
    gpuProc.name = QStringLiteral("game");
    gpuProc.busyPercent = 33.0;
    gpuProc.memBytes = 512ULL * 1024 * 1024;
    gpuProc.engines = QStringLiteral("render 33%");
    withProcs.gpu.ok = true;
    withProcs.gpu.vendor = QStringLiteral("intel");
    withProcs.gpu.devices = {gpuDev};
    withProcs.gpu.processes = {gpuProc};
    withProcs.gpuClientsAvailable = true;
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
              && dashProcs->topLevelItem(0)->text(7) == QStringLiteral("5")
              && dashProcs->topLevelItem(0)->text(8) == QStringLiteral("12"),
          "panel process tree shows threads and fds");
    check(dashProcs != nullptr && dashProcs->topLevelItem(0) != nullptr
              && dashProcs->topLevelItem(0)->text(2) == QStringLiteral("12.5%"),
          "panel process tree shows cpu percent");
    check(dashProcs != nullptr && dashProcs->columnCount() == 11,
          "panel process tree has net + disk + ports columns");
    check(dashProcs != nullptr && dashProcs->topLevelItem(0) != nullptr
              && dashProcs->topLevelItem(0)->text(3) == QStringLiteral("2 KB/s")
              && dashProcs->topLevelItem(0)->text(4) == QStringLiteral("--")
              && dashProcs->topLevelItem(0)->text(10) == QStringLiteral("8080"),
          "panel process tree shows net rates and listening ports");
    check(dashProcs != nullptr && dashProcs->topLevelItem(0) != nullptr
              && dashProcs->topLevelItem(0)->text(5) == QStringLiteral("4 KB/s")
              && dashProcs->topLevelItem(0)->text(6)
                     == QStringLiteral("1.0 MB/s"),
          "panel process tree shows block i/o rates per process");

    const bool perProcDisk = systemBackend().capabilities().perProcessDisk;
    const bool perProcDiskScan =
        systemBackend().capabilities().perProcessDiskScan;
    auto *procDiskNote = hud.findChild<QLabel *>(QStringLiteral("ddProcDiskNote"));
    check(procDiskNote != nullptr
              && (perProcDisk
                      ? procDiskNote->text().contains(QStringLiteral("block i/o"))
                      : procDiskNote->text().contains(
                            QStringLiteral("not supported on this OS"))),
          "panel states the block i/o attribution surface per OS");
    auto *diskTopNote = hud.findChild<QLabel *>(QStringLiteral("ddDiskTopNote"));
    auto *diskTop = hud.findChild<QLabel *>(QStringLiteral("ddDiskTop"));
    if (perProcDiskScan) {
        check(diskTopNote != nullptr
                  && diskTopNote->text().contains(QStringLiteral("9 processes"))
                  && diskTopNote->text().contains(
                      QStringLiteral("41 other users")),
              "panel disk card states tracking honesty");
        check(diskTop != nullptr
                  && diskTop->text().contains(QStringLiteral("dd"))
                  && diskTop->text().contains(QStringLiteral("pid 321"))
                  && diskTop->text().contains(QStringLiteral("write 2.0 MB/s")),
              "panel disk card lists top block i/o consumers");
    } else {
        check(diskTopNote != nullptr
                  && diskTopNote->text().contains(
                      QStringLiteral("not supported on this OS"))
                  && diskTop != nullptr
                  && diskTop->text() == QStringLiteral("--"),
              "panel gates the top disk card on this OS");
    }


    {
        const bool gpuCapable = systemBackend().capabilities().gpu;
        const bool gpuClients = systemBackend().capabilities().gpuClients;
        const bool gpuProcessData =
            systemBackend().capabilities().gpuProcessData;
        auto *gpuUtil = hud.findChild<QLabel *>(QStringLiteral("ddGpuTileUtil"));
        auto *gpuMem = hud.findChild<QLabel *>(QStringLiteral("ddGpuTileMem"));
        auto *gpuProcs = hud.findChild<QLabel *>(QStringLiteral("ddGpuTileProcs"));
        auto *gpuSummary = hud.findChild<QLabel *>(QStringLiteral("ddGpuSummary"));
        auto *gpuDevices = hud.findChild<QLabel *>(QStringLiteral("ddGpuDevices"));
        auto *gpuNote =
            hud.findChild<QLabel *>(QStringLiteral("ddGpuProcessNote"));
        auto *gpuTable =
            hud.findChild<QTreeWidget *>(QStringLiteral("ddGpuTable"));
        check(gpuUtil != nullptr && gpuMem != nullptr && gpuProcs != nullptr
                  && gpuTable != nullptr,
              "panel builds the gpu tiles and process table");
        if (gpuCapable) {
            check(gpuUtil->text() == QStringLiteral("42.0%")
                      && gpuMem->text()
                             == QStringLiteral("2.0 GB / 8.0 GB")
                      && gpuProcs->text() == QStringLiteral("1"),
                  "panel gpu tiles show utilization, vram and process count");
            check(gpuDevices != nullptr
                      && gpuDevices->text().contains(QStringLiteral("Iris Xe"))
                      && gpuDevices->text().contains(QStringLiteral("1100 MHz")),
                  "panel gpu device card renders the device fields");
            check(gpuTable != nullptr && gpuTable->columnCount() == 5
                      && gpuTable->topLevelItemCount() == 1
                      && gpuTable->topLevelItem(0)->text(0)
                             == QStringLiteral("game")
                      && gpuTable->topLevelItem(0)->text(2)
                             == QStringLiteral("33.0"),
                  "panel gpu table lists the using processes");
            check(gpuSummary != nullptr
                      && gpuSummary->text().contains(
                             QStringLiteral("1 process using the gpu")),
                  "panel gpu summary states the process count");
            check(gpuNote != nullptr
                      && (gpuProcessData
                              ? (gpuClients
                                     ? gpuNote->text().contains(
                                           QStringLiteral("fdinfo"))
                                     : gpuNote->text().contains(
                                           QStringLiteral("system gpu counters")))
                              : gpuNote->text().contains(
                                    QStringLiteral("device usage only"))),
                  "panel gpu note states the per-process surface honestly");
        } else {
            check(gpuUtil->text() == QStringLiteral("--")
                      && gpuTable->topLevelItemCount() == 0
                      && gpuSummary != nullptr
                      && gpuSummary->text().contains(
                             QStringLiteral("not supported on this OS")),
                  "panel gates the gpu page on unsupported OSes");
        }
    }

    {
        auto *railForHistory =
            hud.findChild<QListWidget *>(QStringLiteral("ddRail"));
        check(railForHistory != nullptr
                  && railForHistory->count() > 26
                  && railForHistory->item(26)->text()
                         == QStringLiteral("History")
                  && railForHistory->item(27)->text()
                         == QStringLiteral("Cost"),
              "rail inserts the History page before Cost");

        auto *histSamples =
            hud.findChild<QLabel *>(QStringLiteral("ddHistTileSamples"));
        auto *histSpan =
            hud.findChild<QLabel *>(QStringLiteral("ddHistTileSpan"));
        auto *histDisk =
            hud.findChild<QLabel *>(QStringLiteral("ddHistTileDisk"));
        auto *histInterval =
            hud.findChild<QLabel *>(QStringLiteral("ddHistTileInterval"));
        auto *histSummary =
            hud.findChild<QLabel *>(QStringLiteral("ddHistSummary"));
        auto *histPos = hud.findChild<QLabel *>(QStringLiteral("ddHistPos"));
        auto *histDetail =
            hud.findChild<QLabel *>(QStringLiteral("ddHistDetail"));
        auto detailText = [&hud]() {
            QString joined;
            for (QLabel *label : hud.findChildren<QLabel *>()) {
                if (label->objectName().startsWith(
                        QStringLiteral("ddHistDetail"))) {
                    joined += label->text() + QLatin1Char(' ');
                }
            }
            return joined;
        };
        auto *histSlider =
            hud.findChild<QSlider *>(QStringLiteral("ddHistSlider"));
        check(histSamples != nullptr && histSpan != nullptr
                  && histDisk != nullptr && histInterval != nullptr
                  && histSummary != nullptr && histPos != nullptr
                  && histDetail != nullptr && histSlider != nullptr,
              "panel builds the history tiles, card and rewind scrubber");

        QList<int> seeks;
        QObject::connect(panel, &DashboardPanel::historySeekRequested,
                         [&seeks](int index) { seeks.append(index); });

        HistoryViewInfo view;
        view.enabled = true;
        view.path = QStringLiteral("/tmp/history.ring");
        view.count = 900;
        view.capacity = 3600;
        view.bytes = 97264;
        view.spanMs = 900000;
        view.intervalMs = 1000;
        view.newestMs = 1793000900000LL;
        for (int i = 0; i < 4; ++i) {
            ExportRow row;
            row.atMs = 1793009000000LL + i * 1000;
            row.hostCpuPercent = 20.0 + i;
            row.hostMemAvailableKb = 4194304;
            view.window.append(row);
        }
        panel->setHistoryView(view);
        check(histSamples->text().contains(QStringLiteral("900"))
                  && histSamples->text().contains(QStringLiteral("/3600"))
                  && histSpan->text() == QStringLiteral("15 min")
                  && histInterval->text() == QStringLiteral("1 s"),
              "panel history tiles state samples, span and interval");
        check(!histDisk->text().isEmpty() && histDisk->text() != QStringLiteral("--"),
              "panel history tile states the on-disk footprint");
        check(histSummary->text().contains(QStringLiteral("900/3600 samples (25%)"))
                  && histSummary->text().contains(QStringLiteral("15 min"))
                  && histSummary->text().contains(QStringLiteral("history.ring")) == false,
              "panel history card summarizes the stored ring");
        check(histSlider->maximum() == 899 && histSlider->value() == 899
                  && histSlider->isEnabled(),
              "panel scrubber spans the stored window and rests on the newest");

        histSlider->setValue(10);
        check(!seeks.isEmpty() && seeks.last() == 10,
              "panel scrubber asks the owner for a rewound sample");

        HistoryViewInfo disabledView;
        panel->setHistoryView(disabledView);
        check(histSummary->text().contains(QStringLiteral("history: disabled"))
                  && histSamples->text() == QStringLiteral("--")
                  && !histSlider->isEnabled(),
              "panel states a disabled history honestly");

        panel->setHistoryView(view);
        panel->setHistorySelection(
            historyPositionText(10, 900, HistoryRecord{}, false),
            historyRecordText(HistoryRecord{}));
        check(histPos->text().contains(QStringLiteral("11/900"))
                  && detailText().contains(QStringLiteral("--")),
              "panel renders the selected sample position and detail");

        HistoryRecord selected;
        selected.atMs = 1793009010000LL;
        selected.appName = QStringLiteral("firefox");
        selected.pid = 4242;
        selected.hostCpuPercent = 41.5;
        selected.hostMemAvailableKb = 4194304;
        selected.tcpCurrEstab = 12;
        panel->setHistorySelection(
            historyPositionText(10, 900, selected, false),
            historyRecordText(selected));
        check(histPos->text().contains(QStringLiteral("rewound"))
                  && detailText().contains(QStringLiteral("firefox"))
                  && detailText().contains(QStringLiteral("estab 12"))
                  && !detailText().contains(QStringLiteral("%1")),
              "panel renders a rewound sample without placeholders");

        seeks.clear();
        auto *firstButton =
            hud.findChild<QPushButton *>(QStringLiteral("ddHistFirst"));
        auto *prevButton =
            hud.findChild<QPushButton *>(QStringLiteral("ddHistPrev"));
        auto *nextButton =
            hud.findChild<QPushButton *>(QStringLiteral("ddHistNext"));
        auto *lastButton =
            hud.findChild<QPushButton *>(QStringLiteral("ddHistLast"));
        check(firstButton != nullptr && prevButton != nullptr
                  && nextButton != nullptr && lastButton != nullptr,
              "panel offers first/prev/next/latest rewind controls");
        histSlider->setValue(400);
        seeks.clear();
        nextButton->click();
        check(!seeks.isEmpty() && seeks.last() == 401
                  && histSlider->value() == 401,
              "next step walks one stored sample forward");
        prevButton->click();
        check(!seeks.isEmpty() && seeks.last() == 400,
              "prev step walks one stored sample back");
        firstButton->click();
        check(!seeks.isEmpty() && seeks.last() == 0,
              "first jumps to the oldest stored sample");
        lastButton->click();
        check(!seeks.isEmpty() && seeks.last() == 899,
              "latest jumps back to the live sample");
    }

    {
        // M75: the Alerts page carries authored silence windows and the
        // monitor threshold bands ride the CPU charts.
        auto *silenceTarget =
            hud.findChild<QComboBox *>(QStringLiteral("ddSilenceTarget"));
        auto *silenceMinutes =
            hud.findChild<QSpinBox *>(QStringLiteral("ddSilenceMinutes"));
        auto *silenceAdd =
            hud.findChild<QPushButton *>(QStringLiteral("ddSilenceAdd"));
        auto *silenceExpire =
            hud.findChild<QPushButton *>(QStringLiteral("ddSilenceExpire"));
        auto *silenceSummary =
            hud.findChild<QLabel *>(QStringLiteral("ddSilenceSummary"));
        auto *silenceList =
            hud.findChild<QLabel *>(QStringLiteral("ddSilenceList"));
        check(silenceTarget != nullptr && silenceMinutes != nullptr
                  && silenceAdd != nullptr && silenceExpire != nullptr
                  && silenceSummary != nullptr && silenceList != nullptr,
              "panel builds the silences controls and card lines");

        QList<AlertRule> monitorRules = defaultAlertRules();
        for (AlertRule &rule : monitorRules) {
            if (rule.id == QStringLiteral("app_cpu")) {
                rule.threshold = 63.0;
            }
        }
        panel->setMonitors(monitorRules);
        check(silenceTarget->count() == monitorRules.size()
                  && silenceTarget->findText(QStringLiteral("host_cpu")) >= 0,
              "panel silence picker lists every editable monitor");

        bool banded = false;
        for (TimeseriesChart *chart :
             hud.findChildren<TimeseriesChart *>()) {
            for (const ChartBand &band : chart->bands()) {
                if (band.value == 63.0) {
                    banded = true;
                }
            }
        }
        check(banded, "monitor threshold reaches the cpu chart as a band");

        QStringList requested;
        int requestedMinutes = 0;
        QStringList expired;
        QObject::connect(panel, &DashboardPanel::silenceRequested,
                         [&requested, &requestedMinutes](const QString &id,
                                                        int minutes) {
                             requested.append(id);
                             requestedMinutes = minutes;
                         });
        QObject::connect(panel, &DashboardPanel::silenceExpireRequested,
                         [&expired](const QString &id) { expired.append(id); });
        silenceTarget->setCurrentIndex(
            silenceTarget->findText(QStringLiteral("host_cpu")));
        silenceMinutes->setValue(45);
        silenceAdd->click();
        check(requested == QStringList{QStringLiteral("host_cpu")}
                  && requestedMinutes == 45,
              "SNOOZE asks the owner for a 45 minute host_cpu window");
        silenceExpire->click();
        check(expired == QStringList{QStringLiteral("host_cpu")},
              "EXPIRE asks the owner to close that window");

        const qint64 silenceNow = QDateTime::currentMSecsSinceEpoch();
        SilenceRule rule;
        rule.matcher = QStringLiteral("host_cpu");
        rule.startsAtMs = silenceNow - 1000;
        rule.endsAtMs = silenceNow + 600000;
        rule.author = QStringLiteral("ops");
        rule.comment = QStringLiteral("snoozed");
        panel->setSilences({rule});
        check(silenceSummary->text().contains(QStringLiteral("1 active"))
                  && silenceList->text().contains(QStringLiteral("host_cpu"))
                  && silenceList->text().contains(QStringLiteral("10m left")),
              "panel renders an authored silence with its remaining time");

        Alert silencedAlert;
        silencedAlert.id = QStringLiteral("host_cpu");
        silencedAlert.title = QStringLiteral("Host CPU high");
        silencedAlert.severity = QStringLiteral("high");
        silencedAlert.detail = QStringLiteral("93%");
        panel->setAlerts({silencedAlert});
        auto *albums =
            hud.findChild<QLabel *>(QStringLiteral("ddAlerts"));
        check(albums != nullptr
                  && albums->text().contains(QStringLiteral("[silenced")),
              "a silenced monitor keeps its breach row but notes the window");

        panel->setSilences({});
        check(silenceSummary->text().contains(QStringLiteral("none"))
                  && !silenceList->text().contains(QStringLiteral("host_cpu")),
              "an empty silence list says so instead of staying blank");
    }

    {
        // M75: HEALTHY ONLY keeps breaching rows plus their ancestors.
        auto *healthy =
            hud.findChild<QCheckBox *>(QStringLiteral("ddProcHealthy"));
        auto *filterNote =
            hud.findChild<QLabel *>(QStringLiteral("ddProcFilterNote"));
        auto *tree = hud.findChild<QTreeWidget *>(QStringLiteral("ddProcs"));
        check(healthy != nullptr && filterNote != nullptr && tree != nullptr,
              "the process page offers the HEALTHY ONLY filter");

        DisplaySnapshot filterSnapshot;
        filterSnapshot.hostAvailable = true;
        ProcessRow root;
        root.pid = 10;
        root.depth = 0;
        root.cpuPercent = 4.0;
        root.rssKb = 512.0 * 1024.0;
        root.name = QStringLiteral("root-app");
        root.memory = QStringLiteral("512 MB");
        ProcessRow busy;
        busy.pid = 11;
        busy.depth = 1;
        busy.cpuPercent = 96.0;
        busy.rssKb = 1024.0 * 1024.0;
        busy.name = QStringLiteral("busy-child");
        busy.memory = QStringLiteral("1.0 GB");
        ProcessRow quiet;
        quiet.pid = 12;
        quiet.depth = 1;
        quiet.cpuPercent = 1.0;
        quiet.rssKb = 64.0 * 1024.0;
        quiet.name = QStringLiteral("quiet-child");
        quiet.memory = QStringLiteral("64 MB");
        filterSnapshot.processes = {root, busy, quiet};
        hud.setSnapshot(filterSnapshot);
        check(tree->topLevelItemCount() == 1
                  && tree->topLevelItem(0)->childCount() == 2,
              "the unfiltered tree keeps every row");

        healthy->setChecked(true);
        check(tree->topLevelItemCount() == 1
                  && tree->topLevelItem(0)->childCount() == 1
                  && tree->topLevelItem(0)->child(0)->text(0)
                         == QStringLiteral("busy-child"),
              "HEALTHY ONLY hides healthy rows and keeps the breaching child");
        check(filterNote->text().contains(QStringLiteral("1 of 3 rows hidden")),
              "the filter states how many rows it hid");

        healthy->setChecked(false);
        check(tree->topLevelItem(0)->childCount() == 2
                  && filterNote->text().isEmpty(),
              "clearing the filter restores the full tree and the note");

        hud.setSnapshot(withProcs);
    }

    {
        // M75: a live silence mutes the outbound paths (desktop
        // notification here) while the monitor keeps evaluating.
        QTemporaryDir silenceDir;
        const QByteArray previousConfig =
            qgetenv("SYSPEEK_CONFIG");
        qputenv("SYSPEEK_CONFIG",
                silenceDir.filePath(QStringLiteral("config.ini")).toUtf8());
        const int baseCount = fakePtr->count;
        hud.addSilence(QStringLiteral("app_cpu"), 10);

        auto *silenceSummary =
            hud.findChild<QLabel *>(QStringLiteral("ddSilenceSummary"));
        check(silenceSummary != nullptr
                  && silenceSummary->text().contains(QStringLiteral("1 active")),
              "a snooze from the page reaches the silences card");

        DisplaySnapshot hot = withProcs;
        hot.cpuPercent = 99.0;
        hot.cpuValid = true;
        for (int i = 0; i < 4; ++i) {
            hud.setSnapshot(hot);
        }
        check(fakePtr->count == baseCount,
              "a silenced monitor raises no desktop notification");

        hud.expireSilence(QStringLiteral("app_cpu"));
        hud.setSnapshot(withProcs);
        for (int i = 0; i < 4; ++i) {
            hud.setSnapshot(hot);
        }
        check(fakePtr->count == baseCount + 1,
              "the same monitor notifies again once the window is closed");

        const QString written = silenceDir.filePath(
            QStringLiteral("config.ini"));
        QFile silenceFile(written);
        const bool saved = silenceFile.open(QIODevice::ReadOnly);
        const QString savedText = saved
            ? QString::fromUtf8(silenceFile.readAll()) : QString();
        check(savedText.contains(QStringLiteral("app_cpu_until")),
              "an authored silence persists to the config file");
        if (previousConfig.isEmpty()) {
            qunsetenv("SYSPEEK_CONFIG");
        } else {
            qputenv("SYSPEEK_CONFIG", previousConfig);
        }
        hud.setSnapshot(withProcs);
    }

    auto *focusDetail = hud.findChild<QLabel *>(QStringLiteral("ddFocusDetail"));
    check(focusDetail != nullptr
              && focusDetail->text().contains(QStringLiteral("ctx sw")),
          "panel shows focused process detail");
    const bool perProcNet = systemBackend().capabilities().perProcessNet;
    check(focusDetail != nullptr
              && (perProcNet
                      ? focusDetail->text().contains(
                            QStringLiteral("net tcp  2 KB/s down / 512 B/s up"))
                      : focusDetail->text().contains(
                            QStringLiteral("net tcp  not supported on this OS"))),
          "panel focus detail carries the app tcp rate");
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

    auto *netNote = hud.findChild<QLabel *>(QStringLiteral("ddProcNetNote"));
    auto *talkers = hud.findChild<QLabel *>(QStringLiteral("ddTalkers"));
    auto *talkerSummary =
        hud.findChild<QLabel *>(QStringLiteral("ddTalkerSummary"));
    if (perProcNet) {
        check(netNote != nullptr
                  && netNote->text().contains(QStringLiteral("11 tracked"))
                  && netNote->text().contains(
                      QStringLiteral("35 other users")),
              "panel states tcp attribution honesty");
        check(talkers != nullptr
                  && talkers->text().contains(QStringLiteral("alpha"))
                  && talkers->text().contains(QStringLiteral("rx 2 KB/s"))
                  && talkers->text().contains(QStringLiteral(":8080")),
              "panel lists top talkers with their ports");
        check(talkerSummary != nullptr
                  && talkerSummary->text().contains(
                      QStringLiteral("tcp sockets")),
              "panel talker card carries the socket summary");
    } else {
        check(netNote != nullptr
                  && netNote->text().contains(
                      QStringLiteral("not supported on this OS")),
              "panel gates the net columns on this OS");
        check(talkers != nullptr
                  && talkers->text().contains(
                      QStringLiteral("not supported on this OS")),
              "panel talker card is gated instead of empty");
        check(talkerSummary != nullptr
                  && talkerSummary->text().contains(
                      QStringLiteral("not supported on this OS")),
              "panel talker summary is gated instead of empty");
    }

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
            constexpr int kRailCount = 29;
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

        {
            // M76: the GENERAL card states the live metrics endpoint, its
            // bind and whether a token guards it.
            SettingsInfo metricsInfo = info;
            metricsInfo.metricsLine =
                QStringLiteral("http://0.0.0.0:9107/metrics "
                               "(bearer token required)");
            hud.setSettingsInfo(metricsInfo);
            QString metricsText;
            for (QLabel *label :
                 hud.findChildren<QLabel *>(QStringLiteral("ddSettingsInfo"))) {
                if (label->text().contains(QStringLiteral("prometheus&nbsp;"))) {
                    metricsText = label->text();
                }
            }
            check(metricsText.contains(
                      QStringLiteral("http://0.0.0.0:9107/metrics"))
                      && metricsText.contains(
                          QStringLiteral("bearer token required")),
                  "settings GENERAL card states the metrics auth state");
            hud.setSettingsInfo(info);
        }
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
