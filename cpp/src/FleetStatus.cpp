#include "FleetStatus.h"

#include "CloudProbe.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSysInfo>

namespace {

constexpr int kMaxPeers = 64;

QJsonValue numberOrNull(double value) {
    if (value < 0.0) {
        return QJsonValue();
    }
    return QJsonValue(value);
}

QJsonValue countOrNull(double value) {
    if (value < 0.0) {
        return QJsonValue();
    }
    return QJsonValue(value);
}

double doubleField(const QJsonObject &object, const char *key) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble()) {
        return -1.0;
    }
    return value.toDouble();
}

int intField(const QJsonObject &object, const char *key, int fallback = -1) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble()) {
        return fallback;
    }
    return value.toInt(fallback);
}

QString stringField(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toString();
}

QJsonObject objectField(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toObject();
}

QString shortDouble(double value, int decimals) {
    if (value < 0.0) {
        return QStringLiteral("--");
    }
    return QString::number(value, 'f', decimals);
}

double memAvailablePercent(const FleetPeer &peer) {
    if (peer.memTotalKb == 0) {
        return -1.0;
    }
    return 100.0 * double(peer.memAvailableKb) / double(peer.memTotalKb);
}

QString peakLabel(const FleetPeer &peer) {
    if (!peer.name.isEmpty()) {
        return peer.name;
    }
    if (!peer.hostname.isEmpty()) {
        return peer.hostname;
    }
    return peer.address;
}

} // namespace

QString fleetNormalizePeerUrl(const QString &spec, QString *error) {
    if (error != nullptr) {
        error->clear();
    }
    QString rest = spec.trimmed();
    if (rest.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("empty peer entry");
        }
        return QString();
    }
    const int equals = rest.indexOf(QLatin1Char('='));
    if (equals > 0) {
        rest = rest.mid(equals + 1).trimmed();
    }

    QString scheme = QStringLiteral("http");
    if (rest.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        scheme = QStringLiteral("https");
        rest = rest.mid(8);
    } else if (rest.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)) {
        rest = rest.mid(7);
    }

    QString path;
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        path = rest.mid(slash);
        rest = rest.left(slash);
    }

    QString host;
    QString port;
    if (rest.startsWith(QLatin1Char('['))) {
        const int close = rest.indexOf(QLatin1Char(']'));
        if (close < 0) {
            if (error != nullptr) {
                *error = QStringLiteral("unterminated IPv6 literal");
            }
            return QString();
        }
        host = rest.left(close + 1);
        if (rest.mid(close + 1).startsWith(QLatin1Char(':'))) {
            port = rest.mid(close + 2);
        }
    } else {
        const int colon = rest.lastIndexOf(QLatin1Char(':'));
        if (colon > 0) {
            host = rest.left(colon);
            port = rest.mid(colon + 1);
        } else {
            host = rest;
        }
    }

    host = host.trimmed();
    if (host.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("no host in peer entry");
        }
        return QString();
    }

    if (port.isEmpty()) {
        // A bare host is this machine's own endpoint, so it gets our default
        // port. A scheme-only entry is a published endpoint behind a proxy, so
        // it keeps the port the scheme implies instead of 9107.
        port = scheme == QStringLiteral("https")
                   ? QString()
                   : QString::number(kFleetDefaultPort);
    } else {
        bool ok = false;
        const int parsed = port.toInt(&ok);
        if (!ok || parsed < 1 || parsed > 65535) {
            if (error != nullptr) {
                *error = QStringLiteral("port %1 is not 1..65535").arg(port);
            }
            return QString();
        }
        port = QString::number(parsed);
    }

    if (path.isEmpty() || path == QStringLiteral("/")) {
        path = QStringLiteral("/api/v1/snapshot");
    } else if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }

    return scheme + QStringLiteral("://") + host
        + (port.isEmpty() ? QString() : QLatin1Char(':') + port) + path;
}

QList<FleetPeer> parseFleetPeers(const QStringList &specs, QString *error) {
    if (error != nullptr) {
        error->clear();
    }
    QList<FleetPeer> peers;
    for (const QString &raw : specs) {
        const QString spec = raw.trimmed();
        if (spec.isEmpty()) {
            continue;
        }
        FleetPeer peer;
        peer.address = spec;
        const int equals = spec.indexOf(QLatin1Char('='));
        if (equals > 0) {
            peer.name = spec.left(equals).trimmed();
            peer.nameAuthored = !peer.name.isEmpty();
        }
        QString failure;
        peer.url = fleetNormalizePeerUrl(spec, &failure);
        if (peer.url.isEmpty()) {
            peer.configured = false;
            peer.configNote = failure;
            if (error != nullptr && error->isEmpty()) {
                *error = QStringLiteral("%1: %2").arg(spec, failure);
            }
        } else if (peer.name.isEmpty()) {
            QString authority = peer.url;
            authority = authority.mid(authority.indexOf(QStringLiteral("://")) + 3);
            const int slash = authority.indexOf(QLatin1Char('/'));
            if (slash >= 0) {
                authority = authority.left(slash);
            }
            const int colon = authority.lastIndexOf(QLatin1Char(':'));
            if (colon > 0 && !authority.startsWith(QLatin1Char('['))) {
                authority = authority.left(colon);
            }
            peer.name = authority;
        }
        if (peers.size() >= kMaxPeers) {
            break;
        }
        peers.append(peer);
    }
    return peers;
}

FleetPeer fleetSelfPeer(const DisplaySnapshot &snapshot, const QString &name,
                        qint64 nowMs) {
    FleetPeer self;
    self.name = name.isEmpty() ? QStringLiteral("this machine") : name;
    self.address = QStringLiteral("self");
    self.url = QStringLiteral("self");
    self.isSelf = true;
    self.configured = true;
    self.online = true;
    self.everSeen = true;
    self.schema = kFleetSchemaVersion;
    self.sampledAtMs = nowMs;
    self.lastSeenMs = nowMs;
    self.lastAttemptMs = nowMs;
    self.latencyMs = 0;
    self.sequence = snapshot.sequence;
    self.uptimeSec = cloudHostUptimeSeconds();

    self.appName = snapshot.appName;
    self.appCpu = snapshot.cpuValid ? snapshot.cpuPercent : -1.0;
    self.appRssKb = snapshot.rssKb;

    self.cpuPercent = snapshot.hostAvailable && !snapshot.hostWarming
                          ? snapshot.hostCpuPercent
                          : -1.0;
    self.stealPercent = self.cpuPercent < 0.0 ? -1.0 : snapshot.hostStealPercent;
    self.memTotalKb = snapshot.hostMemTotalKb;
    self.memAvailableKb = snapshot.hostMemAvailableKb;
    self.load1 = snapshot.hostLoad1;
    if (snapshot.hostDiskTotalBytes > 0) {
        self.diskFreePct = 100.0 * double(snapshot.hostDiskFreeBytes)
            / double(snapshot.hostDiskTotalBytes);
    }
    self.netRxPerSec = snapshot.hostNetRxPerSec;
    self.netTxPerSec = snapshot.hostNetTxPerSec;
    self.processCount = snapshot.processes.size();
    self.listeningCount = snapshot.listeningPorts.size();
    self.connectionCount = snapshot.connections.size();
    int established = 0;
    for (const ConnectionRow &row : snapshot.connections) {
        if (row.state.contains(QStringLiteral("ESTABLISHED"))) {
            ++established;
        }
    }
    self.establishedCount = established;
    self.retransPercent = snapshot.tcpHealthAvailable
                              ? snapshot.tcpHealth.retransPercent
                              : -1.0;
    if (snapshot.gpu.ok) {
        double worst = -1.0;
        for (const GpuDevice &device : snapshot.gpu.devices) {
            if (device.utilPercent > worst) {
                worst = device.utilPercent;
            }
        }
        self.gpuUtilPercent = worst < 0.0 ? -1 : int(worst + 0.5);
    }
    self.hottestCelsius = snapshot.sensors.hottestCelsius;
    for (const BatteryReading &battery : snapshot.sensors.batteries) {
        if (battery.capacityPercent < 0.0) {
            continue;
        }
        if (self.batteryPercent < 0 || battery.capacityPercent < self.batteryPercent) {
            self.batteryPercent = int(battery.capacityPercent + 0.5);
        }
    }
    if (snapshot.alerts.pushed) {
        self.openIncidents = snapshot.alerts.openIncidents;
        self.flappingIncidents = snapshot.alerts.flappingIncidents;
    }
    return self;
}

QByteArray fleetSnapshotJson(const DisplaySnapshot &snapshot,
                             const QString &name, const QString &hostname,
                             const QString &version,
                             const QString &platform) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), kFleetSchemaVersion);
    if (!name.isEmpty()) {
        root.insert(QStringLiteral("name"), name);
    }
    if (!hostname.isEmpty()) {
        root.insert(QStringLiteral("hostname"), hostname);
    }
    if (!version.isEmpty()) {
        root.insert(QStringLiteral("version"), version);
    }
    if (!platform.isEmpty()) {
        root.insert(QStringLiteral("platform"), platform);
    }
    root.insert(QStringLiteral("sequence"), double(snapshot.sequence));
    root.insert(QStringLiteral("sampledAtMs"),
                double(QDateTime::currentMSecsSinceEpoch()));
    const qint64 uptime = cloudHostUptimeSeconds();
    root.insert(QStringLiteral("uptimeSec"),
                uptime < 0 ? QJsonValue() : QJsonValue(double(uptime)));

    QJsonObject app;
    if (!snapshot.appName.isEmpty()) {
        app.insert(QStringLiteral("name"), snapshot.appName);
    }
    if (!snapshot.pid.isEmpty()) {
        bool ok = false;
        const int pid = snapshot.pid.toInt(&ok);
        if (ok) {
            app.insert(QStringLiteral("pid"), pid);
        }
    }
    app.insert(QStringLiteral("cpu"),
               snapshot.cpuValid ? QJsonValue(snapshot.cpuPercent)
                                 : QJsonValue());
    app.insert(QStringLiteral("rssKb"), double(snapshot.rssKb));
    root.insert(QStringLiteral("app"), app);

    const bool hostReady = snapshot.hostAvailable && !snapshot.hostWarming;
    QJsonObject host;
    host.insert(QStringLiteral("cpu"),
                hostReady ? QJsonValue(snapshot.hostCpuPercent)
                          : QJsonValue());
    host.insert(QStringLiteral("steal"),
                hostReady ? QJsonValue(snapshot.hostStealPercent)
                          : QJsonValue());
    host.insert(QStringLiteral("memTotalKb"), double(snapshot.hostMemTotalKb));
    host.insert(QStringLiteral("memAvailableKb"),
                double(snapshot.hostMemAvailableKb));
    host.insert(QStringLiteral("load1"),
                hostReady ? QJsonValue(snapshot.hostLoad1) : QJsonValue());
    if (snapshot.hostDiskTotalBytes > 0) {
        host.insert(QStringLiteral("diskFreePct"),
                    QJsonValue(100.0 * double(snapshot.hostDiskFreeBytes)
                               / double(snapshot.hostDiskTotalBytes)));
    } else {
        host.insert(QStringLiteral("diskFreePct"), QJsonValue());
    }
    host.insert(QStringLiteral("netRxPerSec"), snapshot.hostNetRxPerSec);
    host.insert(QStringLiteral("netTxPerSec"), snapshot.hostNetTxPerSec);
    root.insert(QStringLiteral("host"), host);

    root.insert(QStringLiteral("processes"), snapshot.processes.size());

    int established = 0;
    for (const ConnectionRow &row : snapshot.connections) {
        if (row.state.contains(QStringLiteral("ESTABLISHED"))) {
            ++established;
        }
    }
    QJsonObject sockets;
    sockets.insert(QStringLiteral("listening"), snapshot.listeningPorts.size());
    sockets.insert(QStringLiteral("connections"), snapshot.connections.size());
    sockets.insert(QStringLiteral("established"), established);
    root.insert(QStringLiteral("sockets"), sockets);

    QJsonObject tcp;
    tcp.insert(QStringLiteral("retransPct"),
               snapshot.tcpHealthAvailable
                   ? QJsonValue(snapshot.tcpHealth.retransPercent)
                   : QJsonValue());
    root.insert(QStringLiteral("tcp"), tcp);

    QJsonObject gpu;
    int gpuUtil = -1;
    if (snapshot.gpu.ok) {
        double worst = -1.0;
        for (const GpuDevice &device : snapshot.gpu.devices) {
            if (device.utilPercent > worst) {
                worst = device.utilPercent;
            }
        }
        if (worst >= 0.0) {
            gpuUtil = int(worst + 0.5);
        }
    }
    gpu.insert(QStringLiteral("util"), countOrNull(gpuUtil));
    root.insert(QStringLiteral("gpu"), gpu);

    QJsonObject sensors;
    sensors.insert(QStringLiteral("hottestC"),
                   numberOrNull(snapshot.sensors.hottestCelsius));
    int battery = -1;
    for (const BatteryReading &reading : snapshot.sensors.batteries) {
        if (reading.capacityPercent < 0.0) {
            continue;
        }
        if (battery < 0 || reading.capacityPercent < battery) {
            battery = int(reading.capacityPercent + 0.5);
        }
    }
    sensors.insert(QStringLiteral("batteryPct"), countOrNull(battery));
    root.insert(QStringLiteral("sensors"), sensors);

    QJsonObject alerts;
    alerts.insert(QStringLiteral("open"),
                  snapshot.alerts.pushed
                      ? QJsonValue(snapshot.alerts.openIncidents)
                      : QJsonValue());
    alerts.insert(QStringLiteral("flapping"),
                  snapshot.alerts.pushed
                      ? QJsonValue(snapshot.alerts.flappingIncidents)
                      : QJsonValue());
    root.insert(QStringLiteral("alerts"), alerts);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool parseFleetSnapshotJson(const QByteArray &json, FleetPeer *out,
                            QString *error) {
    if (error != nullptr) {
        error->clear();
    }
    if (out == nullptr) {
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("not JSON: %1")
                         .arg(parseError.errorString());
        }
        return false;
    }
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("answer is not a JSON object");
        }
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonValue schemaValue = root.value(QStringLiteral("schema"));
    if (!schemaValue.isDouble()) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "no schema field - this is not a Syspeek snapshot");
        }
        return false;
    }
    const int schema = schemaValue.toInt();
    if (schema < 1) {
        if (error != nullptr) {
            *error = QStringLiteral("schema %1 is not a version this build "
                                    "understands")
                         .arg(schema);
        }
        return false;
    }
    if (schema > kFleetSchemaVersion) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "peer runs schema %1, this build reads up to %2 - update "
                "Syspeek here to see it")
                         .arg(schema)
                         .arg(kFleetSchemaVersion);
        }
        return false;
    }
    const QJsonValue sampledValue = root.value(QStringLiteral("sampledAtMs"));
    if (!sampledValue.isDouble()) {
        if (error != nullptr) {
            *error = QStringLiteral("no sampledAtMs - the peer did not say "
                                    "when it sampled");
        }
        return false;
    }

    FleetPeer peer;
    peer.schema = schema;
    peer.name = stringField(root, "name");
    peer.hostname = stringField(root, "hostname");
    peer.version = stringField(root, "version");
    peer.platform = stringField(root, "platform");
    peer.sequence = quint64(root.value(QStringLiteral("sequence")).toDouble());
    peer.sampledAtMs = qint64(sampledValue.toDouble());
    peer.uptimeSec = qint64(doubleField(root, "uptimeSec"));

    const QJsonObject app = objectField(root, "app");
    peer.appName = stringField(app, "name");
    peer.appCpu = doubleField(app, "cpu");
    peer.appRssKb = quint64(doubleField(app, "rssKb"));

    const QJsonObject host = objectField(root, "host");
    peer.cpuPercent = doubleField(host, "cpu");
    peer.stealPercent = doubleField(host, "steal");
    peer.memTotalKb = quint64(doubleField(host, "memTotalKb"));
    peer.memAvailableKb = quint64(doubleField(host, "memAvailableKb"));
    peer.load1 = doubleField(host, "load1");
    peer.diskFreePct = doubleField(host, "diskFreePct");
    peer.netRxPerSec = doubleField(host, "netRxPerSec");
    peer.netTxPerSec = doubleField(host, "netTxPerSec");
    peer.processCount = intField(root, "processes", 0);

    const QJsonObject sockets = objectField(root, "sockets");
    peer.listeningCount = intField(sockets, "listening", 0);
    peer.connectionCount = intField(sockets, "connections", 0);
    peer.establishedCount = intField(sockets, "established", 0);

    const QJsonObject tcp = objectField(root, "tcp");
    peer.retransPercent = doubleField(tcp, "retransPct");

    const QJsonObject gpu = objectField(root, "gpu");
    peer.gpuUtilPercent = intField(gpu, "util", -1);

    const QJsonObject sensors = objectField(root, "sensors");
    peer.hottestCelsius = doubleField(sensors, "hottestC");
    peer.batteryPercent = intField(sensors, "batteryPct", -1);

    const QJsonObject alerts = objectField(root, "alerts");
    peer.openIncidents = intField(alerts, "open", -1);
    peer.flappingIncidents = intField(alerts, "flapping", -1);

    peer.online = true;
    peer.everSeen = true;
    peer.refused = false;
    *out = peer;
    return true;
}

bool fleetPeerStale(const FleetPeer &peer, qint64 nowMs, int pollMs) {
    if (!peer.online || peer.lastSeenMs < 0) {
        return true;
    }
    const int window = qMax(pollMs, 1000) * 3;
    return nowMs - peer.lastSeenMs > window;
}

int fleetPeerAgeMs(const FleetPeer &peer, qint64 nowMs) {
    if (peer.lastSeenMs < 0) {
        return -1;
    }
    const qint64 delta = nowMs - peer.lastSeenMs;
    return delta < 0 ? 0 : int(qMin<qint64>(delta, INT_MAX));
}

int fleetOnlineCount(const FleetStatus &status) {
    int count = 0;
    for (const FleetPeer &peer : status.peers) {
        if (!peer.isSelf && peer.configured && peer.online) {
            ++count;
        }
    }
    return count;
}

int fleetDownCount(const FleetStatus &status) {
    int count = 0;
    for (const FleetPeer &peer : status.peers) {
        if (!peer.isSelf && peer.configured && !peer.online) {
            ++count;
        }
    }
    return count;
}

double fleetWorstCpu(const FleetStatus &status) {
    double worst = -1.0;
    for (const FleetPeer &peer : status.peers) {
        if (peer.isSelf || !peer.configured || !peer.online
            || peer.cpuPercent < 0.0) {
            continue;
        }
        if (peer.cpuPercent > worst) {
            worst = peer.cpuPercent;
        }
    }
    return worst;
}

double fleetLowestMemPct(const FleetStatus &status) {
    double lowest = -1.0;
    for (const FleetPeer &peer : status.peers) {
        if (peer.isSelf || !peer.configured || !peer.online) {
            continue;
        }
        const double pct = memAvailablePercent(peer);
        if (pct < 0.0) {
            continue;
        }
        if (lowest < 0.0 || pct < lowest) {
            lowest = pct;
        }
    }
    return lowest;
}

int fleetAttentionIndex(const FleetStatus &status) {
    for (int i = 0; i < status.peers.size(); ++i) {
        const FleetPeer &peer = status.peers.at(i);
        if (!peer.isSelf && peer.configured && !peer.online) {
            return i;
        }
    }
    int hottest = -1;
    double worst = -1.0;
    for (int i = 0; i < status.peers.size(); ++i) {
        const FleetPeer &peer = status.peers.at(i);
        if (peer.isSelf || !peer.configured || !peer.online
            || peer.cpuPercent < 0.0) {
            continue;
        }
        if (peer.cpuPercent > worst) {
            worst = peer.cpuPercent;
            hottest = i;
        }
    }
    if (hottest >= 0) {
        return hottest;
    }
    int tightest = -1;
    double lowest = -1.0;
    for (int i = 0; i < status.peers.size(); ++i) {
        const FleetPeer &peer = status.peers.at(i);
        if (peer.isSelf || !peer.configured || !peer.online) {
            continue;
        }
        const double pct = memAvailablePercent(peer);
        if (pct < 0.0) {
            continue;
        }
        if (lowest < 0.0 || pct < lowest) {
            lowest = pct;
            tightest = i;
        }
    }
    return tightest;
}

FleetStatus fleetWithSelf(const FleetStatus &status, const FleetPeer &self) {
    FleetStatus merged = status;
    FleetPeer local = self;
    local.configured = false;
    merged.peers.prepend(local);
    return merged;
}

QString fleetPeerStateText(const FleetPeer &peer, qint64 nowMs, int pollMs) {
    if (peer.isSelf) {
        return QStringLiteral("this machine");
    }
    if (!peer.configured) {
        return peer.configNote.isEmpty()
                   ? QStringLiteral("refused")
                   : QStringLiteral("refused: %1").arg(peer.configNote);
    }
    if (!peer.online) {
        if (peer.error.isEmpty()) {
            return QStringLiteral("down");
        }
        return QStringLiteral("down - %1").arg(peer.error);
    }
    const int age = fleetPeerAgeMs(peer, nowMs);
    if (age < 0) {
        return QStringLiteral("online");
    }
    if (fleetPeerStale(peer, nowMs, pollMs)) {
        return QStringLiteral("stale %1s").arg(age / 1000);
    }
    if (age < 1000) {
        return QStringLiteral("online %1ms").arg(age);
    }
    return QStringLiteral("online %1s").arg(age / 1000);
}

QString fleetPeerAgeText(const FleetPeer &peer, qint64 nowMs) {
    if (peer.lastSeenMs < 0) {
        return QStringLiteral("--");
    }
    const qint64 age = qMax<qint64>(0, nowMs - peer.lastSeenMs);
    if (age < 1500) {
        return QStringLiteral("just now");
    }
    if (age < 60000) {
        return QStringLiteral("%1s").arg(age / 1000);
    }
    if (age < 3600000) {
        return QStringLiteral("%1m").arg(age / 60000);
    }
    if (age < 86400000) {
        return QStringLiteral("%1h").arg(age / 3600000);
    }
    return QStringLiteral("%1d").arg(age / 86400000);
}

QString fleetPeerDetailText(const FleetPeer &peer, qint64 nowMs, int pollMs) {
    if (peer.isSelf) {
        return QStringLiteral(
            "this machine - it serves /api/v1/snapshot and polls the peers "
            "below; nothing about it is sent anywhere else");
    }
    if (!peer.configured) {
        return QStringLiteral("%1 - remove it or fix it in [fleet] peers=")
            .arg(peer.address);
    }
    if (!peer.online) {
        return QStringLiteral("%1 (%2)").arg(peer.address,
                                             fleetPeerStateText(peer, nowMs,
                                                                pollMs));
    }
    QStringList parts;
    parts << (peer.hostname.isEmpty() ? peer.address : peer.hostname);
    if (!peer.version.isEmpty()) {
        parts << QStringLiteral("syspeek %1").arg(peer.version);
    }
    if (!peer.platform.isEmpty()) {
        parts << peer.platform;
    }
    parts << QStringLiteral("%1 processes").arg(peer.processCount);
    parts << QStringLiteral("%1 listening").arg(peer.listeningCount);
    if (peer.uptimeSec >= 0) {
        const qint64 hours = peer.uptimeSec / 3600;
        if (hours >= 24) {
            parts << QStringLiteral("up %1d %2h").arg(hours / 24).arg(hours % 24);
        } else {
            parts << QStringLiteral("up %1h").arg(hours);
        }
    }
    if (peer.openIncidents > 0) {
        parts << QStringLiteral("%1 open incident(s)")
                     .arg(peer.openIncidents);
    }
    return parts.join(QStringLiteral(" - "));
}

QString fleetSummaryText(const FleetStatus &status, qint64 nowMs) {
    if (!status.enabled) {
        return fleetGateText(status);
    }
    if (status.peers.isEmpty()) {
        return fleetNoPeersText();
    }
    const int online = fleetOnlineCount(status);
    const int down = fleetDownCount(status);
    int peerCount = 0;
    for (const FleetPeer &peer : status.peers) {
        if (!peer.isSelf) {
            ++peerCount;
        }
    }
    QStringList parts;
    parts << QStringLiteral("%1 peer(s) - %2 online, %3 down")
                 .arg(peerCount)
                 .arg(online)
                 .arg(down);
    double worst = fleetWorstCpu(status);
    if (worst >= 0.0) {
        QString owner;
        for (const FleetPeer &peer : status.peers) {
            if (peer.configured && peer.online
                && qFuzzyCompare(peer.cpuPercent + 1.0, worst + 1.0)) {
                owner = peakLabel(peer);
                break;
            }
        }
        parts << QStringLiteral("worst cpu %1%2")
                     .arg(QStringLiteral("%1%").arg(shortDouble(worst, 0)),
                          owner.isEmpty()
                              ? QString()
                              : QStringLiteral(" on %1").arg(owner));
    }
    double lowest = fleetLowestMemPct(status);
    if (lowest >= 0.0) {
        QString owner;
        for (const FleetPeer &peer : status.peers) {
            if (peer.configured && peer.online
                && qFuzzyCompare(memAvailablePercent(peer) + 1.0, lowest + 1.0)) {
                owner = peakLabel(peer);
                break;
            }
        }
        parts << QStringLiteral("lowest mem %1% avail%2")
                     .arg(shortDouble(lowest, 0),
                          owner.isEmpty()
                              ? QString()
                              : QStringLiteral(" on %1").arg(owner));
    }
    if (status.lastPollMs > 0) {
        const qint64 age = qMax<qint64>(0, nowMs - status.lastPollMs);
        parts << (age < 1000
                      ? QStringLiteral("polled just now")
                      : QStringLiteral("polled %1s ago").arg(age / 1000));
    }
    return parts.join(QStringLiteral(" - "));
}

QString fleetGateText(const FleetStatus &status) {
    if (!status.enabled) {
        return QStringLiteral(
            "fleet: off ([fleet] enabled=false) - this window is showing only "
            "this machine; set enabled=true to poll the peers you list");
    }
    if (status.peers.isEmpty()) {
        return fleetNoPeersText();
    }
    return QString();
}

QString fleetShareText(const FleetStatus &status) {
    if (!status.share) {
        return QStringLiteral(
            "sharing: off ([fleet] share=false) - /api/v1/snapshot is not "
            "served, so peers land on this box only when you turn it on");
    }
    if (!status.tokenConfigured) {
        return QStringLiteral(
            "sharing: %1:%2/api/v1/snapshot - loopback only, so only this "
            "machine can read it; set [metrics] token= and [metrics] bind= to "
            "let peers in")
            .arg(status.shareAddress)
            .arg(status.sharePort);
    }
    return QStringLiteral(
        "sharing: %1:%2/api/v1/snapshot - bearer token required, peers reach "
        "it with the same [metrics] token=")
        .arg(status.shareAddress)
        .arg(status.sharePort);
}

QString fleetNoteText(const FleetStatus &status) {
    if (!status.enabled) {
        return QStringLiteral(
            "polling: off ([fleet] enabled=false) - this machine still answers "
            "the peers that list it; nothing is polled from here");
    }
    int peerTotal = 0;
    for (const FleetPeer &peer : status.peers) {
        if (!peer.isSelf) {
            ++peerTotal;
        }
    }
    if (peerTotal == 0) {
        return QStringLiteral(
            "polling: no peers yet - add them to [fleet] peers= as host, "
            "host:port or name=host:port");
    }
    if (!status.tokenConfigured) {
        return fleetTokenMissingText();
    }
    return QStringLiteral(
        "%1 peer(s) polled every %2s with a %3ms budget each - the summary "
        "carries load, memory, sockets and alert counts, never your process "
        "list, command lines or environment")
        .arg(peerTotal)
        .arg(qMax(1, status.pollMs / 1000))
        .arg(status.timeoutMs);
}

QString fleetNoPeersText() {
    return QStringLiteral(
        "fleet: no peers yet - list them in [fleet] peers= as host, host:port "
        "or name=host:port, and each machine's summary shows up here");
}

QString fleetTokenMissingText() {
    return QStringLiteral(
        "fleet: set the same [metrics] token= on every machine - a peer off "
        "loopback answers only to that bearer token");
}

FleetSelfInfo fleetSelfInfo(const QString &name) {
    FleetSelfInfo info;
    info.hostname = QSysInfo::machineHostName();
    if (info.hostname.isEmpty()) {
        info.hostname = QStringLiteral("unknown-host");
    }
    info.name = name.trimmed().isEmpty() ? info.hostname : name.trimmed();
    info.version = QString::fromLatin1(SYSPEEK_APP_VERSION);
    info.platform = QSysInfo::kernelType() + QLatin1Char('/')
        + QSysInfo::currentCpuArchitecture();
    return info;
}
