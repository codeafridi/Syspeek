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
#include "platform/null/NullBackend.h"
#include "HostInfoStatus.h"
#include "FimStatus.h"
#include "AlertWebhook.h"
#include "AlertSilence.h"
#include "MetricsEndpoint.h"
#include "MetricsServer.h"
#include "MonitorBands.h"
#include "MetricsExport.h"
#include "GpuStatus.h"
#include "HistoryLog.h"
#include "ProcessDiskStatus.h"
#include "ProcessNetStatus.h"
#include "ProcessActions.h"
#include "OverlayStatus.h"
#include "SensorStatus.h"
#include "CloudStatus.h"
#include "CloudPricing.h"
#include "CloudFleetSource.h"
#include "CloudSpendStatus.h"
#include "CloudSpendSource.h"
#include "TickBudget.h"
#include "QtCompat.h"
#include "ServicesSource.h"
#include "platform/mac/MacFimStream.h"
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTimeZone>
#include <QThread>

#include <QApplication>
#include <QPixmap>
#include <QStandardPaths>
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

    // M66: editable monitors — threshold edits take effect live, cycles
    // edits re-arm, restore returns the built-in rules (monit-grounded).
    {
        AlertEngine editable;
        AlertInput hostInput;
        hostInput.hostCpuValid = true;
        hostInput.hostCpu = 15.0;
        editable.update(hostInput, 1);
        editable.update(hostInput, 2);
        editable.update(hostInput, 3);
        check(editable.active().isEmpty(),
              "monitors: default host_cpu threshold stays quiet");
        check(editable.setRuleThreshold(QStringLiteral("host_cpu"), 10.0),
              "monitors: threshold edit accepted");
        const AlertRule edited = [&editable]() {
            for (const AlertRule &rule : editable.rules()) {
                if (rule.id == QStringLiteral("host_cpu")) {
                    return rule;
                }
            }
            return AlertRule{};
        }();
        check(edited.threshold == 10.0,
              "monitors: rules() reflects the edit");
        check(editable.update(hostInput, 4).isEmpty(),
              "monitors: edit cleared the old streak");
        check(editable.update(hostInput, 5).isEmpty()
                  && editable.update(hostInput, 6).isEmpty()
                  && editable.update(hostInput, 7).isEmpty(),
              "monitors: fresh streak builds under the new threshold");
        const QList<Alert> firedAfterEdit = editable.update(hostInput, 8);
        check(firedAfterEdit.size() == 1
                  && firedAfterEdit.at(0).id == QStringLiteral("host_cpu"),
              "monitors: fires at sustained count under the new threshold");

        check(editable.setRuleSustained(QStringLiteral("host_cpu"), 1),
              "monitors: cycles edit accepted");
        const QList<Alert> firedOne =
            editable.update(hostInput, 9);
        check(firedOne.size() == 1, "monitors: single-sample arm after edit");
    }
    {
        AlertEngine invalid;
        check(!invalid.setRuleThreshold(QStringLiteral("nope"), 5.0),
              "monitors: unknown rule id rejected");
        check(!invalid.setRuleThreshold(QStringLiteral("host_cpu"), -1.0),
              "monitors: negative threshold rejected");
        check(!invalid.setRuleThreshold(QStringLiteral("host_cpu"), 2e9),
              "monitors: overflow threshold rejected");
        check(!invalid.setRuleSustained(QStringLiteral("host_cpu"), 0),
              "monitors: zero cycles rejected");
        check(!invalid.setRuleSustained(QStringLiteral("host_cpu"), 101),
              "monitors: 101 cycles rejected (monit bounds)");
        invalid.resetRules();
        bool found = false;
        for (const AlertRule &rule : invalid.rules()) {
            if (rule.id == QStringLiteral("host_cpu")
                && rule.threshold == 90.0 && rule.sustained == 5)
            {
                found = true;
            }
        }
        check(found, "monitors: resetRules restores built-in defaults");
    }

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

        ProcessNetTalker talker;
        talker.pid = 508637;
        talker.name = QStringLiteral("cursor");
        talker.sockets = 7;
        talker.rxPerSec = 2048.0;
        talker.txPerSec = 512.0;
        s.processNetTalkers.append(talker);
        text = prometheusText(s);
        check(text.contains(QStringLiteral(
                  "syspeek_process_net_bytes_per_second{process=\"cursor\","
                  "pid=\"508637\",direction=\"rx\"} 2048.000")),
              "prometheus: per-process rx gauge");
        check(text.contains(QStringLiteral(
                  "syspeek_process_net_bytes_per_second{process=\"cursor\","
                  "pid=\"508637\",direction=\"tx\"} 512.000")),
              "prometheus: per-process tx gauge");
        s.processNetTalkers.clear();
        text = prometheusText(s);
        check(!text.contains(
                  QStringLiteral("syspeek_process_net_bytes_per_second")),
              "prometheus: talker block omitted when no traffic");

        ProcessDiskConsumer consumer;
        consumer.pid = 4242;
        consumer.name = QStringLiteral("dd");
        consumer.readPerSec = 1024.0;
        consumer.writePerSec = 8192.0;
        s.processDiskTop.append(consumer);
        text = prometheusText(s);
        check(text.contains(QStringLiteral(
                  "syspeek_process_disk_bytes_per_second{process=\"dd\","
                  "pid=\"4242\",direction=\"read\"} 1024.000")),
              "prometheus: per-process disk read gauge");
        check(text.contains(QStringLiteral(
                  "syspeek_process_disk_bytes_per_second{process=\"dd\","
                  "pid=\"4242\",direction=\"write\"} 8192.000")),
              "prometheus: per-process disk write gauge");
        s.processDiskTop.clear();
        text = prometheusText(s);
        check(!text.contains(
                  QStringLiteral("syspeek_process_disk_bytes_per_second")),
              "prometheus: disk block omitted when no traffic");
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

        // M67: phnt thread-state / wait-reason names (ntkeapi.h enums).
        check(threadStateName(2) == QStringLiteral("running")
                  && threadStateName(5) == QStringLiteral("waiting")
                  && threadStateName(4) == QStringLiteral("terminated"),
              "profiles: KTHREAD_STATE names");
        check(threadStateName(11) == QStringLiteral("unknown"),
              "profiles: unknown thread state");
        check(threadWaitReasonName(0) == QStringLiteral("Executive")
                  && threadWaitReasonName(4)
                         == QStringLiteral("DelayExecution")
                  && threadWaitReasonName(15) == QStringLiteral("WrQueue"),
              "profiles: KWAIT_REASON names");
        check(threadWaitReasonName(44) == QStringLiteral("reason44"),
              "profiles: out-of-range wait reason");
    }

    {
        // M67: the sampler consumes backend rows (Windows NtQuery path
        // shares this seam) — fake a collector, verify warming + delta.
        class FakeProfileBackend : public NullBackend {
        public:
            bool processThreadSamples(int pid, QList<ThreadSample> &out,
                                      long &tickHz) override {
                Q_UNUSED(pid);
                ThreadSample sample;
                sample.tid = 4242;
                sample.comm = QStringLiteral("demo");
                sample.state = QStringLiteral("waiting");
                sample.waitChannel = QStringLiteral("WrUserRequest");
                // 2 CPU-seconds per sample: the sampler divides by the
                // MEASURED wall gap, and a loaded runner can overshoot
                // the 105 ms fixture by hundreds of ms. A fixture that
                // spends more CPU than wall time keeps the assertion
                // about the delta, not about the runner's scheduler.
                sample.utimeTicks = calls * 20000000ULL;
                sample.stimeTicks = 0;
                out = {sample};
                tickHz = 10000000;
                ++calls;
                return true;
            }
            int calls = 0;
        };
        FakeProfileBackend backend;
        ProfileSampler sampler(&backend);
        const ProfileSnapshot first = sampler.sample(1);
        check(first.warming && first.threadCount == 1,
              "profiles: backend path warms once");
        ::QThread::msleep(105);
        const ProfileSnapshot second = sampler.sample(1);
        check(!second.warming && second.threadCount == 1,
              "profiles: backend path emits a row");
        check(second.threads.size() == 1
                  && second.threads.first().tid == 4242
                  && second.threads.first().cpuPercent > 50.0,
              "profiles: 100ns-thread delta drives cpu percent");
        check(second.waitChannels.size() == 1
                  && second.waitChannels.first().first
                         == QStringLiteral("WrUserRequest"),
              "profiles: wait-reason grouping");
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

        const auto fsevent = decodeFimEventFlags(0x100u | 0x20000u);
        check(fsevent.created && !fsevent.removed && !fsevent.renamed
                  && !fsevent.metadata && fsevent.scanWorthy(),
              "fim: fsevents created decodes");
        const auto fsremoved = decodeFimEventFlags(0x200u);
        check(fsremoved.removed && fsremoved.scanWorthy(),
              "fim: fsevents removed decodes");
        const auto fsrenamed = decodeFimEventFlags(0x800u);
        check(fsrenamed.renamed && fsrenamed.scanWorthy(),
              "fim: fsevents renamed decodes");
        const auto fsmodified = decodeFimEventFlags(0x1000u);
        check(fsmodified.modified && !fsmodified.created && fsmodified.scanWorthy(),
              "fim: fsevents modified decodes");
        const auto fsmeta = decodeFimEventFlags(0x400u | 0x4000u | 0x8000u);
        check(fsmeta.metadata && !fsmeta.scanWorthy(),
              "fim: fsevents attributes-only not scan-worthy");
        const auto fsroot = decodeFimEventFlags(0x20u);
        check(fsroot.rootChanged && fsroot.scanWorthy() && !fsroot.created,
              "fim: fsevents root-changed decodes");
        const auto fsdropped = decodeFimEventFlags(0x1u | 0x4u);
        check(fsdropped.dropped && fsdropped.scanWorthy(),
              "fim: fsevents dropped decodes");
        check(!decodeFimEventFlags(0x10u).anything,
              "fim: fsevents history-done inert");
        check(!decodeFimEventFlags(0u).anything,
              "fim: fsevents empty flags inert");

#if defined(Q_OS_MACOS)
        {
            QTemporaryDir dir;
            const QString file = dir.filePath(QStringLiteral("fim_probe.txt"));
            {
                QFile out(file);
                check(out.open(QIODevice::WriteOnly)
                          && out.write("one") == 3,
                      "fim: the fixture file is written");
            }
            MacFimStream stream;
            int fired = 0;
            QStringList lastPaths;
            auto *holder = new QObject(&app);
            const bool started =
                stream.start({dir.path()},
                             holder,
                             [&fired, &lastPaths](const QStringList &paths) {
                                 ++fired;
                                 lastPaths = paths;
                             });
            check(started, "fim: mac fsevents stream started");
            check(stream.running(), "fim: mac fsevents stream running");
            if (started) {
                {
                    QFile out(file);
                    check(out.open(QIODevice::Append)
                              && out.write("two") == 3,
                          "fim: the fixture file is appended");
                }
                for (int i = 0; i < 100 && fired < 1; ++i) {
                    QThread::msleep(50);
                    app.processEvents();
                }
                check(fired >= 1, "fim: mac fsevents fired on modify");
                {
                    QFile out(dir.filePath(QStringLiteral("fim_new.txt")));
                    check(out.open(QIODevice::WriteOnly)
                              && out.write("new") == 3,
                          "fim: the new fixture file is written");
                }
                for (int i = 0; i < 100 && fired < 2; ++i) {
                    QThread::msleep(50);
                    app.processEvents();
                }
                check(fired >= 2, "fim: mac fsevents fired on create");
                check(!lastPaths.isEmpty(), "fim: mac fsevents reports paths");
                stream.stop();
                check(!stream.running(), "fim: mac fsevents stopped");
            }
            delete holder;
        }
#else
        {
            MacFimStream stream;
            check(!stream.start({QStringLiteral("/")}, nullptr, {}),
                  "fim: non-mac stream stub refuses start");
            check(!stream.running(), "fim: non-mac stream stub inert");
        }
#endif
    }

    // --- alert webhooks (M69): payload schema grounded on Alertmanager 0.34.1
    {
        WebhookEvent firing;
        firing.id = QStringLiteral("app_cpu");
        firing.title = QStringLiteral("Focused app CPU 96% (high)");
        firing.detail = QStringLiteral("cursor cpu 96.0% >= 90%");
        firing.severity = QStringLiteral("critical");
        firing.status = QStringLiteral("firing");
        firing.startsAtMs = 1700000000000LL;

        const QJsonObject generic =
            QJsonDocument::fromJson(alertmanagerWebhookPayload({firing}))
                .object();
        check(generic.value(QStringLiteral("version")).toString()
                  == QStringLiteral("4"),
              "webhook: generic payload version 4");
        check(!generic.value(QStringLiteral("groupKey")).toString().isEmpty(),
              "webhook: generic groupKey present");
        check(generic.value(QStringLiteral("truncatedAlerts")).toInt() == 0,
              "webhook: generic truncatedAlerts 0");
        check(generic.value(QStringLiteral("receiver")).toString()
                  == QStringLiteral("syspeek"),
              "webhook: generic receiver");
        check(generic.value(QStringLiteral("status")).toString()
                  == QStringLiteral("firing"),
              "webhook: generic status");
        check(generic.contains(QStringLiteral("commonLabels"))
                  && generic.contains(QStringLiteral("commonAnnotations"))
                  && generic.contains(QStringLiteral("externalURL")),
              "webhook: generic template fields present");
        const QJsonArray alerts =
            generic.value(QStringLiteral("alerts")).toArray();
        check(alerts.size() == 1, "webhook: generic one alert");
        const QJsonObject alert = alerts.first().toObject();
        const QJsonObject labels =
            alert.value(QStringLiteral("labels")).toObject();
        const QJsonObject annotations =
            alert.value(QStringLiteral("annotations")).toObject();
        check(labels.value(QStringLiteral("alertname")).toString()
                  == QStringLiteral("app_cpu"),
              "webhook: label alertname = rule id");
        check(labels.value(QStringLiteral("severity")).toString()
                  == QStringLiteral("critical"),
              "webhook: label severity");
        check(annotations.value(QStringLiteral("summary")).toString()
                  == firing.title
                  && annotations.value(QStringLiteral("description")).toString()
                         == firing.detail,
              "webhook: annotations summary/description");
        check(alert.value(QStringLiteral("startsAt")).toString()
                  == QStringLiteral("2023-11-14T22:13:20Z"),
              "webhook: startsAt ISO");
        check(alert.value(QStringLiteral("endsAt")).toString()
                  == QStringLiteral("0001-01-01T00:00:00Z"),
              "webhook: firing endsAt epoch");
        check(alert.value(QStringLiteral("fingerprint")).toString()
                  == QStringLiteral("app_cpu"),
              "webhook: fingerprint");

        WebhookEvent resolved = firing;
        resolved.status = QStringLiteral("resolved");
        resolved.endsAtMs = 1700000060000LL;
        const QJsonObject genericResolved =
            QJsonDocument::fromJson(alertmanagerWebhookPayload({resolved}))
                .object();
        check(genericResolved.value(QStringLiteral("status")).toString()
                  == QStringLiteral("resolved"),
              "webhook: generic resolved status");
        check(genericResolved.value(QStringLiteral("alerts"))
                      .toArray()
                      .first()
                      .toObject()
                      .value(QStringLiteral("endsAt"))
                      .toString()
                  == QStringLiteral("2023-11-14T22:14:20Z"),
              "webhook: resolved endsAt ISO");

        const QJsonObject slack =
            QJsonDocument::fromJson(slackWebhookPayload({firing})).object();
        check(slack.value(QStringLiteral("text")).toString().contains(
                  QStringLiteral("firing")),
              "webhook: slack text mentions firing");
        const QJsonArray attachments =
            slack.value(QStringLiteral("attachments")).toArray();
        check(attachments.size() == 1, "webhook: slack one attachment");
        const QJsonObject attachment = attachments.first().toObject();
        check(attachment.value(QStringLiteral("color")).toString()
                  == QStringLiteral("d1685f"),
              "webhook: slack critical color");
        check(attachment.value(QStringLiteral("title")).toString()
                  == firing.title,
              "webhook: slack attachment title");
        check(attachment.value(QStringLiteral("fallback")).toString().contains(
                  QStringLiteral("firing")),
              "webhook: slack fallback");
        const QJsonArray fields =
            attachment.value(QStringLiteral("fields")).toArray();
        check(fields.size() == 2
                  && fields.first().toObject().value(QStringLiteral("short"))
                         .toBool(),
              "webhook: slack fields short");
        check(attachment.value(QStringLiteral("mrkdwn_in"))
                      .toArray()
                      .contains(QJsonValue(QStringLiteral("text"))),
              "webhook: slack mrkdwn_in text");

        const QJsonObject slackResolved =
            QJsonDocument::fromJson(slackWebhookPayload({resolved})).object();
        check(slackResolved.value(QStringLiteral("attachments"))
                      .toArray()
                      .first()
                      .toObject()
                      .value(QStringLiteral("color"))
                      .toString()
                  == QStringLiteral("36a64f"),
              "webhook: slack resolved color");

        WebhookConfig off;
        check(webhookConfigLine(off)
                  == QStringLiteral("off ([webhook] enabled=false)"),
              "webhook: config line off by default");
        WebhookConfig noUrl;
        noUrl.enabled = true;
        check(webhookConfigLine(noUrl).startsWith(QStringLiteral("no url")),
              "webhook: config line missing url");
        WebhookConfig slackCfg;
        slackCfg.enabled = true;
        slackCfg.url = QStringLiteral("https://example.test/hook");
        slackCfg.format = QStringLiteral("slack");
        check(webhookConfigLine(slackCfg)
                  == QStringLiteral("slack -> https://example.test/hook"),
              "webhook: config line enabled");
    }

    // --- alert webhooks (M69): live loopback delivery
    {
        QTcpServer server;
        const bool listening = server.listen(QHostAddress::LocalHost, 0);
        check(listening, "webhook: loopback receiver listening");
        QByteArray received;
        bool statusOk = true;
        QObject::connect(
            &server, &QTcpServer::newConnection, &server, [&]() {
                QTcpSocket *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [&, socket]() {
                                     received += socket->readAll();
                                     if (!received.contains("\r\n\r\n")) {
                                         return;
                                     }
                                     const QByteArray reply =
                                         statusOk
                                             ? QByteArrayLiteral(
                                                   "HTTP/1.1 200 OK\r\n"
                                                   "Content-Length: 2\r\n"
                                                   "Connection: close\r\n\r\nok")
                                             : QByteArrayLiteral(
                                                   "HTTP/1.1 500 Error\r\n"
                                                   "Content-Length: 3\r\n"
                                                   "Connection: close\r\n\r\nerr");
                                     socket->write(reply);
                                     socket->flush();
                                 });
            });

        auto waitDelivery = [&app](AlertWebhook &hook, bool &ok,
                                   QString &error) {
            QEventLoop loop;
            bool done = false;
            QObject::connect(&hook, &AlertWebhook::deliveryFinished, &loop,
                             [&](bool delivered, const QString &err) {
                                 ok = delivered;
                                 error = err;
                                 done = true;
                                 loop.quit();
                             });
            for (int i = 0; i < 200 && !done; ++i) {
                QThread::msleep(25);
                app.processEvents();
            }
            return done;
        };

        WebhookConfig config;
        config.enabled = true;
        config.url = QStringLiteral("http://127.0.0.1:%1/hook")
                         .arg(server.serverPort());
        config.timeoutMs = 3000;

        AlertWebhook hook;
        hook.setConfig(config);

        Alert alert;
        alert.id = QStringLiteral("host_cpu");
        alert.title = QStringLiteral("Host CPU 97% (critical)");
        alert.detail = QStringLiteral("host cpu 97.2% >= 95%");
        alert.severity = QStringLiteral("critical");
        alert.atMs = 1700000000000LL;

        bool ok = false;
        QString error;
        hook.processEvents({alert}, {alert}, 1700000000000LL);
        const bool delivered = waitDelivery(hook, ok, error);
        check(delivered, "webhook: live delivery finished");
        check(ok, "webhook: live delivery 200 ok");
        check(received.startsWith("POST /hook HTTP/1.1"),
              "webhook: live request POST path");
        check(received.toLower().contains("content-type: application/json"),
              "webhook: live request content type");
        check(received.contains("\"version\":\"4\""),
              "webhook: live body carries v4 payload");
        check(hook.statusLine().contains(QStringLiteral("1 sent, 0 failed")),
              "webhook: live status line counts sent");

        statusOk = false;
        Alert second;
        second.id = QStringLiteral("app_rss");
        second.title = QStringLiteral("Focused app RSS 2.4 GB (warn)");
        second.detail = QStringLiteral("cursor rss 2400 MB >= 1024 MB");
        second.severity = QStringLiteral("warning");
        second.atMs = 1700000000000LL;

        ok = true;
        error.clear();
        hook.processEvents({second}, {alert, second}, 1700000060000LL);
        const bool failedDelivery = waitDelivery(hook, ok, error);
        check(failedDelivery, "webhook: failing delivery finished");
        check(!ok && error.contains(QStringLiteral("500")),
              "webhook: non-2xx reported as failure");
        check(hook.statusLine().contains(QStringLiteral("1 failed")),
              "webhook: failure counted");

        hook.processEvents({}, {alert}, 1700000120000LL);
        const bool resolvedDelivery = waitDelivery(hook, ok, error);
        check(resolvedDelivery, "webhook: resolved delivery finished");
        check(received.contains("\"status\":\"resolved\""),
              "webhook: resolved event posted when alert vanishes");

        AlertWebhook disabledHook;
        bool disabledDelivered = false;
        QObject::connect(&disabledHook, &AlertWebhook::deliveryFinished,
                         &disabledHook,
                         [&](bool, const QString &) {
                             disabledDelivered = true;
                         });
        disabledHook.processEvents({alert}, {alert}, 1700000180000LL);
        for (int i = 0; i < 8; ++i) {
            QThread::msleep(25);
            app.processEvents();
        }
        check(!disabledDelivered, "webhook: disabled by default sends nothing");
    }


    {
        DisplaySnapshot snap;
        snap.appName = QStringLiteral("cursor");
        snap.pid = QStringLiteral("36904");
        snap.cpuValid = true;
        snap.cpuPercent = 12.5;
        snap.rssKb = 3141788;
        snap.hostAvailable = true;
        snap.hostWarming = false;
        snap.hostCpuPercent = 21.25;
        snap.hostStealPercent = 0.5;
        snap.hostMemAvailableKb = 6278908;
        snap.hostMemTotalKb = 15482276;
        snap.hostLoad1 = 4.46;
        snap.hostNetRxPerSec = 2048.0;
        snap.hostNetTxPerSec = 512.0;
        snap.disksAvailable = true;
        snap.disksWarming = false;
        DiskRow sda;
        sda.name = QStringLiteral("sda");
        sda.readKbPerSec = 100.0;
        sda.writeKbPerSec = 25.0;
        sda.utilPercent = 4.5;
        DiskRow nvme;
        nvme.name = QStringLiteral("nvme0n1");
        nvme.readKbPerSec = 900.0;
        nvme.writeKbPerSec = 75.0;
        nvme.utilPercent = 12.5;
        snap.disks = {sda, nvme};
        snap.tcpHealthAvailable = true;
        snap.tcpHealth.inSegsPerSec = 1234.5;
        snap.tcpHealth.outSegsPerSec = 678.25;
        snap.tcpHealth.retransPerSec = 3.5;
        snap.tcpHealth.retransPercent = 0.25;
        snap.tcpHealth.currEstab = 42;

        const ExportRow row = exportRowFrom(snap, 1793016000000LL);
        check(row.atMs == 1793016000000LL, "export row: timestamp kept");
        check(row.appName == QStringLiteral("cursor")
                  && row.pid == QStringLiteral("36904"),
              "export row: app identity");
        check(qFuzzyCompare(row.appCpuPercent, 12.5)
                  && row.appRssKb == 3141788,
              "export row: app cpu + rss");
        check(qFuzzyCompare(row.hostCpuPercent, 21.25)
                  && qFuzzyCompare(row.hostStealPercent, 0.5)
                  && row.hostMemAvailableKb == 6278908
                  && row.hostMemTotalKb == 15482276
                  && qFuzzyCompare(row.load1, 4.46),
              "export row: host block");
        check(qFuzzyCompare(row.diskReadKbPerSec, 1000.0)
                  && qFuzzyCompare(row.diskWriteKbPerSec, 100.0)
                  && qFuzzyCompare(row.diskUtilPercent, 12.5),
              "export row: disk sums + max util");
        check(qFuzzyCompare(row.netRxKbPerSec, 2.0)
                  && qFuzzyCompare(row.netTxKbPerSec, 0.5),
              "export row: net bytes/s to kB/s");
        check(qFuzzyCompare(row.tcpInSegsPerSec, 1234.5)
                  && qFuzzyCompare(row.tcpRetransPercent, 0.25)
                  && row.tcpCurrEstab == 42,
              "export row: tcp counters");

        DisplaySnapshot warm;
        warm.appName = QStringLiteral("--");
        warm.pid = QStringLiteral("--");
        warm.hostAvailable = true;
        warm.hostWarming = true;
        warm.hostCpuPercent = 99.0;
        warm.disksAvailable = true;
        warm.disksWarming = true;
        warm.disks = {sda};
        warm.tcpHealthAvailable = false;
        warm.tcpHealth.inSegsPerSec = 5.0;
        const ExportRow warmRow = exportRowFrom(warm, 1793016001000LL);
        check(warmRow.appName.isEmpty() && warmRow.pid.isEmpty(),
              "export row: no-focus app left unset");
        check(warmRow.appCpuPercent < 0.0 && warmRow.appRssKb < 0,
              "export row: no-focus app cpu/rss gated");
        check(warmRow.hostCpuPercent < 0.0 && warmRow.hostMemTotalKb < 0,
              "export row: host warming tick gated");
        check(warmRow.diskReadKbPerSec < 0.0 && warmRow.diskUtilPercent < 0.0,
              "export row: disk warming tick gated");
        check(warmRow.tcpInSegsPerSec < 0.0 && warmRow.tcpCurrEstab < 0,
              "export row: unavailable tcp counters gated");

        ExportMeta meta;
        meta.hostname = QStringLiteral("testbox");
        meta.platform = QStringLiteral("linux");
        meta.version = QStringLiteral("0.8.9");
        meta.intervalMs = 2000;

        const QList<ExportRow> rows = {row, warmRow};
        const QString csv = exportCsv(rows, meta);
        const QStringList csvLines =
            csv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        check(csvLines.size() == 3, "csv: header + one row per sample");
        check(csvLines.first().startsWith(
                  QStringLiteral("# hostname;interval;timestamp;APP;app_pid;")),
              "csv: sadf-style '#' header line");
        const QStringList header =
            csvLines.first().mid(2).split(QLatin1Char(';'));
        const QStringList firstFields = csvLines.at(1).split(QLatin1Char(';'));
        const QStringList secondFields = csvLines.at(2).split(QLatin1Char(';'));
        check(firstFields.size() == header.size()
                  && secondFields.size() == header.size(),
              "csv: every row matches the header width");
        check(firstFields.at(0) == QStringLiteral("testbox")
                  && firstFields.at(1) == QStringLiteral("2"),
              "csv: hostname + interval seconds");
        check(firstFields.at(2) == QStringLiteral("2026-10-26 12:00:00 UTC"),
              "csv: UTC timestamp in sadf shape");
        check(firstFields.at(3) == QStringLiteral("cursor")
                  && firstFields.at(4) == QStringLiteral("36904"),
              "csv: app name + pid fields");
        check(firstFields.at(5) == QStringLiteral("12.50")
                  && firstFields.at(7) == QStringLiteral("21.25")
                  && firstFields.at(12) == QStringLiteral("1000.00"),
              "csv: 2-decimal floats");
        check(firstFields.at(8) == QStringLiteral("0.50")
                  && firstFields.at(19) == QStringLiteral("3.50")
                  && firstFields.at(20) == QStringLiteral("0.25"),
              "csv: sub-unit floats keep 2 decimals");
        check(secondFields.at(5) == QStringLiteral("-1")
                  && secondFields.at(7) == QStringLiteral("-1")
                  && secondFields.at(11) == QStringLiteral("-1")
                  && secondFields.at(20) == QStringLiteral("-1"),
              "csv: -1 sentinel for not-applicable values");
        check(csv.endsWith(QLatin1Char('\n')), "csv: newline-terminated");

        const QJsonObject root = QJsonDocument::fromJson(
            exportJson(rows, meta).toUtf8()).object();
        check(root.contains(QStringLiteral("syspeek")),
              "json: own namespace, not sysstat");
        const QJsonObject body = root.value(QStringLiteral("syspeek")).toObject();
        check(body.value(QStringLiteral("version")).toString()
                  == QStringLiteral("0.8.9")
                  && body.value(QStringLiteral("platform")).toString()
                  == QStringLiteral("linux")
                  && body.value(QStringLiteral("hostname")).toString()
                  == QStringLiteral("testbox")
                  && body.value(QStringLiteral("intervalMs")).toInt() == 2000,
              "json: envelope metadata");
        const QJsonArray samples =
            body.value(QStringLiteral("samples")).toArray();
        check(samples.size() == 2, "json: one object per sample");
        const QJsonObject first = samples.at(0).toObject();
        check(first.contains(QStringLiteral("timestamp"))
                  && first.contains(QStringLiteral("app"))
                  && first.contains(QStringLiteral("host"))
                  && first.contains(QStringLiteral("disk"))
                  && first.contains(QStringLiteral("network"))
                  && first.contains(QStringLiteral("tcp")),
              "json: sadf-style per-sample domains");
        const QJsonObject stamp =
            first.value(QStringLiteral("timestamp")).toObject();
        check(stamp.value(QStringLiteral("date")).toString()
                  == QStringLiteral("2026-10-26")
                  && stamp.value(QStringLiteral("time")).toString()
                  == QStringLiteral("12:00:00")
                  && stamp.value(QStringLiteral("tz")).toString()
                  == QStringLiteral("UTC"),
              "json: timestamp date/time/tz split");
        const QJsonObject firstHost =
            first.value(QStringLiteral("host")).toObject();
        const QJsonObject firstDisk =
            first.value(QStringLiteral("disk")).toObject();
        check(qFuzzyCompare(firstHost.value(QStringLiteral("cpuPercent"))
                                .toDouble(), 21.25)
                  && firstHost.value(QStringLiteral("memTotalKb")).toInt()
                  == 15482276
                  && qFuzzyCompare(firstDisk.value(
                                       QStringLiteral("readKbPerSec"))
                                       .toDouble(),
                                   1000.0),
              "json: measured values carried");
        const QJsonObject second = samples.at(1).toObject();
        const QJsonObject secondHost =
            second.value(QStringLiteral("host")).toObject();
        const QJsonObject secondApp =
            second.value(QStringLiteral("app")).toObject();
        check(secondHost.value(QStringLiteral("cpuPercent")).isNull()
                  && secondHost.value(QStringLiteral("load1")).isNull()
                  && secondApp.value(QStringLiteral("name")).isNull()
                  && second.value(QStringLiteral("tcp"))
                          .toObject()
                          .value(QStringLiteral("currEstab"))
                          .isNull(),
              "json: unavailable values are null");

        check(normalizedExportFormat(QStringLiteral("JSON"))
                  == QStringLiteral("json")
                  && normalizedExportFormat(QStringLiteral(" csv "))
                          == QStringLiteral("csv")
                  && normalizedExportFormat(QStringLiteral("xml"))
                          == QStringLiteral("csv"),
              "export: format normalization falls back to csv");
        check(exportRows(rows, meta, QStringLiteral("json"))
                      .trimmed()
                      .startsWith(QLatin1Char('{'))
                  && exportRows(rows, meta, QStringLiteral("csv"))
                         .startsWith(QLatin1Char('#')),
              "export: dispatch honours the format");

        const QString emptyCsv =
            exportCsv({}, meta);
        check(emptyCsv.count(QLatin1Char('\n')) == 1,
              "csv: header-only file when no samples exist");
    }

    {
        // M71: per-socket tcp_info byte deltas become per-pid rates.
        linuxproc::TcpDiagInfo a;
        a.inode = 111;
        a.hasInfo = true;
        a.bytesReceived = 1000;
        a.bytesSent = 500;
        linuxproc::TcpDiagInfo b;
        b.inode = 222;
        b.hasInfo = true;
        b.bytesReceived = 4096;
        b.bytesSent = 0;

        QHash<unsigned long long, ProcessNetPrior> prior;
        QHash<unsigned long long, ProcessNetRate> rates =
            processNetSocketRates({a, b}, 1000, prior);
        check(rates.value(111).rxPerSec < 0.0
                  && rates.value(222).txPerSec < 0.0,
              "net: first tick per socket is warming (no rate)");

        a.bytesReceived = 3000;
        a.bytesSent = 1500;
        b.bytesReceived = 4096;
        rates = processNetSocketRates({a, b}, 1000, prior);
        check(rates.value(111).rxPerSec == 2000.0
                  && rates.value(111).txPerSec == 1000.0,
              "net: byte delta over the interval yields bytes/s");
        check(rates.value(222).rxPerSec == 0.0
                  && rates.value(222).txPerSec == 0.0,
              "net: idle socket reports an honest zero");

        a.bytesReceived = 10;
        a.bytesSent = 10;
        rates = processNetSocketRates({a}, 1000, prior);
        check(rates.value(111).rxPerSec == 0.0
                  && rates.value(111).txPerSec == 0.0,
              "net: recycled inode rebases to zero, never negative");

        linuxproc::TcpDiagInfo noInfo;
        noInfo.inode = 333;
        noInfo.hasInfo = false;
        rates = processNetSocketRates({noInfo}, 1000, prior);
        check(rates.isEmpty(), "net: sockets without tcp_info are skipped");

        QHash<unsigned long long, ProcessNetRate> socketRates;
        ProcessNetRate known;
        known.rxPerSec = 2048.0;
        known.txPerSec = 1024.0;
        socketRates.insert(111, known);
        ProcessNetRate warm;
        socketRates.insert(222, warm);
        ProcessNetRate heavy;
        heavy.rxPerSec = 8192.0;
        heavy.txPerSec = 4096.0;
        socketRates.insert(999, heavy);
        QHash<unsigned long long, int> inodePid;
        inodePid.insert(111, 500);
        inodePid.insert(222, 500);
        inodePid.insert(999, 501);
        const QHash<int, ProcessNetPidRate> pidRates =
            processNetPidRates(socketRates, inodePid);
        check(pidRates.size() == 2 && pidRates.value(500).sockets == 2,
              "net: pid aggregation counts sockets");
        check(pidRates.value(500).rxPerSec == 2048.0
                  && pidRates.value(500).txPerSec == 1024.0,
              "net: pid rates sum warmed sockets only");
        check(pidRates.value(501).rxPerSec == 8192.0
                  && pidRates.value(501).rxPerSec >= 0.0,
              "net: unattributed sockets are absent from the pid map");

        const QList<ProcessNetTalker> talkers =
            processNetTopTalkers(pidRates, 1);
        check(talkers.size() == 1 && talkers.first().pid == 501,
              "net: top talkers sorted by throughput and limited");

        const QList<ProcessNetTalker> allTalkers =
            processNetTopTalkers(pidRates, 8);
        check(allTalkers.size() == 2, "net: zero-traffic pids filtered");

        QHash<unsigned long long, ProcessNetRate> coldSockets;
        coldSockets.insert(222, warm);
        const QHash<int, ProcessNetPidRate> coldOnly =
            processNetPidRates(coldSockets, inodePid);
        check(coldOnly.value(500).rxPerSec < 0.0
                  && coldOnly.value(500).sockets == 1
                  && processNetTopTalkers(coldOnly, 8).isEmpty(),
              "net: warming pids stay out of the talker list");

        linuxproc::SocketInfo listen4;
        listen4.proto = QStringLiteral("tcp4");
        listen4.state = QStringLiteral("LISTEN");
        listen4.localPort = 8080;
        listen4.inode = 111;
        linuxproc::SocketInfo listen6;
        listen6.proto = QStringLiteral("tcp6");
        listen6.state = QStringLiteral("LISTEN");
        listen6.localPort = 443;
        listen6.inode = 111;
        linuxproc::SocketInfo established;
        established.proto = QStringLiteral("tcp4");
        established.state = QStringLiteral("ESTABLISHED");
        established.localPort = 9000;
        established.inode = 111;
        linuxproc::SocketInfo otherPid;
        otherPid.proto = QStringLiteral("tcp4");
        otherPid.state = QStringLiteral("LISTEN");
        otherPid.localPort = 5432;
        otherPid.inode = 777;
        const QList<quint16> ports = processNetListenPorts(
            {listen4, listen6, established, otherPid}, {111});
        check(ports == QList<quint16>({443, 8080}),
              "net: listen ports filtered, deduped and sorted");

        check(formatNetRate(-1.0) == QStringLiteral("--"),
              "net: unknown rate renders as dashes");
        check(formatNetRate(512.0) == QStringLiteral("512 B/s")
                  && formatNetRate(2048.0) == QStringLiteral("2 KB/s")
                  && formatNetRate(5.0 * 1024 * 1024)
                         == QStringLiteral("5.0 MB/s"),
              "net: rate formatting scales");
        check(formatListenPorts({}) == QStringLiteral("--")
                  && formatListenPorts({22, 80, 443, 8080})
                         == QStringLiteral("22, 80, 443 +1")
                  && formatListenPorts({5432}) == QStringLiteral("5432"),
              "net: port formatting elides long lists");
        check(processNetSummaryText(0, 0)
                  == QStringLiteral("no tcp sockets")
                  && processNetSummaryText(11, 35)
                         == QStringLiteral("tcp sockets: 11 tracked, "
                                           "35 other users"),
              "net: summary states attribution honesty");
    }

    {
        class FakeInodeBackend : public NullBackend {
        public:
            QList<unsigned long long> socketInodes(int pid) override {
                if (pid == 700) {
                    return {111, 0, 222};
                }
                if (pid == 701) {
                    return {333};
                }
                return {};
            }
        };
        FakeInodeBackend backend;
        const QHash<unsigned long long, int> map =
            processNetInodePid(backend, {700, 701, 702});
        check(map.size() == 3 && map.value(111) == 700
                  && map.value(222) == 700 && map.value(333) == 701,
              "net: inode map skips zero inodes and unreadable pids");
    }

    {
        // M72: iotop-style per-process block i/o deltas.
        QHash<int, ProcessDiskCounters> counters;
        counters.insert(100, ProcessDiskCounters{1000, 500});
        counters.insert(200, ProcessDiskCounters{-1, -1});
        QHash<int, ProcessDiskCounters> prior;
        QHash<int, ProcessDiskRate> rates =
            processDiskRates(counters, 1000, prior);
        check(rates.value(100).readPerSec < 0.0
                  && rates.value(100).writePerSec < 0.0,
              "disk: first tick per pid is warming (no rate)");
        check(rates.value(200).readPerSec < 0.0,
              "disk: unreadable pids never report a rate");

        counters.insert(100, ProcessDiskCounters{11000, 1000});
        rates = processDiskRates(counters, 1000, prior);
        check(rates.value(100).readPerSec == 10000.0
                  && rates.value(100).writePerSec == 500.0,
              "disk: counter delta over the window yields bytes/s");

        counters.insert(100, ProcessDiskCounters{10, 10});
        rates = processDiskRates(counters, 1000, prior);
        check(rates.value(100).readPerSec == 0.0
                  && rates.value(100).writePerSec == 0.0,
              "disk: recycled pid rebases to zero, never negative");

        check(processDiskTotal(rates, true) == 0.0,
              "disk: fleet total sums known rates");

        rates = processDiskRates(counters, 0, prior);
        check(rates.value(100).readPerSec < 0.0,
              "disk: a zero-length window yields no rate");
        QHash<int, ProcessDiskRate> emptyRates;
        check(processDiskTotal(emptyRates, true) < 0.0,
              "disk: fleet total is unknown when nothing was read");

        QHash<int, ProcessDiskRate> consumerRates;
        ProcessDiskRate idle;
        idle.readPerSec = 0.0;
        idle.writePerSec = 0.0;
        consumerRates.insert(300, idle);
        ProcessDiskRate reader;
        reader.readPerSec = 4096.0;
        reader.writePerSec = 0.0;
        consumerRates.insert(301, reader);
        ProcessDiskRate writer;
        writer.readPerSec = 0.0;
        writer.writePerSec = 8192.0;
        consumerRates.insert(302, writer);
        consumerRates.insert(303, ProcessDiskRate{});
        QHash<int, QString> names;
        names.insert(301, QStringLiteral("dd"));
        const QList<ProcessDiskConsumer> top =
            processDiskTopConsumers(consumerRates, names, 2);
        check(top.size() == 2 && top.first().pid == 302
                  && top.at(1).pid == 301,
              "disk: consumers sorted by combined throughput, limited");
        check(top.at(1).name == QStringLiteral("dd"),
              "disk: consumer names come from the caller map");
        const QList<ProcessDiskConsumer> allTop =
            processDiskTopConsumers(consumerRates, {}, 8);
        check(allTop.size() == 2 && allTop.first().name
                                          == QStringLiteral("pid 302"),
              "disk: idle and warming pids filtered, fallback name used");

        check(processDiskSummaryText(0, 0, -1.0, -1.0)
                  == QStringLiteral("no process block i/o this tick")
                  && processDiskSummaryText(12, 40, 2048.0, 1024.0)
                         == QStringLiteral("12 processes tracked - read "
                                           "2 KB/s / write 1 KB/s, "
                                           "40 other users"),
              "disk: summary states tracking and unreadable honesty");
        check(processDiskConsumerText({}).contains(QStringLiteral("no block")),
              "disk: empty consumer list is stated, not blank");
        const QString consumerText = processDiskConsumerText(top);
        check(consumerText.contains(QStringLiteral("pid 302"))
                  && consumerText.contains(QStringLiteral("KB/s")),
              "disk: consumer text renders one row per pid");
    }


    {
        GpuEngineSample sample;
        const QByteArray i915 =
            "pos:\t0\nflags:\t02100002\ndrm-driver:\ti915\n"
            "drm-client-id:\t721\ndrm-pdev:\t0000:00:02.0\n"
            "drm-total-system0:\t4096\ndrm-total-stolen-system0:\t0\n"
            "drm-engine-render:\t1000 ns\ndrm-engine-copy:\t250 ns\n"
            "drm-engine-capacity-video:\t2\ndrm-engine-video:\t0 ns\n";
        check(parseDrmFdinfo(i915, sample) && sample.driver == "i915"
                  && sample.clientId == 721
                  && sample.pciId == QStringLiteral("0000:00:02.0")
                  && sample.memSystem == 4096 && sample.hasEngines
                  && sample.engineNanos.value(QStringLiteral("render")) == 1000
                  && !sample.engineNanos.contains(QStringLiteral("capacity-video")),
              "gpu: i915 fdinfo decode (engines in ns, capacity excluded)");

        GpuEngineSample amd;
        const QByteArray amdgpu =
            "drm-driver:\tamdgpu\ndrm-client-id:\t9\n"
            "drm-memory-vram:\t1048576 KiB\ndrm-total-local0:\t8192\n"
            "drm-total-system0:\t2048\ndrm-engine-gfx:\t5000 ns\n";
        check(parseDrmFdinfo(amdgpu, amd) && amd.driver == "amdgpu"
                  && amd.memLocal == 8192 && amd.memSystem == 2048,
              "gpu: amdgpu local/system memory split");

        GpuEngineSample plain;
        check(!parseDrmFdinfo("pos:\t0\nflags:\t02\n", plain),
              "gpu: non-drm fdinfo is rejected");

        QHash<quint64, GpuClientPrior> prior;
        GpuEngineSample first;
        first.pid = 42;
        first.name = QStringLiteral("game");
        first.clientId = 7;
        first.engineNanos.insert(QStringLiteral("render"), 1000);
        first.hasEngines = true;
        const auto warming = gpuClientBusy({first}, 1000, prior);
        check(warming.value(7).busyPercent < 0.0,
              "gpu: first sighting of a client is warming, not zero");

        GpuEngineSample second = first;
        second.engineNanos.insert(QStringLiteral("render"), 1000 + 500 * 1000000LL);
        const auto busy = gpuClientBusy({second}, 1000, prior);
        check(qAbs(busy.value(7).busyPercent - 50.0) < 0.01,
              "gpu: engine nanos over the window become 50 percent");
        check(busy.value(7).engines.contains(QStringLiteral("render")),
              "gpu: busy engines are named in the row");

        GpuEngineSample reset = first;
        reset.engineNanos.insert(QStringLiteral("render"), 10);
        const auto afterReset = gpuClientBusy({reset}, 1000, prior);
        check(afterReset.value(7).busyPercent == 0.0,
              "gpu: a counter reset rebases to zero, never negative");

        GpuEngineSample other;
        other.pid = 77;
        other.clientId = 8;
        other.engineNanos.insert(QStringLiteral("render"), 1);
        QHash<quint64, GpuClientPrior> prune;
        gpuClientBusy({first}, 0, prune);
        gpuClientBusy({other}, 0, prune);
        check(prune.size() == 1 && prune.contains(8),
              "gpu: prior map drops clients that disappeared");

        GpuEngineSample a;
        a.pid = 42;
        a.name = QStringLiteral("game");
        a.clientId = 1;
        a.memLocal = 1024;
        GpuEngineSample b;
        b.pid = 42;
        b.name = QStringLiteral("game");
        b.clientId = 2;
        b.memSystem = 512;
        QHash<quint64, GpuClientBusy> rates;
        GpuClientBusy r1;
        r1.busyPercent = 20.0;
        r1.memBytes = 1024;
        r1.engines = QStringLiteral("render 20%");
        GpuClientBusy r2;
        r2.busyPercent = -1.0;
        r2.memBytes = 512;
        rates.insert(1, r1);
        rates.insert(2, r2);
        const QList<GpuProcess> merged = gpuMergeClients({a, b}, rates, 8);
        check(merged.size() == 1 && merged.at(0).pid == 42
                  && qAbs(merged.at(0).busyPercent - 20.0) < 0.01
                  && merged.at(0).memBytes == 1536,
              "gpu: clients fold per pid (busy keeps known values only)");
        check(qAbs(gpuClientTotalBusy(merged) - 20.0) < 0.01
                  && gpuClientTotalBusy({}) < 0.0,
              "gpu: client total clamps and reports no-data honestly");

        const QList<GpuDevice> nv = parseNvidiaSmiCsv(
            "0, NVIDIA GeForce RTX 4090, 45, 2048, 24564, 61, 120.50, 2100\n"
            "1, NVIDIA A100, [N/A], [N/A], 40960, 35, [N/A], 1410\n");
        check(nv.size() == 2 && nv.at(0).vendor == "nvidia"
                  && nv.at(0).utilPercent == 45.0
                  && nv.at(0).memUsed == 2048ULL * 1024 * 1024
                  && nv.at(0).tempC == 61.0
                  && nv.at(0).powerW == 120.5 && nv.at(0).clockMhz == 2100
                  && nv.at(1).utilPercent < 0.0,
              "gpu: nvidia-smi csv decode keeps [N/A] honest");

        const QList<GpuProcess> apps = parseNvidiaSmiAppsCsv(
            "GPU-abc, 1234, python3, 512\nGPU-abc, 4321, blender, 2048\n");
        check(apps.size() == 2 && apps.at(0).pid == 1234
                  && apps.at(0).memBytes == 512ULL * 1024 * 1024,
              "gpu: nvidia-smi compute apps decode");

        GpuDevice ioreg;
        const QByteArray ioregText =
            "\"PerformanceStatistics\" = {\"Device Utilization %\"=37,"
            "\"Renderer Utilization %\"=20,\"In use system memory\"=734003200,"
            "\"Core Clock(MHz)\"=1200,\"model\"=\"Apple M3\"}\n";
        check(parseIoregAccelerator(ioregText, ioreg)
                  && ioreg.utilPercent == 37.0
                  && ioreg.memUsed == 734003200ULL
                  && ioreg.clockMhz == 1200
                  && ioreg.name == QStringLiteral("Apple M3"),
              "gpu: IOAccelerator PerformanceStatistics decode");

        GpuDevice coreUtil;
        check(parseIoregAccelerator(
                  "\"GPU Core Utilization\"=0.42\n", coreUtil)
                  && qAbs(coreUtil.utilPercent - 42.0) < 0.01,
              "gpu: Apple Silicon fractional GPU Core Utilization");

        check(gpuVendorFromPci("0x8086", {}) == "intel"
                  && gpuVendorFromPci("1002", {}) == "amd"
                  && gpuVendorFromPci("10de", {}) == "nvidia"
                  && gpuVendorFromPci("8680", {}) == "intel"
                  && gpuVendorFromPci("6b10", {}) == "apple"
                  && gpuVendorFromPci("1234", {}) == "unknown",
              "gpu: pci vendor ids map for sysfs and ioreg byte order");
        check(gpuVendorLabel("nvidia") == QStringLiteral("NVIDIA")
                  && gpuVendorLabel("intel") == QStringLiteral("Intel"),
              "gpu: vendor labels");

        check(gpuPidFromPdhInstance(
                  QStringLiteral("pid_1234_luid_0x00000000_0x0000A7A1_phys_0_"
                                 "eng_0_engtype_3D")) == 1234
                  && gpuPidFromPdhInstance(QStringLiteral("luid_0x0_0x1")) == 0,
              "gpu: pdh instance names carry the pid");

        GpuReading reading;
        reading.ok = true;
        reading.vendor = QStringLiteral("intel");
        GpuDevice dev;
        dev.vendor = QStringLiteral("intel");
        dev.name = QStringLiteral("Iris Xe");
        dev.driver = QStringLiteral("i915");
        dev.utilPercent = 12.5;
        dev.memUsed = 512ULL * 1024 * 1024;
        dev.memTotal = 8ULL * 1024 * 1024 * 1024;
        dev.tempC = 55.0;
        dev.powerW = 11.5;
        dev.clockMhz = 900;
        reading.devices.append(dev);
        check(gpuDeviceText(reading).contains(QStringLiteral("12.5%"))
                  && gpuDeviceText(reading).contains(QStringLiteral("900 MHz"))
                  && !gpuDeviceText(reading).contains(QStringLiteral("%1")),
              "gpu: device text renders every field and no raw placeholders");
        check(gpuSummaryText(reading, false, true, 2)
                  .contains(QStringLiteral("2 processes"))
                  && gpuSummaryText(reading, true, true, 0)
                         .contains(QStringLiteral("warming")),
              "gpu: summary states process counts and warming");
        GpuProcess gp;
        gp.pid = 42;
        gp.name = QStringLiteral("game");
        gp.busyPercent = 55.5;
        gp.memBytes = 256ULL * 1024 * 1024;
        gp.engines = QStringLiteral("render 55%");
        check(gpuProcessText({gp}).contains(QStringLiteral("55.5%"))
                  && gpuProcessText({gp}).contains(QStringLiteral("render")),
              "gpu: process text renders busy, memory and engines");

        DisplaySnapshot gpuSnap;
        gpuSnap.gpu = reading;
        const QString prom = prometheusText(gpuSnap);
        check(prom.contains(QStringLiteral("syspeek_gpu_utilization_percent"))
                  && prom.contains(QStringLiteral("syspeek_gpu_memory_total_bytes"))
                  && prom.contains(QStringLiteral("syspeek_gpu_temperature_celsius")),
              "gpu: prometheus exposes device gauges when a gpu is present");
        DisplaySnapshot emptyGpu;
        check(!prometheusText(emptyGpu).contains(QStringLiteral("syspeek_gpu_")),
              "gpu: prometheus omits gpu lines when no reading exists");
    }


    {
        HistoryRecord record;
        record.atMs = 1793000000123LL;
        record.appName = QStringLiteral("firefox");
        record.pid = 4242;
        record.appCpuPercent = 12.5;
        record.appRssKb = 524288;
        record.hostCpuPercent = 33.25;
        record.hostStealPercent = 0.5;
        record.hostMemAvailableKb = 5242880;
        record.hostMemTotalKb = 8064000;
        record.load1 = 1.75;
        record.diskReadKbPerSec = 128.0;
        record.diskWriteKbPerSec = 512.0;
        record.diskUtilPercent = 7.0;
        record.netRxKbPerSec = 64.0;
        record.netTxKbPerSec = 16.0;
        record.tcpInSegsPerSec = 120.0;
        record.tcpOutSegsPerSec = 90.0;
        record.tcpRetransPerSec = 1.0;
        record.tcpRetransPercent = 0.42;
        record.tcpCurrEstab = 17;

        const QByteArray raw = encodeHistoryRecord(record);
        check(raw.size() == kHistoryRecordBytes,
              "history: one record is exactly the fixed width");
        HistoryRecord decoded;
        check(decodeHistoryRecord(raw, decoded) && decoded.atMs == record.atMs
                  && decoded.appName == QStringLiteral("firefox")
                  && decoded.pid == 4242
                  && qAbs(decoded.appCpuPercent - 12.5) < 0.01
                  && qAbs(decoded.hostCpuPercent - 33.25) < 0.01
                  && decoded.hostMemAvailableKb == 5242880
                  && qAbs(decoded.load1 - 1.75) < 0.01
                  && qAbs(decoded.tcpRetransPercent - 0.42) < 0.01,
              "history: record encode/decode round-trips every column");

        HistoryRecord missing;
        HistoryRecord blank;
        check(!decodeHistoryRecord(QByteArray(8, '\0'), missing)
                  && !decodeHistoryRecord(encodeHistoryRecord(blank), missing),
              "history: an unset timestamp is rejected, never read as a sample");

        HistoryRecord longName;
        longName.atMs = 5;
        longName.appName = QStringLiteral("a-very-long-process-name-over-23");
        HistoryRecord truncated;
        decodeHistoryRecord(encodeHistoryRecord(longName), truncated);
        check(truncated.appName.size() == 23,
              "history: long app names truncate to the fixed field");

        HistoryRecord sentinel;
        sentinel.atMs = 9;
        HistoryRecord sentinelBack;
        decodeHistoryRecord(encodeHistoryRecord(sentinel), sentinelBack);
        check(sentinelBack.appCpuPercent == -1.0 && sentinelBack.pid == -1
                  && sentinelBack.tcpCurrEstab == -1 && sentinelBack.appName.isEmpty(),
              "history: missing values keep the -1 sentinel");

        ExportRow row;
        row.atMs = 1793000000456LL;
        row.appName = QStringLiteral("node");
        row.pid = QStringLiteral("777");
        row.appCpuPercent = 4.5;
        row.hostMemAvailableKb = 1024;
        row.tcpCurrEstab = 3;
        const HistoryRecord viaRow = historyRecordFrom(row);
        const ExportRow back = exportRowFromHistory(viaRow);
        check(viaRow.pid == 777 && back.pid == QStringLiteral("777")
                  && back.appName == QStringLiteral("node")
                  && qAbs(back.appCpuPercent - 4.5) < 0.01
                  && back.tcpCurrEstab == 3,
              "history: export rows and ring records share one vocabulary");

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("ring.bin"));
        HistoryLog log;
        check(log.open(path, 4), "history: ring opens and creates its file");
        check(log.fileBytes() == 64 + qint64(4) * kHistoryRecordBytes,
              "history: the byte budget is header + capacity * record");
        int appended = 0;
        for (int i = 0; i < 6; ++i) {
            HistoryRecord sample = record;
            sample.atMs = 1000 + i * 1000;
            sample.hostCpuPercent = 10.0 + i;
            sample.appName = QStringLiteral("s%1").arg(i);
            if (log.append(sample)) {
                ++appended;
            }
        }
        check(appended == 6 && log.count() == 4 && log.totalWritten() == 6,
              "history: a full ring wraps in place and keeps its total");
        HistoryRecord oldest;
        HistoryRecord newest;
        check(log.oldest(oldest) && log.newest(newest)
                  && oldest.atMs == 3000 && newest.atMs == 6000,
              "history: after the wrap the oldest sample is the 3rd append");
        check(log.spanMs() == 3000,
              "history: span covers the stored window only");

        HistoryRecord at;
        check(log.readAt(0, at) && at.atMs == 3000
                  && log.readAt(3, at) && at.atMs == 6000
                  && !log.readAt(-1, at) && !log.readAt(4, at),
              "history: random access is index based and bounded");

        QList<HistoryRecord> last;
        check(log.readLast(2, last) && last.size() == 2
                  && last.at(0).atMs == 5000 && last.at(1).atMs == 6000,
              "history: readLast returns the newest samples in order");
        QList<HistoryRecord> range;
        check(log.readRange(1, 2, range) && range.size() == 2
                  && range.at(0).atMs == 4000,
              "history: readRange starts at the requested index");

        log.close();
        HistoryLog resumed;
        HistoryRecord resumedOldest;
        check(resumed.open(path, 4) && resumed.count() == 4
                  && resumed.totalWritten() == 6
                  && resumed.oldest(resumedOldest)
                  && resumedOldest.atMs == 3000,
              "history: reopening a ring resumes the stored window in order");

        HistoryLog resized;
        check(resized.open(path, 8) && resized.count() == 0,
              "history: a capacity change re-creates the file instead of "
              "misreading it");
        resized.close();

        const QString junkPath = dir.filePath(QStringLiteral("junk.bin"));
        {
            QFile junk(junkPath);
            check(junk.open(QIODevice::WriteOnly)
                      && junk.write(QByteArray(256, 'x')) == 256,
                  "history: the corrupt-file fixture is written");
        }
        HistoryLog junkLog;
        check(junkLog.open(junkPath, 4) && junkLog.count() == 0,
              "history: a corrupt file is reset, never decoded blind");
        junkLog.close();

        HistoryLog unusable;
        check(!unusable.open(QString(), 4) && !unusable.open(path, 0)
                  && !unusable.isOpen(),
              "history: an empty path or zero capacity stays closed");

        check(historyDefaultPath().endsWith(QStringLiteral("history.ring")),
              "history: default ring lives in the cache directory");
        check(historySummaryText(0, 0, 0, 0, 1000)
                  == QStringLiteral("history: disabled")
                  && historySummaryText(0, 3600, 0, 0, 1000)
                         .contains(QStringLiteral("no samples stored yet"))
                  && historySummaryText(1800, 3600, 1800000, 200000, 1000)
                         .contains(QStringLiteral("1800/3600 samples (50%)"))
                  && historySummaryText(1800, 3600, 1800000, 200000, 1000)
                         .contains(QStringLiteral("span 30 min")),
              "history: summary states capacity, fill, span and disk use");

        HistoryRecord pos = record;
        pos.atMs = QDateTime(QDate(2026, 10, 26), QTime(12, 42, 43))
                       .toMSecsSinceEpoch();
        check(historyPositionText(1799, 1800, pos, true)
                  .contains(QStringLiteral("1800/1800"))
                  && historyPositionText(1799, 1800, pos, true)
                         .contains(QStringLiteral("LIVE"))
                  && historyPositionText(10, 1800, pos, false)
                         .contains(QStringLiteral("rewound")),
              "history: position text separates live from rewound");

        const QString detail = historyRecordText(pos);
        check(detail.contains(QStringLiteral("firefox"))
                  && detail.contains(QStringLiteral("0.42%"))
                  && detail.contains(QStringLiteral("estab 17"))
                  && !detail.contains(QStringLiteral("%1")),
              "history: record text renders the columns without placeholders");
        HistoryRecord emptyRecord;
        emptyRecord.atMs = 1;
        check(historyRecordText(emptyRecord).contains(QStringLiteral("--")),
              "history: an empty sample renders dashes, never zeros");
    }
    {
        // M75: alert silences — Alertmanager's three-state window over one
        // monitor, matcher patterns, Go-style durations and the page text.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        SilenceRule past;
        past.matcher = QStringLiteral("host_cpu");
        past.startsAtMs = now - 600000;
        past.endsAtMs = now - 1000;
        check(silenceStateAt(past, now) == QStringLiteral("expired")
                  && !silenceLive(past, now),
              "silence: a closed window reads expired");

        SilenceRule future = past;
        future.startsAtMs = now + 60000;
        future.endsAtMs = now + 3600000;
        check(silenceStateAt(future, now) == QStringLiteral("pending"),
              "silence: before startsAt reads pending");

        SilenceRule live;
        live.matcher = QStringLiteral("host_cpu");
        live.startsAtMs = now - 1000;
        live.endsAtMs = now + 1800000;
        live.comment = QStringLiteral("disk rebuild");
        live.author = QStringLiteral("ops");
        check(silenceStateAt(live, now) == QStringLiteral("active")
                  && silenceLive(live, now),
              "silence: inside the window reads active");

        check(silenceMatches(live, QStringLiteral("host_cpu"))
                  && silenceMatches(live, QStringLiteral("HOST_CPU"))
                  && !silenceMatches(live, QStringLiteral("app_cpu")),
              "silence: an exact matcher matches one monitor, case-insensitive");

        SilenceRule wildcard;
        wildcard.matcher = QStringLiteral("host_*");
        wildcard.startsAtMs = now - 1000;
        wildcard.endsAtMs = now + 600000;
        check(silenceMatches(wildcard, QStringLiteral("host_steal"))
                  && silenceMatches(wildcard, QStringLiteral("host_cpu"))
                  && !silenceMatches(wildcard, QStringLiteral("app_cpu")),
              "silence: '*' matches any run, so one rule can cover a family");

        SilenceRule question;
        question.matcher = QStringLiteral("app_cp?");
        question.startsAtMs = now - 1000;
        question.endsAtMs = now + 60000;
        check(silenceMatches(question, QStringLiteral("app_cpu"))
                  && !silenceMatches(question, QStringLiteral("app_cpu2")),
              "silence: '?' matches exactly one character");

        SilenceRule longer = live;
        longer.endsAtMs = now + 7200000;
        QList<SilenceRule> rules{live, longer, past};
        const SilenceRule *chosen =
            activeSilenceFor(rules, QStringLiteral("host_cpu"), now);
        check(chosen != nullptr && chosen->endsAtMs == longer.endsAtMs,
              "silence: the newest expiry wins for overlapping windows");
        check(activeSilenceFor(rules, QStringLiteral("app_cpu"), now) == nullptr,
              "silence: an unlisted monitor stays loud");
        check(activeSilenceFor(rules, QStringLiteral("host_cpu"),
                               now + 7200001) == nullptr,
              "silence: past the last expiry nothing is muted");

        const QStringList silenced = silencedMonitorIds(
            rules, {QStringLiteral("host_cpu"), QStringLiteral("app_cpu")}, now);
        check(silenced == QStringList{QStringLiteral("host_cpu")},
              "silence: silencedMonitorIds reports only live matches");
        check(silencedIdsFor(rules,
                             {QStringLiteral("app_cpu"),
                              QStringLiteral("host_cpu")},
                             now)
                  == QList<QString>{QStringLiteral("host_cpu")},
              "silence: silencedIdsFor keeps the requested order");

        check(parseSilenceDurationMs(QStringLiteral("45s")) == 45000
                  && parseSilenceDurationMs(QStringLiteral("30m")) == 1800000
                  && parseSilenceDurationMs(QStringLiteral("2h")) == 7200000
                  && parseSilenceDurationMs(QStringLiteral("1h30m"))
                         == 5400000
                  && parseSilenceDurationMs(QStringLiteral("90")) == 90000,
              "silence: Go durations and bare seconds parse");
        check(parseSilenceDurationMs(QStringLiteral("10x")) == -1
                  && parseSilenceDurationMs(QStringLiteral("1h30")) == -1
                  && parseSilenceDurationMs(QString()) == -1
                  && parseSilenceDurationMs(QStringLiteral("-5")) == -1,
              "silence: malformed and negative durations are rejected");

        check(formatSilenceRemaining(20000) == QStringLiteral("20s left")
                  && formatSilenceRemaining(300000)
                         == QStringLiteral("5m left")
                  && formatSilenceRemaining(7500000)
                         == QStringLiteral("2h 5m left")
                  && formatSilenceRemaining(3 * 86400000)
                         == QStringLiteral("3d left")
                  && formatSilenceRemaining(0) == QStringLiteral("expired"),
              "silence: remaining time renders in the amtool scale");

        check(silenceSummaryText({}, now).contains(QStringLiteral("none"))
                  && silenceSummaryText(rules, now)
                         .contains(QStringLiteral("2 active"))
                  && silenceSummaryText(rules, now)
                         .contains(QStringLiteral("1 expired")),
              "silence: summary states empty vs per-state counts");
        const QString rowText = silenceRowText(live, now);
        check(rowText.contains(QStringLiteral("host_cpu"))
                  && rowText.contains(QStringLiteral("active"))
                  && rowText.contains(QStringLiteral("30m left"))
                  && rowText.contains(QStringLiteral("ops"))
                  && rowText.contains(QStringLiteral("disk rebuild")),
              "silence: a row carries matcher, state, window, author, comment");
        check(silencedNoteText({QStringLiteral("host_cpu")}, rules, now)
                  .contains(QStringLiteral("silenced: host_cpu"))
                  && silencedNoteText({}, rules, now).isEmpty(),
              "silence: the muted-monitor note is per live window");
    }

    {
        // M75: monitor-driven chart bands (Grafana threshold vocabulary) and
        // the healthy-only process predicate.
        check(bandStateForSeverity(QStringLiteral("critical"))
                      == QStringLiteral("Error")
                  && bandStateForSeverity(QStringLiteral("warning"))
                         == QStringLiteral("Warning")
                  && bandStateForSeverity(QStringLiteral("ok"))
                         == QStringLiteral("OK"),
              "bands: severity maps onto the Grafana state words");

        QList<AlertRule> rules = defaultAlertRules();
        for (AlertRule &rule : rules) {
            if (rule.id == QStringLiteral("app_cpu")) {
                rule.threshold = 75.0;
            }
        }
        const QList<MonitorBand> bands = monitorBandsFor(
            rules, {QStringLiteral("app_cpu"), QStringLiteral("host_cpu")});
        check(bands.size() == 2, "bands: one band per requested monitor");
        check(bands.at(0).value <= bands.at(1).value,
              "bands: steps stay sorted by value");
        bool sawCpu = false;
        for (const MonitorBand &band : bands) {
            if (band.id == QStringLiteral("app_cpu")) {
                sawCpu = band.value == 75.0 && band.above
                    && band.state == QStringLiteral("Error")
                    && band.unit == QStringLiteral("%");
            }
        }
        check(sawCpu, "bands: the edited threshold and direction carry over");
        check(monitorBandsFor(rules, {QStringLiteral("nope")}).isEmpty(),
              "bands: an unknown monitor id yields no band");

        MonitorBand lower;
        lower.value = 10.0;
        lower.unit = QStringLiteral("% free");
        lower.above = false;
        lower.state = QStringLiteral("Error");
        check(bandVisible(lower, 0.0, 100.0)
                  && !bandVisible(lower, 20.0, 100.0),
              "bands: a threshold outside the axis is not painted");
        check(bandLabelText(lower) == QStringLiteral("Error 10% free"),
              "bands: the label reads like the Grafana threshold step");

        check(processRowBreaches(95.0, 0.2, 90.0, 1.0)
                  && processRowBreaches(1.0, 1.5, 90.0, 1.0)
                  && !processRowBreaches(4.0, 0.3, 90.0, 1.0),
              "healthy-only: cpu or rss above its monitor line hides the row");
        check(!processRowBreaches(100.0, 9.0, 0.0, 0.0),
              "healthy-only: a disabled monitor (0) never hides a row");
    }

    {
        // M76: metrics endpoint policy (bind + bearer auth).
        check(metricsAddressIsLoopback(QStringLiteral("127.0.0.1"))
                  && metricsAddressIsLoopback(QStringLiteral("127.0.0.42"))
                  && metricsAddressIsLoopback(QStringLiteral("::1"))
                  && metricsAddressIsLoopback(QStringLiteral("localhost"))
                  && metricsAddressIsLoopback(QString()),
              "metrics: loopback addresses are recognised");
        check(!metricsAddressIsLoopback(QStringLiteral("0.0.0.0"))
                  && !metricsAddressIsLoopback(QStringLiteral("192.168.1.5"))
                  && !metricsAddressIsLoopback(QStringLiteral("::"))
                  && !metricsAddressIsLoopback(QStringLiteral("example.com"))
                  && !metricsAddressIsLoopback(QStringLiteral("*")),
              "metrics: wildcard, LAN and hostname binds are not loopback");

        MetricsEndpoint plain =
            resolveMetricsEndpoint(MetricsConfig{});
        check(plain.bindAddress == QStringLiteral("127.0.0.1")
                  && plain.port == 9107 && !plain.authRequired
                  && !plain.bindDowngraded,
              "metrics: the default endpoint is loopback with no auth");

        MetricsConfig explicitLoopback;
        explicitLoopback.bindAddress = QStringLiteral("localhost");
        explicitLoopback.port = 9200;
        const MetricsEndpoint localhost =
            resolveMetricsEndpoint(explicitLoopback);
        check(localhost.bindAddress == QStringLiteral("127.0.0.1")
                  && localhost.port == 9200,
              "metrics: localhost resolves to the loopback address");

        MetricsConfig exposed;
        exposed.bindAddress = QStringLiteral("0.0.0.0");
        const MetricsEndpoint refused = resolveMetricsEndpoint(exposed);
        check(refused.bindAddress == QStringLiteral("127.0.0.1")
                  && refused.bindDowngraded
                  && refused.note.contains(QStringLiteral("without a token")),
              "metrics: a wildcard bind without a token falls back to loopback");

        exposed.token = QStringLiteral("s3cret-token");
        const MetricsEndpoint guarded = resolveMetricsEndpoint(exposed);
        check(guarded.bindAddress == QStringLiteral("0.0.0.0")
                  && guarded.authRequired && !guarded.bindDowngraded
                  && guarded.note.contains(QStringLiteral("bearer token")),
              "metrics: a token unlocks the requested wildcard bind");

        MetricsConfig tokenOnly;
        tokenOnly.token = QStringLiteral("s3cret-token");
        const MetricsEndpoint localToken = resolveMetricsEndpoint(tokenOnly);
        check(localToken.bindAddress == QStringLiteral("127.0.0.1")
                  && localToken.authRequired,
              "metrics: a token works on loopback too");

        check(metricsEndpointText(plain)
                  == QStringLiteral("http://127.0.0.1:9107/metrics "
                                    "(no auth, loopback only)"),
              "metrics: the default endpoint text states no auth");
        check(metricsEndpointText(guarded)
                  .contains(QStringLiteral("http://0.0.0.0:9107/metrics"))
                  && metricsEndpointText(guarded)
                         .contains(QStringLiteral("bearer token required")),
              "metrics: an authenticated bind says so in the endpoint text");
        check(metricsEndpointText(refused)
                  .contains(QStringLiteral("refusing to expose 0.0.0.0")),
              "metrics: a downgraded bind explains itself in the endpoint text");

        check(metricsTokenMatches(QByteArrayLiteral("abc123"),
                                  QByteArrayLiteral("abc123"))
                  && !metricsTokenMatches(QByteArrayLiteral("abc124"),
                                          QByteArrayLiteral("abc123"))
                  && !metricsTokenMatches(QByteArrayLiteral("abc12"),
                                          QByteArrayLiteral("abc123"))
                  && !metricsTokenMatches(QByteArray(), QByteArrayLiteral("abc123"))
                  && !metricsTokenMatches(QByteArrayLiteral("abc123"), QByteArray()),
              "metrics: the token compare is exact and rejects empty sides");
        check(metricsAuthorizationValue(QStringLiteral(" s3cret "))
                  == QByteArrayLiteral("Bearer s3cret"),
              "metrics: the Authorization value carries the Bearer scheme");

        const QByteArray token = QByteArrayLiteral("s3cret-token");
        check(metricsRequestAuthorized(
                  QByteArrayLiteral("GET /metrics HTTP/1.1\r\n"
                                    "Host: 127.0.0.1\r\n"
                                    "Authorization: Bearer s3cret-token\r\n"
                                    "\r\n"),
                  token),
              "metrics: a Bearer header with the token authorizes the scrape");
        check(metricsRequestAuthorized(
                  QByteArrayLiteral("GET /metrics HTTP/1.1\r\n"
                                    "authorization:bearer s3cret-token\r\n"
                                    "\r\n"),
                  token),
              "metrics: the header name and scheme are case-insensitive");
        check(!metricsRequestAuthorized(
                  QByteArrayLiteral("GET /metrics HTTP/1.1\r\n"
                                    "Authorization: Bearer S3CRET-TOKEN\r\n"
                                    "\r\n"),
                  token),
              "metrics: the token itself stays case-sensitive");
        check(!metricsRequestAuthorized(
                  QByteArrayLiteral("GET /metrics HTTP/1.1\r\n"
                                    "Authorization: Basic s3cret-token\r\n"
                                    "\r\n"),
                  token)
                  && !metricsRequestAuthorized(
                      QByteArrayLiteral("GET /metrics HTTP/1.1\r\n\r\n"), token)
                  && !metricsRequestAuthorized(
                      QByteArrayLiteral("Authorization: s3cret-token\r\n\r\n"),
                      token),
              "metrics: a missing, wrong-scheme or bare header is refused");
        check(metricsRequestAuthorized(QByteArrayLiteral("GET /metrics"),
                                       QByteArray()),
              "metrics: with no token configured every scrape is allowed");
    }

    {
        // M76: the live endpoint answers 401 without the token, 200 with it,
        // 200 for HEAD and 404 for anything else.
        auto roundTrip = [](quint16 port, const QByteArray &request) {
            QTcpSocket socket;
            QByteArray response;
            QElapsedTimer timer;
            timer.start();
            socket.connectToHost(QHostAddress::LocalHost, port);
            while (socket.state() != QAbstractSocket::ConnectedState
                   && timer.elapsed() < 3000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            }
            if (socket.state() != QAbstractSocket::ConnectedState) {
                return response;
            }
            socket.write(request);
            socket.flush();
            while (timer.elapsed() < 3000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                response += socket.readAll();
                if (socket.state() == QAbstractSocket::UnconnectedState) {
                    break;
                }
            }
            response += socket.readAll();
            return response;
        };

        MetricsConfig cfg;
        cfg.port = 0;
        MetricsServer server(cfg);
        server.start();
        const quint16 anonymousPort = server.port();
        check(anonymousPort > 0, "metrics server: an ephemeral loopback port binds");

        DisplaySnapshot live;
        live.hostAvailable = true;
        live.hostCpuPercent = 12.5;
        server.setSnapshot(live);
        const QByteArray okResponse = roundTrip(
            anonymousPort,
            QByteArrayLiteral("GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n"));
        check(okResponse.startsWith(QByteArrayLiteral("HTTP/1.1 200 OK"))
                  && okResponse.contains(QByteArrayLiteral(
                         "syspeek_host_cpu_percent 12.500")),
              "metrics server: no token configured serves the exposition");
        server.stop();

        cfg.token = QStringLiteral("s3cret-token");
        MetricsServer guardedServer(cfg);
        guardedServer.start();
        const quint16 guardedPort = guardedServer.port();
        check(guardedPort > 0, "metrics server: the authenticated server binds");
        guardedServer.setSnapshot(live);

        const QByteArray denied = roundTrip(
            guardedPort,
            QByteArrayLiteral("GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n"));
        check(denied.startsWith(QByteArrayLiteral("HTTP/1.1 401 Unauthorized"))
                  && denied.contains(QByteArrayLiteral("WWW-Authenticate: Bearer")),
              "metrics server: a scrape without the token gets 401 + challenge");
        check(!denied.contains(QByteArrayLiteral("syspeek_host_cpu_percent")),
              "metrics server: a refused scrape leaks no samples");

        const QByteArray wrongToken = roundTrip(
            guardedPort,
            QByteArrayLiteral("GET /metrics HTTP/1.1\r\n"
                              "Authorization: Bearer s3cret-tokeN\r\n\r\n"));
        check(wrongToken.startsWith(
                  QByteArrayLiteral("HTTP/1.1 401 Unauthorized")),
              "metrics server: a wrong token is refused");

        const QByteArray authorized = roundTrip(
            guardedPort,
            QByteArrayLiteral("GET /metrics HTTP/1.1\r\n"
                              "Authorization: Bearer s3cret-token\r\n\r\n"));
        check(authorized.startsWith(QByteArrayLiteral("HTTP/1.1 200 OK"))
                  && authorized.contains(QByteArrayLiteral(
                         "syspeek_host_cpu_percent 12.500")),
              "metrics server: the token unlocks the exposition");

        const QByteArray headResponse = roundTrip(
            guardedPort,
            QByteArrayLiteral("HEAD /metrics HTTP/1.1\r\n"
                              "Authorization: Bearer s3cret-token\r\n\r\n"));
        check(headResponse.startsWith(QByteArrayLiteral("HTTP/1.1 200 OK"))
                  && headResponse.contains(QByteArrayLiteral("Content-Length:"))
                  && !headResponse.contains(
                         QByteArrayLiteral("syspeek_host_cpu_percent")),
              "metrics server: HEAD reports the length without the body");

        const QByteArray notFound = roundTrip(
            guardedPort,
            QByteArrayLiteral("GET /nope HTTP/1.1\r\n"
                              "Authorization: Bearer s3cret-token\r\n\r\n"));
        check(notFound.startsWith(QByteArrayLiteral("HTTP/1.1 404 Not Found")),
              "metrics server: an unknown path stays a 404");
        guardedServer.stop();
    }

    // ---- M78 process verbs (pure model) ----
    {
        check(processSignalNumber(ProcessSignal::Term) == 15
                  && processSignalNumber(ProcessSignal::Kill) == 9
                  && processSignalNumber(ProcessSignal::Int) == 2
                  && processSignalNumber(ProcessSignal::Hup) == 1,
              "actions: TERM/KILL/INT/HUP map to the POSIX numbers");
#if defined(Q_OS_MACOS)
        check(processSignalNumber(ProcessSignal::Stop) == 17
                  && processSignalNumber(ProcessSignal::Cont) == 19,
              "actions: darwin SIGSTOP/SIGCONT are 17/19");
#else
        check(processSignalNumber(ProcessSignal::Stop) == 19
                  && processSignalNumber(ProcessSignal::Cont) == 18,
              "actions: linux SIGSTOP/SIGCONT are 19/18");
#endif
        check(processSignalName(ProcessSignal::Hup) == QStringLiteral("SIGHUP")
                  && processSignalLabel(ProcessSignal::Stop)
                         == QStringLiteral("pause")
                  && allProcessSignals().size() == 6,
              "actions: signal names, verb labels and the six-entry list");

        check(ioPriorityValue(IoPriorityClass::Idle, 0) == (3 << 13)
                  && ioPriorityValue(IoPriorityClass::BestEffort, 4)
                         == ((2 << 13) | 4)
                  && ioPriorityValue(IoPriorityClass::Realtime, 9)
                         == ((1 << 13) | 7),
              "actions: ioprio packs class<<13 | level and clamps the level");
        check(ioPriorityText(IoPriorityClass::Idle, 5)
                      == QStringLiteral("idle")
                  && ioPriorityText(IoPriorityClass::BestEffort, 4)
                         == QStringLiteral("best-effort/4")
                  && ioPriorityText(IoPriorityClass::None, 0)
                         == QStringLiteral("none"),
              "actions: ioprio text drops the level where the kernel does");
        check(ioPriorityClassName(IoPriorityClass::Realtime)
                  == QStringLiteral("realtime"),
              "actions: ioprio class names match ionice's table");

        check(clampNice(-99) == -20 && clampNice(99) == 19
                  && clampOomScoreAdj(-5000) == -1000
                  && clampOomScoreAdj(5000) == 1000,
              "actions: nice and oom_score_adj bounds");

        ProcessAction rel;
        rel.kind = ProcessActionKind::Renice;
        rel.pid = 4242;
        rel.name = QStringLiteral("worker");
        rel.niceValue = 5;
        rel.relative = true;
        check(processActionLabel(rel)
                  == QStringLiteral("nice worker (4242) +5"),
              "actions: a relative renice is labelled as a delta");
        check(processActionNeedsConfirm(rel) == false,
              "actions: a renice needs no confirmation");

        ProcessAction term;
        term.pid = 4242;
        term.name = QStringLiteral("worker");
        term.signal = ProcessSignal::Term;
        check(processActionNeedsConfirm(term),
              "actions: TERM asks for confirmation");
        check(processActionConfirmText(term)
                  == QStringLiteral("Send SIGTERM to worker (4242)?"),
              "actions: TERM confirmation names the target");
        ProcessAction killAction = term;
        killAction.signal = ProcessSignal::Kill;
        check(processActionConfirmText(killAction).contains(
                  QStringLiteral("Unsaved work")),
              "actions: KILL confirmation warns about lost work");
        ProcessAction hup = term;
        hup.signal = ProcessSignal::Hup;
        check(processActionNeedsConfirm(hup) == false,
              "actions: HUP is immediate");

        ProcessAction oomRel;
        oomRel.kind = ProcessActionKind::OomScore;
        oomRel.pid = 7;
        oomRel.name = QStringLiteral("svc");
        oomRel.oomScoreAdj = -500;
        oomRel.relative = true;
        check(processActionLabel(oomRel)
                  == QStringLiteral("oom_score_adj svc (7) -500"),
              "actions: a relative oom delta is labelled with its sign");
        oomRel.relative = false;
        oomRel.oomScoreAdj = 250;
        check(processActionLabel(oomRel)
                  == QStringLiteral("oom_score_adj svc (7) -> 250"),
              "actions: an absolute oom set is labelled with the value");

        check(processActionResultText(term, true, QString())
                  == QStringLiteral("SIGTERM worker (4242) ok"),
              "actions: an ok result names the verb and the target");
        check(processActionResultText(term, false,
                                      QStringLiteral("Operation not permitted"))
                  .contains(QStringLiteral("Operation not permitted")),
              "actions: a refused verb carries the OS reason");
        check(processActionResultText(term, false, QString())
                  == QStringLiteral("SIGTERM worker (4242) failed: failed"),
              "actions: an empty error still reads as a failure");
    }

    // ---- M78 unit restart verb (supervisor refusal path) ----
    {
        ServicesSource services;
        QString verb;
        QString unit;
        bool ok = true;
        QString reason;
        QObject::connect(&services, &ServicesSource::unitActionFinished,
                         &services,
                         [&verb, &unit, &ok, &reason](const QString &v,
                                                      const QString &u,
                                                      bool success,
                                                      const QString &error) {
                             verb = v;
                             unit = u;
                             ok = success;
                             reason = error;
                         });

        services.restartUnit(QString());
        check(!ok && verb == QStringLiteral("restart")
                  && reason == QStringLiteral("no unit selected"),
              "actions: restarting nothing states it plainly");

        // No live supervisor call here on purpose: `systemctl restart` on a
        // bogus unit blocks on a polkit prompt, which a test must never
        // raise on someone's desktop.
        ok = true;
        services.restartUnit(QStringLiteral("   "));
        check(!ok, "actions: a whitespace-only unit is refused too");
    }

    {
        check(sensorKindFromKey(QStringLiteral("temp1"))
                      == SensorKind::Temperature
                  && sensorKindFromKey(QStringLiteral("fan2"))
                         == SensorKind::Fan
                  && sensorKindFromKey(QStringLiteral("in0"))
                         == SensorKind::Voltage
                  && sensorKindFromKey(QStringLiteral("power1"))
                         == SensorKind::Power
                  && sensorKindFromKey(QStringLiteral("curr1"))
                         == SensorKind::Current
                  && sensorKindFromKey(QStringLiteral("energy1"))
                         == SensorKind::Energy
                  && sensorKindFromKey(QStringLiteral("Humidity1"))
                         == SensorKind::Other,
              "sensors: sysfs key families map to reading kinds");
        check(qFuzzyCompare(sensorScale(SensorKind::Temperature), 0.001)
                  && qFuzzyCompare(sensorScale(SensorKind::Power), 0.000001)
                  && qFuzzyCompare(sensorScale(SensorKind::Fan), 1.0),
              "sensors: millidegree/microwatt/fan sysfs scales");

        SensorReading hot;
        hot.kind = SensorKind::Temperature;
        hot.value = 53.0;
        check(sensorValueText(hot) == QStringLiteral("53.0 \u00b0C"),
              "sensors: temperature text keeps one decimal and the unit");
        SensorReading fanValue;
        fanValue.kind = SensorKind::Fan;
        fanValue.value = 2986.0;
        check(sensorValueText(fanValue) == QStringLiteral("2986 rpm"),
              "sensors: fan text is a whole rpm number");
        SensorReading wattsReading;
        wattsReading.kind = SensorKind::Power;
        wattsReading.value = 12.5;
        check(sensorValueText(wattsReading) == QStringLiteral("12.50 W"),
              "sensors: power text keeps two decimals");

        hot.hasCrit = true;
        hot.crit = 50.0;
        check(sensorSeverity(hot) == 2,
              "sensors: a reading at or past crit is severity 2");
        hot.crit = 100.0;
        hot.hasWarn = true;
        hot.warn = 45.0;
        check(sensorSeverity(hot) == 1,
              "sensors: a reading past high is severity 1");
        hot.warn = 80.0;
        check(sensorSeverity(hot) == 0,
              "sensors: a reading under every limit is severity 0");

        QTemporaryDir tree;
        check(tree.isValid(), "sensors: temp sysfs tree created");
        const QString hwmon = tree.path() + QStringLiteral("/hwmon");
        const QString power = tree.path() + QStringLiteral("/power_supply");

        auto writeFile = [](const QString &path, const QString &content) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return false;
            }
            const bool ok = file.write(content.toUtf8()) >= 0;
            file.close();
            return ok;
        };
        bool fixtureOk = true;
        const auto mkdir = [&fixtureOk](const QString &path) {
            if (!QDir().mkpath(path)) {
                fixtureOk = false;
            }
        };

        mkdir(hwmon + QStringLiteral("/hwmon0"));
        mkdir(hwmon + QStringLiteral("/hwmon1"));
        mkdir(hwmon + QStringLiteral("/hwmon2"));
        mkdir(hwmon + QStringLiteral("/hwmon3"));
        fixtureOk = fixtureOk
            && writeFile(hwmon + QStringLiteral("/hwmon0/name"),
                         QStringLiteral("coretemp\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon0/temp1_input"),
                         QStringLiteral("53000\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon0/temp1_label"),
                         QStringLiteral("Package id 0\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon0/temp1_crit"),
                         QStringLiteral("100000\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon0/temp1_max"),
                         QStringLiteral("95000\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon0/temp2_input"),
                         QStringLiteral("45000\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon0/temp2_label"),
                         QStringLiteral("Core 0\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon0/power1_input"),
                         QStringLiteral("12500000\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon0/power1_label"),
                         QStringLiteral("package\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon1/name"),
                         QStringLiteral("nvme\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon1/temp1_input"),
                         QStringLiteral("43850\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon1/temp1_label"),
                         QStringLiteral("Composite\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon1/temp1_max"),
                         QStringLiteral("65261850\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon2/name"),
                         QStringLiteral("BAT0\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon2/in0_input"),
                         QStringLiteral("12680000\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon2/power1_input"),
                         QStringLiteral("0\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon3/name"),
                         QStringLiteral("hp\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon3/fan1_input"),
                         QStringLiteral("2986\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon3/fan1_label"),
                         QStringLiteral("cpu_fan\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon3/fan2_input"),
                         QStringLiteral("0\n"))
            && writeFile(hwmon + QStringLiteral("/hwmon3/fan2_label"),
                         QStringLiteral("fan2\n"));
        check(fixtureOk, "sensors: hwmon fixture written");

        mkdir(power + QStringLiteral("/BAT0"));
        mkdir(power + QStringLiteral("/BAT1"));
        mkdir(power + QStringLiteral("/ADP1"));
        fixtureOk = fixtureOk
            && writeFile(power + QStringLiteral("/BAT0/type"),
                         QStringLiteral("Battery\n"))
            && writeFile(power + QStringLiteral("/BAT0/status"),
                         QStringLiteral("Discharging\n"))
            && writeFile(power + QStringLiteral("/BAT0/capacity"),
                         QStringLiteral("15\n"))
            && writeFile(power + QStringLiteral("/BAT0/cycle_count"),
                         QStringLiteral("93\n"))
            && writeFile(power + QStringLiteral("/BAT0/energy_now"),
                         QStringLiteral("5700000\n"))
            && writeFile(power + QStringLiteral("/BAT0/energy_full"),
                         QStringLiteral("38092000\n"))
            && writeFile(power + QStringLiteral("/BAT0/energy_full_design"),
                         QStringLiteral("41040000\n"))
            && writeFile(power + QStringLiteral("/BAT0/power_now"),
                         QStringLiteral("8200000\n"))
            && writeFile(power + QStringLiteral("/BAT0/voltage_now"),
                         QStringLiteral("12400000\n"))
            && writeFile(power + QStringLiteral("/BAT0/model_name"),
                         QStringLiteral("Primary\n"))
            && writeFile(power + QStringLiteral("/BAT0/manufacturer"),
                         QStringLiteral("HP\n"))
            && writeFile(power + QStringLiteral("/BAT0/technology"),
                         QStringLiteral("Li-ion\n"))
            && writeFile(power + QStringLiteral("/BAT0/time_to_empty_now"),
                         QStringLiteral("41\n"))
            && writeFile(power + QStringLiteral("/BAT1/type"),
                         QStringLiteral("Battery\n"))
            && writeFile(power + QStringLiteral("/BAT1/status"),
                         QStringLiteral("Charging\n"))
            && writeFile(power + QStringLiteral("/BAT1/charge_now"),
                         QStringLiteral("2000000\n"))
            && writeFile(power + QStringLiteral("/BAT1/charge_full"),
                         QStringLiteral("4000000\n"))
            && writeFile(power + QStringLiteral("/BAT1/charge_full_design"),
                         QStringLiteral("5000000\n"))
            && writeFile(power + QStringLiteral("/BAT1/voltage_now"),
                         QStringLiteral("12000000\n"))
            && writeFile(power + QStringLiteral("/BAT1/power_now"),
                         QStringLiteral("6000000\n"))
            && writeFile(power + QStringLiteral("/ADP1/type"),
                         QStringLiteral("Mains\n"))
            && writeFile(power + QStringLiteral("/ADP1/online"),
                         QStringLiteral("0\n"));
        check(fixtureOk, "sensors: power_supply fixture written");

        const SensorsSnapshot collected =
            collectSensorsLinux(hwmon, power);
        check(collected.available && collected.chips.size() == 4,
              "sensors: every hwmon chip with readings is collected");
        check(batteryHealthPercent(collected.batteries.value(0)) == 93,
              "sensors: energy_full over design gives battery health");
        check(collected.batteries.size() == 2
                  && qAbs(collected.batteries.value(1).energyNowWh - 24.0) < 0.01
                  && qAbs(collected.batteries.value(1).energyFullWh - 48.0) < 0.01
                  && qAbs(collected.batteries.value(1).energyDesignWh - 60.0)
                         < 0.01,
              "sensors: charge-based batteries convert to Wh through voltage");
        check(qAbs(collected.batteries.value(0).energyNowWh - 5.7) < 0.01
                  && qAbs(collected.batteries.value(0).powerW - 8.2) < 0.01
                  && qAbs(collected.batteries.value(0).voltageV - 12.4) < 0.01
                  && collected.batteries.value(0).cycleCount == 93
                  && collected.batteries.value(0).timeToEmptyMin == 41
                  && collected.batteries.value(0).capacityPercent == 15,
              "sensors: energy_now/power_now/voltage_now decode to real units");
        check(collected.batteries.value(0).state == BatteryState::Discharging
                  && collected.batteries.value(1).state
                         == BatteryState::Charging,
              "sensors: sysfs status words map to battery states");
        check(collected.batteries.value(1).timeToFullMin == 240,
              "sensors: charge time is computed when the kernel omits it");
        check(collected.adapters.size() == 1 && !collected.adapters.value(0).online,
              "sensors: the mains adapter is collected with its online state");

        check(qAbs(collected.hottestCelsius - 53.0) < 0.001
                  && collected.hottestLabel.contains(
                      QStringLiteral("Package id 0")),
              "sensors: hottest reading is the highest temperature");
        check(collected.fanCount == 2
                  && qAbs(collected.maxFanRpm - 2986.0) < 0.001,
              "sensors: fan count includes present zero-rpm fans");
        check(qAbs(collected.totalPowerW - 12.5) < 0.001,
              "sensors: power readings sum into the snapshot total");

        const SensorReading &pkgReading = collected.chips.value(0).readings.value(0);
        check(qAbs(pkgReading.crit - 100.0) < 0.001
                  && pkgReading.hasCrit
                  && qAbs(pkgReading.warn - 95.0) < 0.001
                  && pkgReading.hasWarn,
              "sensors: crit and high thresholds scale with the reading");
        check(collected.chips.value(3).readings.value(1).label
                      == QStringLiteral("fan2")
                  && collected.chips.value(3).readings.value(1).value == 0.0,
              "sensors: an unlabeled sensor falls back to its key");

        const SensorReading &nvmeReading =
            collected.chips.value(1).readings.value(0);
        check(!nvmeReading.hasWarn && qAbs(nvmeReading.value - 43.85) < 0.001,
              "sensors: an implausible firmware limit (65261.8 C) is dropped");
        check(sensorLimitPlausible(SensorKind::Temperature, 95.0)
                  && !sensorLimitPlausible(SensorKind::Temperature, 65261.85)
                  && !sensorLimitPlausible(SensorKind::Temperature, -273.15)
                  && sensorLimitPlausible(SensorKind::Fan, 3500.0)
                  && !sensorLimitPlausible(SensorKind::Fan, 0.0),
              "sensors: limit plausibility is per physical quantity");

        check(sensorsSummaryText(collected)
                      .contains(QStringLiteral("4 chips"))
                  && sensorsSummaryText(collected)
                         .contains(QStringLiteral("hottest 53.0"))
                  && sensorsSummaryText(collected)
                         .contains(QStringLiteral("power 12.50 W")),
              "sensors: summary states chips, hottest and power");
        check(sensorsThermalText(collected)
                      .contains(QStringLiteral("Composite")),
              "sensors: thermal text lists the per-chip hottest sensor");
        check(batterySummaryText(collected.batteries.value(0))
                      .contains(QStringLiteral("BAT0 15%"))
                  && batterySummaryText(collected.batteries.value(0))
                         .contains(QStringLiteral("HP Primary")),
              "sensors: battery summary states charge and identity");
        check(batteryDetailText(collected.batteries.value(0))
                      .contains(QStringLiteral("health 93%"))
                  && batteryDetailText(collected.batteries.value(0))
                         .contains(QStringLiteral("41m left")),
              "sensors: battery detail states health and runtime");
        check(adaptersText(collected) == QStringLiteral("ac: ADP1 offline"),
              "sensors: adapter text renders the online state");

        const SensorsSnapshot none =
            collectSensorsLinux(tree.path() + QStringLiteral("/absent"),
                                tree.path() + QStringLiteral("/absent"));
        check(!none.available
                  && none.note.contains(QStringLiteral("no hwmon or power_supply")),
              "sensors: an empty sysfs tree says so instead of staying blank");

        DisplaySnapshot sensorSnap;
        sensorSnap.sensors = collected;
        const QString prom = prometheusText(sensorSnap);
        check(prom.contains(QStringLiteral(
                  "syspeek_sensor_celsius{chip=\"coretemp\""))
                  && prom.contains(QStringLiteral("syspeek_sensor_fan_rpm"))
                  && prom.contains(QStringLiteral("syspeek_sensor_power_watts"))
                  && prom.contains(QStringLiteral("syspeek_sensor_limit_celsius")),
              "sensors: prometheus exposes per-sensor gauges with labels");
        check(prom.contains(QStringLiteral("syspeek_battery_charge_percent"))
                  && prom.contains(QStringLiteral(
                      "syspeek_battery_energy_watt_hours"))
                  && prom.contains(QStringLiteral("syspeek_ac_online{"))
                  && !prometheusText(DisplaySnapshot{})
                          .contains(QStringLiteral("syspeek_sensor_")),
              "sensors: prometheus exposes battery gauges only when present");

        AlertEngine engine;
        AlertInput input;
        input.tempValid = true;
        input.tempCelsius = 90.0;
        input.batteryValid = true;
        input.batteryPercent = 15.0;
        QList<Alert> fired;
        for (int i = 0; i < 3; ++i) {
            const QList<Alert> batch = engine.update(input, 1000 + i * 1000);
            fired.append(batch);
        }
        bool sawTemp = false;
        bool sawBattery = false;
        for (const Alert &alert : fired) {
            if (alert.id == QStringLiteral("host_temp")) {
                sawTemp = true;
            }
            if (alert.id == QStringLiteral("battery_low")) {
                sawBattery = true;
            }
        }
        check(sawTemp && sawBattery,
              "sensors: hot sensors and a low battery raise monitors");
        input.tempValid = false;
        input.batteryValid = false;
        engine.update(input, 10000);
        check(engine.active().isEmpty(),
              "sensors: invalid sensor samples never keep a monitor firing");
    }

    {
        check(overlaySessionName(OverlaySession::X11) == QStringLiteral("x11")
                  && overlaySessionName(OverlaySession::Wayland)
                         == QStringLiteral("wayland")
                  && overlaySessionName(OverlaySession::MacOS)
                         == QStringLiteral("macos")
                  && overlaySessionName(OverlaySession::Windows)
                         == QStringLiteral("windows")
                  && overlaySessionName(OverlaySession::Unknown)
                         == QStringLiteral("unknown"),
              "overlay: every window server has a stable name");

        check(detectOverlaySession(QStringLiteral("wayland"), QString(),
                                  QStringLiteral(":0"))
                      == OverlaySession::Wayland
                  && detectOverlaySession(QStringLiteral("xcb"), QString(),
                                          QStringLiteral(":0"))
                         == OverlaySession::X11
                  && detectOverlaySession(QStringLiteral("cocoa"), QString(),
                                          QString())
                         == OverlaySession::MacOS
                  && detectOverlaySession(QStringLiteral("windows"), QString(),
                                          QString())
                         == OverlaySession::Windows
                  && detectOverlaySession(QString(), QString(),
                                          QString())
                         == OverlaySession::Unknown,
              "overlay: the platform plugin decides the session, xwayland or "
              "not");

        check(detectOverlaySession(QString(), QStringLiteral("wayland-0"),
                                   QString())
                      == OverlaySession::Wayland
                  && detectOverlaySession(QString(), QString(),
                                          QStringLiteral(":0"))
                         == OverlaySession::X11
                  && detectOverlaySession(QString(), QStringLiteral("wayland-0"),
                                          QStringLiteral(":0"))
                         == OverlaySession::Wayland,
              "overlay: the environment decides when no plugin name is given");

        const QStringList states = parseNetWmStateAtoms(
            QByteArray("_NET_WM_STATE_ABOVE\0_NET_WM_STATE_STICKY\0"
                       "_NET_WM_STATE_ABOVE\0\0", 63));
        check(states.size() == 2
                  && overlayAboveConfirmed(states)
                  && overlayStateHas(states, QStringLiteral("_NET_WM_STATE_STICKY"))
                  && !overlayHidden(states)
                  && !overlayDockWindow(states),
              "overlay: _NET_WM_STATE atoms are read, deduped and classified");

        const QStringList hiddenStates = parseNetWmStateAtoms(
            QByteArray("_NET_WM_STATE_HIDDEN\0", 22));
        check(overlayHidden(hiddenStates) && !overlayAboveConfirmed(hiddenStates),
              "overlay: a window the wm hid is reported as hidden");
        check(overlayDockWindow(parseNetWmStateAtoms(
                  QByteArray("_NET_WM_WINDOW_TYPE_DOCK\0", 28)))
                  && !overlayDockWindow(parseNetWmStateAtoms(
                         QByteArray("_NET_WM_WINDOW_TYPE_NORMAL\0", 30))),
              "overlay: dock window types are distinguished from normal ones");

        check(overlaySticky(states) && !overlaySticky(hiddenStates),
              "overlay: sticky is read from the same _NET_WM_STATE list");

        const QRect screenA(0, 0, 1920, 1080);
        const QRect screenB(1920, 0, 1280, 1024);
        const QList<QRect> screens{screenA, screenB};
        const QSize minimum(430, 120);

        check(restoreOverlayGeometry(QRect(100, 100, 430, 200), screens, minimum)
                      == QRect(100, 100, 430, 200),
              "overlay: a saved rect on a live screen is restored verbatim");
        check(restoreOverlayGeometry(QRect(2000, 50, 430, 200), screens, minimum)
                      == QRect(2000, 50, 430, 200),
              "overlay: a saved rect on the second screen keeps that screen");
        const QRect rescued =
            restoreOverlayGeometry(QRect(9000, 9000, 430, 200), screens, minimum);
        check(screenA.intersects(rescued) || screenB.intersects(rescued),
              "overlay: a saved rect on a vanished screen comes back on-screen");
        const QRect clampedRight =
            restoreOverlayGeometry(QRect(1800, 100, 430, 200), screens, minimum);
        check(screenA.contains(clampedRight) || screenB.contains(clampedRight),
              "overlay: a rect hanging off an edge is pulled onto the screen it "
              "mostly covers");
        check(clampOverlayGeometry(QRect(-500, -500, 430, 200), screenA, minimum)
                      == QRect(0, 0, 430, 200),
              "overlay: negative coordinates clamp to the work-area origin");
        check(clampOverlayGeometry(QRect(0, 0, 10, 10), screenA, minimum)
                      == QRect(0, 0, 430, 120),
              "overlay: the minimum size is enforced");
        check(clampOverlayGeometry(QRect(0, 0, 5000, 5000), screenA, minimum)
                      == screenA,
              "overlay: an oversized rect is capped at the screen");
        check(!restoreOverlayGeometry(QRect(0, 0, 430, 200), QList<QRect>(),
                                      minimum)
                       .isValid()
                  && !restoreOverlayGeometry(QRect(), screens, minimum).isValid(),
              "overlay: no screens or no saved rect yields no placement");

        check(overlayClickThroughNote(OverlaySession::X11, true)
                      .contains(QStringLiteral("passes every click"))
                  && overlayClickThroughNote(OverlaySession::Wayland, false)
                         .contains(QStringLiteral("Wayland"))
                  && overlayClickThroughNote(OverlaySession::MacOS, false)
                         .contains(QStringLiteral("not exposed"))
                  && overlayClickThroughNote(OverlaySession::Windows, false)
                         .contains(QStringLiteral("not exposed"))
                  && overlayClickThroughNote(OverlaySession::X11, false)
                         .contains(QStringLiteral("X Shape"))
                  && overlayClickThroughNote(OverlaySession::Unknown, false)
                         .contains(QStringLiteral("no window server")),
              "overlay: every gate states what the OS actually offers");

        OverlayState state;
        state.clickThrough = true;
        state.pinnedAbove = true;
        state.geometry = QRect(10, 20, 430, 120);
        const QString summary = overlaySummaryText(OverlaySession::X11, state);
        check(summary.contains(QStringLiteral("session x11"))
                  && summary.contains(QStringLiteral("pinned above"))
                  && summary.contains(QStringLiteral("click-through on"))
                  && summary.contains(QStringLiteral("430x120 at 10,20")),
              "overlay: the summary names session, pin, mode and geometry");
        state.pinnedAbove = false;
        state.wmName = QStringLiteral("sway");
        state.dockWindow = true;
        state.hidden = true;
        state.clickThrough = false;
        const QString docked = overlaySummaryText(OverlaySession::X11, state);
        check(docked.contains(QStringLiteral("wm sway"))
                  && docked.contains(QStringLiteral("pin not confirmed"))
                  && docked.contains(QStringLiteral("dock window"))
                  && docked.contains(QStringLiteral("hidden by the wm"))
                  && docked.contains(QStringLiteral("click-through off")),
              "overlay: the summary reports an unconfirmed pin and wm actions");

        state.dockWindow = false;
        state.sticky = true;
        check(overlaySummaryText(OverlaySession::X11, state)
                  .contains(QStringLiteral("follows every desktop")),
              "overlay: a sticky hud is reported as following every desktop");
        state.sticky = false;
        check(overlaySummaryText(OverlaySession::X11, state)
                      .contains(QStringLiteral("desktop-local"))
                  && !overlaySummaryText(OverlaySession::Wayland, state)
                          .contains(QStringLiteral("desktop-local"))
                  && !overlaySummaryText(OverlaySession::MacOS, state)
                          .contains(QStringLiteral("follows every desktop")),
              "overlay: only x11 has desktops to follow or not");

        state.wmName.clear();
        state.dockWindow = false;
        state.hidden = false;
        check(overlaySummaryText(OverlaySession::X11, state)
                      .startsWith(QStringLiteral("overlay: session x11"))
                  && !overlaySummaryText(OverlaySession::Wayland, state)
                          .contains(QStringLiteral("pin not confirmed")),
              "overlay: no wm name falls back to the session, non-x11 is "
              "pinned by construction");
        check(overlaySummaryText(OverlaySession::Unknown, OverlayState{})
                  == QStringLiteral("overlay: no window server detected"),
              "overlay: an unknown session states the gate");

        check(overlayGeometryText(QRect(10, 20, 430, 120))
                      == QStringLiteral("430x120 at 10,20")
                  && overlayGeometryText(QRect()) == QStringLiteral("--")
                  && overlayGeometryUsable(QRect(0, 0, 1, 1))
                  && !overlayGeometryUsable(QRect(5, 5, 0, 40)),
              "overlay: geometry text and usability agree");
    }

        // ---- M82 cloud identity + accrued cost ------------------------
        {
            const QByteArray awsDoc = R"({
              "accountId": "123456789012",
              "architecture": "arm64",
              "availabilityZone": "eu-west-1b",
              "billingProducts": ["bp-6ba54002"],
              "imageId": "ami-0abc",
              "instanceId": "i-0123456789abcdef0",
              "instanceType": "c7g.2xlarge",
              "marketplaceProductCodes": ["abc"],
              "privateIp": "10.0.3.17",
              "region": "eu-west-1"
            })";
            const CloudIdentity aws = parseAwsIdentityDocument(awsDoc);
            check(aws.provider == CloudProvider::Aws
                      && aws.instanceId == QStringLiteral("i-0123456789abcdef0")
                      && aws.instanceType == QStringLiteral("c7g.2xlarge")
                      && aws.region == QStringLiteral("eu-west-1")
                      && aws.zone == QStringLiteral("eu-west-1b")
                      && aws.accountId == QStringLiteral("123456789012")
                      && aws.privateIp == QStringLiteral("10.0.3.17")
                      && aws.architecture == QStringLiteral("arm64")
                      && aws.marketplace && !aws.spot,
                  "cloud: the aws identity document maps every field");
            check(parseAwsIdentityDocument(R"({"instanceId":"i-spot",
                      "billingProducts":["bp-spot-9d2f"]})")
                      .spot,
                  "cloud: a spot billing product marks the instance");

            const CloudIdentity awsBroken =
                parseAwsIdentityDocument("not json");
            check(awsBroken.provider == CloudProvider::None
                      && awsBroken.note.contains(QStringLiteral("not valid JSON")),
                  "cloud: a non-JSON aws document states the reason");
            const CloudIdentity awsNoId =
                parseAwsIdentityDocument(R"({"instanceType":"t3.micro"})");
            check(awsNoId.provider == CloudProvider::None
                      && awsNoId.note.contains(QStringLiteral("no instanceId")),
                  "cloud: an aws document without instanceId is refused");

            const CloudIdentity azure = parseAzureCompute(R"({"compute":{
                "vmId":"3f9c1e2a","name":"web-1","vmSize":"Standard_D2s_v3",
                "location":"westeurope","zone":"2",
                "subscriptionId":"sub-1","osType":"Linux",
                "imageReference":"Canonical:ubuntu"}})");
            check(azure.provider == CloudProvider::Azure
                      && azure.instanceId == QStringLiteral("3f9c1e2a")
                      && azure.instanceName == QStringLiteral("web-1")
                      && azure.instanceType == QStringLiteral("Standard_D2s_v3")
                      && azure.zone == QStringLiteral("westeurope-2")
                      && azure.operatingSystem == QStringLiteral("linux"),
                  "cloud: the azure compute document maps zone as region-N");
            const CloudIdentity azureFlat = parseAzureCompute(R"({"vmId":"v2",
                "location":"eastus","osType":"Windows"})");
            check(azureFlat.provider == CloudProvider::Azure
                      && azureFlat.zone.isEmpty()
                      && azureFlat.operatingSystem == QStringLiteral("windows"),
                  "cloud: a flat azure document without a zone keeps region");
            check(!parseAzureCompute(R"({"name":"no-id"})").note.isEmpty(),
                  "cloud: an azure document without vmId states the reason");

            check(cloudMachineTypeTail(QStringLiteral(
                      "projects/123/zones/europe-west1-b/machineTypes/"
                      "n2-standard-4"))
                      == QStringLiteral("n2-standard-4")
                      && cloudMachineTypeTail(QString()).isEmpty(),
                  "cloud: the gcp machine-type path yields the type");
            check(cloudRegionFromZone(QStringLiteral(
                      "projects/1/zones/us-central1-a"))
                          == QStringLiteral("us-central1")
                      && cloudRegionFromZone(QStringLiteral("us-east1-b"))
                             == QStringLiteral("us-east1")
                      && cloudRegionFromZone(QString()).isEmpty(),
                  "cloud: the gcp region is the zone minus its letter");

            GcpMetadataFields gcpFields;
            gcpFields.machineType =
                "projects/123/zones/europe-west1-b/machineTypes/e2-medium";
            gcpFields.zone = "projects/123/zones/europe-west1-b\n";
            gcpFields.id = "87654321\n";
            gcpFields.name = "api-1\n";
            gcpFields.project = "shopist-prod\n";
            const CloudIdentity gcp = parseGcpInstance(gcpFields);
            check(gcp.provider == CloudProvider::Gcp
                      && gcp.instanceId == QStringLiteral("87654321")
                      && gcp.instanceName == QStringLiteral("api-1")
                      && gcp.instanceType == QStringLiteral("e2-medium")
                      && gcp.zone == QStringLiteral("europe-west1-b")
                      && gcp.region == QStringLiteral("europe-west1")
                      && gcp.accountId == QStringLiteral("shopist-prod")
                      && gcp.note.isEmpty(),
                  "cloud: the gcp metadata fields parse, note stays empty");
            GcpMetadataFields badGcp;
            badGcp.machineType = "e2-medium";
            badGcp.id = "5";
            const CloudIdentity badGcpIdentity = parseGcpInstance(badGcp);
            check(badGcpIdentity.provider == CloudProvider::Gcp
                      && badGcpIdentity.note.contains(
                             QStringLiteral("/machineTypes/")),
                  "cloud: a non-canonical gcp machine-type is flagged");

            const CloudIdentity oci = parseOciInstance(R"({
                "id":"ocid1.instance.oc1..abc","displayName":"db-1",
                "shape":"VM.Standard.E4.Flex","region":"eu-frankfurt-1",
                "availabilityDomain":"AD-1","compartmentId":"ocid1.comp"})");
            check(oci.provider == CloudProvider::Oci
                      && oci.instanceId == QStringLiteral("ocid1.instance.oc1..abc")
                      && oci.instanceType == QStringLiteral("VM.Standard.E4.Flex")
                      && oci.region == QStringLiteral("eu-frankfurt-1")
                      && oci.zone == QStringLiteral("AD-1"),
                  "cloud: the oci instance document maps shape + AD");
            check(parseOciInstance(R"({"shape":"x"})")
                          .note.contains(QStringLiteral("empty id")),
                  "cloud: an oci document without id states the reason");

            const CloudRateTable rates = parseCloudRateFile(
                QStringLiteral("# imported 2026-01-02\n"
                               "aws/c7g.2xlarge = 0.2920\n"
                               "aws/c7g.2xlarge = 0.3100\n"
                               "azure/Standard_D2s_v3 = 0.0960  # promo\n"
                               "n2-standard-4 = 0.1901\n"
                               "aws/broken = nope\n"
                               "aws/negative = -1\n"
                               "\n"),
                QStringLiteral("/tmp/rates.txt"));
            check(rates.loaded && rates.importDate == QStringLiteral("2026-01-02")
                      && rates.sourcePath == QStringLiteral("/tmp/rates.txt"),
                  "cloud: the rate file records its import stamp");
            check(rates.rates.size() == 3,
                  "cloud: bad values are dropped and duplicates replaced");
            check(qAbs(cloudRateFor(rates, CloudProvider::Aws,
                                    QStringLiteral("C7G.2XLarge"))
                       - 0.3100) < 1e-9,
                  "cloud: the rate lookup is case-insensitive, last wins");
            check(qAbs(cloudRateFor(rates, CloudProvider::Gcp,
                                    QStringLiteral("n2-standard-4"))
                       - 0.1901) < 1e-9,
                  "cloud: a bare instance type is usable without a prefix");
            check(cloudRateFor(rates, CloudProvider::Aws,
                               QStringLiteral("m7g.large"))
                          < 0.0
                      && cloudRateFor(rates, CloudProvider::None,
                                      QStringLiteral("anything"))
                             < 0.0,
                  "cloud: an unknown instance or provider has no rate");
            check(cloudRateKey(CloudProvider::Aws, QStringLiteral("t3.micro"))
                          == QStringLiteral("aws/t3.micro")
                      && cloudRateKey(CloudProvider::None,
                                      QStringLiteral("t3.micro"))
                             .isEmpty(),
                  "cloud: the rate key is provider/type");

            check(cloudRatesTemplateText().contains(
                      QStringLiteral("aws/c7g.2xlarge = 0.2920")),
                  "cloud: the template shows a concrete example");
            const qint64 now = QDateTime(QDate(2026, 3, 2), QTime(12, 0))
                                   .toMSecsSinceEpoch();
            check(cloudRateStaleDays(QStringLiteral("2026-01-01"), now) == 60
                      && cloudRateStaleDays(QStringLiteral("2026-03-09"), now)
                             == 0
                      && cloudRateStaleDays(QString(), now) == -1
                      && cloudRateStaleDays(QStringLiteral("nope"), now) == -1,
                  "cloud: staleness is whole days, future clamps, junk is -1");
            const QString written = cloudRateFileText(rates, now);
            check(written.startsWith(QStringLiteral("# imported 2026-01-02\n"))
                      && written.contains(QStringLiteral("aws/c7g.2xlarge = 0.3100"))
                      && written.contains(QStringLiteral("59 days old")),
                  "cloud: the written rate file keeps the stamp and warns");

            const CloudCostMath zero = computeCloudCost(0.2920, 0);
            check(qAbs(zero.accruedUsd) < 1e-9
                      && qAbs(zero.projectedMonthlyUsd - 0.2920 * 730.0) < 1e-9,
                  "cloud: zero uptime accrues nothing, the projection is 730 h");
            const CloudCostMath day41 = computeCloudCost(0.2920, 41 * 86400);
            check(qAbs(day41.accruedUsd - 0.2920 * 41 * 24.0) < 1e-9,
                  "cloud: accrual scales linearly with uptime");
            check(computeCloudCost(-1.0, 100).accruedUsd < 0.0
                      && computeCloudCost(1.0, -1).accruedUsd < 0.0,
                  "cloud: an unknown rate or uptime yields no number");

            check(qAbs(cloudProcessCostPerHour(8.0, 800.0, 8) - 8.0) < 1e-9
                      && qAbs(cloudProcessCostPerHour(8.0, 100.0, 8) - 1.0)
                             < 1e-9
                      && qAbs(cloudProcessCostPerHour(8.0, 50.0, 12)
                              - 8.0 * 0.5 / 12.0) < 1e-9,
                  "cloud: the app share is rate x cpu% / vCPU");
            check(cloudProcessCostPerHour(8.0, 100.0, 0) < 0.0
                      && cloudProcessCostPerHour(8.0, 0.0, 8) < 0.0
                      && cloudProcessCostPerHour(-1.0, 100.0, 8) < 0.0,
                  "cloud: no vCPU, no cpu or no rate gives no share");

            const QList<CloudProcessSample> rows = {
                {QStringLiteral("small"), 1, 40.0},
                {QStringLiteral("big"), 2, 400.0},
                {QStringLiteral("mid"), 3, 100.0},
                {QStringLiteral("idle"), 4, 0.0},
                {QString(), 5, 10.0},
            };
            const QList<CloudConsumer> top =
                cloudTopConsumers(rows, 1.0, 4, 3);
            check(top.size() == 3 && top.at(0).pid == 2 && top.at(1).pid == 3
                      && top.at(2).pid == 1,
                  "cloud: consumers sort by dollars and cap at the limit");
            check(qAbs(cloudConsumerTotal(top) - (1.0 + 0.25 + 0.1)) < 1e-9,
                  "cloud: the capped list sums only the top rows");
            const QList<CloudConsumer> named =
                cloudTopConsumers(rows, 1.0, 4, 8);
            check(named.size() == 4
                      && named.constLast().name == QStringLiteral("5"),
                  "cloud: a pid-only row is named by its pid");
            check(qAbs(cloudConsumerTotal(named)
                       - (1.0 + 0.25 + 0.1 + 0.025)) < 1e-9,
                  "cloud: the consumer total sums the shares");
            check(cloudTopConsumers(rows, 1.0, 0, 3).isEmpty()
                      && cloudTopConsumers(rows, -1.0, 4, 3).isEmpty(),
                  "cloud: no vCPU or no rate yields no consumer list");

            CloudSnapshot gated;
            gated.available = false;
            check(cloudSummaryText(gated) == cloudUnreachableText()
                      && cloudDetailText(gated) == cloudUnreachableText()
                      && cloudCostText(gated) == cloudUnreachableText(),
                  "cloud: every gated card prints the same gate text");
            CloudSnapshot disabled;
            disabled.note = QStringLiteral("cloud: disabled ([cloud] "
                                           "enabled=false) - the instance "
                                           "metadata service is never contacted");
            check(cloudSummaryText(disabled).contains(
                      QStringLiteral("enabled=false")),
                  "cloud: a disabled probe states the switch");
            CloudSnapshot probing;
            probing.note = cloudProbingText();
            check(cloudSummaryText(probing) == cloudProbingText()
                      && cloudProbingText().contains(
                          QStringLiteral("next tick")),
                  "cloud: the first sampled tick says the probe is deferred");

            CloudSnapshot live;
            live.available = true;
            live.identity = aws;
            live.vcpus = 8;
            live.uptimeSec = 41 * 86400;
            live.rateKey = QStringLiteral("aws/c7g.2xlarge");
            live.ratePerHour = 0.2920;
            live.rateSource = QStringLiteral("/tmp/rates.txt");
            live.rateImportDate = QStringLiteral("2026-01-01");
            live.rateStaleDays = 60;
            const CloudCostMath liveMath =
                computeCloudCost(live.ratePerHour, live.uptimeSec);
            live.accruedUsd = liveMath.accruedUsd;
            live.projectedMonthlyUsd = liveMath.projectedMonthlyUsd;
            live.appName = QStringLiteral("cursor");
            live.appPid = 508637;
            live.appCpuPercent = 214.5;
            live.appCostPerHour =
                cloudProcessCostPerHour(live.ratePerHour, live.appCpuPercent,
                                        live.vcpus);
            check(cloudSummaryText(live).contains(QStringLiteral("AWS"))
                      && cloudSummaryText(live).contains(
                             QStringLiteral("c7g.2xlarge"))
                      && cloudSummaryText(live).contains(
                             QStringLiteral("8 vCPU"))
                      && !cloudSummaryText(live).contains(
                             QStringLiteral("spot")),
                  "cloud: the summary names provider, type and vCPU");
            CloudSnapshot spotSnap = live;
            spotSnap.identity.spot = true;
            check(cloudSummaryText(spotSnap).endsWith(QStringLiteral("- spot")),
                  "cloud: a spot instance is called out in the summary");
            check(cloudDetailText(live).contains(QStringLiteral("private ip "))
                      && cloudDetailText(live).contains(
                             QStringLiteral("eu-west-1b"))
                      && cloudDetailText(live).contains(
                             QStringLiteral("arm64")),
                  "cloud: the detail card carries the identity fields");
            const QString costLine = cloudCostText(live);
            check(costLine.contains(QStringLiteral("$0.2920/hr"))
                      && costLine.contains(QStringLiteral("this boot"))
                      && costLine.contains(QStringLiteral("41d 0h"))
                      && costLine.contains(QStringLiteral("60 days old"))
                      && costLine.contains(QStringLiteral("/month")),
                  "cloud: the cost card labels accrual as this boot");
            const QString appLine = cloudAppText(live);
            check(appLine.contains(QStringLiteral("cursor"))
                      && appLine.contains(QStringLiteral("214.5%"))
                      && appLine.contains(QStringLiteral("/hr")),
                  "cloud: the app card carries the share");
            CloudSnapshot noRate = live;
            noRate.ratePerHour = -1.0;
            check(cloudCostText(noRate).contains(QStringLiteral("aws/c7g.2xlarge"))
                      && cloudCostText(noRate).contains(
                             QStringLiteral("usd per hour"))
                      && cloudAppText(noRate).contains(
                             QStringLiteral("needs a price")),
                  "cloud: a missing price states the exact key to add");
            CloudSnapshot noApp = live;
            noApp.appCpuPercent = -1.0;
            check(cloudAppText(noApp).contains(
                      QStringLiteral("no focused-process cpu sample")),
                  "cloud: a warmup tick says the sample is missing");

            check(cloudUptimeText(41 * 86400) == QStringLiteral("41d 0h")
                      && cloudUptimeText(3 * 3600 + 5 * 60)
                             == QStringLiteral("3h 5m")
                      && cloudUptimeText(-1) == QStringLiteral("--"),
                  "cloud: uptime text scales from hours to days");
            check(cloudShareNote().contains(QStringLiteral("cpu%"))
                      && cloudShareNote().contains(QStringLiteral("vCPU")),
                  "cloud: the share note states the formula");

            check(cloudSettingsLine(false, QStringLiteral("/tmp/r"), rates, now)
                          .contains(QStringLiteral("disabled")),
                  "cloud: settings states a disabled probe");
            const QString settingsEmpty =
                cloudSettingsLine(true, QStringLiteral("/tmp/r"),
                                  CloudRateTable{}, now);
            check(settingsEmpty.contains(QStringLiteral("169.254.169.254"))
                      && settingsEmpty.contains(
                             QStringLiteral("--cloud-prices"))
                      && settingsEmpty.contains(QStringLiteral("/tmp/r")),
                  "cloud: settings names both IMDS addresses and the import");
            const QString settingsFull =
                cloudSettingsLine(true, QStringLiteral("/tmp/r"), rates, now);
            check(settingsFull.contains(QStringLiteral("prices 3"))
                      && settingsFull.contains(QStringLiteral("2026-01-02"))
                      && settingsFull.contains(QStringLiteral("59 days old")),
                  "cloud: settings states the price count and staleness");

            const QStringList pricingArgs =
                awsPricingArguments(AwsPriceQuery{QStringLiteral("c7g.2xlarge"),
                                                  QStringLiteral("eu-west-1")});
            check(pricingArgs.contains(QStringLiteral("get-products"))
                      && pricingArgs.contains(QStringLiteral("AmazonEC2"))
                      && pricingArgs.contains(QStringLiteral("aws_v1"))
                      && pricingArgs.contains(
                          QStringLiteral("Type=TERM_MATCH,Field=instanceType,"
                                         "Value=c7g.2xlarge"))
                      && pricingArgs.contains(
                          QStringLiteral("Type=TERM_MATCH,Field=location,"
                                         "Value=EU (Ireland)"))
                      && pricingArgs.contains(
                          QStringLiteral("Type=TERM_MATCH,Field=capacitystatus,"
                                         "Value=Used")),
                  "cloud: the pricing query pins service, type, location");
            check(awsPriceLocation(QStringLiteral("us-west-2"))
                          == QStringLiteral("US West (Oregon)")
                      && awsPriceLocation(QStringLiteral("xx-new-1"))
                             == QStringLiteral("xx-new-1"),
                  "cloud: pricing locations map known regions, else the code");

            const QByteArray priceList = R"({"FormatVersion":"aws_v1","PriceList":[
              "{\"product\":{\"attributes\":{\"instanceType\":\"c7g.2xlarge\",
                 \"capacitystatus\":\"Used\",\"preInstalledSw\":\"NA\",
                 \"tenancy\":\"Shared\",\"operatingSystem\":\"Linux\"}},
                \"terms\":{\"OnDemand\":{\"ABC.JRTCKXETXF\":{
                  \"priceDimensions\":{\"ABC.JRTCKXETXF.6YS6EN2CT7\":{
                    \"unit\":\"Hrs\",\"pricePerUnit\":{\"USD\":\"0.2920000000\"}}}}}}}",
              "{\"product\":{\"attributes\":{\"instanceType\":\"c7g.2xlarge\",
                 \"capacitystatus\":\"Used\",\"preInstalledSw\":\"NA\",
                 \"tenancy\":\"Dedicated\"}},
                \"terms\":{\"OnDemand\":{\"D\":{\"priceDimensions\":{
                  \"D.1\":{\"unit\":\"Hrs\",
                           \"pricePerUnit\":{\"USD\":\"9.0000000000\"}}}}}}}",
              "{\"product\":{\"attributes\":{\"capacitystatus\":\"Reserved\"}},
                \"terms\":{\"OnDemand\":{\"R\":{\"priceDimensions\":{
                  \"R.1\":{\"unit\":\"Hrs\",
                           \"pricePerUnit\":{\"USD\":\"0.0100000000\"}}}}}}}"
            ]})";
            QString priceError;
            const double priced = parseAwsPricingProducts(priceList,
                                                          &priceError);
            check(qAbs(priced - 0.2920) < 1e-9 && priceError.isEmpty(),
                  "cloud: the pricing payload yields the shared on-demand rate");
            QString junkError;
            check(parseAwsPricingProducts("{}", &junkError) < 0.0
                      && junkError.contains(QStringLiteral("no products")),
                  "cloud: an empty pricing payload states the reason");
        }

    {
        // M83: the account inventory reads the provider's own CLI listing.
        const CloudFleetStatus aws = parseAwsDescribeInstances(R"JSON(
          {"Reservations":[{"Instances":[
            {"InstanceId":"i-0aaa","InstanceType":"c7g.2xlarge",
             "State":{"Name":"running"},
             "Placement":{"AvailabilityZone":"eu-west-1a"},
             "LaunchTime":"2023-06-01T10:00:00Z",
             "PrivateIpAddress":"10.0.0.5","PublicIpAddress":"54.1.2.3",
             "Tags":[{"Key":"Name","Value":"web-1"},{"Key":"team","Value":"obs"}]},
            {"InstanceId":"i-0bbb","InstanceType":"t4g.small",
             "State":{"Name":"stopped"},
             "Placement":{"AvailabilityZone":"us-west-2-lax-1a"}}]}]}
        )JSON");
        check(aws.rows.size() == 2 && aws.available && aws.running == 1,
              "fleet: an aws document lists every reservation's instances");
        check(aws.rows.at(0).name == QStringLiteral("web-1")
                  && aws.rows.at(0).instanceId == QStringLiteral("i-0aaa")
                  && aws.rows.at(0).state == QStringLiteral("running")
                  && aws.rows.at(0).region == QStringLiteral("eu-west-1")
                  && aws.rows.at(0).privateIp == QStringLiteral("10.0.0.5")
                  && aws.rows.at(0).launchedAtMs > 0,
              "fleet: an aws row carries the name, the zone-derived region "
              "and the launch time");
        check(aws.rows.at(1).region == QStringLiteral("us-west-2")
                  && aws.rows.at(1).state == QStringLiteral("stopped"),
              "fleet: an aws local zone resolves to its parent region");

        const CloudFleetStatus awsProjected = parseAwsDescribeInstances(R"JSON(
          [{"InstanceId":"i-0ccc","InstanceType":"m7g.large",
            "State":{"Name":"pending"},
            "Placement":{"AvailabilityZone":"ap-south-1b"},
            "SpotInstanceRequestId":"sir-1"}]
        )JSON");
        check(awsProjected.rows.size() == 1
                  && awsProjected.rows.at(0).spot
                  && awsProjected.rows.at(0).region
                         == QStringLiteral("ap-south-1"),
              "fleet: the --query projection parses the same way and a spot "
              "request is flagged");

        const CloudFleetStatus azure = parseAzureVmList(R"JSON(
          [{"name":"vm-flat","location":"westus2","vmId":"aaaa",
            "powerState":"VM running",
            "hardwareProfile":{"vmSize":"Standard_D2s_v3"},
            "timeCreated":"2023-04-01T08:30:00Z",
            "tags":{"team":"obs"}},
           {"name":"vm-nested","location":"eastus",
            "properties":{"vmId":"bbbb","provisioningState":"Succeeded",
                          "hardwareProfile":{"vmSize":"Standard_B1s"}},
            "instanceView":{"statuses":[
              {"code":"ProvisioningState/succeeded"},
              {"code":"PowerState/deallocated"}]}}]
        )JSON");
        check(azure.rows.size() == 2
                  && azure.rows.at(0).name == QStringLiteral("vm-flat")
                  && azure.rows.at(0).state == QStringLiteral("running")
                  && azure.rows.at(0).instanceType
                         == QStringLiteral("Standard_D2s_v3")
                  && azure.rows.at(0).launchedAtMs > 0,
              "fleet: a flattened azure row keeps its size and creation time");
        check(azure.rows.at(1).state == QStringLiteral("stopped")
                  && azure.rows.at(1).instanceType
                         == QStringLiteral("Standard_B1s")
                  && azure.rows.at(1).launchedAtMs < 0,
              "fleet: a nested azure row maps the deallocated power state and "
              "states no age");

        const CloudFleetStatus gcp = parseGcpInstanceList(R"JSON(
          [{"id":"123","name":"gce-1",
            "machineType":"https://www.googleapis.com/compute/v1/projects/p/zones/us-central1-a/machineTypes/e2-medium",
            "zone":"https://www.googleapis.com/compute/v1/projects/p/zones/us-central1-a",
            "status":"RUNNING",
            "creationTimestamp":"2023-05-01T00:00:00.000-07:00",
            "networkInterfaces":[{"networkIP":"10.128.0.2",
              "accessConfigs":[{"natIP":"34.1.2.3"}]}],
            "labels":{"team":"obs"},
            "scheduling":{"provisioningModel":"SPOT"}},
           {"id":"456","name":"gce-old",
            "machineType":"https://www.googleapis.com/compute/v1/projects/p/zones/europe-west1-b/machineTypes/e2-small",
            "zone":"https://www.googleapis.com/compute/v1/projects/p/zones/europe-west1-b",
            "status":"TERMINATED"}]
        )JSON");
        check(gcp.rows.at(0).name == QStringLiteral("gce-1")
                  && gcp.rows.at(0).instanceType == QStringLiteral("e2-medium")
                  && gcp.rows.at(0).zone == QStringLiteral("us-central1-a")
                  && gcp.rows.at(0).region == QStringLiteral("us-central1")
                  && gcp.rows.at(0).spot
                  && gcp.rows.at(0).publicIp == QStringLiteral("34.1.2.3"),
              "fleet: a gcp row keeps type, zone, public ip and the spot model");
        check(gcp.rows.at(1).state == QStringLiteral("stopped"),
              "fleet: gcp TERMINATED means stopped, not deleted");

        const CloudFleetStatus oci = parseOciInstanceList(R"JSON(
          {"data":[
            {"id":"ocid1.instance.oc1..aaa","display-name":"oci-1",
             "shape":"VM.Standard.A1.Flex","lifecycle-state":"RUNNING",
             "region":"eu-frankfurt-1",
             "availability-domain":"Uocm:EU-FRANKFURT-1-AD-1",
             "compartment-id":"ocid1.compartment.oc1..ccc",
             "time-created":"2023-07-01T12:00:00.000Z",
             "freeform-tags":{"team":"obs"}},
            {"id":"ocid1.instance.oc1..bbb","display-name":"oci-old",
             "shape":"VM.Standard.E4.Flex","lifecycle-state":"TERMINATED"}]}
        )JSON");
        check(oci.rows.at(0).name == QStringLiteral("oci-1")
                  && oci.rows.at(0).instanceType
                         == QStringLiteral("VM.Standard.A1.Flex")
                  && oci.rows.at(0).zone
                         == QStringLiteral("Uocm:EU-FRANKFURT-1-AD-1")
                  && oci.rows.at(0).group
                         == QStringLiteral("ocid1.compartment.oc1..ccc"),
              "fleet: an oci row keeps its kebab-case fields");
        check(oci.rows.at(1).state == QStringLiteral("stopped"),
              "fleet: oci TERMINATED means stopped");

        check(parseCloudFleetJson(CloudProvider::Aws, R"JSON({"Reservations":[]})JSON").rows.isEmpty()
                  && parseCloudFleetJson(CloudProvider::Azure, "{}").rows.isEmpty()
                  && parseCloudFleetJson(CloudProvider::None, "[]").rows.isEmpty()
                  && parseAzureVmList("not json").rows.isEmpty()
                  && parseOciInstanceList("[]").rows.isEmpty(),
              "fleet: a malformed listing yields no rows instead of a guess");

        check(fleetState(CloudProvider::Aws, QStringLiteral("terminated"))
                      == QStringLiteral("terminated")
                  && fleetState(CloudProvider::Gcp, QStringLiteral("TERMINATED"))
                         == QStringLiteral("stopped")
                  && fleetState(CloudProvider::Oci, QStringLiteral("TERMINATED"))
                         == QStringLiteral("stopped")
                  && fleetState(CloudProvider::Azure,
                                QStringLiteral("VM deallocated"))
                         == QStringLiteral("stopped")
                  && fleetState(CloudProvider::Aws, QStringLiteral("shutting-down"))
                         == QStringLiteral("terminated")
                  && fleetState(CloudProvider::Gcp, QStringLiteral("SUSPENDED"))
                         == QStringLiteral("suspended")
                  && fleetState(CloudProvider::Gcp, QStringLiteral("STAGING"))
                         == QStringLiteral("pending")
                  && fleetState(CloudProvider::Oci, QStringLiteral("MOVING"))
                         == QStringLiteral("pending")
                  && fleetState(CloudProvider::Aws, QString())
                         == QStringLiteral("unknown"),
              "fleet: one state vocabulary spans four providers");

        const qint64 fleetNow = 1'700'000'000'000LL;
        check(fleetAgeSeconds(fleetNow - 2 * 86400LL * 1000LL, fleetNow)
                      == 2 * 86400LL
                  && fleetAgeSeconds(fleetNow, fleetNow) == 0
                  && fleetAgeSeconds(fleetNow + 5000, fleetNow) == -1
                  && fleetAgeSeconds(-1, fleetNow) == -1,
              "fleet: age is whole seconds from launch and never negative");

        CloudFleetStatus aged = aws;
        aged.rows[0].launchedAtMs = fleetNow - 3 * 86400LL * 1000LL;
        aged.rows[1].launchedAtMs = -1;
        fleetResolveAges(aged, fleetNow);
        check(aged.rows.at(0).ageSec == 3 * 86400LL
                  && aged.rows.at(1).ageSec < 0,
              "fleet: resolving ages fills the launched rows and leaves the "
              "rest unknown");

        const CloudRateTable rates =
            parseCloudRateFile(QStringLiteral("aws/c7g.2xlarge = 0.2920\n"
                                              "aws/t4g.small = 0.0208\n"));
        CloudFleetStatus pricedFleet = aws;
        fleetApplyRates(pricedFleet, rates, 730.0);
        check(pricedFleet.running == 1 && pricedFleet.unpriced == 0
                  && qAbs(pricedFleet.monthlyUsd - 213.16) < 1e-6
                  && qAbs(pricedFleet.rows.at(0).monthlyUsd - 213.16) < 1e-6
                  && pricedFleet.rows.at(1).monthlyUsd < 0.0,
              "fleet: $/MO sums the priced running rows and a stopped row "
              "never claims a monthly bill");

        CloudFleetStatus unpricedFleet = aws;
        unpricedFleet.rows[0].instanceType = QStringLiteral("m9.unknown");
        fleetApplyRates(unpricedFleet, rates, 730.0);
        check(unpricedFleet.unpriced == 1 && unpricedFleet.monthlyUsd < 0.0,
              "fleet: an unpriceable running row is counted, never shown as $0");

        CloudFleetStatus selfFleet = aws;
        CloudIdentity awsSelf;
        awsSelf.provider = CloudProvider::Aws;
        awsSelf.instanceId = QStringLiteral("i-0bbb");
        const int selfIndex = fleetMarkSelf(selfFleet, awsSelf);
        check(selfIndex == 0 && selfFleet.selfIndex == 0
                  && selfFleet.rows.at(0).instanceId == QStringLiteral("i-0bbb")
                  && selfFleet.rows.at(0).isSelf
                  && !selfFleet.rows.at(1).isSelf,
              "fleet: this instance is matched by id and moved to the first row");

        CloudFleetStatus nameFleet = aws;
        CloudIdentity awsByName;
        awsByName.provider = CloudProvider::Aws;
        awsByName.instanceName = QStringLiteral("web-1");
        check(fleetMarkSelf(nameFleet, awsByName) == 0
                  && nameFleet.rows.at(0).name == QStringLiteral("web-1"),
              "fleet: an instance without a provider id is matched by name");

        CloudFleetStatus foreignFleet = aws;
        CloudIdentity gcpSelf;
        gcpSelf.provider = CloudProvider::Gcp;
        gcpSelf.instanceId = QStringLiteral("i-0aaa");
        check(fleetMarkSelf(foreignFleet, gcpSelf) == -1
                  && !foreignFleet.rows.at(0).isSelf,
              "fleet: a listing from another provider never claims a self row");

        CloudIdentity awsIdentity;
        awsIdentity.provider = CloudProvider::Aws;
        awsIdentity.region = QStringLiteral("eu-west-1");
        CloudIdentity azureIdentity;
        azureIdentity.provider = CloudProvider::Azure;
        azureIdentity.accountId = QStringLiteral("sub-1");
        CloudIdentity gcpIdentity;
        gcpIdentity.provider = CloudProvider::Gcp;
        gcpIdentity.accountId = QStringLiteral("proj-1");
        CloudIdentity ociIdentity;
        ociIdentity.provider = CloudProvider::Oci;
        ociIdentity.accountId = QStringLiteral("ocid1.compartment.oc1..ccc");
        ociIdentity.region = QStringLiteral("eu-frankfurt-1");

        const QStringList awsArgs = cloudFleetArguments(awsIdentity);
        check(awsArgs.contains(QStringLiteral("ec2"))
                  && awsArgs.contains(QStringLiteral("describe-instances"))
                  && awsArgs.contains(QStringLiteral("--region"))
                  && awsArgs.contains(QStringLiteral("eu-west-1"))
                  && awsArgs.contains(QStringLiteral("--query"))
                  && awsArgs.contains(
                      QStringLiteral("Reservations[].Instances[]")),
              "fleet: the aws listing asks its own region for every instance");
        CloudIdentity awsNoRegion;
        awsNoRegion.provider = CloudProvider::Aws;
        check(!cloudFleetArguments(awsNoRegion).contains(QStringLiteral("--region")),
              "fleet: an aws identity without a region leaves the CLI default");

        const QStringList azureArgs = cloudFleetArguments(azureIdentity);
        check(azureArgs.contains(QStringLiteral("vm"))
                  && azureArgs.contains(QStringLiteral("list"))
                  && azureArgs.contains(QStringLiteral("-d"))
                  && azureArgs.contains(QStringLiteral("--subscription"))
                  && azureArgs.contains(QStringLiteral("sub-1")),
              "fleet: azure lists the subscription's vms with -d");

        const QStringList gcpArgs = cloudFleetArguments(gcpIdentity);
        check(gcpArgs.contains(QStringLiteral("compute"))
                  && gcpArgs.contains(QStringLiteral("instances"))
                  && gcpArgs.contains(QStringLiteral("--format=json"))
                  && gcpArgs.contains(QStringLiteral("--project"))
                  && gcpArgs.contains(QStringLiteral("proj-1")),
              "fleet: gcloud lists the project's instances as json");

        const QStringList ociArgs = cloudFleetArguments(ociIdentity);
        check(ociArgs.contains(QStringLiteral("--compartment-id"))
                  && ociArgs.contains(
                      QStringLiteral("ocid1.compartment.oc1..ccc"))
                  && ociArgs.contains(QStringLiteral("--region"))
                  && ociArgs.contains(QStringLiteral("--all"))
                  && ociArgs.contains(QStringLiteral("--output")),
              "fleet: oci pages the whole compartment");
        check(cloudFleetArguments(CloudIdentity{}).isEmpty()
                  && cloudFleetCli(CloudProvider::None).isEmpty()
                  && cloudFleetCommandText(CloudIdentity{}).isEmpty(),
              "fleet: with no provider there is no command to run");

        check(cloudFleetScope(awsIdentity) == QStringLiteral("eu-west-1")
                  && cloudFleetScope(azureIdentity) == QStringLiteral("sub-1")
                  && cloudFleetScope(gcpIdentity) == QStringLiteral("proj-1")
                  && cloudFleetScope(ociIdentity)
                         == QStringLiteral("ocid1.compartment.oc1..ccc")
                  && cloudFleetScope(CloudIdentity{}).isEmpty(),
              "fleet: the scope names what the listing covers");

        const QString command = cloudFleetCommandText(awsIdentity);
        check(command.startsWith(QStringLiteral("aws ec2 describe-instances"))
                  && command.contains(QStringLiteral("--region eu-west-1")),
              "fleet: the summary quotes the command that produced the list");

        const QString winLine = cloudWindowsCmdLine(
            QStringLiteral("C:\\Program Files\\az.cmd"),
            {QStringLiteral("vm"), QStringLiteral("list"),
             QStringLiteral("-d")});
        check(winLine
                  == QStringLiteral(
                      "/s /c \"\"C:\\Program Files\\az.cmd\" vm list -d\""),
              "fleet: a .cmd shim runs through cmd.exe with the line quoted");

        CloudFleetStatus story = pricedFleet;
        story.dropped = 3;
        story.scope = QStringLiteral("eu-west-1");
        story.source = command;
        story.fetchedAtMs = QDateTime::currentMSecsSinceEpoch() - 90000;
        story.rows[0].launchedAtMs = fleetNow - 5 * 86400LL * 1000LL;
        fleetResolveAges(story, fleetNow);
        CloudIdentity storySelf;
        storySelf.provider = CloudProvider::Aws;
        storySelf.instanceId = QStringLiteral("i-0aaa");
        fleetMarkSelf(story, storySelf);
        const QString storyText = fleetSummaryText(story);
        check(storyText.contains(QStringLiteral("AWS fleet"))
                  && storyText.contains(QStringLiteral("5 instances"))
                  && storyText.contains(QStringLiteral("1 running"))
                  && storyText.contains(QStringLiteral("$213.16/month"))
                  && storyText.contains(QStringLiteral("eu-west-1")),
              "fleet: the summary counts the dropped rows and names the scope");
        check(storyText.contains(QStringLiteral("self      web-1"))
                  && storyText.contains(QStringLiteral("5d 0h"))
                  && storyText.contains(QStringLiteral("showing   first 2 of 5"))
                  && storyText.contains(QStringLiteral("source    aws ec2"))
                  && storyText.contains(QStringLiteral("listed    ")),
              "fleet: the summary states the self row, the cap and the source");

        CloudFleetStatus emptyFleet;
        emptyFleet.attempted = true;
        emptyFleet.provider = CloudProvider::Aws;
        emptyFleet.cliName = QStringLiteral("aws");
        emptyFleet.scope = QStringLiteral("eu-west-1");
        check(fleetSummaryText(emptyFleet)
                  == QStringLiteral("fleet: aws returned 0 instances - "
                                    "eu-west-1 has none"),
              "fleet: an empty account says so instead of showing a blank table");

        CloudFleetStatus disabledFleet;
        disabledFleet.note = fleetDisabledText();
        check(fleetSummaryText(disabledFleet).contains(
                  QStringLiteral("[cloud] inventory=false"))
                  && fleetDisabledText().contains(QStringLiteral("no cloud CLI")),
              "fleet: a disabled inventory names the setting and the promise");

        CloudFleetStatus noProvider;
        noProvider.attempted = true;
        noProvider.note = fleetNoProviderText();
        check(fleetSummaryText(noProvider).contains(
                  QStringLiteral("no cloud instance identified"))
                  && fleetNoProviderText().contains(
                      QStringLiteral("metadata service")),
              "fleet: off a cloud host the card states the gate");

        check(fleetCliMissingText(QStringLiteral("az"))
                      .contains(QStringLiteral("az not on PATH"))
                  && fleetTimeoutText(QStringLiteral("aws"), 25)
                         .contains(QStringLiteral("within 25 s"))
                  && fleetFailureText(QStringLiteral("aws"), 255,
                                      QStringLiteral("boom"))
                         .contains(QStringLiteral("exited 255 - boom"))
                  && fleetTableNote().contains(
                      QStringLiteral("running rows only"))
                  && fleetTableNote().contains(QStringLiteral("* this instance")),
              "fleet: every refusal states the command, the time and the note");

        check(cloudFleetNeedsCredentials(
                  CloudProvider::Aws,
                  QStringLiteral("Unable to locate credentials. You can "
                                 "configure credentials by running \"aws "
                                 "configure\"."))
                  && cloudFleetNeedsCredentials(
                      CloudProvider::Azure,
                      QStringLiteral("Please run 'az login' to setup account."))
                  && cloudFleetNeedsCredentials(
                      CloudProvider::Gcp,
                      QStringLiteral("You do not currently have an active "
                                     "account selected."))
                  && cloudFleetNeedsCredentials(
                      CloudProvider::Oci,
                      QStringLiteral("Could not find config file at "
                                     "/home/dev/.oci/config."))
                  && !cloudFleetNeedsCredentials(
                      CloudProvider::Aws,
                      QStringLiteral("An error occurred (UnauthorizedOperation)")),
              "fleet: each provider's credential refusal is recognised");

        check(fleetCredentialsText(CloudProvider::Aws)
                      .contains(QStringLiteral("aws configure"))
                  && fleetCredentialsText(CloudProvider::Azure)
                         .contains(QStringLiteral("az login"))
                  && fleetCredentialsText(CloudProvider::Gcp)
                         .contains(QStringLiteral("gcloud auth login"))
                  && fleetCredentialsText(CloudProvider::Oci)
                         .contains(QStringLiteral("oci setup config"))
                  && fleetCredentialsText(CloudProvider::Aws)
                         .contains(QStringLiteral("never reads or stores")),
              "fleet: the credential refusal names the fix and the promise");
    }


    {
        // M84: the sampler's own cost. Sections sort slowest-first, the avg
        // and worst cover the bounded window, and the text helpers never
        // claim a number they do not have.
        QList<TickSection> sections;
        sections.append(TickSection{QStringLiteral("focus"), 3.5});
        sections.append(TickSection{QStringLiteral("processes"), 41.2});
        sections.append(TickSection{QStringLiteral("gpu"), 0.01});
        const TickCost cost = tickbudget::summarize(
            sections, 62.0, QList<double>{40.0, 62.0, 84.0});
        check(cost.available && cost.sections.size() == 3
                  && cost.sections.first().name == QStringLiteral("processes"),
              "tickbudget: sections are sorted slowest first");
        check(cost.ticks == 3 && std::abs(cost.avgMs - 62.0) < 0.001
                  && std::abs(cost.worstMs - 84.0) < 0.001
                  && std::abs(cost.lastMs - 62.0) < 0.001,
              "tickbudget: avg/worst come from the window, last from the tick");
        check(tickbudget::tickCostText(cost).contains(QStringLiteral("62 ms"))
                  && tickbudget::tickCostText(cost).contains(
                      QStringLiteral("worst 84 ms"))
                  && tickbudget::tickCostText(cost).contains(
                      QStringLiteral("over 3 ticks")),
              "tickbudget: tick text states last, avg, worst and the window");
        check(tickbudget::tickSectionText(cost)
                      .startsWith(QStringLiteral("processes"))
                  && !tickbudget::tickSectionText(cost).contains(
                      QStringLiteral("gpu")),
              "tickbudget: section text leads with the slowest and elides "
              "sub-0.05 ms work");
        const TickCost empty = tickbudget::summarize({}, -1.0, {});
        check(!empty.available
                  && tickbudget::tickCostText(empty) == QStringLiteral("measuring")
                  && tickbudget::tickSectionText(empty)
                         == QStringLiteral("no section timings yet"),
              "tickbudget: an unmeasured tick says so instead of showing 0 ms");

        SelfCost self;
        check(!self.available
                  && tickbudget::selfCostText(self)
                         == QStringLiteral("not measurable on this platform"),
              "tickbudget: a missing self sample is stated, not faked");
        self.available = true;
        self.warming = true;
        check(tickbudget::selfCostText(self)
                  == QStringLiteral("cpu warming"),
              "tickbudget: a warming self sample omits the cpu number");
        self.warming = false;
        self.cpuPercent = 6.04;
        self.rssKb = 36864.0;
        check(tickbudget::selfCostText(self)
                  == QStringLiteral("cpu 6.04%  rss 36.0 MB"),
              "tickbudget: self cost renders percent and resident size");
        self.rssKb = -1.0;
        check(tickbudget::selfCostText(self)
                  == QStringLiteral("cpu 6.04%"),
              "tickbudget: an unreadable rss is omitted, not zeroed");
        check(tickbudget::tickBudgetLine(self, cost)
                  .contains(QStringLiteral("cpu 6.04%"))
                  && tickbudget::tickBudgetLine(self, cost)
                         .contains(QStringLiteral("tick 62 ms")),
              "tickbudget: the one-line form joins footprint and tick cost");

        // The scope timer appends exactly once, even when end() is explicit.
        QList<TickSection> scoped;
        {
            tickbudget::Section section(&scoped, "cloud");
            section.end();
        }
        check(scoped.size() == 1
                  && scoped.first().name == QStringLiteral("cloud")
                  && scoped.first().ms >= 0.0,
              "tickbudget: a section records once and carries its own name");

        // M84: the cloud footprint disclosure. Every outbound action, the
        // cadence, the measured sweep, and the two guarantees.
        CloudFootprint footprint;
        footprint.timeoutMs = 450;
        footprint.inventoryTimeoutMs = 25000;
        footprint.ratesPath = QStringLiteral("/tmp/rates.txt");
        check(cloudFootprintText(footprint).contains(
                  QStringLiteral("169.254.169.254"))
                  && cloudFootprintText(footprint).contains(
                      QStringLiteral("169.254.0.23"))
                  && cloudFootprintText(footprint).contains(
                      QStringLiteral("link-local only")),
              "footprint: both link-local services are named");
        check(cloudFootprintText(footprint).contains(
                  QStringLiteral("every 5 min"))
                  && cloudFootprintText(footprint).contains(
                      QStringLiteral("every 15 min")),
              "footprint: both probe cadences are stated");
        check(cloudFootprintText(footprint).contains(
                  QStringLiteral("450 ms for the whole sweep"))
                  && cloudFootprintText(footprint).contains(
                      QStringLiteral("second tick")),
              "footprint: the sweep budget and the first-tick deferral are "
              "stated before any sweep ran");
        footprint.probed = true;
        footprint.lastSweepMs = 62;
        footprint.lastRequests = 4;
        check(cloudFootprintText(footprint).contains(
                  QStringLiteral("last 62 ms / 4 request(s)")),
              "footprint: the last sweep is reported with its request count");
        footprint.inventory = false;
        check(cloudFootprintText(footprint).contains(
                  QStringLiteral("no provider CLI is spawned")),
              "footprint: a disabled inventory names the consequence");
        footprint.inventory = true;
        footprint.cliName = QStringLiteral("aws");
        check(cloudFootprintText(footprint).contains(
                  QStringLiteral("aws every 10 min, 25000 ms budget")),
              "footprint: the inventory CLI, cadence and budget are stated");
        check(cloudFootprintText(footprint).contains(
                  QStringLiteral("never from a tick"))
                  && cloudFootprintText(footprint).contains(
                      QStringLiteral("/tmp/rates.txt")),
              "footprint: price import is on demand and the rate file is "
              "named");
        check(cloudFootprintText(footprint).contains(
                  QStringLiteral("no credential is read or stored"))
                  && cloudFootprintText(footprint).contains(
                      QStringLiteral("ever started, stopped or terminated")),
              "footprint: the no-credentials and read-only guarantees are "
              "spelled out");
        // M85: the billed-spend query is the second billable outbound action,
        // so the footprint names it with its cadence and switch.
        footprint.spend = true;
        footprint.spendTimeoutMs = 30000;
        footprint.spendCommand = QStringLiteral(
            "aws ce get-cost-and-usage --time-period Start=2026-10-01,"
            "End=2026-10-02");
        check(cloudFootprintText(footprint).contains(
                  QStringLiteral("spend      aws ce get-cost-and-usage"))
                  && cloudFootprintText(footprint).contains(
                      QStringLiteral("every 6 h, 30000 ms budget"))
                  && cloudFootprintText(footprint).contains(
                      QStringLiteral("billable to your account")),
              "footprint: the cost query is disclosed with its cadence and cost");
        footprint.spend = false;
        check(cloudFootprintText(footprint).contains(
                  QStringLiteral("spend      off ([cloud] spend=false)")),
              "footprint: a disabled cost query says nothing is issued");
        footprint.enabled = false;
        check(cloudFootprintText(footprint).contains(QStringLiteral("disabled"))
                  && cloudFootprintText(footprint).contains(
                      QStringLiteral("is ever issued"))
                  && !cloudFootprintText(footprint).contains(
                      QStringLiteral("169.254")),
              "footprint: a disabled cloud path says nothing is contacted");

        // QtCompat: the shim must yield a real UTC zone on every Qt floor.
        const QDateTime utc =
            qtcompat::fromMSecsSinceEpochUtc(1700000000000LL);
        check(utc.isValid()
                  && utc.timeZone().id() == qtcompat::utcZone().id()
                  && utc.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                         == QStringLiteral("2023-11-14 22:13"),
              "qtcompat: the UTC shim matches the 6.5+ zone exactly");
    }


    {
        // M85a: the cloud CLI paths end to end against a FAKE cli on PATH.
        // The parsers and argument builders are already fixture-tested; what
        // this proves is the wiring a user actually hits — the argv handed to
        // the provider's own client, the credential/format/timeout refusals,
        // the polled runner, and the opt-in price import writing a stamped
        // rate file.
        QTemporaryDir cliDir;
        check(cliDir.isValid(), "cloud cli: a temp dir for the fake CLIs");
        const QString binDir = cliDir.path();
        const QString argvLog = binDir + QStringLiteral("/argv.log");
        const QString outFile = binDir + QStringLiteral("/stdout.json");
        const QString errFile = binDir + QStringLiteral("/stderr.txt");

        const auto writeFile = [](const QString &path, const QByteArray &data,
                                  bool executable) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return false;
            }
            file.write(data);
            file.close();
            if (executable) {
                file.setPermissions(QFile::ReadOwner | QFile::WriteOwner
                                    | QFile::ExeOwner | QFile::ReadGroup
                                    | QFile::ExeGroup | QFile::ReadOther
                                    | QFile::ExeOther);
            }
            return true;
        };

#if defined(Q_OS_WIN)
        // Windows CLIs are .cmd shims, and the runner starts those through
        // cmd.exe. A batch file cannot reliably stream a file to the child's
        // stdout pipe, so the shim delegates to PowerShell, which reads the
        // fixture and writes it verbatim (and dispatches the second CE call).
        QString psPath =
            QStandardPaths::findExecutable(QStringLiteral("powershell"));
        if (psPath.isEmpty()) {
            psPath = qEnvironmentVariable("SystemRoot")
                     + QStringLiteral(
                         "/System32/WindowsPowerShell/v1.0/powershell.exe");
        }
        const QByteArray script =
            ("@echo off\r\n\"" + psPath.toUtf8()
             + "\" -NoProfile -ExecutionPolicy Bypass -File "
               "\"%~dp0fake.ps1\" %*\r\n");
        const QByteArray ps1 =
            "$ErrorActionPreference = \'Stop\'\r\n"
            "$out = $env:SYSPEEK_FAKE_STDOUT\r\n"
            "if ($env:SYSPEEK_FAKE_ALT_MATCH -and $args.Count -ge 2 -and "
            "$args[1] -eq $env:SYSPEEK_FAKE_ALT_MATCH) { $out = "
            "$env:SYSPEEK_FAKE_ALT_STDOUT }\r\n"
            "Add-Content -Path $env:SYSPEEK_FAKE_ARGV_LOG -Value ($args -join "
            "' ')\r\n"
            "if ($env:SYSPEEK_FAKE_STDERR) { "
            "[Console]::Error.Write([IO.File]::ReadAllText("
            "$env:SYSPEEK_FAKE_STDERR)) }\r\n"
            "if ($out) { [Console]::Out.Write([IO.File]::ReadAllText($out)) "
            "}\r\n"
            "if ($env:SYSPEEK_FAKE_SLEEP) { Start-Sleep -Seconds "
            "([int]$env:SYSPEEK_FAKE_SLEEP) }\r\n"
            "exit [int]$env:SYSPEEK_FAKE_EXIT\r\n";
        const QString suffix = QStringLiteral(".cmd");
#else
        const QByteArray script =
            "#!/bin/sh\n"
            "printf '%s\\n' \"$*\" >> \"$SYSPEEK_FAKE_ARGV_LOG\"\n"
            "if [ -n \"${SYSPEEK_FAKE_SLEEP:-}\" ]; then sleep \"$SYSPEEK_FAKE_SLEEP\"; fi\n"
            "if [ -n \"${SYSPEEK_FAKE_STDERR:-}\" ]; then cat \"$SYSPEEK_FAKE_STDERR\" >&2; fi\n"
            "if [ -n \"${SYSPEEK_FAKE_STDOUT:-}\" ]; then cat \"$SYSPEEK_FAKE_STDOUT\"; fi\n"
            "exit \"${SYSPEEK_FAKE_EXIT:-0}\"\n";
        const QString suffix;
#endif
        bool wroteAll = true;
#if defined(Q_OS_WIN)
        wroteAll = writeFile(binDir + QStringLiteral("/fake.ps1"), ps1, false);
#endif
        for (const char *cli : {"aws", "az", "gcloud", "oci"}) {
            wroteAll = wroteAll
                && writeFile(binDir + QLatin1Char('/') + QLatin1String(cli)
                                 + suffix,
                             script, true);
        }
        check(wroteAll, "cloud cli: the fake provider CLIs are executable");

        const QString oldPath = qEnvironmentVariable("PATH");
        // Windows runners ship the real aws.exe/az.exe, and the primary
        // lookup finds those before a .cmd of the same name, so the fake PATH
        // keeps only the OS directories (cmd, powershell) plus the fakes.
        const auto fakePath = [](const QString &dir, const QString &base) {
#if defined(Q_OS_WIN)
            QStringList keep;
            const QString root = qEnvironmentVariable("SystemRoot");
            for (const QString &entry : base.split(QLatin1Char(';'))) {
                if (entry.contains(QStringLiteral("System32"),
                                   Qt::CaseInsensitive)
                    || (!root.isEmpty()
                        && entry.compare(root, Qt::CaseInsensitive) == 0)) {
                    keep.append(entry);
                }
            }
            return dir + QLatin1Char(';') + keep.join(QLatin1Char(';'));
#else
            return dir + QLatin1Char(':') + base;
#endif
        };
        qputenv("PATH", fakePath(binDir, oldPath).toUtf8());
        qputenv("SYSPEEK_FAKE_ARGV_LOG", argvLog.toUtf8());
        {
            const QString resolved =
                QStandardPaths::findExecutable(QStringLiteral("aws"));
            check(resolved.startsWith(binDir),
                  "cloud cli: PATH resolves aws to the fake, not the real CLI");
        }

        const auto setFake = [&](const QByteArray &stdoutData,
                                 const QByteArray &stderrData, int exitCode) {
            writeFile(outFile, stdoutData, false);
            writeFile(errFile, stderrData, false);
            qputenv("SYSPEEK_FAKE_STDOUT",
                    stdoutData.isEmpty() ? QByteArray()
                                         : outFile.toUtf8());
            qputenv("SYSPEEK_FAKE_STDERR",
                    stderrData.isEmpty() ? QByteArray()
                                         : errFile.toUtf8());
            qputenv("SYSPEEK_FAKE_EXIT", QByteArray::number(exitCode));
            qunsetenv("SYSPEEK_FAKE_SLEEP");
            QFile::remove(argvLog);
        };
        const auto lastArgv = [&argvLog]() {
            QFile log(argvLog);
            if (!log.open(QIODevice::ReadOnly)) {
                return QString();
            }
            const QStringList lines =
                QString::fromUtf8(log.readAll()).split(
                    QLatin1Char('\n'), Qt::SkipEmptyParts);
            return lines.isEmpty() ? QString() : lines.last().trimmed();
        };
        // Resolving the CLI hits the filesystem, so it runs with the fake
        // dir first on PATH; the identity below is what the metadata service
        // would report on each provider.
        CloudIdentity awsId;
        awsId.provider = CloudProvider::Aws;
        awsId.instanceId = QStringLiteral("i-0123");
        awsId.region = QStringLiteral("eu-west-1");
        CloudIdentity azId;
        azId.provider = CloudProvider::Azure;
        azId.accountId = QStringLiteral("sub-9");
        CloudIdentity gcpId;
        gcpId.provider = CloudProvider::Gcp;
        gcpId.accountId = QStringLiteral("proj-7");
        CloudIdentity ociId;
        ociId.provider = CloudProvider::Oci;
        ociId.accountId = QStringLiteral("ocid1.compartment");
        ociId.region = QStringLiteral("eu-frankfurt-1");

        const QByteArray awsListing = R"JSON(
          {"Reservations":[{"Instances":[
            {"InstanceId":"i-0123","InstanceType":"c7g.2xlarge",
             "State":{"Name":"running"},
             "Placement":{"AvailabilityZone":"eu-west-1a"},
             "LaunchTime":"2023-06-01T10:00:00Z",
             "Tags":[{"Key":"Name","Value":"hud-box"}]}]}]}
        )JSON";

        CloudFleetOptions cliOptions;
        // A cold PowerShell start on Windows takes ~2 s, and this budget is a
        // NETWORK budget in production (25 s), so a test-sized 2 s would kill
        // the fake's very first run. The hang case below keeps its own tiny
        // budget on purpose.
        cliOptions.timeoutMs = 15000;
        const qint64 cliNow = 1'700'000'000'000LL;

        setFake(awsListing, QByteArray(), 0);
        const CloudFleetStatus awsFleet =
            probeCloudFleet(awsId, cliOptions, cliNow);
        check(lastArgv() == QStringLiteral(
                               "ec2 describe-instances --output json "
                               "--region eu-west-1 --query "
                               "Reservations[].Instances[]"),
              "cloud cli: aws gets the exact regional describe-instances argv");
        check(awsFleet.available && awsFleet.rows.size() == 1
                  && awsFleet.rows.first().name == QStringLiteral("hud-box")
                  && awsFleet.rows.first().ageSec == cliNow / 1000 - 1685613600,
              "cloud cli: the aws listing parses into a row with its age");

        setFake(QByteArrayLiteral(
                    "[{\"name\":\"vm-a\",\"location\":\"westus\","
                    "\"powerState\":\"VM running\","
                    "\"hardwareProfile\":{\"vmSize\":\"Standard_D2s_v3\"}}]"),
                QByteArray(), 0);
        const CloudFleetStatus azFleet =
            probeCloudFleet(azId, cliOptions, cliNow);
        check(lastArgv() == QStringLiteral(
                               "vm list -d -o json --subscription sub-9"),
              "cloud cli: az lists with -d and the imds subscription");
        check(azFleet.available && azFleet.rows.size() == 1
                  && azFleet.rows.first().instanceType
                         == QStringLiteral("Standard_D2s_v3"),
              "cloud cli: the az listing parses into a row");

        setFake(QByteArrayLiteral(
                    "[{\"name\":\"gce-1\",\"id\":\"44\","
                    "\"machineType\":\"projects/p/zones/us-central1-a/"
                    "machineTypes/e2-small\","
                    "\"zone\":\"projects/p/zones/us-central1-a\","
                    "\"status\":\"RUNNING\"}]"),
                QByteArray(), 0);
        const CloudFleetStatus gcpFleet =
            probeCloudFleet(gcpId, cliOptions, cliNow);
        check(lastArgv() == QStringLiteral(
                               "compute instances list --format=json "
                               "--project proj-7"),
              "cloud cli: gcloud lists the imds project as json");
        check(gcpFleet.available && gcpFleet.rows.size() == 1
                  && gcpFleet.rows.first().instanceType
                         == QStringLiteral("e2-small"),
              "cloud cli: the gcloud listing parses into a row");

        setFake(QByteArrayLiteral(
                    "{\"data\":[{\"id\":\"ocid1.instance\","
                    "\"display-name\":\"oci-1\",\"shape\":\"VM.Standard.A1\","
                    "\"lifecycle-state\":\"RUNNING\",\"region\":\"eu-frankfurt-1\"}]}"),
                QByteArray(), 0);
        const CloudFleetStatus ociFleet =
            probeCloudFleet(ociId, cliOptions, cliNow);
        check(lastArgv() == QStringLiteral(
                               "compute instance list --compartment-id "
                               "ocid1.compartment --region eu-frankfurt-1 "
                               "--all --output json"),
              "cloud cli: oci pages the compartment with its region");
        check(ociFleet.available && ociFleet.rows.size() == 1
                  && ociFleet.rows.first().name == QStringLiteral("oci-1"),
              "cloud cli: the oci listing parses into a row");

        // A logged-out CLI is the failure a user actually meets: the refusal
        // must name the fix, not print a stack of provider noise.
        setFake(QByteArray(),
                QByteArrayLiteral("Unable to locate credentials. You can "
                                  "configure credentials by running \"aws "
                                  "configure\".\n"),
                255);
        const CloudFleetStatus refused =
            probeCloudFleet(awsId, cliOptions, cliNow);
        check(!refused.available && refused.note == fleetCredentialsText(
                                        CloudProvider::Aws)
                  && refused.note.contains(QStringLiteral("aws configure")),
              "cloud cli: a credential refusal names the fix");

        setFake(QByteArrayLiteral("<html>gateway</html>"), QByteArray(), 0);
        const CloudFleetStatus notJson =
            probeCloudFleet(awsId, cliOptions, cliNow);
        check(!notJson.available
                  && notJson.note.contains(QStringLiteral("not JSON")),
              "cloud cli: non-JSON output is refused, never half-parsed");

        // A CLI that hangs: the budget must end it, and the card must say so.
        setFake(awsListing, QByteArray(), 0);
        qputenv("SYSPEEK_FAKE_SLEEP", "6");
        CloudFleetOptions hangOptions;
        hangOptions.timeoutMs = 2000;
        QElapsedTimer timeoutClock;
        timeoutClock.start();
        const CloudFleetStatus hung =
            probeCloudFleet(awsId, hangOptions, cliNow);
        const qint64 hungMs = timeoutClock.elapsed();
        qunsetenv("SYSPEEK_FAKE_SLEEP");
        check(!hung.available
                  && hung.note == fleetTimeoutText(QStringLiteral("aws"), 2)
                  && hungMs >= 1800 && hungMs < 6000,
              "cloud cli: a hanging CLI is killed at the budget");

        // No CLI on PATH at all: the gate names the package to install.
        // (The fake dir is dropped and the real PATH is replaced too:
        // a developer box may well have a working `aws` installed.)
        const QString emptyDir = binDir + QStringLiteral("/empty");
        QDir().mkpath(emptyDir);
        qputenv("PATH", emptyDir.toUtf8());
        const CloudFleetStatus missing =
            probeCloudFleet(awsId, cliOptions, cliNow);
        check(!missing.available
                  && missing.note == fleetCliMissingText(QStringLiteral("aws")),
              "cloud cli: a missing CLI states the gate and the fix");
        qputenv("PATH", fakePath(binDir, oldPath).toUtf8());

        // The HUD's path: the polled runner must deliver the same rows without
        // ever blocking the caller.
        {
            setFake(awsListing, QByteArray(), 0);
            CloudFleetRunner runner;
            CloudFleetOptions runnerOptions;
            runnerOptions.timeoutMs = 5000;
            runner.setOptions(runnerOptions);
            QElapsedTimer pollClock;
            pollClock.start();
            qint64 worstUpdateMs = 0;
            for (int i = 0; i < 400; ++i) {
                QElapsedTimer one;
                one.start();
                runner.update(awsId, QDateTime::currentMSecsSinceEpoch());
                worstUpdateMs = qMax(worstUpdateMs, one.elapsed());
                if (runner.status().fetchedAtMs > 0 && !runner.status().rows.isEmpty()) {
                    break;
                }
                app.processEvents(QEventLoop::AllEvents, 10);
                QThread::msleep(10);
            }
            check(runner.status().available
                      && runner.status().rows.size() == 1
                      && runner.status().rows.first().name
                             == QStringLiteral("hud-box"),
                  "cloud cli: the polled runner delivers the listing");
            check(worstUpdateMs < 1200,
                  "cloud cli: no runner update blocks the sampler thread");
            runner.stop();
        }

        // `syspeek --cloud-prices` end to end: the fake aws answers a Price
        // List payload and the import writes a stamped rate file.
        {
            const QString ratesPath = binDir + QStringLiteral("/rates.txt");
            // The API answers PriceList as an array of JSON *strings*, so the
            // fixture is built the way the service builds it.
            const QByteArray productDoc = R"JSON(
              {"product":{"attributes":{"instanceType":"c7g.2xlarge",
                 "capacitystatus":"Used","preInstalledSw":"NA",
                 "tenancy":"Shared","operatingSystem":"Linux"}},
               "terms":{"OnDemand":{"X":{"priceDimensions":{"X.1":{
                 "unit":"Hrs","pricePerUnit":{"USD":"0.2920000000"}}}}}}}
            )JSON";
            QJsonObject priceResponse;
            priceResponse.insert(QStringLiteral("FormatVersion"),
                                 QStringLiteral("aws_v1"));
            priceResponse.insert(
                QStringLiteral("PriceList"),
                QJsonArray{QString::fromUtf8(productDoc)});
            setFake(QJsonDocument(priceResponse).toJson(QJsonDocument::Compact),
                    QByteArray(), 0);
            const QByteArray typeArg =
                QByteArrayLiteral("--cloud-prices=c7g.2xlarge");
            const QByteArray regionArg = QByteArrayLiteral("--region=eu-west-1");
            const QByteArray ratesArg =
                QByteArrayLiteral("--rates=") + ratesPath.toUtf8();
            char *argv[] = {const_cast<char *>("syspeek"),
                            const_cast<char *>(typeArg.constData()),
                            const_cast<char *>(regionArg.constData()),
                            const_cast<char *>(ratesArg.constData()), nullptr};
            const int rc = cloudPriceRun(4, argv);
            QFile written(ratesPath);
            const QString text = written.open(QIODevice::ReadOnly)
                                     ? QString::fromUtf8(written.readAll())
                                     : QString();
            check(rc == 0
                      && text.contains(QStringLiteral(
                          "aws/c7g.2xlarge = 0.2920"))
                      && text.contains(QStringLiteral("# imported "))
                      && lastArgv().startsWith(QStringLiteral("pricing get-products")),
                  "cloud cli: --cloud-prices writes the imported rate and its date");

            // Re-importing the same type updates the line instead of
            // appending a duplicate, and keeps a hand-written rate.
            {
                QFile seed(ratesPath);
                if (seed.open(QIODevice::WriteOnly | QIODevice::Append)) {
                    seed.write("azure/Standard_D2s_v3 = 0.0960\n");
                }
            }
            const int rc2 = cloudPriceRun(4, argv);
            QFile reread(ratesPath);
            const QString text2 = reread.open(QIODevice::ReadOnly)
                                      ? QString::fromUtf8(reread.readAll())
                                      : QString();
            check(rc2 == 0
                      && text2.count(QStringLiteral("aws/c7g.2xlarge")) == 1
                      && text2.contains(QStringLiteral(
                          "azure/Standard_D2s_v3 = 0.0960")),
                  "cloud cli: a re-import replaces its line and keeps the rest");

            // A logged-out CLI: no file is written and the refusal is stated.
            const QString untouched = binDir + QStringLiteral("/rates2.txt");
            setFake(QByteArray(),
                    QByteArrayLiteral("Unable to locate credentials\n"), 255);
            const QByteArray ratesArg2 =
                QByteArrayLiteral("--rates=") + untouched.toUtf8();
            char *argv2[] = {const_cast<char *>("syspeek"),
                             const_cast<char *>(typeArg.constData()),
                             const_cast<char *>(regionArg.constData()),
                             const_cast<char *>(ratesArg2.constData()),
                             nullptr};
            const int rc3 = cloudPriceRun(4, argv2);
            check(rc3 == 1 && !QFile::exists(untouched),
                  "cloud cli: a refused import writes no rate file");
        }

        qputenv("PATH", oldPath.toUtf8());
        for (const char *name : {"SYSPEEK_FAKE_ARGV_LOG", "SYSPEEK_FAKE_STDOUT",
                                 "SYSPEEK_FAKE_STDERR", "SYSPEEK_FAKE_EXIT",
                                 "SYSPEEK_FAKE_SLEEP"}) {
            qunsetenv(name);
        }
    }


    {
        // M85: billed spend. The parsers are pure, so both provider payloads
        // and the arithmetic are asserted without a network or a CLI.
        QString spendError;
        const QByteArray awsCost = QByteArrayLiteral(
            "{\"ResultsByTime\":["
            "{\"TimePeriod\":{\"Start\":\"2026-10-01\",\"End\":\"2026-10-02\"},"
            "\"Total\":{},\"Groups\":["
            "{\"Keys\":[\"Amazon Elastic Compute Cloud - Compute\"],"
            "\"Metrics\":{\"UnblendedCost\":{\"Amount\":\"12.5\",\"Unit\":\"USD\"}}},"
            "{\"Keys\":[\"Amazon Simple Storage Service\"],"
            "\"Metrics\":{\"UnblendedCost\":{\"Amount\":\"1.5\",\"Unit\":\"USD\"}}}],"
            "\"Estimated\":false},"
            "{\"TimePeriod\":{\"Start\":\"2026-10-02\",\"End\":\"2026-10-03\"},"
            "\"Total\":{},\"Groups\":["
            "{\"Keys\":[\"Amazon Elastic Compute Cloud - Compute\"],"
            "\"Metrics\":{\"UnblendedCost\":{\"Amount\":\"10.0\",\"Unit\":\"USD\"}}}],"
            "\"Estimated\":true}]}");
        CloudSpendStatus awsSpend = parseAwsCostAndUsage(awsCost, &spendError);
        check(awsSpend.available
                  && qAbs(awsSpend.monthToDateUsd - 24.0) < 1e-9
                  && awsSpend.periodStart == QStringLiteral("2026-10-01")
                  && awsSpend.periodEnd == QStringLiteral("2026-10-03"),
              "spend: aws groups fold into a month-to-date total and a period");
        check(awsSpend.services.size() == 2
                  && awsSpend.services.first().name
                         == QStringLiteral("Amazon Elastic Compute Cloud - Compute")
                  && qAbs(awsSpend.services.first().usd - 22.5) < 1e-9
                  && qAbs(awsSpend.services.last().usd - 1.5) < 1e-9,
              "spend: per-service totals are summed across days, sorted desc");
        check(awsSpend.days.size() == 2
                  && qAbs(awsSpend.days.at(0).usd - 14.0) < 1e-9
                  && qAbs(awsSpend.days.at(1).usd - 10.0) < 1e-9
                  && awsSpend.estimated,
              "spend: the daily series and the provider's estimated flag survive");
        spendApplyForecast(&awsSpend, 50.0);
        check(qAbs(awsSpend.forecastUsd - 74.0) < 1e-9
                  && qAbs(awsSpend.forecastRemainingUsd - 50.0) < 1e-9,
              "spend: the month-end projection is billed + forecast");

        const CloudSpendStatus azureSpend = parseAzureUsageList(
            QByteArrayLiteral(
                "{\"value\":["
                "{\"properties\":{\"pretaxCost\":\"3.25\","
                "\"product\":\"Virtual Machines\","
                "\"usageStart\":\"2026-10-01T00:00:00Z\"}},"
                "{\"properties\":{\"pretaxCost\":1.75,"
                "\"product\":\"Storage\","
                "\"usageStart\":\"2026-10-02T00:00:00Z\"}},"
                "{\"properties\":{\"pretaxCost\":1.0,"
                "\"product\":\"Storage\","
                "\"usageStart\":\"2026-10-02T00:00:00Z\"}}]}"),
            &spendError);
        check(azureSpend.available
                  && qAbs(azureSpend.monthToDateUsd - 6.0) < 1e-9
                  && azureSpend.days.size() == 2
                  && qAbs(azureSpend.days.at(1).usd - 2.75) < 1e-9
                  && azureSpend.services.size() == 2,
              "spend: azure usage rows accept string costs, fold days and "
              "products");

        const CloudSpendStatus forecastOnly = parseAwsCostForecast(
            QByteArrayLiteral(
                "{\"Total\":{\"Amount\":\"118.33\",\"Unit\":\"USD\"},"
                "\"ForecastResultsByTime\":[]}"),
            &spendError);
        check(forecastOnly.available
                  && qAbs(forecastOnly.forecastRemainingUsd - 118.33) < 1e-9,
              "spend: the forecast payload yields the remaining-month figure");
        QString emptyError;
        check(!parseAwsCostAndUsage(QByteArrayLiteral("{}"), &emptyError)
                   .available
                  && emptyError.contains(QStringLiteral("ResultsByTime"))
                  && !parseAwsCostForecast(QByteArrayLiteral("{}"), &emptyError)
                          .available
                  && !parseAzureUsageList(QByteArrayLiteral("not json"),
                                          &emptyError)
                          .available,
              "spend: an empty or malformed payload states the reason");

        CloudSpendStatus capped;
        capped.available = true;
        capped.monthToDateUsd = 100.0;
        for (int i = 0; i < 20; ++i) {
            capped.services.append(
                CloudSpendService{QStringLiteral("svc%1").arg(i),
                                  double(20 - i)});
        }
        spendSortServices(&capped, 3);
        check(capped.services.size() == 4
                  && capped.services.first().name == QStringLiteral("svc0")
                  && capped.services.last().name.contains(QStringLiteral("17")),
              "spend: the service table caps itself and says how many it hid");

        check(cloudSpendAmountText(12.5) == QStringLiteral("$12.50")
                  && cloudSpendAmountText(0.004) == QStringLiteral("<$0.01")
                  && cloudSpendAmountText(-1.0) == QStringLiteral("--"),
              "spend: money text keeps cents, flags sub-cent and never fakes 0");
        check(cloudSpendSummaryText(awsSpend)
                      .contains(QStringLiteral("month to date $24.00"))
                  && cloudSpendSummaryText(awsSpend)
                         .contains(QStringLiteral("(+$50.00 forecast)"))
                  && cloudSpendSummaryText(awsSpend)
                         .contains(QStringLiteral("estimated")),
              "spend: the summary states billed, forecast and the estimate flag");
        check(cloudSpendDetailText(awsSpend)
                      .contains(QStringLiteral("2026-10-01 .. 2026-10-03"))
                  && cloudSpendDetailText(awsSpend)
                         .contains(QStringLiteral("unblended cost"))
                  && cloudSpendDetailText(azureSpend)
                         .contains(QStringLiteral("pretax cost")),
              "spend: the detail names the window and each provider's metric");
        check(cloudSpendServicesText(awsSpend)
                      .contains(QStringLiteral("(93.8%)"))
                  && cloudSpendServicesText(CloudSpendStatus{})
                         .isEmpty(),
              "spend: service rows carry their share of the month");
        check(cloudSpendGateText(CloudProvider::Gcp)
                      .contains(QStringLiteral("BigQuery"))
                  && cloudSpendGateText(CloudProvider::Oci)
                         .contains(QStringLiteral("usage API"))
                  && cloudSpendNote().contains(QStringLiteral("billable"))
                  && cloudSpendDisabledText().contains(QStringLiteral("spend=false")),
              "spend: gated providers and the disabled state state the reason");
        check(cloudSpendPeriodStart(1700000000000LL)
                      == QStringLiteral("2023-11-01")
                  && cloudSpendPeriodEnd(1700000000000LL)
                         == QStringLiteral("2023-11-15")
                  && cloudSpendForecastEnd(1700000000000LL)
                         == QStringLiteral("2023-12-01"),
              "spend: the window is computed in UTC, so the query is stable "
              "across time zones");

        // The same numbers on the Prometheus endpoint, and nothing at all when
        // the query has not answered.
        DisplaySnapshot spendSnap;
        spendSnap.cloud.spend = awsSpend;
        const QString spendText = prometheusText(spendSnap);
        check(spendText.contains(QStringLiteral("syspeek_cloud_spend_month_usd"))
                  && spendText.contains(
                      QStringLiteral("syspeek_cloud_spend_forecast_usd"))
                  && spendText.contains(
                      QStringLiteral("syspeek_cloud_spend_service_usd{provider=\"aws\""))
                  && spendText.contains(QStringLiteral("service=\"Amazon Simple "
                                                       "Storage Service\"")),
              "spend: prometheus exports month, forecast and per-service cost");
        DisplaySnapshot noSpendSnap;
        check(!prometheusText(noSpendSnap)
                   .contains(QStringLiteral("syspeek_cloud_spend_")),
              "spend: a host with no billed data exports no spend series");
    }

    {
        // M85: billed spend end to end against a fake CLI: two AWS calls in
        // order (cost, then forecast) and one Azure call.
        QTemporaryDir spendDir;
        const QString bin = spendDir.path();
        const QString costFile = bin + QStringLiteral("/cost.json");
        const QString forecastFile = bin + QStringLiteral("/forecast.json");
        const QString argvLog = bin + QStringLiteral("/argv.log");
        const QByteArray costDoc = QByteArrayLiteral(
            "{\"ResultsByTime\":[{\"TimePeriod\":{\"Start\":\"2026-10-01\","
            "\"End\":\"2026-10-02\"},\"Total\":{},\"Groups\":["
            "{\"Keys\":[\"Amazon Elastic Compute Cloud - Compute\"],"
            "\"Metrics\":{\"UnblendedCost\":{\"Amount\":\"100.0\",\"Unit\":\"USD\"}}}]}]}");
        const QByteArray forecastDoc = QByteArrayLiteral(
            "{\"Total\":{\"Amount\":\"118.33\",\"Unit\":\"USD\"}}");
        const auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return false;
            }
            file.write(data);
            file.close();
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner
                                | QFile::ExeOwner | QFile::ReadGroup
                                | QFile::ExeGroup | QFile::ReadOther
                                | QFile::ExeOther);
            return true;
        };
        write(costFile, costDoc);
        write(forecastFile, forecastDoc);
#if defined(Q_OS_WIN)
        const QString awsName = bin + QStringLiteral("/aws.cmd");
        const QByteArray script =
            "@echo off\r\n"
            "powershell -NoProfile -ExecutionPolicy Bypass -File "
            "\"%~dp0fake.ps1\" %*\r\n";
        const QByteArray ps1 =
            "$out = if ($args.Count -ge 2 -and $args[1] -eq "
            "'get-cost-forecast') { $env:SPEND_FORECAST } else { "
            "$env:SPEND_COST }\r\n"
            "Add-Content -Path $env:SPEND_ARGV_LOG -Value ($args -join ' ')\r\n"
            "[Console]::Out.Write([IO.File]::ReadAllText($out))\r\n"
            "exit 0\r\n";
#else
        const QString awsName = bin + QStringLiteral("/aws");
        const QByteArray script =
            "#!/bin/sh\n"
            "printf '%s\\n' \"$*\" >> \"$SPEND_ARGV_LOG\"\n"
            "case \"$2\" in get-cost-forecast) cat \"$SPEND_FORECAST\";; "
            "*) cat \"$SPEND_COST\";; esac\n";
#endif
        bool wrote = write(awsName, script);
#if defined(Q_OS_WIN)
        wrote = wrote && write(bin + QStringLiteral("/fake.ps1"), ps1);
#endif
        check(wrote, "spend: the fake aws CLI is executable");
        const QString oldPath = qEnvironmentVariable("PATH");
        const auto fakePath = [](const QString &dir, const QString &base) {
#if defined(Q_OS_WIN)
            QStringList keep;
            const QString root = qEnvironmentVariable("SystemRoot");
            for (const QString &entry : base.split(QLatin1Char(';'))) {
                if (entry.contains(QStringLiteral("System32"),
                                   Qt::CaseInsensitive)
                    || (!root.isEmpty()
                        && entry.compare(root, Qt::CaseInsensitive) == 0)) {
                    keep.append(entry);
                }
            }
            return dir + QLatin1Char(';') + keep.join(QLatin1Char(';'));
#else
            return dir + QLatin1Char(':') + base;
#endif
        };
        qputenv("PATH", fakePath(bin, oldPath).toUtf8());
        qputenv("SPEND_ARGV_LOG", argvLog.toUtf8());
        qputenv("SPEND_COST", costFile.toUtf8());
        qputenv("SPEND_FORECAST", forecastFile.toUtf8());

        CloudIdentity identity;
        identity.provider = CloudProvider::Aws;
        identity.instanceId = QStringLiteral("i-0aaa");
        identity.accountId = QStringLiteral("123456789012");
        CloudSpendOptions options;
        options.timeoutMs = 10000;
        const qint64 nowMs = 1'760'000'000'000LL;   // 2025-10-09 UTC

        const CloudSpendStatus spend = probeCloudSpend(identity, options, nowMs);
        QStringList calls;
        {
            QFile log(argvLog);
            if (log.open(QIODevice::ReadOnly)) {
                calls = QString::fromUtf8(log.readAll())
                            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            }
        }
        check(calls.size() == 2
                  && calls.at(0).startsWith(
                      QStringLiteral("ce get-cost-and-usage --time-period "
                                     "Start=2025-10-01,End=2025-10-10 "))
                  && calls.at(0).contains(
                      QStringLiteral("--group-by Type=DIMENSION,Key=SERVICE"))
                  && calls.at(0).contains(
                      QStringLiteral("--metrics UnblendedCost"))
                  && calls.at(1).startsWith(
                      QStringLiteral("ce get-cost-forecast --time-period "
                                     "Start=2025-10-10,End=2025-11-01 "))
                  && calls.at(1).contains(
                      QStringLiteral("--metric UNBLENDED_COST")),
              "spend: aws runs cost-and-usage then the month-end forecast");
        check(spend.available
                  && qAbs(spend.monthToDateUsd - 100.0) < 1e-9
                  && qAbs(spend.forecastUsd - 218.33) < 1e-9
                  && spend.scope == QStringLiteral("account 123456789012")
                  && spend.source.startsWith(
                      QStringLiteral("aws ce get-cost-and-usage")),
              "spend: the two answers fold into billed + month-end");

        // A provider whose CLI has no cost API never spawns anything.
        QFile::remove(argvLog);
        CloudIdentity gcpIdentity;
        gcpIdentity.provider = CloudProvider::Gcp;
        const CloudSpendStatus gcp = probeCloudSpend(gcpIdentity, options, nowMs);
        check(!gcp.attempted
                  && gcp.note.contains(QStringLiteral("BigQuery"))
                  && !QFile::exists(argvLog),
              "spend: a gated provider issues no query at all");

        // The polled runner, on the same fake CLI.
        {
            QFile::remove(argvLog);
            CloudSpendRunner runner;
            runner.setOptions(options);
            QElapsedTimer clock;
            clock.start();
            qint64 worstUpdateMs = 0;
            for (int i = 0; i < 400; ++i) {
                QElapsedTimer one;
                one.start();
                runner.update(identity, QDateTime::currentMSecsSinceEpoch());
                worstUpdateMs = qMax(worstUpdateMs, one.elapsed());
                if (runner.status().available
                    && runner.status().forecastUsd >= 0.0) {
                    break;
                }
                app.processEvents(QEventLoop::AllEvents, 10);
                QThread::msleep(10);
            }
            check(runner.status().available
                      && qAbs(runner.status().monthToDateUsd - 100.0) < 1e-9
                      && qAbs(runner.status().forecastUsd - 218.33) < 1e-9,
                  "spend: the polled runner walks both steps and ends with the "
                  "full picture");
            check(worstUpdateMs < 1200,
                  "spend: no runner update blocks the sampler thread");
            runner.stop();
        }

        qputenv("PATH", oldPath.toUtf8());
        for (const char *name : {"SPEND_ARGV_LOG", "SPEND_COST", "SPEND_FORECAST"}) {
            qunsetenv(name);
        }
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
