#include "HudWindow.h"
#include "DashboardPanel.h"

#include <QApplication>
#include <QListWidget>
#include <QLabel>
#include <QPixmap>
#include <QDateTime>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>

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

    return s;
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
    auto driveHistory = [&app, &panel]() {        // Synthetic alert lifecycle: quiet -> warning -> +critical ->
        // resolve -> clear -> refire, so tiles/chart/timeline all move.
        const qint64 baseMs = QDateTime::currentMSecsSinceEpoch()
                              - 241 * 2000;
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
        KubeSnapshot kube;
        kube.state = KubeState::Ok;
        KubePod pod;
        pod.ns = QStringLiteral("default");
        pod.name = QStringLiteral("api-7f9c");
        pod.phase = QStringLiteral("Running");
        pod.reason = QStringLiteral("CrashLoopBackOff");
        pod.restarts = 7;
        pod.cpuMillis = 512.0;
        pod.memBytes = 503L * 1024 * 1024;
        KubePod kubeIdle;
        kubeIdle.ns = QStringLiteral("default");
        kubeIdle.name = QStringLiteral("web-5f8d");
        kubeIdle.phase = QStringLiteral("Running");
        kubeIdle.restarts = 0;
        kubeIdle.cpuMillis = 22.0;
        kubeIdle.memBytes = 64L * 1024 * 1024;
        KubeNode kubeNode;
        kubeNode.name = QStringLiteral("node-1");
        kubeNode.ready = true;
        kubeNode.kubeletVersion = QStringLiteral("v1.31.4");
        kubeNode.cpuAllocatableCores = 11.8;
        kubeNode.cpuCapacityCores = 12.0;
        kubeNode.memAllocatableBytes = 15400000000ULL;
        kubeNode.memCapacityBytes = 15800000000ULL;
        kubeNode.podsAllocatable = 110;
        kubeNode.podsRunning = 2;
        kubeNode.cpuMillis = 1890.0;
        kubeNode.memBytes = 6200L * 1024 * 1024;
        kube.pods = {pod, kubeIdle};
        kube.totalPods = 2;
        kube.nodes = {kubeNode};
        kube.metricsOk = true;
        kube.podsWithUsage = 2;
        kube.nodesWithUsage = 1;
        pod.cpuMillis = 512.0;
        kube.podsCpuMillis = 534.0;
        kube.podsMemBytes = 567L * 1024 * 1024;
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
            const QList<const char *> hosts = {
                QStringLiteral("gateway.local").toUtf8().constData(),
                QStringLiteral("core1-rtr.isp").toUtf8().constData(),
                QStringLiteral("edge1.isp").toUtf8().constData(),
                QStringLiteral("peer TransitNet").toUtf8().constData(),
                dst.toUtf8().constData()};
            for (int h = 0; h < hops; ++h) {
                PathHop hop;
                hop.index = h + 1;
                hop.host = QString::fromUtf8(hosts.at(h));
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

    KubeSnapshot kube;
    kube.state = KubeState::Ok;
    kube.totalPods = 12;
    kube.issueCount = 2;
    KubePod pod;
    pod.ns = QStringLiteral("default");
    pod.name = QStringLiteral("api-7f9c");
    pod.phase = QStringLiteral("Running");
    pod.reason = QStringLiteral("CrashLoopBackOff");
    pod.restarts = 7;
    pod.cpuMillis = 512.0;
    pod.memBytes = 503L * 1024 * 1024;
    KubePod kubeWorking;
    kubeWorking.ns = QStringLiteral("default");
    kubeWorking.name = QStringLiteral("web-5f8d");
    kubeWorking.phase = QStringLiteral("Running");
    kubeWorking.restarts = 0;
    kubeWorking.cpuMillis = 22.0;
    kubeWorking.memBytes = 64L * 1024 * 1024;
    kube.pods = {pod, kubeWorking};
    kube.metricsOk = true;
    kube.podsWithUsage = 2;
    kube.nodesWithUsage = 1;
    kube.podsCpuMillis = 534.0;
    kube.podsMemBytes = 567L * 1024 * 1024;
    KubeNode kubeNode;
    kubeNode.name = QStringLiteral("node-1");
    kubeNode.ready = true;
    kubeNode.kubeletVersion = QStringLiteral("v1.31.4");
    kubeNode.cpuAllocatableCores = 7.9;
    kubeNode.cpuCapacityCores = 8.0;
    kubeNode.memAllocatableBytes = 15000000ULL * 1024;
    kubeNode.memCapacityBytes = 16384000ULL * 1024;
    kubeNode.podsAllocatable = 110;
    kubeNode.podsRunning = 12;
    kubeNode.cpuMillis = 1890.0;
    kubeNode.memBytes = 6200L * 1024 * 1024;
    kube.nodes = {kubeNode};
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
    settingsInfo.version = QStringLiteral("0.8.3");
    settingsInfo.configPath = QStringLiteral("~/.config/Syspeek/syspeek.conf");
    settingsInfo.logPath = QStringLiteral("~/.cache/dev.syspeek/syspeek/hud.log");
    settingsInfo.autostartLine =
        QStringLiteral("on (systemd user unit syspeek.service)");
    settingsInfo.verbsLine =
        QStringLiteral("install.sh run|start|stop|restart|status|enable|"
                       "disable|update|uninstall|logs|vers");
    settingsInfo.sampleIntervalMs = 1000;
    settingsInfo.cpuWarnPercent = 70.0;
    settingsInfo.cpuHighPercent = 90.0;
    settingsInfo.rssWarnGb = 1.0;
    panel.setSettingsInfo(settingsInfo);

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
    driveHistory();
    driveStatsd();
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
    const QPixmap shot =
        grabScroll && grabScroll->widget()
            ? grabScroll->widget()->grab()
            : panel.grab();
    const bool ok = shot.save(out);
    std::printf("%s: %s\n", ok ? "saved" : "failed",
                out.toUtf8().constData());
    return ok ? 0 : 2;
}
