#include "HudWindow.h"
#include "DashboardPanel.h"
#include "HistoryLog.h"
#include "TimeseriesChart.h"
#include "platform/PlatformFactory.h"
#include "ProcessActions.h"
#include "CloudStatus.h"
#include "TickBudget.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
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
#include <QLineEdit>
#include <QMouseEvent>
#include <QListWidget>
#include <QSlider>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QThread>
#include <QTreeWidgetItem>

#include <cstdio>

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

    int failures = 0;
    auto check = [&failures](bool condition, const char *message) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
        // A crash must still show the last check that ran (CTest captures
        // stdout through a pipe, which is block-buffered by default).
        std::fflush(stdout);
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

    SensorChip cpuChip;
    cpuChip.name = QStringLiteral("coretemp");
    SensorReading pkgTemp;
    pkgTemp.chip = cpuChip.name;
    pkgTemp.key = QStringLiteral("temp1");
    pkgTemp.label = QStringLiteral("Package id 0");
    pkgTemp.kind = SensorKind::Temperature;
    pkgTemp.value = 87.0;
    pkgTemp.hasCrit = true;
    pkgTemp.crit = 100.0;
    SensorReading cpuFan;
    cpuFan.chip = cpuChip.name;
    cpuFan.key = QStringLiteral("fan1");
    cpuFan.label = QStringLiteral("cpu fan");
    cpuFan.kind = SensorKind::Fan;
    cpuFan.value = 3200.0;
    cpuChip.readings = {pkgTemp, cpuFan};
    SensorChip nvmeChip;
    nvmeChip.name = QStringLiteral("nvme");
    SensorReading nvmeTemp;
    nvmeTemp.chip = nvmeChip.name;
    nvmeTemp.key = QStringLiteral("temp1");
    nvmeTemp.label = QStringLiteral("Composite");
    nvmeTemp.kind = SensorKind::Temperature;
    nvmeTemp.value = 44.0;
    nvmeChip.readings = {nvmeTemp};
    SensorChip battChip;
    battChip.name = QStringLiteral("BAT0");
    SensorReading battPower;
    battPower.chip = battChip.name;
    battPower.key = QStringLiteral("power1");
    battPower.label = QStringLiteral("battery");
    battPower.kind = SensorKind::Power;
    battPower.value = 12.5;
    battChip.readings = {battPower};
    withProcs.sensors.chips = {cpuChip, nvmeChip, battChip};
    BatteryReading battery;
    battery.name = QStringLiteral("BAT0");
    battery.model = QStringLiteral("Primary");
    battery.manufacturer = QStringLiteral("HP");
    battery.technology = QStringLiteral("Li-ion");
    battery.state = BatteryState::Discharging;
    battery.capacityPercent = 15;
    battery.energyNowWh = 5.7;
    battery.energyFullWh = 38.1;
    battery.energyDesignWh = 41.0;
    battery.powerW = 8.2;
    battery.voltageV = 12.4;
    battery.cycleCount = 93;
    battery.timeToEmptyMin = 41;
    withProcs.sensors.batteries = {battery};
    AdapterReading adapter;
    adapter.name = QStringLiteral("ADP1");
    adapter.online = false;
    withProcs.sensors.adapters = {adapter};
    recomputeSensorTotals(withProcs.sensors);
    withProcs.sensors.available = true;
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

    // ---- M79 sensors ----
    {
        auto *hot = hud.findChild<QLabel *>(QStringLiteral("ddSensorTileHot"));
        auto *fans = hud.findChild<QLabel *>(QStringLiteral("ddSensorTileFans"));
        auto *power =
            hud.findChild<QLabel *>(QStringLiteral("ddSensorTilePower"));
        auto *batteryTile =
            hud.findChild<QLabel *>(QStringLiteral("ddSensorTileBattery"));
        auto *summary =
            hud.findChild<QLabel *>(QStringLiteral("ddSensorSummary"));
        auto *tree = hud.findChild<QTreeWidget *>(QStringLiteral("ddSensorTree"));
        auto *battSummary =
            hud.findChild<QLabel *>(QStringLiteral("ddBatterySummary"));
        auto *battDetail =
            hud.findChild<QLabel *>(QStringLiteral("ddBatteryDetail"));
        auto *adapters = hud.findChild<QLabel *>(QStringLiteral("ddAdapters"));
        check(hot != nullptr && fans != nullptr && power != nullptr
                  && batteryTile != nullptr && tree != nullptr,
              "sensors: page builds tiles and the readings tree");
        check(hot->text() == QStringLiteral("87.0 \u00b0C")
                  && hot->styleSheet().contains(QStringLiteral("#"))
                  && fans->text() == QStringLiteral("3200 rpm")
                  && power->text() == QStringLiteral("12.5 W"),
              "sensors: tiles show hottest, fan and power");
        check(batteryTile->text() == QStringLiteral("15%"),
              "sensors: battery tile shows the charge, state stays in the card");
        check(summary->text().contains(QStringLiteral("3 chips"))
                  && summary->text().contains(QStringLiteral("hottest 87.0"))
                  && summary->text().contains(QStringLiteral("fan max 3200")),
              "sensors: summary states the hwmon totals");
        check(tree->columnCount() == 4 && tree->topLevelItemCount() == 4,
              "sensors: tree lists temperature, fan and power readings");
        bool sawCritRow = false;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = tree->topLevelItem(i);
            if (item->text(1) == QStringLiteral("Package id 0")
                && item->text(3).startsWith(QStringLiteral("crit 100.0"))) {
                sawCritRow = true;
            }
        }
        check(sawCritRow, "sensors: a crit threshold renders in the limit cell");
        check(battSummary->text().contains(QStringLiteral("BAT0 15%"))
                  && battSummary->text().contains(
                      QStringLiteral("discharging"))
                  && battSummary->text().contains(QStringLiteral("HP Primary")),
              "sensors: battery summary states charge, state and identity");
        check(battDetail->text().contains(QStringLiteral("health 93%"))
                  && battDetail->text().contains(QStringLiteral("cycles     93"))
                  && battDetail->text().contains(QStringLiteral("8.20 W"))
                  && battDetail->text().contains(QStringLiteral("41m left")),
              "sensors: battery detail states health, cycles and power");
        check(adapters->text() == QStringLiteral("ac: ADP1 offline"),
              "sensors: adapter line states the AC state");

        DisplaySnapshot emptySensors = withProcs;
        emptySensors.sensors = SensorsSnapshot{};
        emptySensors.sensors.note = QStringLiteral(
            "battery: no system battery reported (desktop or VM)");
        hud.setSnapshot(emptySensors);
        check(summary->text().contains(QStringLiteral("no system battery"))
                  && hot->text() == QStringLiteral("--")
                  && tree->topLevelItemCount() == 0,
              "sensors: an empty snapshot keeps the honest note, no fake rows");

        // The low battery in the fixture would fire the battery_low monitor
        // and pollute the incident counts the silence test compares, so the
        // shared fixture goes back to "no sensor surface" afterwards.
        withProcs.sensors = SensorsSnapshot{};
        hud.setSnapshot(withProcs);
    }

    {
        auto *railForHistory =
            hud.findChild<QListWidget *>(QStringLiteral("ddRail"));
        check(railForHistory != nullptr
                  && railForHistory->count() > 27
                  && railForHistory->item(27)->text()
                         == QStringLiteral("History")
                  && railForHistory->item(28)->text()
                         == QStringLiteral("Timeline")
                  && railForHistory->item(29)->text()
                         == QStringLiteral("Cost"),
              "rail inserts Timeline between History and Cost");

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
        view.newestMs = 1799000000000LL;
        for (int i = 0; i < 4; ++i) {
            ExportRow row;
            row.atMs = 1793009000000LL + i * 1000;
            row.hostCpuPercent = 20.0 + i;
            row.hostMemAvailableKb = 4194304;
            view.window.append(row);
        }
        // publishHistory() re-reads the ring only every fifth tick; the
        // empty-window ticks must leave the stored charts alone.
        railForHistory->setCurrentRow(27);
        panel->setHistoryView(view);
        QList<QPair<TimeseriesChart *, int>> before;
        for (TimeseriesChart *chart :
             hud.findChildren<TimeseriesChart *>()) {
            before.append({chart, chart->sampleCount()});
        }
        HistoryViewInfo emptyWindow = view;
        emptyWindow.window.clear();
        emptyWindow.newestMs = 0;
        panel->setHistoryView(emptyWindow);
        bool kept = false;
        bool dropped = false;
        for (const auto &entry : before) {
            const int now = entry.first->sampleCount();
            kept = kept || (entry.second == 4 && now == 4);
            dropped = dropped || now < entry.second;
        }
        check(kept && !dropped,
              "an empty history window does not blank the stored charts");
        panel->setHistoryView(view);

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
        // M86: the event stream page and its "why did this spike" window.
        auto *tlEvents =
            hud.findChild<QLabel *>(QStringLiteral("ddTlTileEvents"));
        auto *tlErrors =
            hud.findChild<QLabel *>(QStringLiteral("ddTlTileErrors"));
        auto *tlWarnings =
            hud.findChild<QLabel *>(QStringLiteral("ddTlTileWarnings"));
        auto *tlSubjects =
            hud.findChild<QLabel *>(QStringLiteral("ddTlTileSubjects"));
        auto *tlSummary =
            hud.findChild<QLabel *>(QStringLiteral("ddTlSummary"));
        auto *tlWindow = hud.findChild<QLabel *>(QStringLiteral("ddTlWindow"));
        auto *tlEmpty = hud.findChild<QLabel *>(QStringLiteral("ddTlEmpty"));
        auto *tlDetail = hud.findChild<QLabel *>(QStringLiteral("ddTlDetail"));
        auto *tlKind = hud.findChild<QComboBox *>(QStringLiteral("ddTlKind"));
        auto *tlSearch = hud.findChild<QLineEdit *>(QStringLiteral("ddTlSearch"));
        auto *tlClear = hud.findChild<QPushButton *>(QStringLiteral("ddTlClear"));
        auto *tlTable = hud.findChild<QTreeWidget *>(QStringLiteral("ddTlTable"));
        check(tlEvents != nullptr && tlErrors != nullptr && tlWarnings != nullptr
                  && tlSubjects != nullptr && tlSummary != nullptr
                  && tlWindow != nullptr && tlEmpty != nullptr
                  && tlDetail != nullptr && tlKind != nullptr
                  && tlSearch != nullptr && tlClear != nullptr
                  && tlTable != nullptr,
              "panel builds the timeline tiles, filters, table and detail");
        check(tlTable->columnCount() == 5
                  && tlTable->headerItem()->text(0) == QStringLiteral("TIME")
                  && tlTable->headerItem()->text(1) == QStringLiteral("SEV")
                  && tlTable->headerItem()->text(3) == QStringLiteral("SUBJECT"),
              "timeline table carries time, severity, kind, subject and event");

        const qint64 tlNow = 1800000000000LL;
        TimelineViewInfo tlView;
        tlView.enabled = true;
        tlView.markers = true;
        tlView.capacity = 2000;
        tlView.count = 3;
        auto tlMake = [](qint64 atMs, TimelineKind kind,
                         TimelineSeverity severity, const QString &subject,
                         const QString &title) {
            TimelineEvent event;
            event.atMs = atMs;
            event.kind = kind;
            event.severity = severity;
            event.subject = subject;
            event.title = title;
            return event;
        };
        tlView.events = {
            tlMake(tlNow - 3000, TimelineKind::Kubernetes,
                   TimelineSeverity::Info, QStringLiteral("kube:web/pod-1"),
                   QStringLiteral("pod restarted")),
            tlMake(tlNow - 2000, TimelineKind::FileChange,
                   TimelineSeverity::Warning, QStringLiteral("file:/etc/passwd"),
                   QStringLiteral("modified /etc/passwd")),
            tlMake(tlNow - 1000, TimelineKind::Alert, TimelineSeverity::Error,
                   QStringLiteral("monitor:app_cpu"),
                   QStringLiteral("Focused app CPU high"))};
        tlView.oldestMs = tlNow - 3000;
        tlView.newestMs = tlNow - 1000;
        panel->showTimeline();
        panel->setTimeline(tlView);
        check(tlEvents->text() == QStringLiteral("3")
                  && tlErrors->text() == QStringLiteral("1")
                  && tlWarnings->text() == QStringLiteral("1")
                  && tlSubjects->text() == QStringLiteral("3"),
              "timeline tiles count events, errors, warnings and subjects");
        check(tlSummary->text().contains(QStringLiteral("3 events"))
                  && tlSummary->text().contains(QStringLiteral("ring 2,000")),
              "timeline summary states the ring and the severity split");
        check(tlTable->topLevelItemCount() == 3
                  && tlTable->topLevelItem(0)->text(2) == QStringLiteral("alert")
                  && tlTable->topLevelItem(0)->text(1).contains(
                         QStringLiteral("error")),
              "timeline rows are newest first with severity and kind");
        check(tlEmpty->isHidden(),
              "timeline hides the empty state once it has rows");
        check(tlDetail->text().contains(QStringLiteral("monitor:app_cpu"))
                  && tlDetail->text().contains(
                         QStringLiteral("Focused app CPU high")),
              "timeline detail follows the selected row");

        tlKind->setCurrentIndex(tlKind->findData(QStringLiteral("file")));
        check(tlTable->topLevelItemCount() == 1
                  && tlTable->topLevelItem(0)->text(2)
                         == QStringLiteral("file"),
              "the kind picker narrows the stream to one kind");
        tlKind->setCurrentIndex(0);
        tlSearch->setText(QStringLiteral("POD"));
        check(tlTable->topLevelItemCount() == 1
                  && tlTable->topLevelItem(0)->text(2)
                         == QStringLiteral("kube"),
              "the search box narrows the stream by subject or title");
        tlSearch->clear();
        check(tlTable->topLevelItemCount() == 3,
              "clearing the search restores the whole stream");

        TimelineViewInfo tlFocus = tlView;
        tlFocus.focus = true;
        tlFocus.focusMs = tlNow - 1000;
        tlFocus.focusWindowMs = 1000;
        tlFocus.focusEvents = timelineNear(tlView.events, tlFocus.focusMs,
                                           1000, 50);
        panel->setTimeline(tlFocus);
        check(!tlWindow->isHidden()
                  && tlWindow->text().contains(QStringLiteral("+-1s"))
                  && !tlClear->isHidden(),
              "a focused window states its radius and offers a clear");
        check(tlTable->topLevelItemCount() == 2,
              "a focused window lists only the events inside it");

        QList<qint64> cleared;
        QObject::connect(panel, &DashboardPanel::timelineFocusCleared,
                         [&cleared]() { cleared.append(1); });
        tlClear->click();
        check(!cleared.isEmpty(),
              "clearing the window asks the owner to drop the focus");

        panel->setTimeline(tlView);
        check(tlWindow->isHidden() && tlClear->isHidden(),
              "leaving the window hides its chrome again");

        TimelineViewInfo tlDisabled;
        tlDisabled.enabled = false;
        panel->setTimeline(tlDisabled);
        check(!tlEmpty->isHidden()
                  && tlEmpty->text().contains(QStringLiteral("[timeline]"))
                  && tlSummary->text().contains(
                         QStringLiteral("disabled in [timeline]")),
              "a disabled stream says so on the page and in the summary");

        panel->setTimeline(tlView);

        // Markers: the same ring reaches the live charts as annotation ticks.
        int markedCharts = 0;
        for (TimeseriesChart *chart : hud.findChildren<TimeseriesChart *>()) {
            if (chart->eventCount() > 0) {
                ++markedCharts;
            }
        }
        check(markedCharts > 0,
              "the event ring reaches the charts as annotation ticks");

        TimelineViewInfo tlNoMarkers = tlView;
        tlNoMarkers.markers = false;
        panel->setTimeline(tlNoMarkers);
        int stillMarked = 0;
        for (TimeseriesChart *chart : hud.findChildren<TimeseriesChart *>()) {
            stillMarked += chart->eventCount() > 0 ? 1 : 0;
        }
        check(stillMarked == 0 && markedCharts > 0,
              "markers=false clears the ticks the charts were carrying");

        panel->setTimeline(tlView);

    }

    {
        // The Settings page names the stream's config next to the other
        // collection rows, so "why is nothing here" has an answer on the page.
        DashboardPanel settingsPanel;
        SettingsInfo info;
        info.timelineLine = QStringLiteral(
            "on - ring 2,000 events, window +-90 s, chart markers on");
        settingsPanel.setSettingsInfo(info);
        bool sawTimelineRow = false;
        for (QLabel *label : settingsPanel.findChildren<QLabel *>(
                 QStringLiteral("ddSettingsInfo"))) {
            if (label->text().contains(QStringLiteral("timeline&nbsp;"))
                && label->text().contains(QStringLiteral("ring 2,000 events"))) {
                sawTimelineRow = true;
            }
        }
        check(sawTimelineRow,
              "settings states the timeline ring, window and marker switch");
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

        bool tempBanded = false;
        bool batteryRule = false;
        for (const AlertRule &rule : monitorRules) {
            if (rule.id == QStringLiteral("battery_low")) {
                batteryRule = !rule.above && rule.threshold == 20.0;
            }
        }
        for (TimeseriesChart *chart :
             hud.findChildren<TimeseriesChart *>()) {
            for (const ChartBand &band : chart->bands()) {
                if (band.value == 85.0 && band.above
                    && band.label.contains(QStringLiteral("Warning"))) {
                    tempBanded = true;
                }
            }
        }
        check(tempBanded && batteryRule,
              "host_temp bands the temperature chart and battery_low fires "
              "below its threshold");

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
        // M75: a live silence mutes the outbound paths (incident creation
        // here) while the monitor keeps evaluating.
        QTemporaryDir silenceDir;
        const QByteArray previousConfig =
            qgetenv("SYSPEEK_CONFIG");
        qputenv("SYSPEEK_CONFIG",
                silenceDir.filePath(QStringLiteral("config.ini")).toUtf8());
        auto openIncidents = [&hud]() {
            auto *label =
                hud.findChild<QLabel *>(QStringLiteral("ddIncidents"));
            return label == nullptr
                ? -1
                : int(label->text().count(QLatin1String("OPEN")));
        };
        const int baseOpen = openIncidents();
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
        check(openIncidents() == baseOpen,
              "a silenced monitor opens no incident");

        hud.expireSilence(QStringLiteral("app_cpu"));
        hud.setSnapshot(withProcs);
        for (int i = 0; i < 4; ++i) {
            hud.setSnapshot(hot);
        }
        check(openIncidents() == baseOpen + 1,
              "the same monitor opens an incident once the window is closed");

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

    {
        // The thresholds loaded from [alerts] must reach the engine, not just
        // the editors: a restart used to quietly revert every persisted
        // monitor to its built-in default.
        auto alertsText = [&hud]() {
            auto *label = hud.findChild<QLabel *>(QStringLiteral("ddAlerts"));
            return label == nullptr ? QString() : label->text();
        };
        QList<AlertRule> tuned = defaultAlertRules();
        for (AlertRule &rule : tuned) {
            if (rule.id == QStringLiteral("app_cpu")) {
                rule.threshold = 5.0;
                rule.sustained = 1;
            }
        }
        hud.setMonitors(tuned);
        DisplaySnapshot warm = withProcs;
        warm.cpuPercent = 12.0;
        warm.cpuValid = true;
        hud.setSnapshot(warm);
        hud.setSnapshot(warm);
        check(alertsText().contains(QStringLiteral("Focused app CPU high")),
              "monitor thresholds passed to the HUD reach the alert engine");
        hud.setMonitors(defaultAlertRules());
        for (int i = 0; i < 3; ++i) {
            hud.setSnapshot(withProcs);
        }
        check(!alertsText().contains(QStringLiteral("Focused app CPU high")),
              "restoring the default monitors clears the tuned breach");
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


    {
        // M81 Cost page: a priced cluster fills the tiles, the allocation
        // split, the right-sizing card, the history line and the share
        // tables; an unreachable cluster states the gate instead.
        KubeSnapshot cost = kubeSnap;
        cost.costMonthly = 1336.40;
        cost.costPods = 9;
        cost.costMissingRequests = 1;
        cost.costCpuPerCoreHour = 0.031611;
        cost.costMemPerGbHour = 0.004237;
        cost.costCpuMonthly = 980.0;
        cost.costMemMonthly = 356.4;
        cost.costCpuEfficiency = 0.42;
        cost.costMemEfficiency = 0.81;
        cost.costWastedMonthly = 412.35;
        cost.costEfficiencyPods = 7;
        cost.costByNamespace = {qMakePair(QStringLiteral("prod"), 1100.0),
                                qMakePair(QStringLiteral("dev"), 236.4)};
        cost.costTopWorkloads = {
            qMakePair(QStringLiteral("prod/api-7f9c"), 812.0),
            qMakePair(QStringLiteral("prod/web-5f8d"), 288.0)};
        hud.setKubeSnapshot(cost);

        auto *tileMonthly =
            hud.findChild<QLabel *>(QStringLiteral("ddCostTileMonthly"));
        auto *tileHourly =
            hud.findChild<QLabel *>(QStringLiteral("ddCostTileHourly"));
        auto *tileCpuEff =
            hud.findChild<QLabel *>(QStringLiteral("ddCostTileCpuEff"));
        auto *tileMemEff =
            hud.findChild<QLabel *>(QStringLiteral("ddCostTileMemEff"));
        auto *costSummary =
            hud.findChild<QLabel *>(QStringLiteral("ddCostSummary"));
        auto *costWarnings =
            hud.findChild<QLabel *>(QStringLiteral("ddCostWarnings"));
        auto *costNamespaces =
            hud.findChild<QTreeWidget *>(QStringLiteral("ddCostNamespaces"));
        auto *costWorkloads =
            hud.findChild<QTreeWidget *>(QStringLiteral("ddCostWorkloads"));
        auto *costChart =
            hud.findChild<TimeseriesChart *>(QStringLiteral("ddCostChart"));

        check(tileMonthly != nullptr
                  && tileMonthly->text() == QStringLiteral("$1.3K"),
              "cost: monthly tile renders compact money");
        check(tileHourly != nullptr
                  && tileHourly->text() == QStringLiteral("$1.83"),
              "cost: hourly tile divides by the month length");
        check(tileCpuEff != nullptr
                  && tileCpuEff->text() == QStringLiteral("42.0%")
                  && tileCpuEff->styleSheet().contains(QStringLiteral("#c9a24a")),
              "cost: cpu efficiency warns below 60%");
        check(tileMemEff != nullptr
                  && tileMemEff->text() == QStringLiteral("81.0%")
                  && tileMemEff->styleSheet().contains(QStringLiteral("#4fae7d")),
              "cost: ram efficiency reads healthy");
        check(costSummary != nullptr
                  && costSummary->text().contains(QStringLiteral("$980.00"))
                  && costSummary->text().contains(QStringLiteral("73.3%"))
                  && costSummary->text().contains(QStringLiteral("/core-hr")),
              "cost: allocation card splits cpu and ram pricing");
        check(costSummary->text().contains(QStringLiteral("9 pod(s)"))
                  && costSummary->text().contains(QStringLiteral("2 namespace(s)")),
              "cost: allocation card states the priced inventory");
        check(costSummary->text().contains(
                  QStringLiteral("1 container(s) declare no cpu/memory request")),
              "cost: allocation card warns about missing requests");
        check(costWarnings != nullptr
                  && costWarnings->text().contains(QStringLiteral("42.0%"))
                  && costWarnings->text().contains(QStringLiteral("$412.35"))
                  && costWarnings->text().contains(QStringLiteral("buffer 1.2")),
              "cost: right-sizing card states efficiency, waste and advice");
        check(costNamespaces != nullptr
                  && costNamespaces->topLevelItemCount() == 2
                  && costNamespaces->topLevelItem(0)->text(0)
                         == QStringLiteral("prod")
                  && costNamespaces->topLevelItem(0)->text(2)
                         == QStringLiteral("82.3%"),
              "cost: namespace table carries each share");
        check(costWorkloads != nullptr
                  && costWorkloads->topLevelItemCount() == 2
                  && costWorkloads->topLevelItem(0)->text(1)
                         == QStringLiteral("$812.00"),
              "cost: workload table formats money");
        check(costChart != nullptr && costChart->sampleCount() >= 1,
              "cost: the estimate history is charted");

        KubeSnapshot noCost;
        noCost.state = KubeState::NotConfigured;
        hud.setKubeSnapshot(noCost);
        check(tileMonthly->text() == QStringLiteral("--")
                  && tileCpuEff->text() == QStringLiteral("--"),
              "cost: no cluster leaves the tiles at --");
        check(costSummary->text().contains(QStringLiteral("k8s: not configured")),
              "cost: the allocation card states the gate");
        check(costNamespaces->topLevelItemCount() == 0,
              "cost: the gate leaves the tables empty");
        hud.setKubeSnapshot(cost);
    }

    DisplaySnapshot hot = makeSnapshot("hot", "9", "95%", "10 MB", "WARN");
    hot.cpuValid = true;
    hot.cpuPercent = 95.0;
    hud.setSnapshot(hot);
    hud.setSnapshot(hot);
    hud.setSnapshot(hot);
    auto *breachIncidents =
        hud.findChild<QLabel *>(QStringLiteral("ddIncidents"));
    check(breachIncidents != nullptr
              && breachIncidents->text().contains(QStringLiteral("OPEN")),
          "a breached rule opens an incident");
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

    DisplaySnapshot cloudSnap =
        makeSnapshot("cursor", "508637", "214.5%", "4.19 GB", "OK");
    cloudSnap.cloud.available = true;
    cloudSnap.cloud.identity.provider = CloudProvider::Aws;
    cloudSnap.cloud.identity.instanceId =
        QStringLiteral("i-0123456789abcdef0");
    cloudSnap.cloud.identity.instanceType = QStringLiteral("c7g.2xlarge");
    cloudSnap.cloud.identity.region = QStringLiteral("eu-west-1");
    cloudSnap.cloud.identity.zone = QStringLiteral("eu-west-1b");
    cloudSnap.cloud.identity.privateIp = QStringLiteral("10.0.3.17");
    cloudSnap.cloud.vcpus = 8;
    cloudSnap.cloud.uptimeSec = 41 * 86400;
    cloudSnap.cloud.ratePerHour = 0.2920;
    cloudSnap.cloud.rateKey = QStringLiteral("aws/c7g.2xlarge");
    cloudSnap.cloud.rateSource = QStringLiteral("/tmp/rates.txt");
    cloudSnap.cloud.rateImportDate = QStringLiteral("2026-01-01");
    cloudSnap.cloud.rateStaleDays = 60;
    cloudSnap.cloud.accruedUsd = 0.2920 * 41 * 24.0;
    cloudSnap.cloud.projectedMonthlyUsd = 0.2920 * 730.0;
    cloudSnap.cloud.appName = QStringLiteral("cursor");
    cloudSnap.cloud.appPid = 508637;
    cloudSnap.cloud.appCpuPercent = 214.5;
    cloudSnap.cloud.appCostPerHour =
        cloudProcessCostPerHour(0.2920, 214.5, 8);
    cloudSnap.cloud.consumers = {
        CloudConsumer{QStringLiteral("cursor"), 508637, 214.5, 0.0783},
        CloudConsumer{QStringLiteral("gopls"), 509001, 41.0, 0.0150},
    };
    // M83: the account listing is the only source of the launch time, so the
    // age tile and the fleet table cannot be faked from local uptime.
    const qint64 fleetNow = QDateTime::currentMSecsSinceEpoch();
    cloudSnap.cloud.instanceAgeSec = 3 * 86400;
    cloudSnap.cloud.launchedAtMs = fleetNow - 3 * 86400LL * 1000LL;
    cloudSnap.cloud.accruedLaunchUsd = 0.2920 * 3 * 24.0;
    CloudFleetStatus fleetStory;
    fleetStory.attempted = true;
    fleetStory.available = true;
    fleetStory.provider = CloudProvider::Aws;
    fleetStory.cliName = QStringLiteral("aws");
    fleetStory.scope = QStringLiteral("eu-west-1");
    fleetStory.source =
        QStringLiteral("aws ec2 describe-instances --output json --region "
                       "eu-west-1");
    fleetStory.fetchedAtMs = fleetNow - 120000;
    CloudFleetInstance fleetSelf;
    fleetSelf.instanceId = QStringLiteral("i-0123456789abcdef0");
    fleetSelf.name = QStringLiteral("hud-box");
    fleetSelf.instanceType = QStringLiteral("c7g.2xlarge");
    fleetSelf.state = QStringLiteral("running");
    fleetSelf.rawState = QStringLiteral("running");
    fleetSelf.region = QStringLiteral("eu-west-1");
    fleetSelf.zone = QStringLiteral("eu-west-1b");
    fleetSelf.launchedAtMs = fleetNow - 3 * 86400LL * 1000LL;
    CloudFleetInstance fleetWeb;
    fleetWeb.instanceId = QStringLiteral("i-0feedface");
    fleetWeb.name = QStringLiteral("web-2");
    fleetWeb.instanceType = QStringLiteral("t4g.small");
    fleetWeb.state = QStringLiteral("running");
    fleetWeb.rawState = QStringLiteral("running");
    fleetWeb.launchedAtMs = fleetNow - 5 * 86400LL * 1000LL;
    CloudFleetInstance fleetOld;
    fleetOld.instanceId = QStringLiteral("i-0deadbeef");
    fleetOld.name = QStringLiteral("batch-old");
    fleetOld.instanceType = QStringLiteral("t4g.small");
    fleetOld.state = QStringLiteral("stopped");
    fleetOld.rawState = QStringLiteral("stopped");
    fleetOld.launchedAtMs = fleetNow - 12 * 86400LL * 1000LL;
    fleetStory.rows = {fleetSelf, fleetWeb, fleetOld};
    fleetApplyRates(
        fleetStory,
        parseCloudRateFile(QStringLiteral("aws/c7g.2xlarge = 0.2920\n"
                                          "aws/t4g.small = 0.0208\n")),
        730.0);
    fleetResolveAges(fleetStory, fleetNow);
    fleetMarkSelf(fleetStory, cloudSnap.cloud.identity);
    cloudSnap.cloud.fleet = fleetStory;
    // M84: the outbound footprint card. The sampler fills this from the live
    // budget + measured sweep; here it carries a measured sweep.
    cloudSnap.cloud.footprint.enabled = true;
    cloudSnap.cloud.footprint.inventory = true;
    cloudSnap.cloud.footprint.cliName = QStringLiteral("aws");
    cloudSnap.cloud.footprint.probed = true;
    cloudSnap.cloud.footprint.identified = true;
    cloudSnap.cloud.footprint.lastSweepMs = 62;
    cloudSnap.cloud.footprint.lastRequests = 4;
    cloudSnap.cloud.footprint.ratesPath = QStringLiteral("/tmp/rates.txt");
    // M85: a CE-shaped month for the render.
    cloudSnap.cloud.spend.attempted = true;
    cloudSnap.cloud.spend.available = true;
    cloudSnap.cloud.spend.provider = CloudProvider::Aws;
    cloudSnap.cloud.spend.cliName = QStringLiteral("aws");
    cloudSnap.cloud.spend.scope = QStringLiteral("account 123456789012");
    cloudSnap.cloud.spend.source = QStringLiteral(
        "aws ce get-cost-and-usage --time-period Start=2026-10-01,"
        "End=2026-10-29");
    cloudSnap.cloud.spend.periodStart = QStringLiteral("2026-10-01");
    cloudSnap.cloud.spend.periodEnd = QStringLiteral("2026-10-29");
    cloudSnap.cloud.spend.monthToDateUsd = 452.10;
    cloudSnap.cloud.spend.forecastRemainingUsd = 331.02;
    cloudSnap.cloud.spend.forecastUsd = 783.12;
    cloudSnap.cloud.spend.services = {
        CloudSpendService{
            QStringLiteral("Amazon Elastic Compute Cloud - Compute"), 380.40},
        CloudSpendService{QStringLiteral("Amazon Simple Storage Service"), 51.70},
        CloudSpendService{QStringLiteral("Amazon CloudWatch"), 20.00},
    };
    for (int day = 1; day <= 28; ++day) {
        cloudSnap.cloud.spend.days.append(
            CloudSpendDay{QStringLiteral("2026-10-%1").arg(day, 2, 10,
                                                           QLatin1Char('0')),
                          12.0 + day % 5});
    }
    hud.setSnapshot(cloudSnap);
    hud.setSnapshot(cloudSnap);

    auto *cloudProvider =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudTileProvider"));
    auto *cloudAccrued =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudTileAccrued"));
    auto *cloudMonthly =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudTileMonthly"));
    auto *cloudAge =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudTileAge"));
    auto *cloudFleet =
        hud.findChild<QTreeWidget *>(QStringLiteral("ddCloudFleet"));
    auto *cloudFleetSummary =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudFleetSummary"));
    auto *cloudFleetChart =
        hud.findChild<TimeseriesChart *>(QStringLiteral("ddCloudFleetChart"));
    auto *cloudSummary =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudSummary"));
    auto *cloudDetail =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudDetail"));
    auto *cloudCost = hud.findChild<QLabel *>(QStringLiteral("ddCloudCost"));
    auto *cloudApp = hud.findChild<QLabel *>(QStringLiteral("ddCloudApp"));
    auto *cloudShare = hud.findChild<QLabel *>(QStringLiteral("ddCloudShare"));
    auto *cloudConsumers =
        hud.findChild<QTreeWidget *>(QStringLiteral("ddCloudConsumers"));
    auto *cloudChart =
        hud.findChild<TimeseriesChart *>(QStringLiteral("ddCloudChart"));

    check(cloudProvider != nullptr
              && cloudProvider->text() == QStringLiteral("AWS")
              && cloudAccrued != nullptr
              && cloudAccrued->text() == QStringLiteral("$287.33")
              && cloudMonthly != nullptr
              && cloudMonthly->text() == QStringLiteral("$213.16")
              && cloudAge != nullptr
              && cloudAge->text() == QStringLiteral("3d 0h"),
          "cloud page tiles show provider, instance age, accrual and "
          "projection");
    check(cloudSummary != nullptr
              && cloudSummary->text().contains(QStringLiteral("c7g.2xlarge"))
              && cloudSummary->text().contains(QStringLiteral("8 vCPU")),
          "cloud page names the instance type and vCPU count");
    check(cloudDetail != nullptr
              && cloudDetail->text().contains(QStringLiteral("eu-west-1b"))
              && cloudDetail->text().contains(QStringLiteral("10.0.3.17")),
          "cloud page renders the identity detail card");
    check(cloudCost != nullptr
              && cloudCost->text().contains(QStringLiteral("this boot"))
              && cloudCost->text().contains(
                  QStringLiteral("since launch (upper bound)"))
              && cloudCost->text().contains(QStringLiteral("60 days old")),
          "cloud cost card splits launch-age accrual from this boot and flags "
          "a stale price");
    check(cloudDetail != nullptr
              && cloudDetail->text().contains(QStringLiteral("age        3d 0h"))
              && cloudDetail->text().contains(QStringLiteral("launched "))
              && cloudDetail->text().contains(QStringLiteral("UTC")),
          "cloud detail card states the provider launch time");
    check(cloudFleet != nullptr && cloudFleet->topLevelItemCount() == 3
              && cloudFleet->topLevelItem(0)->text(0)
                     == QStringLiteral("* hud-box")
              && cloudFleet->topLevelItem(0)->text(1)
                     == QStringLiteral("c7g.2xlarge")
              && cloudFleet->topLevelItem(0)->text(2)
                     == QStringLiteral("running")
              && cloudFleet->topLevelItem(0)->text(3)
                     == QStringLiteral("3d 0h")
              && cloudFleet->topLevelItem(0)->text(4)
                     == QStringLiteral("$213.16"),
          "cloud fleet table leads with this instance, its age and its monthly "
          "rate");
    check(cloudFleet->topLevelItem(1)->text(0) == QStringLiteral("web-2")
              && cloudFleet->topLevelItem(1)->text(4)
                     == QStringLiteral("$15.18")
              && cloudFleet->topLevelItem(2)->text(0)
                     == QStringLiteral("batch-old")
              && cloudFleet->topLevelItem(2)->text(4) == QStringLiteral("--"),
          "cloud fleet table prices running rows only and dashes a stopped one");
    check(cloudFleetSummary != nullptr
              && cloudFleetSummary->text().contains(
                  QStringLiteral("AWS fleet"))
              && cloudFleetSummary->text().contains(
                  QStringLiteral("3 instances"))
              && cloudFleetSummary->text().contains(
                  QStringLiteral("2 running"))
              && cloudFleetSummary->text().contains(
                  QStringLiteral("$228.34/month"))
              && cloudFleetSummary->text().contains(
                  QStringLiteral("self      hud-box")),
          "cloud fleet summary counts the listing, the running rows and the "
          "self row");
    check(cloudFleetChart != nullptr && cloudFleetChart->sampleCount() == 2,
          "cloud fleet chart follows the running count per tick");
    auto *cloudFleetNote =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudFleetNote"));
    check(cloudFleetNote != nullptr
              && cloudFleetNote->text().contains(
                  QStringLiteral("* this instance"))
              && cloudFleetNote->text().contains(
                  QStringLiteral("running rows only")),
          "cloud fleet note explains the star, the age source and the sum");
    auto *cloudFootprint =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudFootprint"));
    check(cloudFootprint != nullptr
              && cloudFootprint->text().contains(
                  QStringLiteral("169.254.169.254"))
              && cloudFootprint->text().contains(
                  QStringLiteral("every 5 min"))
              && cloudFootprint->text().contains(
                  QStringLiteral("last 62 ms / 4 request(s)"))
              && cloudFootprint->text().contains(
                  QStringLiteral("aws every 10 min"))
              && cloudFootprint->text().contains(
                  QStringLiteral("no credential is read or stored"))
              && cloudFootprint->text().contains(QStringLiteral("/tmp/rates.txt")),
          "cloud footprint card states every outbound action, cadence and "
          "guarantee");
    // M85: billed spend. The fixture carries a real CE-shaped fold: $452.10
    // billed, +$331.02 forecast for the rest of the month.
    auto *cloudSpendSummary =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudSpendSummary"));
    auto *cloudSpendDetail =
        hud.findChild<QLabel *>(QStringLiteral("ddCloudSpendDetail"));
    auto *cloudSpendServices =
        hud.findChild<QTreeWidget *>(QStringLiteral("ddCloudSpendServices"));
    auto *cloudSpendChart =
        hud.findChild<TimeseriesChart *>(QStringLiteral("ddCloudSpendChart"));
    check(cloudSpendSummary != nullptr
              && cloudSpendSummary->text().contains(
                  QStringLiteral("month to date $452.10"))
              && cloudSpendSummary->text().contains(
                  QStringLiteral("month end $783.12"))
              && cloudSpendSummary->text().contains(
                  QStringLiteral("(+$331.02 forecast)")),
          "cloud spend card states billed, month end and the forecast");
    check(cloudSpendDetail != nullptr
              && cloudSpendDetail->text().contains(
                  QStringLiteral("2026-10-01 .. 2026-10-29"))
              && cloudSpendDetail->text().contains(
                  QStringLiteral("unblended cost")),
          "cloud spend detail names the window and the metric");
    check(cloudSpendServices != nullptr && cloudSpendServices->topLevelItemCount() >= 2
              && cloudSpendServices->topLevelItem(0)->text(0)
                     == QStringLiteral("Amazon Elastic Compute Cloud - Compute")
              && cloudSpendServices->topLevelItem(0)->text(1)
                     .contains(QStringLiteral("$")),
          "cloud spend table lists services with their share");
    check(cloudSpendChart != nullptr && cloudSpendChart->sampleCount() > 1,
          "cloud spend chart carries the daily series");
    check(cloudApp != nullptr
              && cloudApp->text().contains(QStringLiteral("cursor"))
              && cloudApp->text().contains(QStringLiteral("214.5%")),
          "cloud app card shows the focused process share");
    check(cloudConsumers != nullptr
              && cloudConsumers->topLevelItemCount() == 2
              && cloudConsumers->topLevelItem(0)->text(0)
                     == QStringLiteral("cursor")
              && cloudConsumers->topLevelItem(0)->text(1)
                     == QStringLiteral("508637")
              && cloudConsumers->topLevelItem(0)->text(3)
                     == QStringLiteral("$0.0783"),
          "cloud consumer table lists the focused app processes");
    check(cloudShare != nullptr
              && cloudShare->text().contains(QStringLiteral("vCPU")),
          "cloud share note states the formula");
    check(cloudChart != nullptr && cloudChart->sampleCount() == 2,
          "cloud chart charts one app-cost sample per tick");

    DisplaySnapshot cloudGated = makeSnapshot("idle", "7", "0%", "10 MB", "OK");
    hud.setSnapshot(cloudGated);
    check(cloudProvider != nullptr
              && cloudProvider->text() == QStringLiteral("--")
              && cloudAccrued != nullptr
              && cloudAccrued->text() == QStringLiteral("--")
              && cloudMonthly != nullptr
              && cloudMonthly->text() == QStringLiteral("--")
              && cloudAge != nullptr
              && cloudAge->text() == QStringLiteral("--"),
          "cloud page falls back to dashes off a cloud instance");
    check(cloudFleetSummary != nullptr
              && cloudFleetSummary->text().contains(
                  QStringLiteral("no cloud instance identified"))
              && cloudFleet != nullptr
              && cloudFleet->topLevelItemCount() == 1
              && cloudFleet->topLevelItem(0)->text(0)
                     == cloudFleetSummary->text()
              && cloudFleetChart != nullptr
              && cloudFleetChart->sampleCount() == 2,
          "cloud fleet card states the gate off a cloud instance and charts "
          "nothing");
    check(cloudSummary != nullptr
              && cloudSummary->text().contains(
                  QStringLiteral("no instance metadata service")),
          "cloud page states the gate instead of showing a blank card");
    check(cloudConsumers != nullptr
              && cloudConsumers->topLevelItemCount() == 1
              && cloudConsumers->topLevelItem(0)->text(0)
                     == QStringLiteral("cloud: unavailable"),
          "cloud consumer table states the gate, never an empty list");

    if (moreButton2 && panel) {
        moreButton2->click();
        check(hud.isExpanded() && !panel->isHidden(),
              "MORE expands the overlay in place");
        check(moreButton2->text() == QStringLiteral("LESS"), "MORE -> LESS");

        auto *rail = hud.findChild<QListWidget *>(QStringLiteral("ddRail"));
        if (rail != nullptr) {
            constexpr int kRailCount = 32;
            bool allPagesReachable = rail->count() == kRailCount;
            const int settingsRow = kRailCount - 1;
            allPagesReachable = allPagesReachable
                && rail->item(kRailCount - 3)->text() == QStringLiteral("Cost")
                && rail->item(kRailCount - 2)->text() == QStringLiteral("Cloud")
                && rail->item(settingsRow)->text() == QStringLiteral("Settings");
            rail->setCurrentRow(settingsRow);
            const auto *stack =
                hud.findChild<QStackedWidget *>(QStringLiteral("ddViews"));
            allPagesReachable = allPagesReachable && stack != nullptr
                && stack->currentIndex() == settingsRow;
            auto *software =
                hud.findChild<QLabel *>(QStringLiteral("ddSettingsInfo"));
            allPagesReachable = allPagesReachable && software != nullptr
                && software->text().contains(QStringLiteral("version"));
            rail->setCurrentRow(kRailCount - 2);
            allPagesReachable = allPagesReachable && stack != nullptr
                && stack->currentIndex() == kRailCount - 2
                && hud.findChild<QLabel *>(
                       QStringLiteral("ddCloudTileProvider"))
                       != nullptr;
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
        {
            // M82: the GENERAL card states the metadata probe + price age.
            SettingsInfo cloudInfo = info;
            cloudInfo.cloudLine =
                QStringLiteral("imds 169.254.169.254 - read only, "
                               "3 price(s) at /tmp/rates.txt (60 days old)");
            hud.setSettingsInfo(cloudInfo);
            QString cloudText;
            for (QLabel *label :
                 hud.findChildren<QLabel *>(QStringLiteral("ddSettingsInfo"))) {
                if (label->text().contains(QStringLiteral("cloud&nbsp;"))) {
                    cloudText = label->text();
                }
            }
            check(cloudText.contains(QStringLiteral("169.254.169.254"))
                      && cloudText.contains(QStringLiteral("60 days old")),
                  "settings GENERAL card states the cloud probe and price age");
            hud.setSettingsInfo(info);
        }
        {
            // M84: the console measures itself. A tick with a measured cost
            // and footprint must reach the GENERAL card as three rows.
            DisplaySnapshot selfSnap;
            selfSnap.self.available = true;
            selfSnap.self.cpuPercent = 6.04;
            selfSnap.self.rssKb = 36864.0;
            QList<TickSection> sections;
            sections.append(TickSection{QStringLiteral("processes"), 41.2});
            sections.append(TickSection{QStringLiteral("host"), 3.1});
            selfSnap.tick = tickbudget::summarize(
                sections, 62.0, QList<double>{40.0, 62.0, 84.0});
            hud.setSnapshot(selfSnap);
            QString costText;
            for (QLabel *label :
                 hud.findChildren<QLabel *>(QStringLiteral("ddSettingsInfo"))) {
                if (label->text().contains(QStringLiteral("self&nbsp;cost&nbsp;"))) {
                    costText = label->text();
                }
            }
            check(costText.contains(QStringLiteral("cpu 6.04%"))
                      && costText.contains(QStringLiteral("rss 36.0 MB")),
                  "settings GENERAL card states Syspeek's own cpu and rss");
            check(costText.contains(QStringLiteral("tick&nbsp;"))
                      && costText.contains(QStringLiteral("62 ms"))
                      && costText.contains(QStringLiteral("worst 84 ms")),
                  "settings GENERAL card states the tick cost and its window");
            check(costText.contains(QStringLiteral("sections&nbsp;"))
                      && costText.contains(QStringLiteral("processes"))
                      && costText.contains(QStringLiteral("host")),
                  "settings GENERAL card names the slowest sections");
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

        // M81 Settings page: KPI tiles, the collection-scope matrix and
        // the COPY REPORT flare analog.
        auto *scopeLabel =
            hud.findChild<QLabel *>(QStringLiteral("ddSettingsScope"));
        check(scopeLabel != nullptr
                  && scopeLabel->text().contains(
                      QStringLiteral("collection surfaces live on")),
              "settings: COLLECTION SCOPE counts the live surfaces");
        check(scopeLabel->text().contains(QStringLiteral("tcp health counters")),
              "settings: the scope matrix names an OS-gated surface");

        auto *tileVersion = hud.findChild<QLabel *>(
            QStringLiteral("ddSettingsTileVersion"));
        auto *tileRefresh = hud.findChild<QLabel *>(
            QStringLiteral("ddSettingsTileRefresh"));
        auto *tileMonitors = hud.findChild<QLabel *>(
            QStringLiteral("ddSettingsTileMonitors"));
        auto *tileSilenced = hud.findChild<QLabel *>(
            QStringLiteral("ddSettingsTileSilenced"));
        check(tileVersion != nullptr
                  && tileVersion->text() == QStringLiteral("9.9.9-test"),
              "settings: the version tile states the build");
        check(tileRefresh != nullptr
                  && tileRefresh->text() == QStringLiteral("1s"),
              "settings: the refresh tile states the cadence");
        check(tileMonitors != nullptr && tileMonitors->text().toInt() >= 10,
              "settings: the monitor tile counts the rules");
        check(tileSilenced != nullptr
                  && tileSilenced->text() == QStringLiteral("0"),
              "settings: the silence tile starts at zero");

        SilenceRule liveSilence;
        liveSilence.matcher = QStringLiteral("app_cpu");
        liveSilence.startsAtMs = QDateTime::currentMSecsSinceEpoch() - 1000;
        liveSilence.endsAtMs = QDateTime::currentMSecsSinceEpoch() + 60000;
        liveSilence.comment = QStringLiteral("deploy");
        panel->setSilences({liveSilence});
        check(tileSilenced->text() == QStringLiteral("1")
                  && tileSilenced->styleSheet().contains(
                      QStringLiteral("#c9a24a")),
              "settings: a live silence turns the tile amber");
        panel->setSilences({});

        auto *copyReport = static_cast<QPushButton *>(nullptr);
        for (QPushButton *button : hud.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("COPY REPORT")) {
                copyReport = button;
            }
        }
        check(copyReport != nullptr, "settings: COPY REPORT button exists");
        if (copyReport != nullptr) {
            copyReport->click();
            const QString report = QGuiApplication::clipboard()->text();
            check(report.contains(QStringLiteral("syspeek 9.9.9-test"))
                      && report.contains(QStringLiteral("/tmp/syspeek.conf"))
                      && report.contains(QStringLiteral("monitors"))
                      && report.contains(QStringLiteral("refresh")),
                  "settings: COPY REPORT copies the diagnostic report");
            check(copyReport->text() == QStringLiteral("COPIED"),
                  "settings: COPY REPORT confirms the copy");
        }

        moreButton2->click();
        check(!hud.isExpanded() && panel->isHidden(),
              "LESS collapses the overlay in place");
        check(moreButton2->text() == QStringLiteral("MORE"), "LESS -> MORE");
    }

    // ---- M78 process verbs ----
    {
        auto *panel = hud.findChild<DashboardPanel *>();
        auto *procs = hud.findChild<QTreeWidget *>(QStringLiteral("ddProcs"));
        auto *status =
            hud.findChild<QLabel *>(QStringLiteral("ddActionStatus"));
        check(panel != nullptr && procs != nullptr && status != nullptr,
              "actions: processes page exposes the tree and a status line");

        DisplaySnapshot snap;
        snap.appName = QStringLiteral("probe");
        snap.pid = QStringLiteral("4242");
        ProcessRow row;
        row.pid = 4242;
        row.name = QStringLiteral("worker");
        row.depth = 0;
        snap.processes.append(row);
        hud.setSnapshot(snap);

        check(procs->topLevelItemCount() == 1,
              "actions: the process tree has the fixture row");

        auto *term = static_cast<QPushButton *>(nullptr);
        auto *niceUp = static_cast<QPushButton *>(nullptr);
        for (QPushButton *button : panel->findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("TERM")) {
                term = button;
            } else if (button->text() == QStringLiteral("NICE +5")) {
                niceUp = button;
            }
        }
        check(term != nullptr && niceUp != nullptr,
              "actions: TERM and NICE +5 buttons exist");

        QList<ProcessAction> emitted;
        QObject::connect(panel, &DashboardPanel::processActionRequested,
                         panel, [&emitted](const ProcessAction &action) {
                             emitted.append(action);
                         });

        const PlatformCapabilities caps = systemBackend().capabilities();
        if (caps.processSignals && term != nullptr) {
            term->click();
            check(emitted.isEmpty()
                      && status->text().contains(QStringLiteral("select")),
                  "actions: TERM without a target states it plainly");

            procs->setCurrentItem(procs->topLevelItem(0));
            term->click();
            check(emitted.isEmpty()
                      && status->text().startsWith(QStringLiteral("confirm:")),
                  "actions: the first TERM press only asks for confirmation");

            term->click();
            check(emitted.size() == 1,
                  "actions: the second TERM press emits the action");
            if (emitted.size() == 1) {
                const ProcessAction action = emitted.at(0);
                check(action.pid == 4242
                          && action.signal == ProcessSignal::Term
                          && action.name == QStringLiteral("worker"),
                      "actions: the emitted TERM carries pid, name and signal");
            }

            emitted.clear();
            if (niceUp->isEnabled()) {
                niceUp->click();
                check(emitted.size() == 1,
                      "actions: NICE +5 emits immediately");
                if (emitted.size() == 1) {
                    const ProcessAction action = emitted.at(0);
                    check(action.kind == ProcessActionKind::Renice
                              && action.niceValue == 5 && action.relative,
                          "actions: NICE +5 is a relative renice of 5");
                }
            } else {
                check(!caps.processRenice,
                      "actions: NICE is disabled only where renice is absent");
            }
        }

        // The selection must survive a tree rebuild (the sampler replaces
        // every item once per tick).
        procs->setCurrentItem(procs->topLevelItem(0));
        hud.setSnapshot(snap);
        check(procs->currentItem() != nullptr
                  && procs->currentItem()->data(0, Qt::UserRole).toInt() == 4242,
              "actions: the selected pid survives a repopulate");

        panel->setActionStatus(QStringLiteral("SIGTERM worker (4242) ok"),
                               true);
        check(status->text().contains(QStringLiteral("worker")),
              "actions: the status line renders a result");
    }

    {
        std::fprintf(stderr, "[test] overlay block 1 (click control)\n");
        // M80: the click-through control must exist and must never claim a
        // mode the session cannot honour.
        auto *clickButton =
            hud.findChild<QPushButton *>(QStringLiteral("clickButton"));
        check(clickButton != nullptr
                  && clickButton->text() == QStringLiteral("CLICK"),
              "overlay: the CLICK control sits in the footer");

        if (clickButton != nullptr) {
            hud.show();
            clickButton->click();
            const OverlayState state = hud.overlayState();
            check(!state.clickThrough,
                  "overlay: click-through is refused when no window server "
                  "can honour it");
            check(clickButton->text() == QStringLiteral("CLICK"),
                  "overlay: a refused click-through leaves the control off");
            check(!state.note.isEmpty(),
                  "overlay: the refusal states the reason");
            hud.hide();
        }
    }

    {
        std::fprintf(stderr, "[test] overlay block 2 (restore + persist)\n");
        // The saved rectangle is restored on show, clamped to a live screen
        // and written back with the mode when the layout changes.
        QTemporaryDir overlayDir;
        const QString overlayPath = overlayDir.filePath(QStringLiteral("config.ini"));

        HudWindow window;
        OverlayConfig saved;
        saved.x = 120;
        saved.y = 90;
        saved.width = 820;
        saved.height = 560;
        window.setOverlayConfig(saved, overlayPath);
        window.show();
        QCoreApplication::processEvents();

        bool onScreen = false;
        for (const QScreen *screen : QGuiApplication::screens()) {
            if (screen->availableGeometry().contains(window.frameGeometry())) {
                onScreen = true;
            }
        }
        check(window.x() == 120 && window.y() == 90,
              "overlay: a saved position is restored on show");
        check(onScreen || QGuiApplication::screens().isEmpty(),
              "overlay: the restored window lands on a live screen");

        auto *expand =
            window.findChild<QPushButton *>(QStringLiteral("moreButton"));
        if (expand != nullptr) {
            expand->click();
            QCoreApplication::processEvents();
        }
        check(expand != nullptr && window.isExpanded(),
              "overlay: MORE expands the console before the size is saved");

        // The size is persisted from the window's own resize signal after a
        // short debounce, and the layout may grow the frame after the resize
        // we asked for, so read the file once it agrees with the window.
        int persistedX = -1;
        int persistedY = -1;
        int persistedW = -1;
        int persistedH = -1;
        bool persistedMode = false;
        QElapsedTimer persistClock;
        persistClock.start();
        while (persistClock.elapsed() < 2000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
            QSettings probe(overlayPath, QSettings::IniFormat);
            persistedX =
                probe.value(QStringLiteral("overlay/x")).toString().toInt();
            persistedY =
                probe.value(QStringLiteral("overlay/y")).toString().toInt();
            persistedW =
                probe.value(QStringLiteral("overlay/width")).toString().toInt();
            persistedH =
                probe.value(QStringLiteral("overlay/height")).toString().toInt();
            persistedMode =
                probe.value(QStringLiteral("overlay/click_through")).toBool();
            if (persistedX == window.x() && persistedY == window.y()
                && persistedW == window.width()
                && persistedH == window.height()) {
                break;
            }
        }
        if (persistedW != window.width() || persistedH != window.height()) {
            std::printf("  (persisted %dx%d at %d,%d, window %dx%d at %d,%d)\n",
                        persistedW, persistedH, persistedX, persistedY,
                        window.width(), window.height(), window.x(), window.y());
        }

        check(persistedX == window.x() && persistedY == window.y(),
              "overlay: the position is persisted to the config file");
        check(persistedW == window.width() && persistedH == window.height(),
              "overlay: the expanded size is persisted too");
        check(!persistedMode,
              "overlay: the persisted mode matches the refused state");

        QString overlayLine;
        auto *settingsGeneral =
            window.findChild<QLabel *>(QStringLiteral("ddSettingsInfo"));
        if (settingsGeneral != nullptr) {
            for (QLabel *label : window.findChildren<QLabel *>(
                     QStringLiteral("ddSettingsInfo"))) {
                if (label->text().contains(QStringLiteral("overlay&nbsp;"))) {
                    overlayLine = label->text();
                }
            }
        }
        check(overlayLine.contains(QStringLiteral("overlay&nbsp;"))
                  && overlayLine.contains(QStringLiteral("overlay:")),
              "overlay: the settings card states the live overlay state");
        window.hide();
    }

    {
        std::fprintf(stderr, "[test] overlay block 3 (off-screen rescue)\n");
        // A monitor can vanish (or the frame be dragged past the last edge):
        // the 1 s watch pulls the HUD back instead of leaving it unreachable.
        HudWindow window;
        window.setOverlayConfig(OverlayConfig{}, QString());
        window.show();
        QCoreApplication::processEvents();
        window.move(9000, 9000);

        QElapsedTimer clock;
        clock.start();
        while (window.x() == 9000 && clock.elapsed() < 3000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
        }

        bool reachable = false;
        for (const QScreen *screen : QGuiApplication::screens()) {
            if (screen->availableGeometry().intersects(window.frameGeometry())) {
                reachable = true;
            }
        }
        check(window.x() != 9000 && reachable,
              "overlay: a frame dragged off every screen is pulled back");
        window.hide();
    }

    {
        // End to end: a monitor breach becomes one event, and a chart tick
        // exists for it.
        QList<AlertRule> hotRules = defaultAlertRules();
        for (AlertRule &rule : hotRules) {
            if (rule.id == QStringLiteral("app_cpu")) {
                rule.threshold = 5.0;
                rule.sustained = 1;
            }
        }
        hud.setMonitors(hotRules);
        DisplaySnapshot warmSnap = withProcs;
        warmSnap.cpuPercent = 42.0;
        warmSnap.cpuValid = true;
        hud.setSnapshot(warmSnap);
        hud.setSnapshot(warmSnap);
        bool sawBreach = false;
        for (const TimelineEvent &event : hud.timelineEvents()) {
            if (event.kind == TimelineKind::Alert
                && event.subject == QStringLiteral("monitor:app_cpu")
                && event.severity == TimelineSeverity::Error) {
                sawBreach = true;
            }
        }
        check(sawBreach,
              "a monitor breach lands in the event stream as one event");
        int breachMarkers = 0;
        for (TimeseriesChart *chart : hud.findChildren<TimeseriesChart *>()) {
            breachMarkers += chart->eventCount() > 0 ? 1 : 0;
        }
        check(breachMarkers > 0,
              "the breach tick reaches a chart");
        hud.setMonitors(defaultAlertRules());
        for (int i = 0; i < 3; ++i) {
            hud.setSnapshot(withProcs);
        }
    }

    {
        // The marker hit test: a click anywhere over the tick reports the
        // instant it stands for, and a click away from it reports nothing.
        TimeseriesChart chart;
        chart.resize(420, 150);
        chart.show();
        for (int i = 0; i < 24; ++i) {
            chart.push(10.0 + i);
        }
        QList<qint64> hits;
        QObject::connect(&chart, &TimeseriesChart::eventActivated,
                         [&hits](qint64 atMs) { hits.append(atMs); });
        const qint64 markMs = QDateTime::currentMSecsSinceEpoch();
        QList<ChartEvent> ticks;
        ChartEvent tick;
        tick.atMs = markMs;
        tick.severity = 3;
        tick.label = QStringLiteral("error alert - Focused app CPU high");
        ticks.append(tick);
        chart.setEvents(ticks);
        QApplication::processEvents();
        check(chart.eventCount() == 1 && chart.lastSampleMs() > 0,
              "a chart keeps its event ticks and its own sample clock");

        auto clickAt = [&chart](int x, int y) {
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(x, y),
                              chart.mapToGlobal(QPoint(x, y)), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&chart, &press);
        };
        const int probeY = chart.height() - 40;
        for (int x = 0; x < chart.width() && hits.isEmpty(); ++x) {
            clickAt(x, probeY);
        }
        check(hits.size() == 1 && hits.first() == markMs,
              "clicking a chart tick reports the instant it stands for");
    }

    {
        // A chart tick reaches the HUD, which rows the console on the
        // timeline and narrows it to the window around that instant.
        auto *railNow = hud.findChild<QListWidget *>(QStringLiteral("ddRail"));
        TimeseriesChart *marked = nullptr;
        for (TimeseriesChart *chart :
             hud.findChildren<TimeseriesChart *>()) {
            if (chart->eventCount() > 0) {
                marked = chart;
                break;
            }
        }
        check(railNow != nullptr && marked != nullptr,
              "the console charts carry the event ticks");
        if (railNow != nullptr && marked != nullptr) {
            int timelineRow = -1;
            for (int i = 0; i < railNow->count(); ++i) {
                if (railNow->item(i)->text() == QStringLiteral("Timeline")) {
                    timelineRow = i;
                }
            }
            railNow->setCurrentRow(0);
            const qint64 at = marked->events().first().atMs;
            marked->eventActivated(at);
            QApplication::processEvents();
            auto *window = hud.findChild<QLabel *>(QStringLiteral("ddTlWindow"));
            auto *clear = hud.findChild<QPushButton *>(QStringLiteral("ddTlClear"));
            check(timelineRow >= 0 && railNow->currentRow() == timelineRow,
                  "clicking a chart tick opens the timeline page");
            check(window != nullptr && !window->isHidden()
                      && clear != nullptr && !clear->isHidden(),
                  "the click narrows the timeline to a window");
            if (clear != nullptr) {
                clear->click();
            }
            QApplication::processEvents();
            check(window != nullptr && window->isHidden(),
                  "clearing the window returns the stream to everything");
            railNow->setCurrentRow(0);
        }
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
