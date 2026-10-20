#include "HudWindow.h"
#include "DashboardPanel.h"

#include <QApplication>
#include <QListWidget>
#include <QLabel>
#include <QPixmap>
#include <QDateTime>

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
    s.connections = {conn};

    InterfaceRow iface;
    iface.name = QStringLiteral("wlp0s20f3");
    iface.rxBytes = 84ULL * 1024ULL * 1024ULL;
    iface.txBytes = 12ULL * 1024ULL * 1024ULL;
    iface.rxPerSec = 412000.0;
    iface.txPerSec = 118000.0;
    s.interfaces = {iface};

    EndpointRow endpoint;
    endpoint.proto = QStringLiteral("tcp");
    endpoint.addr = QStringLiteral("127.0.0.1");
    endpoint.port = 4318;
    endpoint.exposure = QStringLiteral("localhost");
    endpoint.service = QStringLiteral("syspeek");
    s.endpoints = {endpoint};
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
    {
        // 240 synthetic ticks: fills the chart rings so the panels show
        // plausible series instead of two-point stubs.
        for (int i = 0; i < 240; ++i) {
            panel.setSnapshot(demoSnapshot(i));
            app.processEvents();
        }
        panel.setSnapshot(demoSnapshot(240));
    }

    // Synthetic DogStatsD receiver history: cumulative counters + a few
    // series so the Metrics page renders tiles, rate charts and the
    // explorer table populated. Two parse bursts exercise the red tile.
    StatsdSnapshot statsd;
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

    DockerSnapshot docker;
    docker.state = DockerState::Ok;
    docker.runningCount = 2;
    docker.issueCount = 1;
    DockerContainer db;
    db.id = QStringLiteral("c1");
    db.name = QStringLiteral("postgres");
    db.state = QStringLiteral("running");
    db.status = QStringLiteral("Up 5 minutes (unhealthy)");
    db.health = QStringLiteral("unhealthy");
    DockerContainer web;
    web.id = QStringLiteral("c2");
    web.name = QStringLiteral("web");
    web.state = QStringLiteral("running");
    web.status = QStringLiteral("Up 2 days (healthy)");
    web.health = QStringLiteral("healthy");
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
    kube.pods = {pod};
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
    const bool ok = panel.grab().save(out);
    std::printf("%s: %s\n", ok ? "saved" : "failed",
                out.toUtf8().constData());
    return ok ? 0 : 2;
}
