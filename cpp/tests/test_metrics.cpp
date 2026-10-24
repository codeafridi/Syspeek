#include "MetricStore.h"
#include "Sparkline.h"
#include "IoRateTracker.h"
#include "NetRateTracker.h"
#include "ServicesStatus.h"
#include "JournalStatus.h"
#include "AlertEngine.h"
#include "PrometheusStatus.h"
#include "SyntheticStatus.h"
#include "DiskStatus.h"
#include "TcpStatus.h"
#include "SecurityStatus.h"
#include "LogParser.h"
#include "TraceStatus.h"
#include "SbomStatus.h"
#include "PathStatus.h"
#include "StatsdStatus.h"
#include "SensitiveStatus.h"
#include "SloStatus.h"
#include "ThreatStatus.h"
#include "DbmStatus.h"
#include "WatchdogStatus.h"
#include "TlsStatus.h"
#include "RuntimeStatus.h"
#include "IncidentStatus.h"
#include "ProfileStatus.h"
#include "HostInfoStatus.h"
#include "FimStatus.h"

#include <QApplication>
#include <QPixmap>
#include <QThread>

#include <cmath>
#include <cstdio>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    int failures = 0;
    auto check = [&failures](bool condition, const char *message) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition) {
            ++failures;
        }
    };

    MetricStore store(3);
    check(store.capacity() == 3, "store capacity");
    check(store.size() == 0, "store starts empty");
    store.push(10);
    store.push(20);
    check(store.size() == 2 && store.last() == 20, "store push/last");
    check(store.minimum() == 10 && store.maximum() == 20, "store min/max");
    store.push(30);
    store.push(40);
    check(store.size() == 3, "store trims to capacity");
    check(store.values().first() == 20 && store.values().last() == 40,
          "store keeps most recent window");
    store.clear();
    check(store.size() == 0 && store.last() == 0.0, "store clear");

    Sparkline spark;
    spark.resize(120, 24);
    check(spark.valueCount() == 0, "sparkline starts empty");
    check(spark.grab().width() == 120, "sparkline renders empty");
    spark.setValues({1.0, 2.0, 3.0, 2.0});
    check(spark.valueCount() == 4, "sparkline stores values");
    check(!spark.grab().isNull(), "sparkline renders a polyline");
    spark.setValues({5.0});
    spark.grab();
    check(true, "sparkline handles a single value");
    spark.setValues({});
    check(spark.valueCount() == 0, "sparkline accepts empty reset");

    IoRateTracker tracker;
    const IoRateTracker::Rate first = tracker.update(42, 1000, 2000);
    check(first.readPerSec == 0.0 && first.writePerSec == 0.0,
          "io tracker: first sample -> 0");
    QThread::msleep(30);
    const IoRateTracker::Rate second = tracker.update(42, 4000, 8000);
    check(second.readPerSec > 0.0 && second.writePerSec > 0.0,
          "io tracker: counters growing -> positive rate");
    const IoRateTracker::Rate reset = tracker.update(42, 100, 100);
    check(reset.readPerSec == 0.0 && reset.writePerSec == 0.0,
          "io tracker: counter reset -> 0");
    const IoRateTracker::Rate noPerm = tracker.update(7, -1, -1);
    check(noPerm.readPerSec == 0.0, "io tracker: no permission -> 0");

    NetRateTracker netTracker;
    const NetRateTracker::Rate n1 = netTracker.update(QStringLiteral("eth0"), 1000, 2000);
    check(n1.rxPerSec == 0.0 && n1.txPerSec == 0.0,
          "net tracker: first sample -> 0");
    QThread::msleep(30);
    const NetRateTracker::Rate n2 = netTracker.update(QStringLiteral("eth0"), 4000, 5000);
    check(n2.rxPerSec > 0.0 && n2.txPerSec > 0.0,
          "net tracker: counters growing -> positive rate");
    const NetRateTracker::Rate n3 = netTracker.update(QStringLiteral("eth0"), 100, 100);
    check(n3.rxPerSec == 0.0 && n3.txPerSec == 0.0,
          "net tracker: counter reset -> 0");

    QList<UnitRow> units;
    check(parseSystemctlUnits(
              "nginx.service loaded active running A high performance web server\n"
              "db.service loaded failed failed Database\n"
              "ssh.service loaded active exited OpenBSD Secure Shell server\n"
              "19 loaded units listed. Pass --all to see inactive units.\n",
              units)
              && units.size() == 3
              && units.at(0).name == QStringLiteral("nginx.service")
              && units.at(0).active == QStringLiteral("active")
              && units.at(0).sub == QStringLiteral("running")
              && units.at(0).description
                     == QStringLiteral("A high performance web server")
              && units.at(1).active == QStringLiteral("failed"),
          "parseSystemctlUnits: rows + description, footer skipped");
    check(servicesRunningCount(units) == 1, "servicesRunningCount == 1");
    check(servicesFailedCount(units) == 1, "servicesFailedCount == 1");

    QList<UnitRow> bulletUnits;
    check(parseSystemctlUnits(
              "\xE2\x97\x8F boom.service loaded failed failed Boom\n",
              bulletUnits)
              && bulletUnits.size() == 1
              && bulletUnits.at(0).name == QStringLiteral("boom.service")
              && bulletUnits.at(0).active == QStringLiteral("failed"),
          "parseSystemctlUnits: bullet-prefixed failed unit");

    QList<UnitRow> launchUnits;
    check(parseLaunchctlList(
              "PID\tStatus\tLabel\n"
              "-\t0\tcom.apple.Finder\n"
              "215\t0\tcom.apple.WindowServer\n"
              "-\t78\tcom.example.broken\n",
              launchUnits)
              && launchUnits.size() == 3
              && launchUnits.at(0).name == QStringLiteral("com.apple.Finder")
              && launchUnits.at(0).active == QStringLiteral("inactive")
              && launchUnits.at(1).active == QStringLiteral("active")
              && launchUnits.at(1).sub == QStringLiteral("running")
              && launchUnits.at(2).active == QStringLiteral("failed"),
          "parseLaunchctlList: pid/status/label rows, header skipped");
    check(servicesRunningCount(launchUnits) == 1
              && servicesFailedCount(launchUnits) == 1,
          "launchctl counts: running + failed");

    QList<ServiceRecord> scmRecords;
    ServiceRecord scmRun;
    scmRun.name = QStringLiteral("W32Time");
    scmRun.displayName = QStringLiteral("Windows Time");
    scmRun.state = 4;
    scmRun.processId = 1234;
    scmRecords.append(scmRun);
    ServiceRecord scmStopped;
    scmStopped.name = QStringLiteral("Browser");
    scmStopped.state = 1;
    scmRecords.append(scmStopped);
    ServiceRecord scmNeverStarted;
    scmNeverStarted.name = QStringLiteral("neverstart");
    scmNeverStarted.state = 1;
    scmNeverStarted.exitCode = 1077;
    scmRecords.append(scmNeverStarted);
    ServiceRecord scmFailed;
    scmFailed.name = QStringLiteral("brokenvc");
    scmFailed.state = 1;
    scmFailed.exitCode = 3221226505; // 0xC0000149 real crash exit
    scmRecords.append(scmFailed);
    ServiceRecord scmPending;
    scmPending.name = QStringLiteral("a-pending");
    scmPending.state = 2;
    scmRecords.append(scmPending);
    ServiceRecord scmPaused;
    scmPaused.name = QStringLiteral("a-paused");
    scmPaused.state = 7;
    scmRecords.append(scmPaused);
    ServiceRecord scmEmpty;
    scmEmpty.name.clear();
    scmRecords.append(scmEmpty);

    QList<UnitRow> scmUnits;
    check(serviceRecordsToUnits(scmRecords, scmUnits) && scmUnits.size() == 6,
          "serviceRecordsToUnits: records mapped, empty name skipped");
    check(scmUnits.at(0).name == QStringLiteral("Browser")
              && scmUnits.at(0).active == QStringLiteral("inactive")
              && scmUnits.at(0).sub == QStringLiteral("dead"),
          "serviceRecordsToUnits: sorted by name, STOPPED + exit 0 -> dead");
    check(scmUnits.at(1).name == QStringLiteral("W32Time")
              && scmUnits.at(1).active == QStringLiteral("active")
              && scmUnits.at(1).sub == QStringLiteral("running"),
          "serviceRecordsToUnits: RUNNING -> active/running");
    check(scmUnits.at(2).name == QStringLiteral("a-paused")
              && scmUnits.at(2).active == QStringLiteral("active")
              && scmUnits.at(2).sub == QStringLiteral("paused"),
          "serviceRecordsToUnits: PAUSED mapping");
    check(scmUnits.at(3).active == QStringLiteral("activating"),
          "serviceRecordsToUnits: START_PENDING -> activating");
    check(scmUnits.at(4).name == QStringLiteral("brokenvc")
              && scmUnits.at(4).sub == QStringLiteral("failed"),
          "serviceRecordsToUnits: STOPPED + crash exit -> failed");
    check(scmUnits.at(5).name == QStringLiteral("neverstart")
              && scmUnits.at(5).active == QStringLiteral("inactive")
              && scmUnits.at(5).sub == QStringLiteral("dead"),
          "serviceRecordsToUnits: 1077 never-started -> inactive, not failed");
    check(servicesRunningCount(scmUnits) == 1
              && servicesFailedCount(scmUnits) == 1,
          "scm counts align with launchctl semantics");

    ServicesSnapshot services;
    services.available = true;
    services.runningCount = 3;
    services.failedCount = 1;
    check(servicesSummaryText(services).contains(QStringLiteral("3 running"))
              && servicesSummaryText(services).contains(QStringLiteral("1 failed")),
          "services summary text");

    QList<JournalEntry> entries;
    const QByteArray journal =
        "{\"__REALTIME_TIMESTAMP\":\"1791907430214266\",\"PRIORITY\":\"3\","
        "\"_PID\":\"2196\",\"_COMM\":\"nginx\",\"MESSAGE\":\"boom\"}\n"
        "{\"PRIORITY\":\"6\",\"_COMM\":\"cron\",\"MESSAGE\":\"tick\"}\n"
        "not json\n"
        "{\"PRIORITY\":\"2\",\"_COMM\":\"kernel\",\"MESSAGE\":[104,105]}\n";
    check(parseJournalJson(journal, entries) && entries.size() == 3,
          "parseJournalJson: 3 objects, junk skipped");
    check(entries.at(0).comm == QStringLiteral("nginx")
              && entries.at(0).message == QStringLiteral("boom")
              && entries.at(0).priority == 3 && entries.at(0).pid == 2196
              && entries.at(0).timestampUsec == 1791907430214266LL,
          "parseJournalJson: fields decoded");
    check(entries.at(2).message == QStringLiteral("hi"),
          "parseJournalJson: byte-array MESSAGE decoded");
    check(journalErrorCount(entries) == 2, "journalErrorCount: priority <= 3");

    JournalSnapshot journal2;
    journal2.available = true;
    journal2.entries = entries;
    journal2.errorCount = 2;
    check(journalSummaryText(journal2).contains(QStringLiteral("2 errors")),
          "journal summary text");

    JournalEntry errA;
    errA.priority = 3;
    errA.comm = QStringLiteral("nginx");
    errA.message = QStringLiteral("connect() failed at 10.0.0.5:8080 attempt 1");
    errA.timestampUsec = 100;
    JournalEntry errB;
    errB.priority = 3;
    errB.comm = QStringLiteral("nginx");
    errB.message = QStringLiteral("connect() failed at 10.0.0.9:9999 attempt 7");
    errB.timestampUsec = 200;
    JournalEntry info;
    info.priority = 6;
    info.comm = QStringLiteral("nginx");
    info.message = QStringLiteral("started");
    info.timestampUsec = 150;
    const QList<ErrorGroup> groups = groupJournalErrors({errA, errB, info});
    check(groups.size() == 1, "error grouping: templates collapse");
    check(groups.at(0).count == 2 && groups.at(0).lastSeenUsec == 200,
          "error group: count + lastSeen");
    check(journalErrorGroupsText(groups).contains(QStringLiteral("x2")),
          "error groups text");

    UnitRow prevUnit;
    prevUnit.name = QStringLiteral("a.service");
    prevUnit.active = QStringLiteral("active");
    prevUnit.sub = QStringLiteral("running");
    UnitRow failedUnit;
    failedUnit.name = QStringLiteral("a.service");
    failedUnit.active = QStringLiteral("failed");
    failedUnit.sub = QStringLiteral("failed");
    UnitRow newUnit;
    newUnit.name = QStringLiteral("b.service");
    newUnit.active = QStringLiteral("active");
    newUnit.sub = QStringLiteral("running");
    const QList<ServiceChange> changes =
        diffServices({prevUnit}, {failedUnit, newUnit}, 1000);
    bool sawFail = false;
    bool sawAppear = false;
    for (const ServiceChange &change : changes) {
        if (change.unit == QStringLiteral("a.service")
            && change.summary.contains(QStringLiteral("failed"))) {
            sawFail = true;
        }
        if (change.unit == QStringLiteral("b.service")
            && change.summary.contains(QStringLiteral("appeared"))) {
            sawAppear = true;
        }
    }
    check(sawFail && sawAppear, "service diff: failure + new unit");
    check(servicesChangesText(changes).contains(QStringLiteral("a.service")),
          "services changes text");

    AlertEngine engine;
    AlertInput input;
    input.appCpuValid = true;
    input.appCpu = 95.0;
    check(engine.update(input, 1).isEmpty(), "alert: no fire before sustained");
    check(engine.update(input, 2).isEmpty(), "alert: still not firing");
    const QList<Alert> fired = engine.update(input, 3);
    check(fired.size() == 1 && fired.at(0).id == QStringLiteral("app_cpu"),
          "alert: fires at sustained count");
    check(engine.active().size() == 1, "alert: active list has one");
    input.appCpu = 10.0;
    engine.update(input, 4);
    check(engine.active().isEmpty(), "alert: resolves on recovery");

    AlertInput servicesInput;
    servicesInput.servicesValid = true;
    servicesInput.servicesFailed = 1;
    const QList<Alert> immediate = engine.update(servicesInput, 5);
    check(immediate.size() == 1
              && immediate.at(0).id == QStringLiteral("services_failed"),
          "alert: service failure fires immediately");

    engine.reset();
    AlertInput memInput;
    memInput.hostMemValid = true;
    memInput.hostMemFreePct = 5.0;
    engine.update(memInput, 6);
    engine.update(memInput, 7);
    check(engine.update(memInput, 8).size() == 1,
          "alert: low memory fires after sustained");

    engine.reset();
    AlertInput fdInput;
    fdInput.kernelFdValid = true;
    fdInput.kernelFdPct = 85.0;
    check(engine.update(fdInput, 1).isEmpty(), "alert: fd not before sustained");
    check(engine.update(fdInput, 2).isEmpty(), "alert: fd still not firing");
    const QList<Alert> fdFired = engine.update(fdInput, 3);
    check(fdFired.size() == 1
              && fdFired.at(0).id == QStringLiteral("kernel_fd"),
          "alert: kernel fd pressure fires at sustained count");
    check(fdFired.at(0).detail.contains(QStringLiteral("85.0")),
          "alert: fd detail carries the ratio");
    fdInput.kernelFdPct = 40.0;
    engine.update(fdInput, 4);
    check(engine.active().isEmpty(), "alert: fd resolves on recovery");

    engine.reset();
    AlertInput stealInput;
    stealInput.stealValid = true;
    stealInput.stealPercent = 6.0;
    for (int i = 1; i <= 4; ++i) {
        engine.update(stealInput, i);
    }
    check(engine.update(stealInput, 5).size() == 1,
          "alert: host steal fires at sustained 5");
    stealInput.stealPercent = 1.0;
    engine.update(stealInput, 6);
    check(engine.active().isEmpty(), "alert: steal resolves below threshold");

    AlertInput noSteal;
    noSteal.stealValid = false;
    check(engine.update(noSteal, 7).isEmpty(),
          "alert: missing steal sample does not fire");

    {
        DisplaySnapshot s;
        QString text = prometheusText(s);
        check(!text.contains(QStringLiteral("syspeek_host_cpu_percent")),
              "prometheus: host metrics omitted when host unavailable");
        check(!text.contains(QStringLiteral("syspeek_app_")),
              "prometheus: app metrics omitted when no focus");
        check(
            text.contains(QStringLiteral("# HELP syspeek_tcp_sockets")),
            "prometheus: socket state helper always present");

        s.hostAvailable = true;
        s.hostCpuPercent = 34.5;
        s.hostStealPercent = 2.4;
        s.hostMemTotalKb = 16 * 1024 * 1024ULL;
        s.hostMemAvailableKb = 15 * 1024 * 1024ULL;
        s.hostDiskTotalBytes = 500 * 1024 * 1024 * 1024ULL;
        s.hostDiskFreeBytes = 250 * 1024 * 1024 * 1024ULL;
        s.hostLoad1 = 1.5;
        s.kernelLimits.available = true;
        s.kernelLimits.fdAllocated = 1234;
        s.kernelLimits.fdMax = 10000;
        s.kernelLimits.runningProcs = 2;
        s.kernelLimits.blockedProcs = 0;
        text = prometheusText(s);
        check(text.contains(QStringLiteral(
                  "syspeek_host_cpu_percent 34.500")),
              "prometheus: host cpu gauge value");
        check(text.contains(QStringLiteral("syspeek_host_load1 1.500")),
              "prometheus: load gauge value");
        check(text.contains(
                  QStringLiteral("syspeek_kernel_fd_allocated 1234")),
              "prometheus: kernel fd gauge");
        check(text.contains(QStringLiteral("syspeek_kernel_pids_running 2")),
              "prometheus: runnable gauge");
        check(text.contains(
                  QStringLiteral("syspeek_host_mem_total_bytes "
                                 "17179869184")),
              "prometheus: memory in bytes");

        s.cpuValid = true;
        s.appName = QStringLiteral("cu\"rsor \\weird\n");
        s.pid = QStringLiteral("508637");
        s.cpuPercent = 63.9;
        s.rssKb = 634 * 1024;
        ProcessRow row;
        row.threads = 98;
        row.fds = 1234;
        s.processes.append(row);
        text = prometheusText(s);
        check(text.contains(QStringLiteral(
                  "app=\"cu\\\"rsor \\\\weird\\n\",pid=\"508637\"")),
              "prometheus: label escaping (quote, backslash, newline)");
        check(text.contains(QStringLiteral(
                  "syspeek_app_rss_bytes{app=\"cu\\\"rsor")),
              "prometheus: app rss with labels");
        check(text.contains(QStringLiteral("syspeek_app_fds{app=")),
              "prometheus: app fd sum");

        InterfaceRow iface;
        iface.name = QStringLiteral("eth0");
        iface.rxBytes = 1'000'000'000ULL;
        iface.txBytes = 500'000'000ULL;
        iface.rxPerSec = 12500.0;
        s.interfaces.append(iface);
        text = prometheusText(s);
        check(text.contains(QStringLiteral(
                  "syspeek_net_rx_bytes_total{device=\"eth0\"} 1000000000")),
              "prometheus: interface counter total");
        check(text.contains(QStringLiteral(
                  "syspeek_net_tx_bytes_per_second{device=\"eth0\"}")),
              "prometheus: interface rate gauge");
        check(text.contains(QStringLiteral(
                  "# TYPE syspeek_net_rx_bytes_total counter")),
              "prometheus: counter type declared");

        DiskRow disk;
        disk.name = QStringLiteral("sda");
        disk.readKbPerSec = 100.0;
        disk.utilPercent = 52.3;
        disk.readAwaitMs = 2.5;
        s.disks.append(disk);
        s.disksAvailable = true;
        text = prometheusText(s);
        check(text.contains(QStringLiteral(
                  "syspeek_disk_read_bytes_per_second{device=\"sda\"} 102400."
                  "000")),
              "prometheus: disk rate in bytes");
        check(text.contains(QStringLiteral(
                  "syspeek_disk_io_util_percent{device=\"sda\"} 52.300"))
                  && text.contains(QStringLiteral(
                      "syspeek_disk_await_seconds{device=\"sda\"} 0.003")),
              "prometheus: disk util + await in seconds");

        s.cgroupAvailable = true;
        s.cgroupMemUsage = 7232409600ULL;
        s.cgroupMemLimit = -1;
        s.cgroupHasMemPeak = true;
        s.cgroupMemPeakBytes = 3775ULL * 1024ULL * 1024ULL;
        s.cgroupOomEvents = 0;
        s.cgroupOomKills = 3;
        s.cgroupPidsCurrent = 148;
        text = prometheusText(s);
        check(text.contains(
                  QStringLiteral("syspeek_cgroup_mem_usage_bytes 7232409600")),
              "prometheus: cgroup usage");
        check(!text.contains(QStringLiteral("syspeek_cgroup_mem_limit_bytes")),
              "prometheus: unlimited limit omitted");
        check(text.contains(QStringLiteral(
                  "syspeek_cgroup_oom_kills_total 3")),
              "prometheus: oom kill counter");
        check(text.contains(QStringLiteral("syspeek_cgroup_pids 148")),
              "prometheus: cgroup pids gauge");
    }

    int status = 0;
    double total = 0;
    double dns = 0;
    double connect = 0;
    double tls = 0;
    double processing = 0;
    double transfer = 0;
    check(parseCurlWriteOut("200 0.175 0.354 0.500 0.830 0.831", status, total,
                            dns, connect, tls, processing, transfer)
              && status == 200
              && std::fabs(dns - 175.0) < 1.0
              && std::fabs(connect - (354.0 - 175.0)) < 1.0
              && std::fabs(tls - (500.0 - 354.0)) < 1.0
              && std::fabs(processing - (830.0 - 500.0)) < 1.0
              && std::fabs(transfer - (831.0 - 830.0)) < 1.0
              && std::fabs(total - 831.0) < 1.0,
          "parseCurlWriteOut: phase decomposition");
    int s2 = 0;
    double a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
    check(!parseCurlWriteOut(QByteArrayLiteral("garbage"), s2, a, b, c, d, e, f),
          "parseCurlWriteOut: garbage -> false");

    SyntheticSnapshot synthetic;
    ProbeResult up;
    up.url = QStringLiteral("https://x");
    up.success = true;
    up.status = 200;
    up.totalMs = 12;
    ProbeResult down;
    down.url = QStringLiteral("https://y");
    down.success = false;
    down.error = QStringLiteral("connection refused");
    synthetic.results = {up, down};
    synthetic.upCount = 1;
    synthetic.downCount = 1;
    check(syntheticSummaryText(synthetic).contains(QStringLiteral("1 up")),
          "synthetic summary");
    check(syntheticDetailText(synthetic).contains(QStringLiteral("DOWN")),
          "synthetic detail lists down target");

    {
        auto near = [](double a, double b) { return std::fabs(a - b) < 1e-6; };

        check(diskIsPhysical(QStringLiteral("sda")), "disk: sda physical");
        check(diskIsPhysical(QStringLiteral("nvme0n1")), "disk: nvme0n1 physical");
        check(!diskIsPhysical(QStringLiteral("sda1")), "disk: sda1 partition skipped");
        check(!diskIsPhysical(QStringLiteral("nvme0n1p1")),
              "disk: nvme partition skipped");
        check(!diskIsPhysical(QStringLiteral("loop0")), "disk: loop skipped");
        check(!diskIsPhysical(QStringLiteral("dm-0")), "disk: dm skipped");

        const QByteArray stats = QByteArrayLiteral(
            "   8       0 sda 1000 20 50000 300 2000 40 80000 500 3 800 900\n"
            "   8       1 sda1 900 10 40000 250 1900 30 70000 450 2 700 800\n"
            "   7       0 loop0 0 0 0 0 0 0 0 0 0 0 0\n");
        const QList<DiskCounters> parsed = parseDiskstats(stats);
        check(parsed.size() == 3, "disk: parsed three entries");
        check(parsed.at(0).name == QStringLiteral("sda")
                  && parsed.at(0).reads == 1000
                  && parsed.at(0).sectorsRead == 50000
                  && parsed.at(0).msReading == 300
                  && parsed.at(0).writes == 2000
                  && parsed.at(0).sectorsWritten == 80000
                  && parsed.at(0).msDoingIo == 800
                  && parsed.at(0).weightedMsDoingIo == 900,
              "disk: parsed sda counters");

        const QList<DiskCounters> prev = parseDiskstats(QByteArrayLiteral(
            "   8       0 sda 0 0 0 0 0 0 0 0 0 0 0\n"));
        const QList<DiskCounters> cur = parseDiskstats(QByteArrayLiteral(
            "   8       0 sda 100 0 2000 100 200 0 4000 200 4 500 600\n"));
        const QList<DiskRow> rows = computeDiskRates(prev, cur, 1000.0);
        check(rows.size() == 1 && rows.at(0).name == QStringLiteral("sda"),
              "disk: rate row for physical device only");
        const DiskRow &r = rows.at(0);
        check(near(r.readPerSec, 100.0), "disk: read iops");
        check(near(r.writePerSec, 200.0), "disk: write iops");
        check(near(r.readKbPerSec, 1000.0), "disk: read kB/s");
        check(near(r.writeKbPerSec, 2000.0), "disk: write kB/s");
        check(near(r.readAwaitMs, 1.0), "disk: read await");
        check(near(r.writeAwaitMs, 1.0), "disk: write await");
        check(near(r.aquSz, 0.6), "disk: aqu-sz");
        check(near(r.utilPercent, 50.0), "disk: %util");

        const IoPressure pressure = parsePressureIo(QByteArrayLiteral(
            "some avg10=1.20 avg60=0.50 avg300=0.10 total=1000\n"
            "full avg10=0.30 avg60=0.20 avg300=0.05 total=500\n"));
        check(pressure.available, "disk: pressure available");
        check(near(pressure.someAvg10, 1.20) && near(pressure.someAvg300, 0.10),
              "disk: pressure some fields");
        check(near(pressure.fullAvg300, 0.05), "disk: pressure full field");

        DiskSnapshot snap;
        snap.available = true;
        snap.disks = rows;
        snap.pressure = pressure;
        check(diskSummaryText(snap).contains(QStringLiteral("sda")),
              "disk: summary names busiest device");
        check(diskSummaryText(snap).contains(QStringLiteral("io-pressure")),
              "disk: summary includes io pressure");
        check(diskDetailText(snap).contains(QStringLiteral("sda")),
              "disk: detail lists device");
        DiskSnapshot warm;
        warm.available = true;
        warm.warming = true;
        check(diskSummaryText(warm).contains(QStringLiteral("warming")),
              "disk: warming summary");
    }

    {
        auto near = [](double a, double b) { return std::fabs(a - b) < 1e-6; };

        const QByteArray snmp = QByteArrayLiteral(
            "Tcp: RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens PassiveOpens "
            "AttemptFails EstabResets CurrEstab InSegs OutSegs RetransSegs "
            "InErrs OutRsts InCsumErrors\n"
            "Tcp: 1 200 120000 -1 100 50 3 1 40 1000 900 9 0 2 0\n"
            "Ip: InReceives\nIp: 55\n");
        const QByteArray netstat = QByteArrayLiteral(
            "TcpExt: TCPTimeouts TCPSynRetrans ListenDrops\n"
            "TcpExt: 12 3 1\nIpExt: X\nIpExt: 7\n");

        const TcpCounters counters = parseTcpCounters(snmp, netstat);
        check(counters.tcp.value(QStringLiteral("OutSegs")) == 900
                  && counters.tcp.value(QStringLiteral("RetransSegs")) == 9
                  && counters.tcp.value(QStringLiteral("CurrEstab")) == 40,
              "tcp: parsed Tcp section by header name");
        check(counters.ext.value(QStringLiteral("TCPTimeouts")) == 12
                  && counters.ext.value(QStringLiteral("ListenDrops")) == 1,
              "tcp: parsed TcpExt section");

        const TcpCounters prev = parseTcpCounters(
            QByteArrayLiteral(
                "Tcp: InSegs OutSegs RetransSegs InErrs OutRsts CurrEstab\n"
                "Tcp: 900 800 6 0 1 40\n"),
            QByteArrayLiteral("TcpExt: TCPTimeouts TCPSynRetrans ListenDrops\n"
                              "TcpExt: 10 1 0\n"));
        const TcpCounters cur = parseTcpCounters(
            QByteArrayLiteral(
                "Tcp: InSegs OutSegs RetransSegs InErrs OutRsts CurrEstab\n"
                "Tcp: 1000 900 9 0 3 42\n"),
            QByteArrayLiteral("TcpExt: TCPTimeouts TCPSynRetrans ListenDrops\n"
                              "TcpExt: 12 3 1\n"));
        const TcpHealth health = computeTcpHealth(prev, cur, 1000.0);
        check(health.available, "tcp: health available");
        check(near(health.inSegsPerSec, 100.0), "tcp: in segs/s");
        check(near(health.outSegsPerSec, 100.0), "tcp: out segs/s");
        check(near(health.retransPerSec, 3.0), "tcp: retrans/s");
        check(near(health.retransPercent, 3.0), "tcp: retrans percent");
        check(near(health.outRstsPerSec, 2.0), "tcp: rsts/s");
        check(near(health.timeoutsPerSec, 2.0), "tcp: timeouts/s");
        check(near(health.synRetransPerSec, 2.0), "tcp: syn retrans/s");
        check(near(health.listenDropsPerSec, 1.0), "tcp: listen drops/s");
        check(health.currEstab == 42, "tcp: current established");

        TcpCounters redacted;
        redacted.tcp.insert(QStringLiteral("InSegs"), 0);
        redacted.tcp.insert(QStringLiteral("OutSegs"), 0);
        redacted.ext.insert(QStringLiteral("TCPTimeouts"), 0);
        check(tcpCountersRedacted(redacted),
              "tcp: all-zero counters (kernel-redacted read) detected");
        TcpCounters live = redacted;
        live.tcp[QStringLiteral("OutSegs")] = 42;
        check(!tcpCountersRedacted(live),
              "tcp: nonzero counters count as usable");
        check(tcpCountersRedacted(TcpCounters{}),
              "tcp: empty counters treated as unavailable");
        check(tcpHealthSummaryText(health).contains(QStringLiteral("retrans")),
              "tcp: summary mentions retrans");
        check(tcpHealthDetailText(health).contains(QStringLiteral("syn-retr")),
              "tcp: detail lists syn retrans");
    }

    {
        const QStringList caps =
            decodeCapabilities((1ULL << 21) | (1ULL << 16) | (1ULL << 12));
        check(caps.contains(QStringLiteral("CAP_SYS_ADMIN"))
                  && caps.contains(QStringLiteral("CAP_SYS_MODULE"))
                  && caps.contains(QStringLiteral("CAP_NET_ADMIN")),
              "security: capability decode");

        SecurityInput hardened;
        hardened.capKnown = true;
        hardened.capEff = 0;
        hardened.seccomp = 2;
        hardened.noNewPrivs = true;
        hardened.macConfined = true;
        hardened.macLabel = QStringLiteral("unconfined-userns");
        hardened.isolatedNamespaces = 7;
        hardened.totalNamespaces = 7;
        const SecurityPosture ok = evaluateSecurity(hardened);
        check(ok.available && std::fabs(ok.exposure - 0.2) < 1e-6,
              "security: hardened exposure is low");
        check(ok.grade == QStringLiteral("low"), "security: hardened grade low");

        SecurityInput exposed;
        exposed.capKnown = true;
        exposed.capEff = (1ULL << 21) | (1ULL << 16);
        exposed.seccomp = 0;
        exposed.noNewPrivs = false;
        exposed.macConfined = false;
        exposed.rootUser = true;
        exposed.deletedExe = true;
        exposed.isolatedNamespaces = 0;
        exposed.totalNamespaces = 7;
        const SecurityPosture bad = evaluateSecurity(exposed);
        check(std::fabs(bad.exposure - 4.8) < 1e-6,
              "security: exposed exposure sums correctly");
        check(bad.grade == QStringLiteral("medium"),
              "security: exposed grade medium");
        check(bad.exposedCount >= 6, "security: counts exposed findings");
        check(securitySummaryText(bad).contains(QStringLiteral("exposure")),
              "security: summary text");
        check(securityDetailText(bad).contains(QStringLiteral("CAP_SYS_ADMIN")),
              "security: detail lists capability");

        QList<SecurityFinding> host;
        host.append({QStringLiteral("system integrity protection"), 0.0, true});
        host.append({QStringLiteral("disk encryption"), 8.0, false});
        host.append({QStringLiteral("gatekeeper assessments"), 0.0, true});
        host.append({QStringLiteral("application firewall"), 6.0, false});
        const SecurityPosture mac = postureFromFindings(host);
        check(mac.available && std::fabs(mac.exposure - 3.5) < 1e-6
                  && mac.grade == QStringLiteral("medium")
                  && mac.exposedCount == 2,
              "security: host-posture findings averaged");
    }

    {
        const LogFields nginx = parseLogLineAuto(QStringLiteral(
            "1.2.3.4 example.com - [10/Oct/2026:13:55:36 +0000] "
            "\"GET /api/v1/items HTTP/1.1\" 200 2326 \"-\" \"curl/8.0\""));
        check(nginx.matched && nginx.parser == QStringLiteral("nginx"),
              "logs: nginx parser matched");
        check(nginx.fields.value(QStringLiteral("code")) == QStringLiteral("200")
                  && nginx.fields.value(QStringLiteral("path"))
                         == QStringLiteral("/api/v1/items"),
              "logs: nginx extracted code/path");

        const LogFields apache = parseLogLineAuto(QStringLiteral(
            "127.0.0.1 - frank [10/Oct/2026:13:55:36 +0000] "
            "\"GET /index.html HTTP/1.0\" 404 512"));
        check(apache.matched
                  && (apache.parser == QStringLiteral("apache")
                      || apache.parser == QStringLiteral("apache2")),
              "logs: apache parser matched");
        check(apache.fields.value(QStringLiteral("code")) == QStringLiteral("404"),
              "logs: apache extracted code");

        const LogFields syslog = parseLogLineAuto(QStringLiteral(
            "<34>1 2026-10-10T22:14:15.003Z host app 1234 ID47 - message body"));
        check(syslog.matched
                  && syslog.parser == QStringLiteral("syslog-rfc5424"),
              "logs: rfc5424 parser matched");
        check(syslog.fields.value(QStringLiteral("ident")) == QStringLiteral("app"),
              "logs: rfc5424 extracted ident");

        const LogFields cri = parseLogLineAuto(QStringLiteral(
            "2026-10-10T22:14:15.003Z stdout F hello from container"));
        check(cri.matched && cri.parser == QStringLiteral("cri"),
              "logs: cri parser matched");
        check(cri.fields.value(QStringLiteral("stream")) == QStringLiteral("stdout"),
              "logs: cri extracted stream");

        const LogFields json = parseLogLineAuto(QStringLiteral(
            "{\"level\":\"error\",\"code\":500,\"path\":\"/x\",\"duration\":12.5}"));
        check(json.matched && json.parser == QStringLiteral("json"),
              "logs: json parser matched");
        check(json.fields.value(QStringLiteral("level")) == QStringLiteral("error"),
              "logs: json extracted level");

        const LogFields logfmt = parseLogLineAuto(QStringLiteral(
            "level=warn code=429 path=/rate msg=\"slow down\""));
        check(logfmt.matched && logfmt.parser == QStringLiteral("logfmt"),
              "logs: logfmt parser matched");
        check(logfmt.fields.value(QStringLiteral("msg")) == QStringLiteral("slow down"),
              "logs: logfmt extracted quoted value");

        QStringList lines;
        lines << QStringLiteral(
                     "1.2.3.4 h - [10/Oct/2026:13:55:36 +0000] \"GET /a HTTP/1.1\" "
                     "200 10 \"-\" \"curl/8.0\"")
              << QStringLiteral(
                     "1.2.3.4 h - [10/Oct/2026:13:55:37 +0000] \"GET /b HTTP/1.1\" "
                     "200 20 \"-\" \"curl/8.0\"")
              << QStringLiteral(
                     "1.2.3.4 h - [10/Oct/2026:13:55:38 +0000] \"GET /a HTTP/1.1\" "
                     "500 5 \"-\" \"curl/8.0\"")
              << QStringLiteral("this line does not match any parser");
        const LogFacets facets = computeLogFacets(lines);
        check(facets.total == 4 && facets.parsed == 3,
              "logs: facets parsed 3/4 lines");
        check(facets.format == QStringLiteral("nginx"),
              "logs: facets dominant format nginx");
        bool saw500 = false;
        bool saw200 = false;
        for (const QPair<QString, int> &c : facets.codes) {
            if (c.first == QStringLiteral("500") && c.second == 1) {
                saw500 = true;
            }
            if (c.first == QStringLiteral("200") && c.second == 2) {
                saw200 = true;
            }
        }
        check(saw200 && saw500, "logs: facets aggregate http codes");
        check(logPipelineSummaryText(facets).contains(QStringLiteral("format")),
              "logs: pipeline summary text");
    }

    {
        const QByteArray otlp = QByteArrayLiteral(
            "{\"resourceSpans\":["
            "{\"resource\":{\"attributes\":[{\"key\":\"service.name\","
            "\"value\":{\"stringValue\":\"frontend\"}}]},"
            "\"scopeSpans\":[{\"spans\":["
            "{\"traceId\":\"t1\",\"spanId\":\"s1\",\"name\":\"GET /checkout\","
            "\"kind\":2,\"startTimeUnixNano\":\"1000000000\","
            "\"endTimeUnixNano\":\"1050000000\",\"status\":{\"code\":1}},"
            "{\"traceId\":\"t1\",\"spanId\":\"s2\",\"parentSpanId\":\"s1\","
            "\"name\":\"GET /pay\",\"kind\":3,\"startTimeUnixNano\":\"1010000000\","
            "\"endTimeUnixNano\":\"1030000000\",\"status\":{\"code\":2}}]}]},"
            "{\"resource\":{\"attributes\":[{\"key\":\"service.name\","
            "\"value\":{\"stringValue\":\"payments\"}}]},"
            "\"scopeSpans\":[{\"spans\":["
            "{\"traceId\":\"t1\",\"spanId\":\"s3\",\"parentSpanId\":\"s2\","
            "\"name\":\"POST /charge\",\"kind\":2,\"startTimeUnixNano\":\"1015000000\","
            "\"endTimeUnixNano\":\"1025000000\",\"status\":{\"code\":1}}]}]}]}");

        bool ok = false;
        const QList<Span> spans = parseOtlpTraces(otlp, &ok);
        check(ok && spans.size() == 3, "traces: parsed three OTLP spans");
        check(spans.at(0).service == QStringLiteral("frontend"),
              "traces: resource service.name applied");
        check(spans.at(1).status == QStringLiteral("error"),
              "traces: error status decoded");

        const TraceSnapshot snapshot = summarizeTraces(spans);
        check(snapshot.spanCount == 3 && snapshot.traceCount == 1,
              "traces: span/trace counts");
        check(snapshot.errorCount == 1, "traces: error count");
        check(snapshot.services.size() == 2, "traces: two services");
        check(snapshot.edges.size() == 1
                  && snapshot.edges.at(0).from == QStringLiteral("frontend")
                  && snapshot.edges.at(0).to == QStringLiteral("payments"),
              "traces: cross-service edge frontend->payments");
        check(snapshot.recent.size() == 1
                  && snapshot.recent.at(0).rootService == QStringLiteral("frontend"),
              "traces: recent trace root service");
        check(std::fabs(snapshot.recent.at(0).durationMs - 50.0) < 1e-6,
              "traces: trace duration 50ms");
        check(snapshot.recent.at(0).hasError, "traces: trace flagged error");
        check(traceSummaryText(snapshot).contains(QStringLiteral("traces")),
              "traces: summary text");
        check(traceServiceMapText(snapshot).contains(QStringLiteral("->")),
              "traces: service map text");
    }

    {
        const QByteArray status = QByteArrayLiteral(
            "Package: libc6\nStatus: install ok installed\nArchitecture: amd64\n"
            "Source: glibc\nVersion: 2.43-2ubuntu2.4\n\n"
            "Package: zlib1g\nArchitecture: amd64\nVersion: 1:1.3\n\n");
        const QList<DpkgPackage> pkgs = parseDpkgStatus(status);
        check(pkgs.size() == 2, "sbom: parsed two packages");
        QHash<QString, DpkgPackage> byName;
        for (const DpkgPackage &pkg : pkgs) {
            byName.insert(pkg.name, pkg);
        }
        check(byName.value(QStringLiteral("libc6")).version
                      == QStringLiteral("2.43-2ubuntu2.4")
                  && byName.value(QStringLiteral("libc6")).arch
                         == QStringLiteral("amd64")
                  && byName.value(QStringLiteral("libc6")).source
                         == QStringLiteral("glibc"),
              "sbom: package metadata");
        check(makePurl(byName.value(QStringLiteral("libc6")))
                  == QStringLiteral(
                      "pkg:deb/ubuntu/libc6@2.43-2ubuntu2.4?arch=amd64"),
              "sbom: purl");

        QHash<QString, QString> fileOwner;
        fileOwner.insert(QStringLiteral("/usr/lib/x86_64-linux-gnu/libc.so.6"),
                         QStringLiteral("libc6"));
        fileOwner.insert(QStringLiteral("/usr/lib/x86_64-linux-gnu/libz.so.1"),
                         QStringLiteral("zlib1g"));
        const QStringList paths = {
            QStringLiteral("/usr/lib/x86_64-linux-gnu/libc.so.6"),
            QStringLiteral("/usr/lib/x86_64-linux-gnu/libz.so.1"),
            QStringLiteral("/usr/lib/x86_64-linux-gnu/libc.so.6"),
            QStringLiteral("/opt/app/app")};
        const SbomReport report = buildSbom(paths, byName, fileOwner);
        check(report.available && report.libraryCount == 4,
              "sbom: library count");
        check(report.resolvedCount == 2, "sbom: two owning packages");
        check(report.entries.at(0).package == QStringLiteral("libc6")
                  && report.entries.at(0).files.size() == 1,
              "sbom: dedup + sorted, libc6 first");
        check(sbomSummaryText(report).contains(QStringLiteral("2 packages")),
              "sbom: summary text");
        check(sbomDetailText(report).contains(QStringLiteral("libc6@")),
              "sbom: detail text");
    }

    {
        const QByteArray json = QByteArrayLiteral(
            "{\"report\":{\"mtr\":{\"src\":\"host\",\"dst\":\"1.1.1.1\","
            "\"tos\":0,\"tests\":3,\"psize\":\"64\",\"bitpattern\":\"0x00\"},"
            "\"hubs\":[{\"count\":1,\"host\":\"10.0.0.1\",\"Loss%\":0.0,"
            "\"Snt\":3,\"Last\":2.1,\"Avg\":2.5,\"Best\":2.0,\"Wrst\":3.0,"
            "\"StDev\":0.4},{\"count\":2,\"host\":\"???.\",\"Loss%\":33.3,"
            "\"Snt\":3,\"Last\":10.0,\"Avg\":11.0,\"Best\":10.0,\"Wrst\":12.0,"
            "\"StDev\":0.9}]}}");
        bool ok = false;
        const QList<PathReport> reports = parseMtrJson(json, &ok);
        check(ok && reports.size() == 1, "path: parsed one mtr report");
        const PathReport &report = reports.first();
        check(report.dst == QStringLiteral("1.1.1.1") && report.tests == 3,
              "path: target + tests");
        check(report.hops.size() == 2, "path: two hops");
        check(report.hops.at(0).host == QStringLiteral("10.0.0.1")
                  && std::fabs(report.hops.at(0).avgMs - 2.5) < 1e-6,
              "path: hop host + avg rtt");
        check(std::fabs(report.hops.at(1).lossPercent - 33.3) < 1e-6,
              "path: hop loss percent");
        check(std::fabs(report.worstLoss() - 33.3) < 1e-6,
              "path: worst loss");
        check(pathHopsText(report).contains(QStringLiteral("10.0.0.1")),
              "path: hops text");
    }

    {
        StatsdPoint point;
        check(parseStatsdMetric(
                  QStringLiteral("requests:3|c|@0.5|#env:prod,svc:api"), point)
                  && point.name == QStringLiteral("requests")
                  && point.type == QStringLiteral("counter")
                  && std::fabs(point.sampleRate - 0.5) < 1e-9
                  && point.tags == QStringLiteral("env:prod,svc:api"),
              "statsd: parse counter with sample rate + tags");
        check(parseStatsdMetric(QStringLiteral("lat:1:2:3|h"), point)
                  && point.type == QStringLiteral("histogram")
                  && point.values.size() == 3,
              "statsd: parse multi-value histogram");
        check(parseStatsdMetric(QStringLiteral("gauge:42|g"), point)
                  && point.type == QStringLiteral("gauge"),
              "statsd: parse gauge");

        StatsdAggregator agg;
        agg.ingest(QByteArrayLiteral(
            "requests:2|c|#svc:api\nrequests:3|c|#svc:api\n"
            "mem:100|g|#svc:api\nmem:120|g|#svc:api\n"
            "lat:5|h|#svc:api\nlat:15|h|#svc:api\n"
            "users:1|s\nusers:1|s\nusers:2|s\n"
            "_e{5,4}:title|text\n_sc|web|0"));
        const StatsdSnapshot snap = agg.snapshot();
        check(snap.metrics == 9, "statsd: counted 9 metrics");
        check(snap.events == 1 && snap.serviceChecks == 1,
              "statsd: counted events + service checks");

        bool checkedCounter = false;
        bool checkedGauge = false;
        bool checkedHist = false;
        bool checkedSet = false;
        for (const StatsdSeries &series : snap.series) {
            if (series.name == QStringLiteral("requests")) {
                checkedCounter = std::fabs(series.sum - 5.0) < 1e-9;
            } else if (series.name == QStringLiteral("mem")) {
                checkedGauge = std::fabs(series.last - 120.0) < 1e-9;
            } else if (series.name == QStringLiteral("lat")) {
                checkedHist = std::fabs(series.avg - 10.0) < 1e-9
                              && std::fabs(series.max - 15.0) < 1e-9
                              && series.count == 2;
            } else if (series.name == QStringLiteral("users")) {
                checkedSet = series.type == QStringLiteral("set")
                             && series.unique == 2;
            }
        }
        check(checkedCounter, "statsd: counter sums");
        check(checkedGauge, "statsd: gauge keeps last");
        check(checkedHist, "statsd: histogram avg/max/count");
        check(checkedSet, "statsd: set unique count");
        check(statsdSummaryText(snap).contains(QStringLiteral("metrics")),
              "statsd: summary text");
    }

    {
        const QList<SensitiveFinding> aws = scanSensitiveLine(
            QStringLiteral("export AWS_KEY=AKIAIOSFODNN7EXAMPLE done"));
        check(!aws.isEmpty() && aws.first().id == QStringLiteral("aws-access-token"),
              "sensitive: detects AWS access key");
        check(!aws.first().sample.contains(QStringLiteral("IOSFODNN7EXAMPLE")),
              "sensitive: redacts the secret value");

        check(!scanSensitiveLine(
                   QStringLiteral("token: ghp_0123456789abcdefghijklmnopqrstuvwxyz"))
                   .isEmpty(),
              "sensitive: detects GitHub PAT");
        check(!scanSensitiveLine(
                   QStringLiteral("eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0."
                                  "abcdefghij"))
                   .isEmpty(),
              "sensitive: detects JWT");
        check(!scanSensitiveLine(
                   QStringLiteral("sk_live_abcdefghijklmnop12345"))
                   .isEmpty(),
              "sensitive: detects Stripe key");
        check(!scanSensitiveLine(QStringLiteral("contact alice@example.com now"))
                   .isEmpty(),
              "sensitive: detects email");
        check(!scanSensitiveLine(QStringLiteral("card 4242 4242 4242 4242 on file"))
                   .isEmpty(),
              "sensitive: detects Luhn-valid card");
        check(scanSensitiveLine(QStringLiteral("plain log line with no secrets"))
                  .isEmpty(),
              "sensitive: clean line has no findings");

        QStringList lines;
        lines << QStringLiteral("user AKIAIOSFODNN7EXAMPLE logged in")
              << QStringLiteral("mail bob@example.org")
              << QStringLiteral("nothing here");
        const SensitiveReport report = scanSensitiveLines(lines);
        check(report.scannedLines == 3 && report.findings >= 2,
              "sensitive: report aggregates findings");
        check(sensitiveSummaryText(report).contains(QStringLiteral("finding")),
              "sensitive: summary text");
        check(sensitiveDetailText(report).contains(QStringLiteral("aws-access-token")),
              "sensitive: detail names the rule");
    }

    {
        SloSpec spec;
        spec.objective = 0.99;
        spec.windowHours = 1.0;
        const qint64 base = 1000000;
        QList<SloSample> samples;
        for (int i = 0; i < 100; ++i) {
            SloSample s;
            s.atMs = base + i * 1000;
            s.good = (i != 50); // one bad in 100
            samples.append(s);
        }
        const SloReport report = evaluateSlo(spec, samples);
        check(report.total == 100 && report.bad == 1, "slo: counts samples");
        check(std::fabs(report.sli - 0.99) < 1e-9, "slo: SLI computed");
        // allowed error = 1%; consumed = 1%; budget ~ 0% left.
        check(std::fabs(report.budgetRemaining) < 1e-6,
              "slo: budget consumed exactly");
        check(report.windows.size() == 3, "slo: three burn windows");
        check(sloSummaryText(report).contains(QStringLiteral("objective")),
              "slo: summary text");

        SloSpec tight;
        tight.objective = 0.999; // 0.1% allowed; 1% consumed -> breached
        const SloReport breached = evaluateSlo(tight, samples);
        check(breached.breached, "slo: budget exhausted flagged");
        check(sloDetailText(breached).contains(QStringLiteral("BUDGET")),
              "slo: detail shows budget state");
        check(breached.windows.at(0).burnRate > 1.0,
              "slo: burn rate exceeds 1 when over budget");
    }

    {
        const ThreatEvent ev = parseAuthEvent(
            QStringLiteral("Failed password for root from 203.0.113.9 port 51022 ssh2"),
            1000);
        check(ev.logType == QStringLiteral("ssh_failed-auth")
                  && ev.sourceIp == QStringLiteral("203.0.113.9")
                  && ev.targetUser == QStringLiteral("root"),
              "threat: parse ssh failed-auth");

        QList<ThreatEvent> burst;
        for (int i = 0; i < 8; ++i) {
            ThreatEvent e;
            e.atMs = 1000 + i * 100; // 0.1s apart -> deep within 10s window
            e.logType = QStringLiteral("ssh_failed-auth");
            e.sourceIp = QStringLiteral("203.0.113.9");
            e.targetUser = QStringLiteral("root");
            burst.append(e);
        }
        const ThreatReport report = evaluateThreats(burst);
        check(report.events == 8 && report.overflows >= 1,
              "threat: ssh brute-force overflow detected");
        check(!report.findings.isEmpty()
                  && report.findings.first().label
                         == QStringLiteral("SSH Bruteforce"),
              "threat: labeled SSH Bruteforce");
        check(!report.findings.isEmpty()
                  && report.findings.first().sourceIp
                         == QStringLiteral("203.0.113.9"),
              "threat: source IP reported");

        QList<ThreatEvent> spread;
        for (int i = 0; i < 6; ++i) {
            ThreatEvent e;
            e.atMs = 1000 + i * 30000; // 30s apart -> bucket leaks
            e.logType = QStringLiteral("ssh_failed-auth");
            e.sourceIp = QStringLiteral("198.51.100.4");
            spread.append(e);
        }
        check(evaluateThreats(spread).overflows == 0,
              "threat: slow attempts do not overflow");
        check(threatDetailText(report).contains(QStringLiteral("SSH")),
              "threat: detail text");
    }

    {
        // pg_stat_database row: numbackends, xact_commit, xact_rollback,
        // blks_read, blks_hit, deadlocks, temp_bytes, blk_read_time,
        // blk_write_time, size.
        const QStringList row = {QStringLiteral("7"),
                                 QStringLiteral("1000"),
                                 QStringLiteral("3"),
                                 QStringLiteral("50"),
                                 QStringLiteral("9950"),
                                 QStringLiteral("1"),
                                 QStringLiteral("0"),
                                 QStringLiteral("12.5"),
                                 QStringLiteral("4.0"),
                                 QStringLiteral("1048576")};
        DbmSnapshot snapshot;
        snapshot.database = QStringLiteral("syspeek");
        check(parseDatabaseStat(row, snapshot), "dbm: parse database stat");
        check(snapshot.connections == 7 && snapshot.xactCommit == 1000
                  && snapshot.deadlocks == 1,
              "dbm: counters");
        check(std::fabs(snapshot.cacheHitPercent - 99.5) < 1e-6,
              "dbm: cache hit ratio");
        check(snapshot.sizeBytes == 1048576, "dbm: database size");

        QList<QStringList> states;
        states.append({QStringLiteral("active"), QStringLiteral("3")});
        states.append({QStringLiteral("idle"), QStringLiteral("16")});
        parseActivityStates(states, snapshot);
        check(snapshot.states.size() == 2
                  && snapshot.states.at(0).first == QStringLiteral("active"),
              "dbm: activity states");

        check(dbmSummaryText(snapshot).contains(QStringLiteral("cache")),
              "dbm: summary text");
        check(dbmDetailText(snapshot).contains(QStringLiteral("deadlocks")),
              "dbm: detail text");
    }

    {
        QList<double> flat;
        for (int i = 0; i < 30; ++i) flat.append(10.0 + (i % 2));
        const AnomalyResult flatZ =
            detectAnomaly(flat, AnomalyModel::Mad, 3.5);
        check(!flatZ.anomalous, "watchdog: flat series not anomalous");

        QList<double> spike = flat;
        spike.append(90.0);
        const AnomalyResult mad = detectAnomaly(spike, AnomalyModel::Mad, 3.5);
        check(mad.anomalous && mad.value == 90.0,
              "watchdog: mad flags spike");
        check(std::fabs(mad.expected - 10.5) < 1.5,
              "watchdog: mad expected value");

        const AnomalyResult z = detectAnomaly(spike, AnomalyModel::ZScore, 3.0);
        check(z.anomalous, "watchdog: zscore flags spike");
        const AnomalyResult q =
            detectAnomaly(spike, AnomalyModel::Quantile, 3.0);
        check(q.anomalous, "watchdog: quantile flags spike");
        const AnomalyResult p =
            detectAnomaly(spike, AnomalyModel::PredictLinear, 3.0);
        check(p.anomalous, "watchdog: predict_linear flags spike");

        QList<WatchdogMetric> metrics;
        metrics.append(WatchdogMetric{QStringLiteral("cpu"), mad});
        check(watchdogText(metrics).contains(QStringLiteral("ANOMALY")),
              "watchdog: text marks anomaly");
        check(watchdogText(metrics).contains(QStringLiteral("mad")),
              "watchdog: text names the model");
    }

    {
        const QDateTime now = QDateTime::fromSecsSinceEpoch(1791907433);
        check(daysUntil(now.addDays(10), now) == 10, "tls: days until expiry");
        check(tlsSeverity(-1) == QStringLiteral("expired"),
              "tls: expired severity");
        check(tlsSeverity(5) == QStringLiteral("critical"),
              "tls: critical severity");
        check(tlsSeverity(20) == QStringLiteral("warning"),
              "tls: warning severity");
        check(tlsSeverity(200) == QStringLiteral("ok"), "tls: ok severity");

        TlsSnapshot snapshot;
        snapshot.available = true;
        TlsResult good;
        good.ok = true;
        good.host = QStringLiteral("example.com");
        good.port = 443;
        good.daysLeft = 120;
        good.protocol = QStringLiteral("TLSv1.3");
        good.cipher = QStringLiteral("TLS_AES_256_GCM_SHA384");
        TlsResult soon;
        soon.ok = true;
        soon.host = QStringLiteral("api.example.com");
        soon.port = 8443;
        soon.daysLeft = 7;
        soon.protocol = QStringLiteral("TLSv1.2");
        soon.cipher = QStringLiteral("ECDHE-RSA-AES256-GCM-SHA384");
        snapshot.results = {good, soon};
        check(tlsSummaryText(snapshot).contains(QStringLiteral("warning")),
              "tls: summary counts warnings");
        check(tlsDetailText(snapshot).contains(QStringLiteral("CRITICAL")),
              "tls: detail marks critical");
    }

    {
        check(languageFromCommand(QStringLiteral("python3.11"), {})
                  == Runtime::Python,
              "runtime: python prefix");
        check(languageFromCommand(QStringLiteral("python"), {})
                  == Runtime::Python,
              "runtime: python exact");
        check(languageFromCommand(QStringLiteral("node"), {}) == Runtime::Node,
              "runtime: node exact");
        check(languageFromCommand(QStringLiteral("npm"), {}) == Runtime::Node,
              "runtime: npm exact");
        check(languageFromCommand(QStringLiteral("java"), {}) == Runtime::Java,
              "runtime: java exact");
        check(languageFromCommand(QStringLiteral("javac"), {})
                  == Runtime::Unknown,
              "runtime: javac is not java");
        check(languageFromCommand(QStringLiteral("ruby3.2"), {})
                  == Runtime::Ruby,
              "runtime: ruby version prefix");
        check(languageFromCommand(QStringLiteral("rubyw"), {})
                  == Runtime::Ruby,
              "runtime: rubyw exact");
        check(languageFromCommand(QStringLiteral("php-fpm8.2"), {})
                  == Runtime::Php,
              "runtime: php-fpm version");
        check(languageFromCommand(QStringLiteral("dotnet"), {})
                  == Runtime::Dotnet,
              "runtime: dotnet exact");
        check(languageFromCommand(QStringLiteral("java"),
                                  {QStringLiteral("-jar"),
                                   QStringLiteral("x"),
                                   QStringLiteral("org.jruby.Main")})
                  == Runtime::Ruby,
              "runtime: JRuby detected");
        check(languageFromCommand(QStringLiteral("myapp"), {})
                  == Runtime::Unknown,
              "runtime: unknown app");

        check(mapsHasDotnet(QByteArrayLiteral(
                  "7f00-7f10 r-xp 0 0 0 0 /usr/share/dotnet/shared/"
                  "Microsoft.NETCore.App/System.Runtime.dll\n")),
              "runtime: dotnet maps marker");
        check(!mapsHasDotnet(QByteArrayLiteral("/usr/lib/libc.so.6\n")),
              "runtime: non-dotnet maps");
        check(runtimeSummaryText(RuntimeInfo{}) .contains(QStringLiteral("native")),
              "runtime: summary for unknown");
    }

    {
        IncidentTracker tracker(5000, 10);
        Alert cpu;
        cpu.id = QStringLiteral("app_cpu");
        cpu.title = QStringLiteral("Focused app CPU high");
        cpu.severity = QStringLiteral("critical");
        cpu.detail = QStringLiteral("95%");
        QList<Alert> alerts{cpu};

        tracker.update(alerts, 100000);
        QList<Incident> incidents = tracker.incidents();
        check(incidents.size() == 1
                  && incidents.first().status == QStringLiteral("open")
                  && incidents.first().count == 1,
              "incident: opened from alert");

        tracker.update(alerts, 101000);
        incidents = tracker.incidents();
        check(incidents.size() == 1 && incidents.first().count == 2,
              "incident: repeated alert grouped");

        Alert mem;
        mem.id = QStringLiteral("host_mem");
        mem.title = QStringLiteral("Host memory low");
        mem.severity = QStringLiteral("warning");
        mem.detail = QStringLiteral("90%");
        alerts.append(mem);
        tracker.update(alerts, 102000);
        incidents = tracker.incidents();
        check(incidents.size() == 2, "incident: separate groups by alert id");

        tracker.update({}, 102000 + 6000);
        incidents = tracker.incidents();
        bool allResolved = true;
        for (const Incident &incident : incidents) {
            if (incident.status != QStringLiteral("resolved")) {
                allResolved = false;
            }
        }
        check(allResolved, "incident: resolved after silence interval");
        check(incidentsSummaryText(incidents).contains(QStringLiteral("resolved")),
              "incident: summary text");
        check(incidentsDetailText(incidents).contains(QStringLiteral("resolved")),
              "incident: detail text");
    }

    {
        QList<double> rising;
        for (int i = 0; i < 60; ++i) {
            rising.append(i);
        }
        const ForecastResult risingForecast =
            forecastCrossing(rising, 90.0, 60);
        check(risingForecast.crosses
                  && risingForecast.stepsToThreshold > 0,
              "forecast: rising series crosses threshold");
        check(risingForecast.direction == QStringLiteral("rising"),
              "forecast: rising direction");

        QList<double> flat;
        for (int i = 0; i < 60; ++i) {
            flat.append(12.0);
        }
        const ForecastResult flatForecast = forecastCrossing(flat, 90.0, 60);
        check(!flatForecast.crosses
                  && flatForecast.direction == QStringLiteral("flat"),
              "forecast: flat series does not cross");

        QList<double> seasonal;
        for (int i = 0; i < 40; ++i) {
            seasonal.append(10.0 + (i % 4));
        }
        seasonal.append(100.0); // injected spike
        const AnomalyResult hw =
            holtWinters(seasonal, 4, 0.5, 0.1, 0.1, 3.0);
        check(hw.anomalous, "holt-winters: seasonal spike flagged");
        check(hw.model == QStringLiteral("holt_winters"),
              "holt-winters: model name");
    }

    {
        const QByteArray stat = QByteArrayLiteral(
            "1234 (my (app) worker) S 1 1234 1234 0 -1 4194560 100 0 0 0 12 34 0 0");
        const ThreadSample sample =
            parseTaskStat(stat, QStringLiteral("futex_wait"), 1234);
        check(sample.comm == QStringLiteral("my (app) worker"),
              "profile: parses comm with parens");
        check(sample.state == QStringLiteral("S"), "profile: thread state");
        check(sample.utimeTicks == 12 && sample.stimeTicks == 34,
              "profile: thread cpu ticks");
        check(sample.waitChannel == QStringLiteral("futex_wait"),
              "profile: wait channel");

        const QByteArray smaps = QByteArrayLiteral(
            "7f0000000000-7f0000001000 r-xp 00000000 08:01 1234 "
            "/usr/lib/libfoo.so\n"
            "Size:                  4 kB\n"
            "Pss:                   8 kB\n"
            "7f0000001000-7f0000002000 rw-p 00000000 00:00 0\n"
            "Size:                  4 kB\n"
            "Pss:                   4 kB\n");
        const QList<MemoryRegion> regions = parseSmaps(smaps);
        check(regions.size() == 2, "profile: parses two memory regions");
        check(regions.at(0).path == QStringLiteral("/usr/lib/libfoo.so")
                  && regions.at(0).pssBytes == 8ULL * 1024,
              "profile: top region by PSS");
        check(regions.at(1).path == QStringLiteral("[anon]"),
              "profile: anonymous mapping grouped");
        check(profileSummaryText(ProfileSnapshot{}).contains("unavailable"),
              "profile: summary for unavailable");
    }

    {
        AlertEngine engine;
        AlertInput input;
        input.watchdogValid = true;
        input.watchdogScore = 6.0;
        input.watchdogForecastSteps = 0;
        QList<Alert> fired = engine.update(input, 1000);
        bool anomalyAlert = false;
        for (const Alert &alert : fired) {
            if (alert.id == QStringLiteral("watchdog_anomaly")) {
                anomalyAlert = true;
            }
        }
        check(anomalyAlert, "watchdog-alert: anomaly fired");

        input.watchdogForecastSteps = 5;
        fired = engine.update(input, 2000);
        bool forecastAlert = false;
        for (const Alert &alert : fired) {
            if (alert.id == QStringLiteral("watchdog_forecast")) {
                forecastAlert = true;
            }
        }
        check(forecastAlert, "watchdog-alert: forecast fired");

        input.watchdogScore = 1.0;
        input.watchdogForecastSteps = 0;
        engine.update(input, 3000);
        check(engine.active().isEmpty(), "watchdog-alert: clears when normal");

        WatchdogMetric cpu;
        cpu.name = QStringLiteral("cpu");
        cpu.result.anomalous = true;
        cpu.result.model = QStringLiteral("mad");
        cpu.result.value = 95.0;
        cpu.result.score = 8.0;
        WatchdogMetric rss;
        rss.name = QStringLiteral("rss");
        rss.result.anomalous = true;
        rss.result.model = QStringLiteral("predict_linear");
        rss.result.value = 900.0;
        rss.result.score = 6.0;
        ForecastResult forecast;
        forecast.name = QStringLiteral("cpu");
        forecast.crosses = true;
        forecast.direction = QStringLiteral("rising");
        forecast.threshold = 90.0;
        forecast.stepsToThreshold = 12;
        const QString insights =
            watchdogInsightsText({cpu, rss}, {forecast});
        check(insights.contains(QStringLiteral("CORRELATED")),
              "watchdog-insight: correlated anomalies");
        check(insights.contains(QStringLiteral("FORECAST")),
              "watchdog-insight: forecast line");
        check(insights.contains(QStringLiteral("cpu")),
              "watchdog-insight: metric named");
    }

    {
        const QByteArray cpuinfo = QByteArrayLiteral(
            "processor\t: 0\n"
            "vendor_id\t: GenuineIntel\n"
            "model name\t: Intel(R) Core(TM) i7-9750H CPU @ 2.60GHz\n"
            "cpu MHz\t\t: 2600.000\n"
            "cache size\t: 12288 KB\n"
            "physical id\t: 0\n"
            "cpu cores\t: 6\n"
            "processor\t: 1\n"
            "processor\t: 2\n");
        const HostCpuInfo cpu = parseCpuInfo(cpuinfo);
        check(cpu.vendorId == QStringLiteral("GenuineIntel"),
              "hostinfo: cpu vendor");
        check(cpu.modelName.contains(QStringLiteral("i7-9750H")),
              "hostinfo: cpu model");
        check(cpu.logicalProcessors == 3 && cpu.cores == 6
                  && cpu.packages == 1,
              "hostinfo: cpu topology");
        check(std::fabs(cpu.mhz - 2600.0) < 1e-6, "hostinfo: cpu mhz");
        check(cpu.cacheSize == QStringLiteral("12288 KB"),
              "hostinfo: cpu cache");

        const QByteArray mounts = QByteArrayLiteral(
            "/dev/nvme0n1p2 / ext4 rw,relatime 0 0\n"
            "proc /proc proc rw 0 0\n"
            "sysfs /sys sysfs rw 0 0\n"
            "tmpfs /run tmpfs rw 0 0\n");
        const QList<HostFsInfo> fs = parseMounts(mounts);
        check(fs.size() == 2, "hostinfo: pseudo filesystems filtered");
        check(fs.at(0).name == QStringLiteral("/dev/nvme0n1p2")
                  && fs.at(0).mountedOn == QStringLiteral("/"),
              "hostinfo: root filesystem");

        check(parseOsRelease(QByteArrayLiteral(
                  "NAME=\"Ubuntu\"\nPRETTY_NAME=\"Ubuntu 24.04 LTS\"\n"))
                  == QStringLiteral("Ubuntu 24.04 LTS"),
              "hostinfo: os pretty name");

        HostFsInfo withFree;
        withFree.mountedOn = QStringLiteral("/");
        withFree.name = QStringLiteral("ext4");
        withFree.kbSize = 480ULL * 1024ULL * 1024ULL;
        withFree.kbFree = 96ULL * 1024ULL * 1024ULL;
        withFree.hasKbFree = true;
        HostFsInfo unknownFree;
        unknownFree.mountedOn = QStringLiteral("/data");
        unknownFree.name = QStringLiteral("xfs");
        unknownFree.kbSize = 100ULL * 1024ULL * 1024ULL;
        unknownFree.hasKbFree = false;
        HostInfo textInfo;
        textInfo.filesystems.append(withFree);
        textInfo.filesystems.append(unknownFree);
        const QString fsText = hostInfoFilesystemsText(textInfo);
        check(fsText.contains(QStringLiteral("480.0 GB  free 96.0")),
              "hostinfo: fs text free column");
        check(fsText.contains(QStringLiteral("free --")),
              "hostinfo: fs text unknown free is keyed");
    }

    {
        QList<QStringList> rows;
        rows.append({QStringLiteral("123"), QStringLiteral("5"),
                     QStringLiteral("100.5"), QStringLiteral("20.1"),
                     QStringLiteral("10"),
                     QStringLiteral("SELECT count(*) FROM pg_class")});
        rows.append({QStringLiteral("-42"), QStringLiteral("2"),
                     QStringLiteral("3.0"), QStringLiteral("1.5"),
                     QStringLiteral("1"), QStringLiteral("SELECT 1")});
        DbmSnapshot snapshot;
        parseStatements(rows, snapshot);
        check(snapshot.statementsAvailable && snapshot.statements.size() == 2,
              "dbm: parsed pg_stat_statements rows");
        check(snapshot.statements.first().calls == 5
                  && std::fabs(snapshot.statements.first().totalMs - 100.5) < 1e-6
                  && snapshot.statements.first().query.contains(
                         QStringLiteral("pg_class")),
              "dbm: statement metrics");
        check(dbmStatementsText(snapshot).contains(QStringLiteral("pg_class")),
              "dbm: statements text");
        DbmSnapshot empty;
        check(dbmStatementsText(empty).contains(QStringLiteral("not available")),
              "dbm: statements unavailable text");
    }

    {
        auto meta = [](const char *path, const char *hash, quint64 size,
                       quint64 inode, unsigned int mode, qint64 mtime) {
            FimFileMeta m;
            m.path = QString::fromLatin1(path);
            m.hash = QString::fromLatin1(hash);
            m.size = size;
            m.inode = inode;
            m.mode = mode;
            m.mtimeMs = mtime;
            return m;
        };

        QHash<QString, FimFileMeta> previous;
        previous.insert(QStringLiteral("/etc/passwd"),
                        meta("/etc/passwd", "aa11", 100, 10, 0644, 1000));
        previous.insert(QStringLiteral("/etc/hosts"),
                        meta("/etc/hosts", "bb22", 50, 20, 0644, 2000));
        previous.insert(QStringLiteral("/etc/old"),
                        meta("/etc/old", "cc33", 30, 30, 0600, 3000));
        previous.insert(QStringLiteral("/etc/cfg"),
                        meta("/etc/cfg", "ff66", 40, 50, 0644, 3500));
        previous.insert(QStringLiteral("/etc/gone"),
                        meta("/etc/gone", "9191", 20, 60, 0644, 3600));

        QHash<QString, FimFileMeta> current;
        current.insert(QStringLiteral("/etc/passwd"),
                       meta("/etc/passwd", "dd44", 120, 10, 0640, 5000));
        current.insert(QStringLiteral("/etc/hosts"),
                       meta("/etc/hosts", "bb22", 50, 20, 0644, 2000));
        current.insert(QStringLiteral("/etc/new"),
                       meta("/etc/new", "cc33", 30, 30, 0600, 4000));
        current.insert(QStringLiteral("/etc/cfg"),
                       meta("/etc/cfg", "ff66", 40, 50, 0600, 3500));
        current.insert(QStringLiteral("/etc/added"),
                       meta("/etc/added", "ee55", 10, 40, 0644, 6000));

        const QList<FimChange> changes = diffFim(previous, current, 9000);
        check(changes.size() == 5, "fim: five drift events");

        int created = 0;
        int modified = 0;
        int metadata = 0;
        int deleted = 0;
        int renamed = 0;
        QString renamedFrom;
        for (const FimChange &change : changes) {
            switch (change.kind) {
            case FimChangeKind::Created:
                ++created;
                check(change.path == QStringLiteral("/etc/added"),
                      "fim: created path");
                break;
            case FimChangeKind::Modified:
                ++modified;
                check(change.path == QStringLiteral("/etc/passwd")
                          && change.oldHash == QStringLiteral("aa11")
                          && change.hash == QStringLiteral("dd44"),
                      "fim: modified hashes");
                break;
            case FimChangeKind::Metadata:
                ++metadata;
                check(change.path == QStringLiteral("/etc/cfg"),
                      "fim: metadata path");
                break;
            case FimChangeKind::Deleted:
                ++deleted;
                check(change.path == QStringLiteral("/etc/gone"),
                      "fim: deleted path");
                break;
            case FimChangeKind::Renamed:
                ++renamed;
                renamedFrom = change.oldPath;
                check(change.path == QStringLiteral("/etc/new")
                          && change.oldPath == QStringLiteral("/etc/old"),
                      "fim: rename pair");
                break;
            }
        }
        check(created == 1 && modified == 1 && metadata == 1 && deleted == 1
                  && renamed == 1,
              "fim: change kinds classified");
        check(renamedFrom == QStringLiteral("/etc/old"),
              "fim: rename source path");

        QHash<QString, FimFileMeta> same = current;
        check(diffFim(current, same, 9000).isEmpty(),
              "fim: identical scans -> no drift");

        FimSnapshot baseline;
        baseline.enabled = true;
        baseline.available = true;
        baseline.baseline = true;
        baseline.fileCount = 7;
        baseline.algorithm = QStringLiteral("sha256");
        check(fimSummaryText(baseline).contains(QStringLiteral("baseline"))
                  && fimSummaryText(baseline).contains(QStringLiteral("sha256")),
              "fim: baseline summary");

        FimSnapshot drift;
        drift.enabled = true;
        drift.available = true;
        drift.fileCount = 7;
        drift.algorithm = QStringLiteral("sha256");
        drift.changes = changes;
        check(fimSummaryText(drift).contains(QStringLiteral("5 change")),
              "fim: drift summary count");
        const QString detail = fimDetailText(drift);
        check(detail.contains(QStringLiteral("MODIFIED"))
                  && detail.contains(QStringLiteral("/etc/passwd"))
                  && detail.contains(QStringLiteral("RENAMED")),
              "fim: detail text");
        check(fimDetailText(FimSnapshot{}).contains(QStringLiteral("disabled")),
              "fim: disabled detail");
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
