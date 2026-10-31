#include "HudWindow.h"
#include "DashboardPanel.h"
#include "HistoryLog.h"
#include "SensorStatus.h"
#include "CloudStatus.h"
#include "TickBudget.h"

#include <QApplication>
#include <QListWidget>
#include <QLabel>
#include <QPixmap>
#include <QDateTime>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

DisplaySnapshot demoSnapshot(int tick) {
    const double w1 = std::sin(tick / 18.0);
    const double w2 = std::sin(tick / 30.0 + 1.2);
    const double w3 = std::sin(tick / 40.0 + 2.4);
    const double w4 = std::sin(tick / 9.0 + 0.7);
    const double cpuPct = std::clamp(30.0 + 42.0 * w1 + 6.0 * w4, 2.0, 97.0);
    const double rssMb = 612.0 + 55.0 * w2 + 12.0 * w4;
    const double hostCpuPct = std::clamp(26.0 + 34.0 * w2 + 7.0 * w4, 3.0, 95.0);
    const double memAvailGb = 13.1 + 1.8 * w3;
    const double load1 = std::clamp(1.1 + 1.3 * w3 + 0.3 * w4, 0.1, 6.0);
    const double rx = 412000.0 * (0.4 + 0.85 * std::abs(w2) + 0.5 * std::abs(w4));
    const double tx = 118000.0 * (0.5 + 0.9 * std::abs(w1));

    DisplaySnapshot s;
    s.appName = QStringLiteral("cursor");
    s.pid = QStringLiteral("2092707");
    s.cpu = QStringLiteral("%1%").arg(cpuPct, 0, 'f', 1);
    s.memory = QStringLiteral("%1 MB").arg(rssMb, 0, 'f', 0);
    s.status = QStringLiteral("OK");
    s.cpuPercent = cpuPct;
    s.cpuValid = true;
    s.rssKb = static_cast<qulonglong>(rssMb * 1024.0);
    s.hostAvailable = true;
    s.hostCpuPercent = hostCpuPct;
    s.hostMemTotalKb = 16777216;
    s.hostMemAvailableKb = static_cast<qulonglong>(memAvailGb * 1024.0 * 1024.0);
    s.hostLoad1 = load1;
    s.hostNetRxPerSec = rx;
    s.hostNetTxPerSec = tx;
    s.hostDiskTotalBytes = 512ULL * 1024ULL * 1024ULL * 1024ULL;
    s.hostDiskFreeBytes = 214ULL * 1024ULL * 1024ULL * 1024ULL;
    s.hostStealPercent = 2.4;
    s.cgroupAvailable = true;
    s.cgroupVersion = QStringLiteral("v2");
    s.cgroupPath = QStringLiteral("user.slice/app.cursor.scope");
    s.cgroupMemUsage = 612ULL * 1024ULL * 1024ULL;
    s.cgroupMemLimit = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    s.cgroupThrottled = 12;
    s.cgroupPeriods = 6480;
    s.cgroupThrottledUsec = 154000000;
    s.cgroupMemPeakBytes = 3775ULL * 1024ULL * 1024ULL;
    s.cgroupHasMemPeak = true;
    s.cgroupPidsCurrent = 148;
    s.cgroupPidsMax = 10813;
    s.focusOom.score = 187;
    s.focusOom.scoreAdj = 0;
    s.hostRun = 3;
    s.hostBlocked = 1;
    s.kernelLimits.available = true;
    s.kernelLimits.fdAllocated = 14664;
    s.kernelLimits.fdMax = 9223372036854775807ULL;
    s.kernelLimits.somaxconn = 4096;
    s.kernelLimits.congestion = QStringLiteral("cubic");
    s.kernelLimits.congestionAvailable = {QStringLiteral("cubic"),
                                          QStringLiteral("bbr2")};
    s.kernelLimits.inotifyWatchesMax = 65536;
    s.kernelLimits.pidMax = 4194304;
    s.kernelLimits.swappiness = 60;
    s.kernelLimits.overcommit = 1;

    ProcessRow proc;
    proc.pid = 2092707;
    proc.threads = 148;
    proc.fds = 1204;
    proc.cpuPercent = 12.4;
    proc.netRxPerSec = 1'258'291.0;
    proc.netTxPerSec = 196'608.0;
    proc.diskReadPerSec = 3'145'728.0;
    proc.diskWritePerSec = 524'288.0;
    proc.listenPorts = {9222};
    proc.name = QStringLiteral("cursor");
    proc.memory = QStringLiteral("612.4 MB");
    s.processes = {proc};
    for (const auto &[pid, name, cpu, mem] :
         std::initializer_list<std::tuple<int, const char *, double, const char *>>{
             {2092708, "node", 8.9, "421 MB"},
             {2092709, "renderer", 4.2, "188 MB"},
             {3014, "pulseaudio", 0.4, "12 MB"},
         }) {
        ProcessRow row;
        row.pid = pid;
        row.threads = 32;
        row.fds = 192;
        row.cpuPercent = cpu;
        row.name = QString::fromUtf8(name);
        row.memory = QString::fromUtf8(mem);
        s.processes.append(row);
    }
    // The focused group root carries the aggregate the top bar shows, so the
    // Cloud page's app share and its consumer table describe the same pid.
    s.processes.first().cpuPercent = cpuPct;
    s.processes[1].netRxPerSec = 262'144.0;
    s.processes[1].netTxPerSec = 65'536.0;
    s.processes[1].diskReadPerSec = 65'536.0;
    s.processes[1].diskWritePerSec = 1'048'576.0;
    s.processes[1].listenPorts = {3000, 3001};
    s.processes.last().netRxPerSec = 0.0;
    s.processes.last().netTxPerSec = 0.0;
    s.processes.last().diskReadPerSec = 0.0;
    s.processes.last().diskWritePerSec = 0.0;
    s.processNetAvailable = true;
    s.processNetTracked = 14;
    s.processNetUnattributed = 39;
    s.focusNetRxPerSec = 1'258'291.0;
    s.focusNetTxPerSec = 196'608.0;
    ProcessNetTalker talkerA;
    talkerA.pid = 2092707;
    talkerA.sockets = 4;
    talkerA.rxPerSec = 1'258'291.0;
    talkerA.txPerSec = 196'608.0;
    talkerA.name = QStringLiteral("cursor");
    talkerA.listenPorts = {9222};
    ProcessNetTalker talkerB;
    talkerB.pid = 2092708;
    talkerB.sockets = 2;
    talkerB.rxPerSec = 262'144.0;
    talkerB.txPerSec = 65'536.0;
    talkerB.name = QStringLiteral("node");
    talkerB.listenPorts = {3000, 3001};
    ProcessNetTalker talkerC;
    talkerC.pid = 8842;
    talkerC.sockets = 1;
    talkerC.rxPerSec = 12'288.0;
    talkerC.txPerSec = 40'960.0;
    talkerC.name = QStringLiteral("rsync");
    s.processNetTalkers = {talkerA, talkerB, talkerC};

    s.processDiskAvailable = true;
    s.processDiskScanning = true;
    s.processDiskTracked = 11;
    s.processDiskUnreadable = 42;
    s.processDiskReadPerSec = 3'211'264.0;
    s.processDiskWritePerSec = 5'242'880.0;
    ProcessDiskConsumer diskA;
    diskA.pid = 8842;
    diskA.name = QStringLiteral("rsync");
    diskA.readPerSec = 2'097'152.0;
    diskA.writePerSec = 4'194'304.0;
    ProcessDiskConsumer diskB;
    diskB.pid = 2092708;
    diskB.name = QStringLiteral("node");
    diskB.readPerSec = 1'048'576.0;
    diskB.writePerSec = 524'288.0;
    ProcessDiskConsumer diskC;
    diskC.pid = 611;
    diskC.name = QStringLiteral("postgres");
    diskC.readPerSec = 65'536.0;
    diskC.writePerSec = 524'288.0;
    s.processDiskTop = {diskA, diskB, diskC};


    s.gpu.ok = true;
    s.gpu.vendor = QStringLiteral("intel");
    s.gpuClientsAvailable = true;
    {
        GpuDevice dev;
        dev.index = 0;
        dev.vendor = QStringLiteral("intel");
        dev.name = QStringLiteral("Iris Xe Graphics");
        dev.driver = QStringLiteral("i915");
        dev.pciId = QStringLiteral("8086:A7A1");
        dev.utilPercent = 62.5;
        dev.memUsed = 2ULL * 1024 * 1024 * 1024;
        dev.memTotal = 8ULL * 1024 * 1024 * 1024;
        dev.tempC = 64.0;
        dev.powerW = 18.5;
        dev.clockMhz = 1150;
        s.gpu.devices = {dev};
        GpuProcess game;
        game.pid = 2092707;
        game.name = QStringLiteral("cursor");
        game.busyPercent = 62.5;
        game.memBytes = 1ULL * 1024 * 1024 * 1024;
        game.engines = QStringLiteral("render 62%");
        GpuProcess shell;
        shell.pid = 2048;
        shell.name = QStringLiteral("gnome-shell");
        shell.busyPercent = 11.0;
        shell.memBytes = 512ULL * 1024 * 1024;
        shell.engines = QStringLiteral("render 11%");
        s.gpu.processes = {game, shell};
    }

    ConnectionRow conn;
    conn.proto = QStringLiteral("tcp");
    conn.local = QStringLiteral("127.0.0.1:59640");
    conn.remote = QStringLiteral("140.82.114.3:22");
    conn.state = QStringLiteral("ESTABLISHED");
    conn.hasTcpDiag = true;
    conn.congestion = QStringLiteral("cubic");
    conn.rttUsec = 18400;
    conn.rttVarUsec = 4200;
    conn.rtoUsec = 208000;
    conn.cwnd = 19;
    conn.sndMss = 1448;
    conn.retransmits = 2;
    conn.totalRetrans = 47;
    conn.segsOut = 48112;
    conn.segsIn = 47918;
    conn.pmtu = 1500;
    conn.bytesSent = 250723456;
    conn.bytesAcked = 250721920;
    conn.bytesReceived = 88174032;
    conn.hasSkmem = true;
    conn.skdropDelta = 1;
    conn.retransDelta = 2;
    conn.skDrops = 3;
    s.connections = {conn};

    // SBOM fixture (M61q/M61r): two deb packages + one brew purl so the
    // package/resolution tiles and purl-family count render.
    SbomReport sbom;
    sbom.available = true;
    sbom.libraryCount = 12;
    sbom.resolvedCount = 9;
    auto mkPkg = [](const char *pkg, const char *ver, const char *purl) {
        SbomEntry entry;
        entry.package = QString::fromUtf8(pkg);
        entry.version = QString::fromUtf8(ver);
        entry.arch = QStringLiteral("amd64");
        entry.purl = QString::fromUtf8(purl);
        return entry;
    };
    sbom.entries.append(mkPkg("libssl3", "3.5.5",
                              "pkg:deb/ubuntu/libssl3@3.5.5?arch=amd64"));
    sbom.entries.append(mkPkg("libpq5", "17.4-2",
                              "pkg:deb/ubuntu/libpq5@17.4-2?arch=amd64"));
    sbom.entries.append(mkPkg("openssl", "3.5.5",
                              "pkg:brew/openssl@3.5.5"));
    s.sbom = sbom;

    // Security posture fixture (M61w): systemd-analyze-style exposure
    // raster with the real grade banding; high band crossed on peaks.
    SecurityPosture sec;
    sec.available = true;
    sec.exposure = std::clamp(4.6 + 1.7 * w1, 1.0, 8.2);
    sec.grade = sec.exposure < 2.5   ? QStringLiteral("low")
                : sec.exposure < 5.0 ? QStringLiteral("medium")
                : sec.exposure < 7.5 ? QStringLiteral("high")
                                     : QStringLiteral("critical");
    sec.exposedCount = 3 + static_cast<int>(std::abs(w2) * 2.0);
    sec.findings.append(SecurityFinding{QStringLiteral("cap_sys_admin"),
                                        8.0, false});
    sec.findings.append(SecurityFinding{QStringLiteral("seccomp 0"), 6.5, false});
    sec.findings.append(SecurityFinding{QStringLiteral("no-new-privs"),
                                        1.0, true});
    s.security = sec;

    // Thread-profile fixture (M37): thread count + cpu wave so the
    // profiles page and its chart render real math.
    ProfileSnapshot prof;
    prof.available = true;
    prof.comm = QStringLiteral("cursor");
    prof.threadCount = 12 + static_cast<int>(std::abs(w1) * 3.0);
    prof.totalCpuPercent = cpuPct * 0.92;
    prof.waitChannels = {
        {QStringLiteral("ep_poll"), 4},
        {QStringLiteral("futex_wait"), 3},
        {QStringLiteral("read"), 2},
        {QStringLiteral("nanosleep"), 1},
        {QStringLiteral("select"), 0},
    };
    prof.totalPssBytes = static_cast<unsigned long long>(rssMb * 1e6 * 0.78);
    prof.sampledAtMs = static_cast<qint64>(tick) * 2000;
    ThreadProfile topThread;
    topThread.tid = 2092714;
    topThread.comm = QStringLiteral("gpu-compositor");
    topThread.state = QStringLiteral("R");
    topThread.cpuPercent = cpuPct * 0.4;
    prof.threads.append(topThread);
    ThreadProfile worker;
    worker.tid = 2092721;
    worker.comm = QStringLiteral("worker-3");
    worker.state = QStringLiteral("S");
    worker.waitChannel = QStringLiteral("ep_poll");
    worker.cpuPercent = cpuPct * 0.2;
    prof.threads.append(worker);
    ThreadProfile ioThread;
    ioThread.tid = 2092733;
    ioThread.comm = QStringLiteral("io-writer");
    ioThread.state = QStringLiteral("D");
    ioThread.waitChannel = QStringLiteral("futex_wait");
    ioThread.cpuPercent = cpuPct * 0.1;
    prof.threads.append(ioThread);
    s.profile = prof;

    InterfaceRow iface;
    iface.name = QStringLiteral("wlp0s20f3");
    iface.rxBytes = 84ULL * 1024ULL * 1024ULL;
    iface.txBytes = 12ULL * 1024ULL * 1024ULL;
    iface.rxPerSec = 412000.0;
    iface.txPerSec = 118000.0;
    s.interfaces = {iface};

    // Endpoint fixture (M61s): real exposure vocabulary from
    // portcatalog::endpointExposure ("local" / "public"), one db port.
    auto mkEndpoint = [](const char *proto, const char *addr, quint16 port,
                         const char *exposure, const char *service,
                         bool database, int backlog) {
        EndpointRow row;
        row.proto = QString::fromUtf8(proto);
        row.addr = QString::fromUtf8(addr);
        row.port = port;
        row.exposure = QString::fromUtf8(exposure);
        row.service = QString::fromUtf8(service);
        row.database = database;
        row.backlog = backlog;
        return row;
    };
    s.endpoints = {
        mkEndpoint("tcp", "127.0.0.1", 4318, "local", "syspeek", false, 0),
        mkEndpoint("tcp", "0.0.0.0", 5432, "public", "postgres", true, 41),
        mkEndpoint("tcp", "10.0.0.5", 8080, "public", "nginx", false, 0),
        mkEndpoint("tcp", "[::1]", 6379, "local", "redis", false, -1),
    };
    s.instrumentation = {QStringLiteral("datadog"),
                         QStringLiteral("opentelemetry")};
    s.listeningPorts = {QStringLiteral("127.0.0.1:4318")};

    DiskRow disk;
    disk.name = QStringLiteral("nvme0n1");
    disk.readPerSec = 12.0;
    disk.writePerSec = 45.0;
    disk.readKbPerSec = 480.0 * (0.5 + 0.9 * std::abs(w3));
    disk.writeKbPerSec = 1200.0 * (0.4 + 0.95 * std::abs(w1));
    disk.readAwaitMs = 1.4;
    disk.writeAwaitMs = 2.1;
    disk.aquSz = 0.3;
    disk.utilPercent = std::clamp(18.0 + 55.0 * std::abs(w1), 0.0, 99.0);
    s.disks = {disk};
    s.disksAvailable = true;

    SensorChip cpuChip;
    cpuChip.name = QStringLiteral("coretemp");
    {
        SensorReading pkg;
        pkg.chip = cpuChip.name;
        pkg.key = QStringLiteral("temp1");
        pkg.label = QStringLiteral("Package id 0");
        pkg.kind = SensorKind::Temperature;
        pkg.value = std::clamp(58.0 + 13.0 * w2 + 3.0 * w4, 40.0, 96.0);
        pkg.hasCrit = true;
        pkg.crit = 100.0;
        pkg.hasWarn = true;
        pkg.warn = 95.0;
        SensorReading core0;
        core0.chip = cpuChip.name;
        core0.key = QStringLiteral("temp2");
        core0.label = QStringLiteral("Core 0");
        core0.kind = SensorKind::Temperature;
        core0.value = pkg.value - 4.0;
        cpuChip.readings = {pkg, core0};
    }
    SensorChip boardChip;
    boardChip.name = QStringLiteral("acpitz");
    {
        SensorReading board;
        board.chip = boardChip.name;
        board.key = QStringLiteral("temp1");
        board.label = QStringLiteral("temp1");
        board.kind = SensorKind::Temperature;
        board.value = 44.5;
        boardChip.readings = {board};
    }
    SensorChip nvmeChip;
    nvmeChip.name = QStringLiteral("nvme");
    {
        SensorReading composite;
        composite.chip = nvmeChip.name;
        composite.key = QStringLiteral("temp1");
        composite.label = QStringLiteral("Composite");
        composite.kind = SensorKind::Temperature;
        composite.value = 47.0 + 4.0 * w3;
        composite.hasWarn = true;
        composite.warn = 80.0;
        nvmeChip.readings = {composite};
    }
    SensorChip fanChip;
    fanChip.name = QStringLiteral("hp");
    {
        SensorReading cpuFan;
        cpuFan.chip = fanChip.name;
        cpuFan.key = QStringLiteral("fan1");
        cpuFan.label = QStringLiteral("cpu_fan");
        cpuFan.kind = SensorKind::Fan;
        cpuFan.value = 2900.0 + 420.0 * std::abs(w1);
        SensorReading caseFan;
        caseFan.chip = fanChip.name;
        caseFan.key = QStringLiteral("fan2");
        caseFan.label = QStringLiteral("fan2");
        caseFan.kind = SensorKind::Fan;
        caseFan.value = 0.0;
        SensorReading rail;
        rail.chip = fanChip.name;
        rail.key = QStringLiteral("power1");
        rail.label = QStringLiteral("cpu_power");
        rail.kind = SensorKind::Power;
        rail.value = 11.5 + 4.5 * std::abs(w1);
        fanChip.readings = {cpuFan, caseFan, rail};
    }
    s.sensors.chips = {cpuChip, boardChip, nvmeChip, fanChip};

    BatteryReading battery;
    battery.name = QStringLiteral("BAT0");
    battery.model = QStringLiteral("Primary");
    battery.manufacturer = QStringLiteral("HP");
    battery.technology = QStringLiteral("Li-ion");
    battery.state = BatteryState::Discharging;
    battery.capacityPercent = 18;
    battery.energyNowWh = 6.9;
    battery.energyFullWh = 38.1;
    battery.energyDesignWh = 41.0;
    battery.powerW = 8.4;
    battery.voltageV = 12.4;
    battery.cycleCount = 93;
    battery.timeToEmptyMin = 49;
    battery.temperatureC = 31.0;
    s.sensors.batteries = {battery};
    AdapterReading adapter;
    adapter.name = QStringLiteral("ADP1");
    adapter.online = false;
    s.sensors.adapters = {adapter};
    s.sensors.available = true;
    recomputeSensorTotals(s.sensors);

    // M82: a priced AWS instance so the Cloud page renders real numbers --
    // identity from IMDS, accrual since boot, and the focused app's share of
    // the hourly rate split by the CPU it actually used.
    const int cloudVcpus = 8;
    s.cloud.available = true;
    s.cloud.identity.provider = CloudProvider::Aws;
    s.cloud.identity.instanceId = QStringLiteral("i-0123456789abcdef0");
    s.cloud.identity.instanceType = QStringLiteral("c7g.2xlarge");
    s.cloud.identity.region = QStringLiteral("eu-west-1");
    s.cloud.identity.zone = QStringLiteral("eu-west-1b");
    s.cloud.identity.accountId = QStringLiteral("123456789012");
    s.cloud.identity.privateIp = QStringLiteral("10.0.3.17");
    s.cloud.identity.imageId = QStringLiteral("ami-0d3f6a4b2c1e9f8a7");
    s.cloud.identity.architecture = QStringLiteral("arm64");
    s.cloud.vcpus = cloudVcpus;
    s.cloud.uptimeSec = 41 * 86400 + qint64(tick) * 2;
    s.cloud.ratePerHour = 0.2920;
    s.cloud.rateKey = QStringLiteral("aws/c7g.2xlarge");
    s.cloud.rateSource =
        QStringLiteral("/home/dev/.config/syspeek/cloud-rates.txt");
    s.cloud.rateImportDate = QStringLiteral("2026-09-01");
    s.cloud.rateStaleDays = 56;
    const CloudCostMath cloudMath =
        computeCloudCost(s.cloud.ratePerHour, s.cloud.uptimeSec);
    s.cloud.accruedUsd = cloudMath.accruedUsd;
    s.cloud.projectedMonthlyUsd = cloudMath.projectedMonthlyUsd;
    s.cloud.appName = s.appName;
    s.cloud.appPid = s.pid.toInt();
    s.cloud.appCpuPercent = cpuPct;
    s.cloud.appCostPerHour =
        cloudProcessCostPerHour(s.cloud.ratePerHour, cpuPct, cloudVcpus);
    QList<CloudProcessSample> cloudRows;
    for (const ProcessRow &row : s.processes) {
        if (row.depth != 0) {
            continue;
        }
        cloudRows.append(CloudProcessSample{row.name, row.pid, row.cpuPercent});
    }
    s.cloud.consumers =
        cloudTopConsumers(cloudRows, s.cloud.ratePerHour, cloudVcpus, 4);

    // M83: the account listing. The instance is 96 days old, which is only
    // knowable from the provider, and four neighbours in the same region show
    // the fleet table and the $/MO roll-up.
    const qint64 shotNow = QDateTime::currentMSecsSinceEpoch();
    const qint64 selfAge = (96 * 86400 + qint64(tick) * 2) * 1000LL;
    s.cloud.instanceAgeSec = 96 * 86400 + qint64(tick) * 2;
    s.cloud.launchedAtMs = shotNow - selfAge;
    s.cloud.accruedLaunchUsd =
        computeCloudCost(s.cloud.ratePerHour, s.cloud.instanceAgeSec).accruedUsd;
    CloudFleetStatus fleet;
    fleet.attempted = true;
    fleet.available = true;
    fleet.provider = CloudProvider::Aws;
    fleet.cliName = QStringLiteral("aws");
    fleet.scope = QStringLiteral("eu-west-1");
    fleet.source = QStringLiteral("aws ec2 describe-instances --output json "
                                  "--region eu-west-1 --query "
                                  "Reservations[].Instances[]");
    fleet.fetchedAtMs = shotNow - 180000;

    const auto fleetRow = [&shotNow](const char *id, const char *name,
                                     const char *type, const char *state,
                                     qint64 ageDays, const char *zone) {
        CloudFleetInstance row;
        row.instanceId = QLatin1String(id);
        row.name = QLatin1String(name);
        row.instanceType = QLatin1String(type);
        row.state = QLatin1String(state);
        row.rawState = row.state;
        row.zone = QLatin1String(zone);
        row.region = cloudRegionFromZone(row.zone);
        row.launchedAtMs = shotNow - ageDays * 86400LL * 1000LL;
        return row;
    };
    fleet.rows = {
        fleetRow("i-0123456789abcdef0", "hud-box", "c7g.2xlarge", "running",
                 96, "eu-west-1b"),
        fleetRow("i-0feedfacecafe0001", "web-2", "t4g.small", "running", 12,
                 "eu-west-1a"),
        fleetRow("i-0feedfacecafe0002", "web-3", "t4g.small", "running", 12,
                 "eu-west-1a"),
        fleetRow("i-0feedfacecafe0003", "api-edge", "m7g.large", "running", 3,
                 "eu-west-1c"),
        fleetRow("i-0deadbeefdeadbe01", "batch-old", "t4g.small", "stopped",
                 47, "eu-west-1b"),
    };
    fleetApplyRates(
        fleet,
        parseCloudRateFile(QStringLiteral("aws/c7g.2xlarge = 0.2920\n"
                                          "aws/t4g.small = 0.0208\n"
                                          "aws/m7g.large = 0.0816\n")),
        730.0);
    fleetResolveAges(fleet, shotNow);
    fleetMarkSelf(fleet, s.cloud.identity);
    s.cloud.fleet = fleet;

    // M84: the footprint card. A measured sweep, so the render proves the
    // request count and budget both reach the page.
    s.cloud.footprint.enabled = true;
    s.cloud.footprint.inventory = true;
    s.cloud.footprint.cliName = fleet.cliName;
    s.cloud.footprint.probed = true;
    s.cloud.footprint.identified = true;
    s.cloud.footprint.lastSweepMs = 62;
    s.cloud.footprint.lastRequests = 4;
    s.cloud.footprint.ratesPath = QStringLiteral(
        "/home/you/.config/syspeek/cloud-rates.txt");
    s.cloud.footprint.spend = true;
    s.cloud.footprint.spendTimeoutMs = 30000;
    s.cloud.footprint.spendCommand = QStringLiteral(
        "aws ce get-cost-and-usage --time-period Start=2026-10-01,"
        "End=2026-10-29");

    // M85: a CE-shaped billed month, so the render proves the card, the
    // service split and the daily chart.
    s.cloud.spend.attempted = true;
    s.cloud.spend.available = true;
    s.cloud.spend.provider = CloudProvider::Aws;
    s.cloud.spend.cliName = QStringLiteral("aws");
    s.cloud.spend.scope = QStringLiteral("account 123456789012");
    s.cloud.spend.source = QStringLiteral(
        "aws ce get-cost-and-usage --time-period Start=2026-10-01,"
        "End=2026-10-29 --granularity DAILY --metrics UnblendedCost");
    s.cloud.spend.periodStart = QStringLiteral("2026-10-01");
    s.cloud.spend.periodEnd = QStringLiteral("2026-10-29");
    s.cloud.spend.monthToDateUsd = 452.10;
    s.cloud.spend.forecastRemainingUsd = 331.02;
    s.cloud.spend.forecastUsd = 783.12;
    s.cloud.spend.services = {
        CloudSpendService{
            QStringLiteral("Amazon Elastic Compute Cloud - Compute"), 380.40},
        CloudSpendService{QStringLiteral("Amazon Simple Storage Service"), 51.70},
        CloudSpendService{QStringLiteral("Amazon CloudWatch"), 20.00},
    };
    for (int day = 1; day <= 28; ++day) {
        s.cloud.spend.days.append(
            CloudSpendDay{QStringLiteral("2026-10-%1").arg(day, 2, 10,
                                                           QLatin1Char('0')),
                          12.0 + double((day * 7) % 9)});
    }

    // M84: the console measures itself. A measured footprint and a 62 ms tick
    // with named sections, so the Settings render shows real rows.
    s.self.available = true;
    s.self.warming = false;
    s.self.cpuPercent = 6.04;
    s.self.rssKb = 36864.0;
    {
        QList<TickSection> sections;
        sections.append(TickSection{QStringLiteral("processes"), 41.2});
        sections.append(TickSection{QStringLiteral("host"), 12.4});
        sections.append(TickSection{QStringLiteral("sockets"), 4.1});
        sections.append(TickSection{QStringLiteral("gpu"), 1.2});
        s.tick = tickbudget::summarize(
            sections, 62.0,
            QList<double>{54.0, 48.0, 62.0, 84.0, 51.0});
    }

    return s;
}

KubeSnapshot demoKube() {
    KubeSnapshot kube;
    kube.state = KubeState::Ok;
    kube.totalPods = 18;
    kube.issueCount = 4;

    auto mkPod = [](const char *ns, const char *name, const char *phase,
                    const char *reason, int restarts, double cpuMillis,
                    long long memBytes) {
        KubePod pod;
        pod.ns = QString::fromUtf8(ns);
        pod.name = QString::fromUtf8(name);
        pod.phase = QString::fromUtf8(phase);
        pod.reason = QString::fromUtf8(reason);
        pod.restarts = restarts;
        pod.cpuMillis = cpuMillis;
        pod.memBytes = memBytes;
        return pod;
    };
    kube.pods = {
        mkPod("default", "api-7f9c", "Running", "CrashLoopBackOff", 7, 512.0,
              503LL * 1024 * 1024),
        mkPod("default", "web-5f8d", "Running", "", 0, 22.0,
              64LL * 1024 * 1024),
        mkPod("staging", "worker-2b1a", "Running", "", 0, 310.0,
              288LL * 1024 * 1024),
        mkPod("prod", "cache-redis-0", "Running", "", 0, 41.0,
              96LL * 1024 * 1024),
        mkPod("batch", "nightly-rollup-6x2", "Pending", "Insufficient cpu", 2,
              -1.0, -1),
        mkPod("monitoring", "metrics-agent-9dk4p", "Running", "", 0, 7.0,
              48LL * 1024 * 1024)};

    auto mkDeploy = [](const char *ns, const char *name, int desired,
                       int available) {
        KubeDeployment deploy;
        deploy.ns = QString::fromUtf8(ns);
        deploy.name = QString::fromUtf8(name);
        deploy.desired = desired;
        deploy.available = available;
        deploy.unavailable = desired - available;
        return deploy;
    };
    kube.deployments = {mkDeploy("default", "api", 3, 2),
                        mkDeploy("default", "web", 2, 2),
                        mkDeploy("staging", "worker", 2, 2),
                        mkDeploy("monitoring", "metrics-agent", 1, 1)};

    auto mkEvent = [](const char *ns, const char *name, const char *reason,
                      const char *message, qint64 agoMs, int count) {
        KubeEvent event;
        event.ns = QString::fromUtf8(ns);
        event.involvedName = QString::fromUtf8(name);
        event.type = QStringLiteral("Warning");
        event.reason = QString::fromUtf8(reason);
        event.message = QString::fromUtf8(message);
        event.lastTimestampMs = QDateTime::currentMSecsSinceEpoch() - agoMs;
        event.count = count;
        return event;
    };
    kube.events = {
        mkEvent("default", "api-7f9c", "BackOff",
                "Back-off restarting failed container api", 120000, 7),
        mkEvent("batch", "nightly-rollup-6x2", "FailedScheduling",
                "0/2 nodes are available: 2 Insufficient cpu", 400000, 3)};
    kube.eventCount = kube.events.size();

    auto mkNode = [](const char *name, double capacityCores,
                     unsigned long long capacityKiB, int running,
                     double cpuMillis, long long memBytes) {
        KubeNode node;
        node.name = QString::fromUtf8(name);
        node.ready = true;
        node.roles = QStringLiteral("worker");
        node.kubeletVersion = QStringLiteral("v1.31.4");
        node.runtime = QStringLiteral("containerd://2.0.4");
        node.osImage = QStringLiteral("Ubuntu 24.04.1 LTS");
        node.instanceType = QStringLiteral("m7g.2xlarge");
        node.zone = QStringLiteral("eu-west-1a");
        node.region = QStringLiteral("eu-west-1");
        node.cpuCapacityCores = capacityCores;
        node.cpuAllocatableCores = capacityCores - 0.1;
        node.memCapacityBytes = capacityKiB * 1024;
        node.memAllocatableBytes = (capacityKiB - 300000) * 1024;
        node.podsCapacity = 110;
        node.podsAllocatable = 110;
        node.podsRunning = running;
        node.cpuMillis = cpuMillis;
        node.memBytes = memBytes;
        return node;
    };
    kube.nodes = {
        mkNode("ip-10-0-1-11", 8.0, 16384000ULL, 7, 1890.0,
               6200LL * 1024 * 1024),
        mkNode("ip-10-0-2-24", 4.0, 8192000ULL, 5, 610.0,
               2400LL * 1024 * 1024)};
    kube.metricsOk = true;
    kube.podsWithUsage = 5;
    kube.nodesWithUsage = 2;
    kube.podsCpuMillis = 892.0;
    kube.podsMemBytes = 999LL * 1024 * 1024;

    kube.costPods = 18;
    kube.costMissingRequests = 1;
    kube.costMonthly = 1180.0;
    kube.costCpuPerCoreHour = 0.031611;
    kube.costMemPerGbHour = 0.004237;
    kube.costCpuMonthly = 864.0;
    kube.costMemMonthly = 316.0;
    kube.costCpuEfficiency = 0.42;
    kube.costMemEfficiency = 0.61;
    kube.costWastedMonthly = 512.40;
    kube.costEfficiencyPods = 9;
    kube.costByNamespace = {{QStringLiteral("prod"), 712.0},
                            {QStringLiteral("staging"), 268.0},
                            {QStringLiteral("dev"), 200.0}};
    kube.costTopWorkloads = {{QStringLiteral("prod/api-7f9c"), 486.20},
                             {QStringLiteral("prod/web-5f8d"), 226.10},
                             {QStringLiteral("staging/worker-2b1a"), 148.60}};
    return kube;
}

// M86: one plausible evening on this machine, spread across the demo
// window so the stream grows as the fixture advances and the chart ticks
// appear where the story says they should.
QList<TimelineEvent> demoTimelineEvents(qint64 baseMs) {
    auto at = [baseMs](int tick) { return baseMs + qint64(tick) * 2000; };
    auto make = [](qint64 atMs, TimelineKind kind, TimelineSeverity severity,
                   const QString &subject, const QString &title,
                   const QString &detail = QString(),
                   const QStringList &tags = QStringList()) {
        TimelineEvent event;
        event.atMs = atMs;
        event.kind = kind;
        event.severity = severity;
        event.subject = subject;
        event.title = title;
        event.detail = detail;
        event.tags = tags;
        return event;
    };
    return {
        make(at(30), TimelineKind::Alert, TimelineSeverity::Warning,
             QStringLiteral("monitor:app_mem"),
             QStringLiteral("Focused app memory high"),
             QStringLiteral("rss 612 MB > 512 MB for 3 samples"),
             {QStringLiteral("monitor:app_mem"),
              QStringLiteral("severity:warning")}),
        make(at(62), TimelineKind::Kubernetes, TimelineSeverity::Info,
             QStringLiteral("kube:default/web-5f8d"),
             QStringLiteral("pod restarted (backoff 0s)"),
             QStringLiteral("container web terminated with exit 0"),
             {QStringLiteral("kind:Pod"), QStringLiteral("ns:default")}),
        make(at(88), TimelineKind::Kubernetes, TimelineSeverity::Warning,
             QStringLiteral("kube:default/api-7f9c"),
             QStringLiteral("CrashLoopBackOff (4 restarts)"),
             QStringLiteral("back-off restarting failed container api"),
             {QStringLiteral("kind:Pod"), QStringLiteral("ns:default")}),
        make(at(90), TimelineKind::Synthetic, TimelineSeverity::Error,
             QStringLiteral("check:https://api.internal/health"),
             QStringLiteral("https://api.internal/health down"),
             QStringLiteral("connection refused after 5002 ms"),
             {QStringLiteral("status:503")}),
        make(at(100), TimelineKind::Alert, TimelineSeverity::Error,
             QStringLiteral("monitor:host_cpu"),
             QStringLiteral("Host CPU high"),
             QStringLiteral("94.2% > 90.0% for 5 samples"),
             {QStringLiteral("monitor:host_cpu"),
              QStringLiteral("severity:critical")}),
        make(at(110), TimelineKind::Synthetic, TimelineSeverity::Ok,
             QStringLiteral("check:https://api.internal/health"),
             QStringLiteral("https://api.internal/health up again (41 ms)")),
        make(at(118), TimelineKind::Service, TimelineSeverity::Warning,
             QStringLiteral("unit:postgresql.service"),
             QStringLiteral("failed (exit-code 1)"),
             QStringLiteral("pg_ctl: could not start server"),
             {QStringLiteral("unit:postgresql.service")}),
        make(at(120), TimelineKind::Kubernetes, TimelineSeverity::Ok,
             QStringLiteral("kube:default/api-7f9c"),
             QStringLiteral("Running (ready 1/1)")),
        make(at(143), TimelineKind::FileChange, TimelineSeverity::Warning,
             QStringLiteral("file:/etc/pam.d/login"),
             QStringLiteral("modified /etc/pam.d/login"),
             QStringLiteral("sha256 9be2c41f -> 1d77a0b6"),
             {QStringLiteral("sha:1d77a0b6")}),
        make(at(155), TimelineKind::ContainerAction, TimelineSeverity::Info,
             QStringLiteral("container:api-7f9c2e10b4a8"),
             QStringLiteral("docker restart"),
             QStringLiteral("docker restart 7f9c2e10b4a8 ok"),
             {QStringLiteral("result:ok")}),
        make(at(170), TimelineKind::Alert, TimelineSeverity::Ok,
             QStringLiteral("monitor:app_mem"),
             QStringLiteral("app_mem recovered")),
        make(at(201), TimelineKind::FileChange, TimelineSeverity::Warning,
             QStringLiteral("file:/usr/local/bin/syspeek-agent"),
             QStringLiteral("created /usr/local/bin/syspeek-agent"),
             QStringLiteral("sha256 4c1e8f20")),
        make(at(212), TimelineKind::Cloud, TimelineSeverity::Info,
             QStringLiteral("cloud:aws/i-0a1b2c3d"),
             QStringLiteral("fleet grew to 5 running instances"),
             QStringLiteral("new instance launched in eu-west-1c"),
             {QStringLiteral("provider:aws")}),
    };
}

void tracePhase(const char *what) {
    std::fprintf(stderr, "[shot] %s\n", what);
    std::fflush(stderr);
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    const bool panelMode = argc > 2
        && QString::fromUtf8(argv[2]) == QStringLiteral("panel");

    if (!panelMode) {
        HudWindow hud;
        hud.setSnapshot(demoSnapshot(0));
        hud.show();
        app.processEvents();
        const QString out = argc > 1 ? QString::fromUtf8(argv[1])
                                     : QStringLiteral("hud.png");
        const bool ok = hud.grab().save(out);
        std::printf("%s: %s\n", ok ? "saved" : "failed",
                    out.toUtf8().constData());
        return ok ? 0 : 2;
    }

    DashboardPanel panel;
    tracePhase("panel constructed");
    auto driveHistory = [&app, &panel]() {        // Synthetic alert lifecycle: quiet -> warning -> +critical ->
        // resolve -> clear -> refire, so tiles/chart/timeline all move.
        const qint64 baseMs = QDateTime::currentMSecsSinceEpoch()
                              - 241 * 2000;
        const QList<TimelineEvent> timelineEvents = demoTimelineEvents(baseMs);
        auto alertsFor = [baseMs](int tick) {
            QList<Alert> list;
            auto add = [&](const QString &id, const QString &title,
                           const QString &detail, const QString &sev,
                           double value) {
                Alert alert;
                alert.id = id;
                alert.title = title;
                alert.detail = detail;
                alert.severity = sev;
                alert.value = value;
                alert.atMs = baseMs + tick * 2000;
                list.append(alert);
            };
            if (tick >= 30 && !(tick >= 170 && tick < 190)) {
                add(QStringLiteral("a-mem"),
                    QStringLiteral("app memory"),
                    QStringLiteral("rss 612 MB (limit 512)"),
                    QStringLiteral("warn"), 612.0);
            }
            if (tick >= 100 && !(tick >= 140 && tick < 190)) {
                add(QStringLiteral("a-cpu"), QStringLiteral("host cpu"),
                    QStringLiteral("94.2% (threshold 90)"),
                    QStringLiteral("high"), 94.2);
            }
            return list;
        };
        // 240 synthetic ticks: fills the chart rings so the panels show
        // plausible series instead of two-point stubs.
        for (int i = 0; i < 240; ++i) {
            panel.setSnapshot(demoSnapshot(i));
            panel.setAlerts(alertsFor(i));
            app.processEvents();
        }
        panel.setSnapshot(demoSnapshot(240));
        panel.setAlerts(alertsFor(240));
        // Alertmanager-style grouping through the real tracker (M35), so
        // the Incidents page shows grouped folds, re-fires and resolves.
        // A real SloTracker (M27, Nobl9 model) rides the same story:
        // critical windows count as bad availability samples.
        IncidentTracker tracker(12500, 20);
        SloTracker sloTracker;
        DockerSnapshot docker;
        docker.state = DockerState::Ok;
        DockerContainer db;
        db.id = QStringLiteral("c1");
        db.name = QStringLiteral("postgres");
        db.image = QStringLiteral("postgres:16");
        db.state = QStringLiteral("running");
        db.status = QStringLiteral("Up 5 minutes (unhealthy)");
        db.health = QStringLiteral("unhealthy");
        DockerContainer web;
        web.id = QStringLiteral("c2");
        web.name = QStringLiteral("web");
        web.image = QStringLiteral("nginx:alpine");
        web.state = QStringLiteral("running");
        web.status = QStringLiteral("Up 2 days (healthy)");
        web.health = QStringLiteral("healthy");
        db.hasDepth = true;
        db.depth.cpuPercent = 14.2;
        db.depth.cpuCores = 12;
        db.depth.memUsedBytes = 380 * 1000ULL * 1000ULL;
        db.depth.memLimitBytes = 2ULL * 1000ULL * 1000ULL * 1000ULL;
        db.depth.hasMem = true;
        db.depth.netRxBytes = 48'200'000ULL;
        db.depth.netTxBytes = 12'400'000ULL;
        db.depth.hasNet = true;
        db.depth.pids = 19;
        web.hasDepth = true;
        web.depth.cpuPercent = 61.7;
        web.depth.cpuCores = 12;
        web.depth.memUsedBytes = 54 * 1000ULL * 1000ULL;
        web.depth.hasMem = true;
        web.depth.netRxBytes = 1'442'100'000ULL;
        web.depth.netTxBytes = 318'200'000ULL;
        web.depth.hasNet = true;
        web.depth.blkReadBytes = 1'204'000'000ULL;
        web.depth.blkWriteBytes = 92'400'000ULL;
        web.depth.hasBlk = true;
        web.depth.pids = 6;
        docker.containers = {db, web};
        KubeSnapshot kube = demoKube();
        // Services story (M61n): three units, one crash-looping, so the
        // FAILED tile flaps🗴 and the timeline carries a restart row.
        ServicesSnapshot svc;
        svc.available = true;
        auto mkUnit = [](const char *name, const char *active,
                         const char *sub) {
            UnitRow row;
            row.name = QString::fromUtf8(name);
            row.load = QStringLiteral("loaded");
            row.active = QString::fromUtf8(active);
            row.sub = QString::fromUtf8(sub);
            row.description = row.name + QStringLiteral(" service");
            return row;
        };
        svc.units.append(mkUnit("ssh.service", "active", "running"));
        svc.units.append(mkUnit("cron.service", "failed", "failed"));
        svc.units.append(mkUnit("nginx.service", "active", "running"));
        svc.runningCount = 2;
        svc.failedCount = 1;
        // Synthetic fixture declared beside the other riding stories.
        SyntheticSnapshot synthetic;
        synthetic.available = true;
        // APM story (M61v): three services with drifting latencies +
        // error burst on a 41-tick cycle so the ERRORS tile goes red.
        TraceSnapshot trace;
        trace.available = true;
        // DBM story (M61x): cache-hit around 98%, commits/sec drift,
        // rollbacks and deadlocks on long cycles.
        DbmSnapshot dbm;
        dbm.available = true;
        dbm.database = QStringLiteral("shop_prod");
        // FIM story (M61w): 9 watched paths, drift flapping 0..2.
        FimSnapshot fim;
        fim.available = true;
        fim.enabled = true;
        fim.watchedPaths = 9;
        fim.fileCount = 1408;
        fim.algorithm = QStringLiteral("sha256");
        ServiceChange svcChange;
        svcChange.unit = QStringLiteral("nginx.service");
        svcChange.summary = QStringLiteral("restart (worker reset)");
        svc.changes.append(svcChange);
        // Journal story (M61o/M61p): a mixed-priority window with one
        // recurring error group; errorCount flaps upward mid-run.
        JournalSnapshot journal;
        journal.available = true;
        auto mkEntry = [](int prio, int pid, const char *comm,
                          const char *msg) {
            JournalEntry entry;
            entry.priority = prio;
            entry.pid = pid;
            entry.comm = QString::fromUtf8(comm);
            entry.message = QString::fromUtf8(msg);
            return entry;
        };
        journal.entries.append(
            mkEntry(6, 1, "systemd", "Started nightly refresh"));
        journal.entries.append(
            mkEntry(4, 456, "postgres", "checkpoint starting"));
        journal.entries.append(
            mkEntry(3, 456, "postgres", "db.connect refused (max_slots)"));
        ErrorGroup dbGroup;
        dbGroup.fingerprint = QStringLiteral("pg.connect");
        dbGroup.comm = QStringLiteral("postgres");
        dbGroup.sample = QStringLiteral("db.connect refused (max_slots)");
        dbGroup.count = 3;
        journal.errorGroups.append(dbGroup);
        // Network-path story rides the same loop (M61m): two mtr
        // targets; the primary's RTT drifts and one hop flaps loss.
        PathSnapshot path;
        path.available = true;
        auto mkRoute = [](const QString &dst, int hops) {
            PathReport report;
            report.dst = dst;
            report.src = QStringLiteral("wlp0s20f3");
            report.tests = 10;
            report.ok = true;
            const QStringList hosts = {QStringLiteral("gateway.local"),
                                       QStringLiteral("core1-rtr.isp"),
                                       QStringLiteral("edge1.isp"),
                                       QStringLiteral("peer TransitNet"), dst};
            for (int h = 0; h < hops; ++h) {
                PathHop hop;
                hop.index = h + 1;
                hop.host = hosts.at(h);
                hop.sent = 10;
                hop.bestMs = 2.0 + h * 1.5;
                hop.worstMs = 8.0 + h * 2.0;
                hop.stdevMs = 0.4 + h * 0.2;
                hop.avgMs = (hop.bestMs + hop.worstMs) / 2.0;
                hop.lastMs = hop.avgMs;
                hop.lossPercent = 0.0;
                report.hops.append(hop);
            }
            return report;
        };
        path.reports.append(mkRoute(QStringLiteral("1.1.1.1"), 5));
        path.reports.append(mkRoute(QStringLiteral("example.test"), 3));
        auto hasCrit = [](const QList<Alert> &list) {
            for (const Alert &alert : list) {
                if (alert.severity.compare(QLatin1String("critical"),
                                           Qt::CaseInsensitive) == 0
                    || alert.severity.compare(QLatin1String("high"),
                                              Qt::CaseInsensitive) == 0) {
                    return true;
                }
            }
            return false;
        };
        for (int i = 0; i < 240; ++i) {
            tracker.update(alertsFor(i), baseMs + i * 2000);
            sloTracker.record(!hasCrit(alertsFor(i)), baseMs + i * 2000);
            // Container fleet drifts: one cadence bump + flapping issues;
            // Kubernetes pod count creeps upward with the story.
            docker.runningCount = 2 + (i % 13 <= 1 ? 1 : 0);
            docker.issueCount = 1 + (i % 31 < 8 ? 1 : 0);
            kube.totalPods = 12 + i / 40;
            kube.issueCount = 2 + (i > 150 ? 2 : 0);
            // Cost drifts with the fleet so the estimate history has shape;
            // the split keeps the cpu/ram ratio of the fixture.
            kube.costMonthly = 1180.0 + 46.0 * std::sin(i / 26.0);
            kube.costCpuMonthly = kube.costMonthly * (864.0 / 1180.0);
            kube.costMemMonthly = kube.costMonthly - kube.costCpuMonthly;
            path.reports.first().hops[2].lossPercent = (i % 29 < 6) ? 2.5 : 0.0;
            path.reports.first().hops[4].avgMs = 12.0 + (i % 17) * 2.0;
            path.reports.first().hops[4].lastMs = path.reports.first().hops[4].avgMs;
            svc.failedCount = (i % 37 < 9) ? 1 : 0;
            journal.errorCount = (i % 19 < 6) ? 4 : 1;
            dbGroup.count = 1 + (i % 11);
            journal.errorGroups.clear();
            journal.errorGroups.append(dbGroup);
            // Synthetic story (M61t): two probes, latency drifts and
            // one probe fails on a 53-tick cycle so DOWN flaps 0<->1.
            synthetic.available = true;
            ProbeResult probeUp;
            probeUp.url = QStringLiteral("https://local-app/status");
            probeUp.success = true;
            probeUp.status = 200;
            probeUp.totalMs = 120.0 + (i % 23) * 6.0;
            probeUp.connectMs = 14.0;
            probeUp.tlsMs = 22.0;
            synthetic.results = {probeUp};
            synthetic.upCount = 1;
            synthetic.downCount = 0;
            if (i % 53 < 7) {
                ProbeResult probeDown;
                probeDown.url = QStringLiteral("https://backup/status");
                probeDown.success = false;
                probeDown.error = QStringLiteral("connection timeout");
                probeDown.totalMs = 5000.0;
                synthetic.results.append(probeDown);
                synthetic.downCount = 1;
                synthetic.upCount = 0;
            }
            // APM flow: spans creep, latency drifts per service, errors
            // burst on a 41-tick cycle (red tile + map unchanged).
            const double wa = std::sin(i / 18.0);
            const double wb = std::sin(i / 30.0 + 1.2);
            const double wc = std::sin(i / 40.0 + 2.4);
            trace.traceCount = 40 + i / 6;
            trace.spanCount = 900 + i * 7;
            trace.errorCount = (i % 41 < 5) ? 12 : 0;
            trace.services = {
                {QStringLiteral("api"), 20 + i / 4, trace.errorCount,
                 120.0 + 26.0 * wa, 310.0, 240.0},
                {QStringLiteral("web"), 12 + i / 7, 0,
                 84.0 + 18.0 * wb, 190.0, 150.0},
                {QStringLiteral("billing-db"), 6 + i / 11, 0,
                 44.0 + 9.0 * wc, 120.0, 96.0},
            };
            trace.sampledAtMs = baseMs + i * 2000;
            // Postgres story: cache-hit ~98%, commits/sec drifts 30..80,
            // rollback spikes on a 67-tick cycle, deadlocks rare.
            dbm.cacheHitPercent = 98.4 + 0.8 * wc;
            dbm.commitsPerSec = 48.0 + 21.0 * wb;
            dbm.xactCommit = static_cast<quint64>(i * 51);
            dbm.xactRollback = (i % 67 < 4) ? 7 : 0;
            dbm.deadlocks = (i % 137 < 3) ? 2 : 0;
            dbm.connections = 24;
            dbm.sizeBytes = static_cast<quint64>(8.4e9);
            dbm.blkReadTimeMs = 3.0 + 2.0 * std::abs(wc);
            dbm.blkWriteTimeMs = 6.0 + 3.0 * std::abs(wb);
            dbm.commitsPerSec = std::max(6.0, dbm.commitsPerSec);
            // FIM drift: 0..2 modified files flapping on a 43-tick cycle.
            fim.changes.clear();
            if (i % 43 < 4) {
                FimChange change;
                change.kind = FimChangeKind::Modified;
                change.path = QStringLiteral("/etc/hosts");
                change.hash = QStringLiteral("a15f…");
                change.atMs = baseMs + i * 2000;
                fim.changes.append(change);
            }
            if (i % 43 > 2 && i % 31 < 5) {
                FimChange change;
                change.kind = FimChangeKind::Modified;
                change.path = QStringLiteral("/etc/pam.d/login");
                change.hash = QStringLiteral("9be2…");
                change.atMs = baseMs + i * 2000;
                fim.changes.append(change);
            }
            fim.sampledAtMs = baseMs + i * 2000;
            DisplaySnapshot snap = demoSnapshot(i);
            // The fleet chart needs movement: a constant series sits on the
            // top gridline and reads as an empty plot box.
            snap.cloud.fleet.running = 4 + (i % 37 < 9 ? 1 : 0);
            snap.slo = sloTracker.report(SloSpec{});
            panel.setSnapshot(snap);
            panel.setDockerSnapshot(docker);
            panel.setKubeSnapshot(kube);
            panel.setPathSnapshot(path);
            panel.setServicesSnapshot(svc);
            panel.setJournalSnapshot(journal);
            panel.setSyntheticSnapshot(synthetic);
            panel.setTraceSnapshot(trace);
            panel.setDbmSnapshot(dbm);
            panel.setFimSnapshot(fim);
            panel.setAlerts(alertsFor(i));
            panel.setIncidents(tracker.incidents());
            {
                const QList<TimelineEvent> all = timelineEvents;
                TimelineViewInfo tl;
                tl.enabled = true;
                tl.markers = true;
                tl.capacity = 2000;
                const qint64 nowMs = baseMs + qint64(i) * 2000;
                for (const TimelineEvent &event : all) {
                    if (event.atMs > nowMs) {
                        break;
                    }
                    tl.events.prepend(event);
                }
                tl.count = int(tl.events.size());
                if (!tl.events.isEmpty()) {
                    tl.newestMs = tl.events.constFirst().atMs;
                    tl.oldestMs = tl.events.constLast().atMs;
                }
                panel.setTimeline(tl);
            }
            app.processEvents();
        }
        tracker.update(alertsFor(240), baseMs + 240 * 2000);
        sloTracker.record(!hasCrit(alertsFor(240)), baseMs + 240 * 2000);
        docker.runningCount = 3;
        docker.issueCount = 2;
        kube.totalPods = 18;
        kube.issueCount = 4;
        DisplaySnapshot finalSnap = demoSnapshot(240);
        finalSnap.slo = sloTracker.report(SloSpec{});
        panel.setSnapshot(finalSnap);
        panel.setDockerSnapshot(docker);
        panel.setKubeSnapshot(kube);
        panel.setPathSnapshot(path);
        panel.setServicesSnapshot(svc);
        panel.setJournalSnapshot(journal);
        panel.setSyntheticSnapshot(synthetic);
        panel.setTraceSnapshot(trace);
        panel.setDbmSnapshot(dbm);
        panel.setFimSnapshot(fim);
        panel.setAlerts(alertsFor(240));
        panel.setIncidents(tracker.incidents());
        {
            TimelineViewInfo tl;
            tl.enabled = true;
            tl.markers = true;
            tl.capacity = 2000;
            tl.events = timelineEvents;
            tl.count = int(tl.events.size());
            tl.newestMs = tl.events.constFirst().atMs;
            tl.oldestMs = tl.events.constLast().atMs;
            panel.setTimeline(tl);
        }
    };
    StatsdSnapshot statsd;
    auto driveStatsd = [&app, &panel, &statsd]() -> void {
        // Synthetic DogStatsD receiver history: cumulative counters + a
        // few series so the Metrics page renders tiles, rate charts and
        // the explorer table populated. Two parse bursts exercise the
        // red tile.
        statsd.available = true;
    auto appendSeries = [&statsd](const char *name, const char *type,
                                  const char *tags, double last, double sum,
                                  double avg, double max, quint64 count,
                                  quint64 unique = 0) {
        StatsdSeries s;
        s.name = QString::fromUtf8(name);
        s.type = QString::fromUtf8(type);
        s.tags = QString::fromUtf8(tags);
        s.last = last;
        s.sum = sum;
        s.min = 0.0;
        s.max = max;
        s.avg = avg;
        s.count = count;
        s.unique = unique;
        statsd.series.append(s);
    };
    appendSeries("app.checkout.count", "counter", "env:dev,service:web",
                 0.0, 1422.0, 0.0, 0.0, 237);
    appendSeries("app.reply_ms", "timer", "endpoint:checkout",
                 4.0, 0.0, 33.5, 210.7, 196);
    appendSeries("cache.hit", "gauge", "pool:main", 0.87, 0.0,
                 0.83, 0.95, 203);
    appendSeries("session.users", "set", "env:dev", 0.0, 0.0, 0.0, 0.0,
                 31, 71);
    appendSeries("shopist.page.views", "counter", "page:home", 0.0,
                 9821.0, 0.0, 0.0, 989);
    appendSeries("request.errors", "counter", "code:503", 0.0,
                 14.0, 0.0, 0.0, 14);
    appendSeries("cart.abandoned", "histogram", "env:dev", 0.0, 0.0,
                 0.031, 0.2, 85);
    {
        quint64 metrics = 0;
        quint64 events = 0;
        quint64 checks = 0;
        quint64 errors = 0;
        std::srand(7);
        for (int i = 0; i < 241; ++i) {
            const int burst = (i > 150 && i < 175) ? (i % 5 == 0 ? 2 : 0)
                                                   : 0;
            const quint64 perTick = 9 + std::rand() % 7;
            metrics += perTick;
            if (i % 19 == 0) {
                events += 1;
            }
            if (i % 27 == 0) {
                checks += 1;
            }
            errors += perTick % 17 == 0 ? quint64(1) : quint64(0);
            errors += quint64(burst);
            statsd.datagrams += perTick;
            statsd.metrics = metrics;
            statsd.events = events;
            statsd.serviceChecks = checks;
            statsd.parseErrors = errors;
            statsd.sampledAtMs = 0;
            panel.setStatsdSnapshot(statsd);
            app.processEvents();
        }
    }
};

    DockerSnapshot docker;
    docker.state = DockerState::Ok;
    docker.runningCount = 2;
    docker.issueCount = 1;
    DockerContainer db;
    db.id = QStringLiteral("c1");
    db.name = QStringLiteral("postgres");
    db.image = QStringLiteral("postgres:16");
    db.state = QStringLiteral("running");
    db.status = QStringLiteral("Up 5 minutes (unhealthy)");
    db.health = QStringLiteral("unhealthy");
    DockerContainer web;
    web.id = QStringLiteral("c2");
    web.name = QStringLiteral("web");
    web.image = QStringLiteral("nginx:alpine");
    web.state = QStringLiteral("running");
    web.status = QStringLiteral("Up 2 days (healthy)");
    web.health = QStringLiteral("healthy");
    db.hasDepth = true;
    db.depth.cpuPercent = 14.2;
    db.depth.cpuCores = 12;
    db.depth.memUsedBytes = 380 * 1000ULL * 1000ULL;
    db.depth.memLimitBytes = 2ULL * 1000ULL * 1000ULL * 1000ULL;
    db.depth.hasMem = true;
    db.depth.netRxBytes = 48'200'000ULL;
    db.depth.netTxBytes = 12'400'000ULL;
    db.depth.hasNet = true;
    db.depth.pids = 19;
    web.hasDepth = true;
    web.depth.cpuPercent = 61.7;
    web.depth.cpuCores = 12;
    web.depth.memUsedBytes = 54 * 1000ULL * 1000ULL;
    web.depth.hasMem = true;
    web.depth.netRxBytes = 1'442'100'000ULL;
    web.depth.netTxBytes = 318'200'000ULL;
    web.depth.hasNet = true;
    web.depth.blkReadBytes = 1'204'000'000ULL;
    web.depth.blkWriteBytes = 92'400'000ULL;
    web.depth.hasBlk = true;
    web.depth.pids = 6;
    docker.containers = {db, web};
    panel.setDockerSnapshot(docker);

    KubeSnapshot kube = demoKube();
    panel.setKubeSnapshot(kube);

    QList<Alert> alerts;
    Alert alert;
    alert.id = QStringLiteral("host_cpu_high");
    alert.title = QStringLiteral("host CPU high");
    alert.detail = QStringLiteral("host CPU 94% over the last 20s");
    alert.severity = QStringLiteral("high");
    alert.value = 94.0;
    alert.atMs = QDateTime::currentMSecsSinceEpoch();
    alerts.append(alert);
    panel.setAlerts(alerts);

    QList<Incident> incidents;
    Incident incident;
    incident.id = QStringLiteral("inc-1");
    incident.key = QStringLiteral("host_cpu_high");
    incident.title = QStringLiteral("host CPU high");
    incident.severity = QStringLiteral("high");
    incident.status = QStringLiteral("open");
    incident.openedAt = QDateTime::currentMSecsSinceEpoch() - 300000;
    incident.lastSeen = QDateTime::currentMSecsSinceEpoch();
    incident.count = 3;
    incident.timeline = {"opened", "updated x2"};
    incidents.append(incident);
    panel.setIncidents(incidents);

    SettingsInfo settingsInfo;
    settingsInfo.version = QStringLiteral(SYSPEEK_APP_VERSION);
    settingsInfo.configPath = QStringLiteral("~/.config/Syspeek/syspeek.conf");
    settingsInfo.logPath = QStringLiteral("~/.cache/dev.syspeek/syspeek/hud.log");
    settingsInfo.autostartLine =
        QStringLiteral("on (systemd user unit syspeek.service)");
    settingsInfo.verbsLine =
        QStringLiteral("install.sh run|start|stop|restart|status|enable|"
                       "disable|update|uninstall|logs|vers");
    settingsInfo.metricsLine = QStringLiteral(
        "http://127.0.0.1:9107/metrics (no auth, loopback only)");
    settingsInfo.webhookLine =
        QStringLiteral("generic -> https://alerts.example.test/syspeek"
                       " - 4 sent, 0 failed; last ok 17:42 (1 firing)");
    settingsInfo.exportLine =
        QStringLiteral("csv - 412 samples -> ~/.cache/dev.syspeek/syspeek/"
                       "syspeek-history-20261026-174210.csv");
    settingsInfo.historyLine =
        QStringLiteral("3600 samples max - ~/.cache/dev.syspeek/syspeek/"
                       "history.ring");
    settingsInfo.overlayLine = QStringLiteral(
        "wm GNOME Shell - pinned above - follows every desktop - "
        "click-through on - 430x191 at 200,120");
    settingsInfo.cloudLine = cloudSettingsLine(
        true,
        QStringLiteral("/home/dev/.config/syspeek/cloud-rates.txt"),
        parseCloudRateFile(QStringLiteral("# imported 2026-09-01\n"
                                          "aws/c7g.2xlarge = 0.2920\n")),
        QDateTime(QDate(2026, 10, 28), QTime(12, 0)).toMSecsSinceEpoch());
    settingsInfo.sampleIntervalMs = 1000;
    settingsInfo.cpuWarnPercent = 70.0;
    settingsInfo.cpuHighPercent = 90.0;
    settingsInfo.rssWarnGb = 1.0;
    panel.setSettingsInfo(settingsInfo);
    panel.setMonitors(defaultAlertRules());

    {
        // M75 silences: two live quiet windows and one expired, so the
        // Alerts page shows counts, remaining times and the muted-alert mark.
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        QList<SilenceRule> silences;
        auto addSilence = [nowMs, &silences](const QString &matcher,
                                            const QString &comment,
                                            qint64 startOffsetMs,
                                            qint64 endOffsetMs) {
            SilenceRule rule;
            rule.matcher = matcher;
            rule.startsAtMs = nowMs + startOffsetMs;
            rule.endsAtMs = nowMs + endOffsetMs;
            rule.author = QStringLiteral("ops");
            rule.comment = comment;
            silences.append(rule);
        };
        addSilence(QStringLiteral("host_cpu"),
                   QStringLiteral("index rebuild window"), -420000, 1500000);
        addSilence(QStringLiteral("a-cpu"),
                   QStringLiteral("same window, alert id"), -420000, 1500000);
        addSilence(QStringLiteral("disk"), QStringLiteral("one-off cleanup"),
                   -7200000, -3600000);
        panel.setSilences(silences);
    }

    auto driveHistoryLog = [&app, &panel]() {
        // Local history ring (M74): a full hour of 1 s samples with a
        // rewound selection so the tiles, card, scrubber, detail line and
        // stored-window charts all render from real records.
        const qint64 baseMs = QDateTime::currentMSecsSinceEpoch() - 900 * 1000;
        HistoryViewInfo info;
        info.enabled = true;
        info.path = QStringLiteral("/home/dev/.cache/dev.syspeek/syspeek/"
                                   "history.ring");
        info.count = 900;
        info.capacity = 3600;
        info.bytes = 97264;
        info.intervalMs = 1000;
        info.spanMs = 899000;
        for (int i = 0; i < 240; ++i) {
            const double wave = std::sin(double(i) / 9.0);
            ExportRow row;
            row.atMs = baseMs + i * 1000;
            row.appName = QStringLiteral("cursor");
            row.pid = QStringLiteral("508637");
            row.appCpuPercent = 22.0 + wave * 9.0;
            row.appRssKb = 4194304;
            row.hostCpuPercent = 28.0 + wave * 12.0;
            row.hostStealPercent = 0.4;
            row.hostMemAvailableKb = 5242880 - qint64(wave * 262144.0);
            row.hostMemTotalKb = 16659456;
            row.load1 = 1.8 + wave * 0.6;
            row.diskReadKbPerSec = 380.0 + wave * 120.0;
            row.diskWriteKbPerSec = 940.0 - wave * 300.0;
            row.diskUtilPercent = 12.0 + wave * 4.0;
            row.netRxKbPerSec = 240.0 + wave * 90.0;
            row.netTxKbPerSec = 64.0;
            row.tcpInSegsPerSec = 420.0;
            row.tcpOutSegsPerSec = 380.0;
            row.tcpRetransPerSec = 1.4;
            row.tcpRetransPercent = 0.31;
            row.tcpCurrEstab = 34;
            info.window.append(row);
        }
        info.newestMs = info.window.last().atMs;
        panel.setHistoryView(info);

        HistoryRecord selected = historyRecordFrom(info.window.at(180));
        selected.appName = QStringLiteral("cursor");
        selected.pid = 508637;
        panel.setHistorySelection(
            historyPositionText(660, info.count, selected, false),
            historyRecordText(selected));
        app.processEvents();
    };

    panel.resize(1020, 1040);
    panel.show();
    app.processEvents();

    if (argc > 3) {
        const int row = QString::fromUtf8(argv[3]).toInt();
        if (auto *rail = panel.findChild<QListWidget *>(
                QStringLiteral("ddRail"))) {
            rail->setCurrentRow(row);
            app.processEvents();
        }
    }
    const QString out = argc > 1 ? QString::fromUtf8(argv[1])
                                 : QStringLiteral("panel.png");
    tracePhase("drive history");
    driveHistory();
    tracePhase("drive statsd");
    driveStatsd();
    tracePhase("drive history log");
    driveHistoryLog();
    tracePhase("grab");
    QScrollArea *grabScroll = nullptr;
    if (argc > 4
        && QString::fromUtf8(argv[4]) == QStringLiteral("full")) {
        if (auto *stack = panel.findChild<QStackedWidget *>()) {
            grabScroll = stack->currentWidget()->findChild<QScrollArea *>(
                QStringLiteral("ddScroll"));
        }
        if (!grabScroll) {
            grabScroll = panel.findChild<QScrollArea *>(
                QStringLiteral("ddScroll"));
        }
    }
    if (grabScroll && grabScroll->widget()) {
        if (auto *stack = panel.findChild<QStackedWidget *>()) {
            if (QWidget *page = stack->currentWidget()) {
                page->resize(1460, 1240);
            }
        }
        panel.resize(1460, 1240);
        app.processEvents();
        QWidget *content = grabScroll->widget();
        content->setMinimumHeight(qMax(1700, content->sizeHint().height()));
        content->resize(1440, content->minimumHeight());
        app.processEvents();
    }
    const QPixmap shot =
        grabScroll && grabScroll->widget()
            ? grabScroll->widget()->grab()
            : panel.grab();
    const bool ok = shot.save(out);
    std::printf("%s: %s\n", ok ? "saved" : "failed",
                out.toUtf8().constData());
    return ok ? 0 : 2;
}
