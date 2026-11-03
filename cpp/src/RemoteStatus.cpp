#include "RemoteStatus.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStandardPaths>

#include <algorithm>

namespace {

QString refuse(QString *error, const QString &reason) {
    if (error != nullptr) {
        *error = reason;
    }
    return QString();
}

QJsonValue numberOrNull(double value, bool valid = true) {
    return valid ? QJsonValue(value) : QJsonValue();
}

QJsonValue countOrNull(int value) {
    return value < 0 ? QJsonValue() : QJsonValue(value);
}

double readNumber(const QJsonObject &object, const QString &key,
                  double fallback) {
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toDouble() : fallback;
}

int readCount(const QJsonObject &object, const QString &key) {
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toInt() : -1;
}

qint64 readInt64(const QJsonObject &object, const QString &key) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return -1;
    }
    return qint64(value.toDouble());
}

QString severityFromName(const QString &name) {
    const QString lower = name.toLower();
    if (lower == QLatin1String("error")) {
        return QStringLiteral("error");
    }
    if (lower == QLatin1String("warn") || lower == QLatin1String("warning")) {
        return QStringLiteral("warn");
    }
    if (lower == QLatin1String("ok")) {
        return QStringLiteral("ok");
    }
    return QStringLiteral("info");
}

const QStringList &forbiddenKeys() {
    static const QStringList keys{
        QStringLiteral("cmdline"),  QStringLiteral("environ"),
        QStringLiteral("env"),      QStringLiteral("exe"),
        QStringLiteral("path"),     QStringLiteral("argv"),
        QStringLiteral("cwd"),      QStringLiteral("args"),
        QStringLiteral("log"),      QStringLiteral("lines"),
        QStringLiteral("processes"), QStringLiteral("processlist")};
    return keys;
}

QString scanForbidden(const QJsonValue &value) {
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (forbiddenKeys().contains(it.key().toLower())) {
                return it.key();
            }
            const QString nested = scanForbidden(it.value());
            if (!nested.isEmpty()) {
                return nested;
            }
        }
        return QString();
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &entry : array) {
            const QString nested = scanForbidden(entry);
            if (!nested.isEmpty()) {
                return nested;
            }
        }
    }
    return QString();
}

} // namespace

void remoteValidate(RemoteOptions &options) {
    options.intervalMs = std::clamp(options.intervalMs, kRemoteIntervalMinMs,
                                    kRemoteIntervalMaxMs);
    options.timeoutMs = std::clamp(options.timeoutMs, kRemoteTimeoutMinMs,
                                   kRemoteTimeoutMaxMs);
    options.spoolMb =
        std::clamp(options.spoolMb, kRemoteSpoolMinMb, kRemoteSpoolMaxMb);
}

QString remoteNormalizeUrl(const QString &spec, QString *error) {
    if (error != nullptr) {
        error->clear();
    }

    QString rest = spec.trimmed();
    if (rest.isEmpty()) {
        return refuse(error, QStringLiteral("empty relay url"));
    }

    QString scheme = QStringLiteral("https");
    const QString separator = QStringLiteral("://");
    const int schemeEnd = rest.indexOf(separator);
    if (schemeEnd >= 0) {
        const QString declared = rest.left(schemeEnd).toLower();
        if (declared != QLatin1String("http") && declared != QLatin1String("https")) {
            return refuse(error, QStringLiteral("%1 is not http or https")
                                     .arg(declared + separator));
        }
        scheme = declared;
        rest = rest.mid(schemeEnd + separator.size());
    }

    if (rest.contains(QLatin1Char('?'))) {
        return refuse(error,
                      QStringLiteral("a query string is not part of a relay address"));
    }
    if (rest.contains(QLatin1Char('#'))) {
        return refuse(error,
                      QStringLiteral("a fragment is not part of a relay address"));
    }
    if (rest.contains(QLatin1Char('@'))) {
        return refuse(error,
                      QStringLiteral("credentials in the url are refused - use "
                                     "[remote] token="));
    }
    if (rest.contains(QLatin1Char(' ')) || rest.contains(QLatin1Char('\t'))) {
        return refuse(error, QStringLiteral("whitespace inside the relay url"));
    }

    QString path;
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        path = rest.mid(slash);
        rest = rest.left(slash);
    }
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    if (path == QLatin1String("/")) {
        path.clear();
    }

    QString host;
    QString port;
    if (rest.startsWith(QLatin1Char('['))) {
        const int close = rest.indexOf(QLatin1Char(']'));
        if (close < 0) {
            return refuse(error, QStringLiteral("unterminated IPv6 literal"));
        }
        host = rest.left(close + 1);
        const QString tail = rest.mid(close + 1);
        if (!tail.isEmpty()) {
            if (!tail.startsWith(QLatin1Char(':'))) {
                return refuse(error,
                              QStringLiteral("unexpected text after the IPv6 "
                                             "literal"));
            }
            port = tail.mid(1);
        }
    } else {
        const int colon = rest.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0) {
            host = rest.left(colon);
            port = rest.mid(colon + 1);
            if (host.contains(QLatin1Char(':'))) {
                return refuse(error,
                              QStringLiteral("IPv6 literals need brackets"));
            }
        } else {
            host = rest;
        }
    }

    if (host.isEmpty()) {
        return refuse(error, QStringLiteral("no host in the relay url"));
    }
    if (host == QLatin1String("[]")) {
        return refuse(error, QStringLiteral("no host in the relay url"));
    }

    if (!port.isEmpty()) {
        bool ok = false;
        const int parsed = port.toInt(&ok);
        if (!ok || parsed < 1 || parsed > 65535) {
            return refuse(error, QStringLiteral("port %1 is not 1..65535").arg(port));
        }
        host += QLatin1Char(':') + QString::number(parsed);
    } else if (rest.endsWith(QLatin1Char(':'))) {
        return refuse(error, QStringLiteral("port is empty"));
    }

    return scheme + separator + host + path;
}

QString remoteIngestUrl(const QString &base, QString *error) {
    const QString normalized = remoteNormalizeUrl(base, error);
    if (normalized.isEmpty()) {
        return QString();
    }
    const QString ingest = QStringLiteral("/api/v1/ingest");
    if (normalized.endsWith(ingest)) {
        return normalized;
    }
    return normalized + ingest;
}

bool remoteConfigUsable(const RemoteOptions &options) {
    if (!options.enabled || options.token.isEmpty()) {
        return false;
    }
    return !remoteNormalizeUrl(options.url, nullptr).isEmpty();
}

QString remoteSpoolPath() {
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath() + QStringLiteral("/syspeek");
    }
    return base + QStringLiteral("/remote-spool.bin");
}

QString remoteConfigNote(const RemoteOptions &options) {
    if (!options.enabled) {
        return QStringLiteral("remote: off - nothing leaves this machine");
    }
    QString error;
    const QString base = remoteNormalizeUrl(options.url, &error);
    if (base.isEmpty()) {
        return error.isEmpty()
                   ? QStringLiteral("remote: no relay address - set [remote] url=")
                   : QStringLiteral("remote: %1 (set [remote] url=)").arg(error);
    }
    if (options.token.isEmpty()) {
        return QStringLiteral("remote: configured for %1 but no token - the relay "
                              "will refuse it (set [remote] token=)")
            .arg(base);
    }
    return QStringLiteral("remote: configured for %1 - transport not started in "
                          "this build")
        .arg(base);
}

// ---------------------------------------------------------------------------
// M96: the envelope
// ---------------------------------------------------------------------------

QString remoteSampleId(const QString &machineId, qint64 sentAtMs) {
    const QString prefix =
        machineId.isEmpty() ? QStringLiteral("machine") : machineId;
    return prefix + QLatin1Char('-') + QString::number(sentAtMs);
}

RemoteMachine remoteMachine(const RemoteOptions &options, const QString &version,
                            const QString &platform, qint64 sentAtMs) {
    RemoteMachine machine;
    machine.id = options.machineId;
    machine.name = options.name;
    machine.version = version;
    machine.platform = platform;
    machine.sentAtMs = sentAtMs;
    return machine;
}

RemoteSample remoteBuildSample(const DisplaySnapshot &snapshot, qint64 uptimeSec,
                               const RemoteCounts &counts) {
    RemoteSample sample;
    if (!snapshot.appName.isEmpty() && snapshot.appName != QLatin1String("--")) {
        sample.appPresent = true;
        sample.appName = snapshot.appName;
        bool ok = false;
        const int pid = snapshot.pid.toInt(&ok);
        if (ok) {
            sample.appPid = pid;
        }
        if (snapshot.cpuValid) {
            sample.appCpu = snapshot.cpuPercent;
        }
    }

    sample.hostReady = snapshot.hostAvailable && !snapshot.hostWarming;
    if (sample.hostReady) {
        sample.hostCpu = snapshot.hostCpuPercent;
        sample.hostSteal = snapshot.hostStealPercent;
        sample.hostLoad1 = snapshot.hostLoad1;
    }
    if (snapshot.hostMemTotalKb > 0) {
        sample.memTotalKb = qint64(snapshot.hostMemTotalKb);
        sample.memAvailableKb = qint64(snapshot.hostMemAvailableKb);
    }
    if (snapshot.hostDiskTotalBytes > 0) {
        sample.diskTotalBytes = qint64(snapshot.hostDiskTotalBytes);
        sample.diskFreeBytes = qint64(snapshot.hostDiskFreeBytes);
    }
    sample.netRxPerSec = snapshot.hostNetRxPerSec;
    sample.netTxPerSec = snapshot.hostNetTxPerSec;

    sample.connections = snapshot.connections.size();
    sample.listening = snapshot.listeningPorts.size();
    int established = 0;
    for (const ConnectionRow &row : snapshot.connections) {
        if (row.state.contains(QStringLiteral("ESTABLISHED"))) {
            ++established;
        }
    }
    sample.established = established;

    if (snapshot.alerts.pushed) {
        sample.incidentsOpen = snapshot.alerts.openIncidents;
        sample.incidentsFlapping = snapshot.alerts.flappingIncidents;
        sample.incidentsUnowned = snapshot.alerts.unownedOpen;
    }

    sample.containersRunning = counts.containersRunning;
    sample.containersIssues = counts.containersIssues;
    sample.kubeNodes = counts.kubeNodes;
    sample.kubePods = counts.kubePods;
    sample.uptimeSec = uptimeSec;
    return sample;
}

QList<RemoteEvent> remoteBuildEvents(const QList<TimelineEvent> &ring,
                                     qint64 sinceMs, qint64 nowMs,
                                     const QString &machineId) {
    QList<TimelineEvent> picked;
    for (const TimelineEvent &event : ring) {
        if (event.atMs < sinceMs || event.atMs > nowMs) {
            continue;
        }
        // A file path is exactly what the paid tier promises never to carry, and
        // a FIM event is nothing but a path. It stays local; the console still
        // lists it, and the relay learns about it from the silence/health rules.
        if (event.kind == TimelineKind::FileChange) {
            continue;
        }
        picked.append(event);
    }

    std::stable_sort(picked.begin(), picked.end(),
                     [](const TimelineEvent &a, const TimelineEvent &b) {
                         return a.atMs < b.atMs;
                     });

    const int excess = picked.size() - kRemoteMaxEvents;
    if (excess > 0) {
        picked = picked.mid(excess);
    }

    QList<RemoteEvent> events;
    events.reserve(picked.size());
    for (const TimelineEvent &event : picked) {
        RemoteEvent wire;
        wire.atMs = event.atMs;
        wire.kind = timelineKindName(event.kind);
        wire.severity = severityFromName(timelineSeverityName(event.severity));
        wire.subject = event.subject;
        wire.title = event.title;
        // Detail is never sent: it carries free text the privacy check cannot
        // classify, and the relay has no use for it.
        wire.tags = event.tags;
        const QByteArray seed =
            (machineId + QLatin1Char('|') + QString::number(event.atMs)
             + QLatin1Char('|') + wire.kind + QLatin1Char('|') + wire.severity
             + QLatin1Char('|') + wire.subject + QLatin1Char('|') + wire.title)
                .toUtf8();
        wire.id = QString::fromLatin1(
            QCryptographicHash::hash(seed, QCryptographicHash::Md5).toHex().left(16));
        events.append(wire);
    }
    return events;
}

RemoteEnvelope remoteBuildEnvelope(const RemoteMachine &machine,
                                   const DisplaySnapshot &snapshot,
                                   const QList<TimelineEvent> &ring,
                                   qint64 sinceMs, const RemoteCounts &counts,
                                   qint64 uptimeSec) {
    RemoteEnvelope envelope;
    envelope.schema = kRemoteSchemaVersion;
    envelope.machine = machine;
    envelope.sample = remoteBuildSample(snapshot, uptimeSec, counts);
    envelope.sample.id = remoteSampleId(machine.id, machine.sentAtMs);
    envelope.events = remoteBuildEvents(ring, sinceMs, machine.sentAtMs,
                                        machine.id);
    return envelope;
}

QJsonObject remoteEnvelopeObject(const RemoteEnvelope &envelope) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), envelope.schema);

    QJsonObject machine;
    machine.insert(QStringLiteral("id"), envelope.machine.id);
    machine.insert(QStringLiteral("name"), envelope.machine.name);
    machine.insert(QStringLiteral("version"), envelope.machine.version);
    machine.insert(QStringLiteral("platform"), envelope.machine.platform);
    machine.insert(QStringLiteral("sentAtMs"),
                   double(envelope.machine.sentAtMs));
    root.insert(QStringLiteral("machine"), machine);

    const RemoteSample &sample = envelope.sample;
    QJsonObject wire;
    wire.insert(QStringLiteral("id"), sample.id);

    QJsonObject app;
    app.insert(QStringLiteral("name"), sample.appName);
    app.insert(QStringLiteral("pid"), countOrNull(sample.appPid));
    app.insert(QStringLiteral("cpu"), numberOrNull(sample.appCpu,
                                                   sample.appCpu >= 0.0));
    wire.insert(QStringLiteral("app"), app);

    QJsonObject host;
    host.insert(QStringLiteral("cpu"),
                numberOrNull(sample.hostCpu, sample.hostReady));
    host.insert(QStringLiteral("steal"),
                numberOrNull(sample.hostSteal, sample.hostReady));
    host.insert(QStringLiteral("load1"),
                numberOrNull(sample.hostLoad1, sample.hostReady));
    host.insert(QStringLiteral("memTotalKb"),
                numberOrNull(double(sample.memTotalKb), sample.memTotalKb >= 0));
    host.insert(QStringLiteral("memAvailableKb"),
                numberOrNull(double(sample.memAvailableKb),
                             sample.memAvailableKb >= 0));
    host.insert(QStringLiteral("diskTotalBytes"),
                numberOrNull(double(sample.diskTotalBytes),
                             sample.diskTotalBytes >= 0));
    host.insert(QStringLiteral("diskFreeBytes"),
                numberOrNull(double(sample.diskFreeBytes),
                             sample.diskFreeBytes >= 0));
    host.insert(QStringLiteral("netRxPerSec"), sample.netRxPerSec);
    host.insert(QStringLiteral("netTxPerSec"), sample.netTxPerSec);
    wire.insert(QStringLiteral("host"), host);

    QJsonObject sockets;
    sockets.insert(QStringLiteral("connections"),
                   countOrNull(sample.connections));
    sockets.insert(QStringLiteral("listening"), countOrNull(sample.listening));
    sockets.insert(QStringLiteral("established"),
                   countOrNull(sample.established));
    wire.insert(QStringLiteral("sockets"), sockets);

    QJsonObject alerts;
    alerts.insert(QStringLiteral("open"), countOrNull(sample.incidentsOpen));
    alerts.insert(QStringLiteral("flapping"),
                  countOrNull(sample.incidentsFlapping));
    alerts.insert(QStringLiteral("unowned"),
                  countOrNull(sample.incidentsUnowned));
    wire.insert(QStringLiteral("alerts"), alerts);

    QJsonObject containers;
    containers.insert(QStringLiteral("running"),
                      countOrNull(sample.containersRunning));
    containers.insert(QStringLiteral("issues"),
                      countOrNull(sample.containersIssues));
    wire.insert(QStringLiteral("containers"), containers);

    QJsonObject kube;
    kube.insert(QStringLiteral("nodes"), countOrNull(sample.kubeNodes));
    kube.insert(QStringLiteral("pods"), countOrNull(sample.kubePods));
    wire.insert(QStringLiteral("kube"), kube);

    wire.insert(QStringLiteral("uptimeSec"),
                numberOrNull(double(sample.uptimeSec), sample.uptimeSec >= 0));
    root.insert(QStringLiteral("sample"), wire);

    QJsonArray events;
    for (const RemoteEvent &event : envelope.events) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), event.id);
        entry.insert(QStringLiteral("atMs"), double(event.atMs));
        entry.insert(QStringLiteral("kind"), event.kind);
        entry.insert(QStringLiteral("severity"), event.severity);
        entry.insert(QStringLiteral("subject"), event.subject);
        entry.insert(QStringLiteral("title"), event.title);
        QJsonArray tags;
        for (const QString &tag : event.tags) {
            tags.append(tag);
        }
        entry.insert(QStringLiteral("tags"), tags);
        events.append(entry);
    }
    root.insert(QStringLiteral("events"), events);
    return root;
}

QByteArray remoteEnvelopeJson(const RemoteEnvelope &envelope) {
    return QJsonDocument(remoteEnvelopeObject(envelope)).toJson(
        QJsonDocument::Compact);
}

bool parseRemoteEnvelopeJson(const QByteArray &json, RemoteEnvelope *out,
                             QString *error) {
    if (error != nullptr) {
        error->clear();
    }
    if (out == nullptr) {
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("not JSON: %1").arg(parseError.errorString());
        }
        return false;
    }
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("envelope is not a JSON object");
        }
        return false;
    }
    const QJsonObject root = document.object();

    const QJsonValue schemaValue = root.value(QStringLiteral("schema"));
    if (!schemaValue.isDouble()) {
        if (error != nullptr) {
            *error = QStringLiteral("no schema field - this is not a Syspeek "
                                    "envelope");
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
    if (schema > kRemoteSchemaVersion) {
        if (error != nullptr) {
            *error = QStringLiteral("envelope runs schema %1, this build reads "
                                    "up to %2 - update Syspeek to read it")
                         .arg(schema)
                         .arg(kRemoteSchemaVersion);
        }
        return false;
    }

    const QJsonValue machineValue = root.value(QStringLiteral("machine"));
    if (!machineValue.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("no machine object");
        }
        return false;
    }
    const QJsonObject machineObject = machineValue.toObject();
    RemoteEnvelope envelope;
    envelope.schema = schema;
    envelope.machine.id = machineObject.value(QStringLiteral("id")).toString();
    if (envelope.machine.id.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("machine.id is empty - every report needs a "
                                    "stable machine id");
        }
        return false;
    }
    envelope.machine.name =
        machineObject.value(QStringLiteral("name")).toString();
    envelope.machine.version =
        machineObject.value(QStringLiteral("version")).toString();
    envelope.machine.platform =
        machineObject.value(QStringLiteral("platform")).toString();
    envelope.machine.sentAtMs =
        readInt64(machineObject, QStringLiteral("sentAtMs"));

    const QJsonValue sampleValue = root.value(QStringLiteral("sample"));
    if (!sampleValue.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("no sample object");
        }
        return false;
    }
    const QJsonObject sampleObject = sampleValue.toObject();
    RemoteSample &sample = envelope.sample;
    sample.id = sampleObject.value(QStringLiteral("id")).toString();

    const QJsonObject app = sampleObject.value(QStringLiteral("app")).toObject();
    sample.appName = app.value(QStringLiteral("name")).toString();
    sample.appPresent = !sample.appName.isEmpty();
    sample.appPid = readCount(app, QStringLiteral("pid"));
    sample.appCpu = readNumber(app, QStringLiteral("cpu"), -1.0);

    const QJsonObject host = sampleObject.value(QStringLiteral("host")).toObject();
    sample.hostCpu = readNumber(host, QStringLiteral("cpu"), -1.0);
    sample.hostSteal = readNumber(host, QStringLiteral("steal"), -1.0);
    sample.hostLoad1 = readNumber(host, QStringLiteral("load1"), -1.0);
    sample.hostReady = sample.hostCpu >= 0.0;
    sample.memTotalKb = readInt64(host, QStringLiteral("memTotalKb"));
    sample.memAvailableKb = readInt64(host, QStringLiteral("memAvailableKb"));
    sample.diskTotalBytes = readInt64(host, QStringLiteral("diskTotalBytes"));
    sample.diskFreeBytes = readInt64(host, QStringLiteral("diskFreeBytes"));
    sample.netRxPerSec = readNumber(host, QStringLiteral("netRxPerSec"), -1.0);
    sample.netTxPerSec = readNumber(host, QStringLiteral("netTxPerSec"), -1.0);

    const QJsonObject sockets =
        sampleObject.value(QStringLiteral("sockets")).toObject();
    sample.connections = readCount(sockets, QStringLiteral("connections"));
    sample.listening = readCount(sockets, QStringLiteral("listening"));
    sample.established = readCount(sockets, QStringLiteral("established"));

    const QJsonObject alerts =
        sampleObject.value(QStringLiteral("alerts")).toObject();
    sample.incidentsOpen = readCount(alerts, QStringLiteral("open"));
    sample.incidentsFlapping = readCount(alerts, QStringLiteral("flapping"));
    sample.incidentsUnowned = readCount(alerts, QStringLiteral("unowned"));

    const QJsonObject containers =
        sampleObject.value(QStringLiteral("containers")).toObject();
    sample.containersRunning = readCount(containers, QStringLiteral("running"));
    sample.containersIssues = readCount(containers, QStringLiteral("issues"));

    const QJsonObject kube = sampleObject.value(QStringLiteral("kube")).toObject();
    sample.kubeNodes = readCount(kube, QStringLiteral("nodes"));
    sample.kubePods = readCount(kube, QStringLiteral("pods"));
    sample.uptimeSec = readInt64(sampleObject, QStringLiteral("uptimeSec"));

    const QJsonValue eventsValue = root.value(QStringLiteral("events"));
    if (!eventsValue.isUndefined() && !eventsValue.isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("events is not an array");
        }
        return false;
    }
    const QJsonArray events = eventsValue.toArray();
    for (const QJsonValue &value : events) {
        if (!value.isObject()) {
            if (error != nullptr) {
                *error = QStringLiteral("an event is not an object");
            }
            return false;
        }
        const QJsonObject entry = value.toObject();
        RemoteEvent event;
        event.id = entry.value(QStringLiteral("id")).toString();
        event.atMs = readInt64(entry, QStringLiteral("atMs"));
        event.kind = entry.value(QStringLiteral("kind")).toString();
        event.severity = severityFromName(
            entry.value(QStringLiteral("severity")).toString());
        event.subject = entry.value(QStringLiteral("subject")).toString();
        event.title = entry.value(QStringLiteral("title")).toString();
        const QJsonArray tags = entry.value(QStringLiteral("tags")).toArray();
        for (const QJsonValue &tag : tags) {
            event.tags.append(tag.toString());
        }
        envelope.events.append(event);
    }

    *out = envelope;
    return true;
}

QString remotePrivacyOffenders(const QJsonObject &root) {
    return scanForbidden(root);
}

