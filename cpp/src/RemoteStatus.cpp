#include "RemoteStatus.h"

#include "HostInfoStatus.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>

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

// One table serves two readers: the scan refuses these keys, and the
// disclosure card prints these phrases. A new forbidden key without a phrase
// would make the card lie, so the test asserts every key has one.
struct ForbiddenKey {
    const char *key;
    const char *phrase;
};

const QList<ForbiddenKey> &forbiddenTable() {
    static const QList<ForbiddenKey> table{
        {"processes", "process list"},
        {"processlist", "process list"},
        {"cmdline", "command lines"},
        {"argv", "arguments"},
        {"args", "arguments"},
        {"environ", "environment"},
        {"env", "environment"},
        {"cwd", "working directory"},
        {"exe", "executable paths"},
        {"path", "file paths"},
        {"log", "logs"},
        {"lines", "log lines"}};
    return table;
}

const QStringList &forbiddenKeys() {
    static const QStringList keys = [] {
        QStringList out;
        for (const ForbiddenKey &entry : forbiddenTable()) {
            out << QString::fromLatin1(entry.key);
        }
        return out;
    }();
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
    return QStringLiteral("remote: configured for %1")
        .arg(base);
}

QString remoteSettingsLine(const RemoteOptions &options) {
    if (!options.enabled) {
        return QStringLiteral("off - nothing leaves this machine "
                              "([remote] enabled=false)");
    }
    QString error;
    const QString base = remoteNormalizeUrl(options.url, &error);
    if (base.isEmpty()) {
        return error.isEmpty()
                   ? QStringLiteral("off - no relay address ([remote] url=)")
                   : QStringLiteral("off - %1 ([remote] url=)").arg(error);
    }
    if (options.token.isEmpty()) {
        return QStringLiteral("%1 - no token, the relay will refuse it "
                              "([remote] token=)")
            .arg(base);
    }
    const QString spool = options.spoolMb > 0
        ? QStringLiteral("spool %1 MB").arg(options.spoolMb)
        : QStringLiteral("spool off");
    return QStringLiteral("%1 every %2 s - %3")
        .arg(base)
        .arg(options.intervalMs / 1000.0, 0, 'g', 3)
        .arg(spool);
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

// ---------------------------------------------------------------------------
// M97: transport classification
// ---------------------------------------------------------------------------

RemoteClass remoteClassify(int httpStatus) {
    if (httpStatus <= 0) {
        return RemoteClass::Transport;
    }
    if (httpStatus >= 200 && httpStatus < 300) {
        return RemoteClass::Ok;
    }
    if (httpStatus == 401 || httpStatus == 403) {
        return RemoteClass::Auth;
    }
    if (httpStatus == 404) {
        return RemoteClass::NotFound;
    }
    if (httpStatus == 429) {
        return RemoteClass::Throttled;
    }
    if (httpStatus >= 500) {
        return RemoteClass::Server;
    }
    // Any other 4xx: the relay understood the request and refused the body, so
    // the same bytes will be refused again.
    return RemoteClass::Rejected;
}

QString remoteClassName(RemoteClass value) {
    switch (value) {
    case RemoteClass::None:
        return QStringLiteral("none");
    case RemoteClass::Ok:
        return QStringLiteral("ok");
    case RemoteClass::Auth:
        return QStringLiteral("auth");
    case RemoteClass::NotFound:
        return QStringLiteral("not-found");
    case RemoteClass::Rejected:
        return QStringLiteral("rejected");
    case RemoteClass::Throttled:
        return QStringLiteral("throttled");
    case RemoteClass::Server:
        return QStringLiteral("server");
    case RemoteClass::Transport:
        return QStringLiteral("transport");
    }
    return QStringLiteral("none");
}

QString remoteClassText(RemoteClass value, int httpStatus) {
    switch (value) {
    case RemoteClass::None:
        return QStringLiteral("no report sent yet");
    case RemoteClass::Ok:
        return QStringLiteral("ok");
    case RemoteClass::Auth:
        return QStringLiteral("%1 - the relay refused this token (check "
                              "[remote] token= and that it was not revoked)")
            .arg(httpStatus);
    case RemoteClass::NotFound:
        return QStringLiteral("%1 - no ingest endpoint at that address (check "
                              "[remote] url=)")
            .arg(httpStatus);
    case RemoteClass::Rejected:
        return QStringLiteral("%1 - the relay refused this payload (413 means "
                              "it is too large, 422 means a field was rejected)")
            .arg(httpStatus);
    case RemoteClass::Throttled:
        return QStringLiteral("%1 - the relay asked us to slow down")
            .arg(httpStatus);
    case RemoteClass::Server:
        return QStringLiteral("%1 - the relay failed the request")
            .arg(httpStatus);
    case RemoteClass::Transport:
        return httpStatus > 0
            ? QStringLiteral("HTTP %1").arg(httpStatus)
            : QStringLiteral("no answer from the relay");
    }
    return QStringLiteral("no report sent yet");
}

bool remoteClassIsTerminal(RemoteClass value) {
    return value == RemoteClass::Auth || value == RemoteClass::NotFound
        || value == RemoteClass::Rejected;
}

int remoteBackoffMs(int consecutiveFailures, int intervalMs) {
    const int cap = std::clamp(intervalMs, kRemoteIntervalMinMs,
                               kRemoteIntervalMaxMs);
    if (consecutiveFailures <= 0) {
        return cap;
    }
    const int shift = std::min(consecutiveFailures - 1, 20);
    const qint64 ladder = qint64(2000) << shift;
    return int(std::clamp<qint64>(ladder, 2000, cap));
}

qint64 remoteRetryAfterMs(const QByteArray &headerBlock) {
    const QList<QByteArray> lines = headerBlock.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        const int colon = trimmed.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        if (trimmed.left(colon).trimmed().toLower()
            != QByteArrayLiteral("retry-after")) {
            continue;
        }
        bool ok = false;
        const qint64 seconds = trimmed.mid(colon + 1).trimmed().toLongLong(&ok);
        if (!ok || seconds < 0) {
            return -1;
        }
        return qMin<qint64>(seconds * 1000, qint64(kRemoteIntervalMaxMs));
    }
    return -1;
}

QString remoteReportNote(const RemoteReportState &state) {
    // The spool line is appended, never folded into the verdict: "it was
    // refused" and "there are still reports waiting" are two separate facts.
    const QString held = remoteSpoolNoteSuffix(state);
    if (state.stopped) {
        return QStringLiteral("remote: stopped - %1%2")
            .arg(remoteClassText(state.lastClass, state.lastStatus), held);
    }
    if (state.lastClass == RemoteClass::None) {
        if (state.spooled > 0) {
            // The count is already the sentence, so only the loss is added -
            // "2 report(s) held, 2 spooled" would say the same thing twice.
            return QStringLiteral("remote: %1 report(s) held for delivery")
                       .arg(state.spooled)
                   + (state.spoolDropped > 0
                          ? QStringLiteral(", %1 dropped")
                                .arg(state.spoolDropped)
                          : QString());
        }
        return QStringLiteral("remote: nothing sent yet");
    }
    QString line = QStringLiteral("remote: %1 sent, %2 failed - last %3 in "
                                  "%4ms")
                       .arg(state.sent)
                       .arg(state.failed)
                       .arg(remoteClassText(state.lastClass, state.lastStatus))
                       .arg(state.lastLatencyMs);
    if (state.nextRetryMs > 0) {
        line += QStringLiteral(", next attempt in %1s")
                    .arg(state.nextRetryMs / 1000.0, 0, 'f', 1);
    }
    return line + held;
}

QString remoteSpoolNoteSuffix(const RemoteReportState &state) {
    if (!state.spoolOn) {
        return QString();
    }
    QString suffix;
    if (state.spooled > 0) {
        suffix += QStringLiteral(", %1 spooled").arg(state.spooled);
    }
    if (state.spoolDropped > 0) {
        suffix += QStringLiteral(", %1 dropped").arg(state.spoolDropped);
    }
    return suffix;
}

// The same verdict without its "remote: " prefix, which is the Settings row's
// own key: a value that repeats the label is a wasted line.
QString remoteRowText(const RemoteReportState &state) {
    const QString note = remoteReportNote(state);
    return note.startsWith(QStringLiteral("remote: ")) ? note.mid(8) : note;
}

QStringList remoteForbiddenKeys() {
    return forbiddenKeys();
}

QString remoteNeverSentLine() {
    QStringList phrases;
    for (const ForbiddenKey &entry : forbiddenTable()) {
        const QString phrase = QString::fromLatin1(entry.phrase);
        if (!phrases.contains(phrase)) {
            phrases << phrase;
        }
    }
    return QStringLiteral("never sent: ") + phrases.join(QStringLiteral(", "));
}

QList<RemoteFieldGroup> remoteFieldGroups() {
    return {
        {QStringLiteral("host cpu, steal, load, memory, disk free, network rate"),
         true},
        {QStringLiteral("socket counts (connections, listening, established)"),
         true},
        {QStringLiteral("alert and incident counts"), true},
        {QStringLiteral("container and kubernetes counts"), true},
        {QStringLiteral("focused app name and pid"), true},
        {QStringLiteral("version, platform and machine name"), true},
        {QStringLiteral("uptime"), true},
        {QStringLiteral("events (kind, severity, subject, title, up to 50)"),
         true},
        {QStringLiteral("process list, command lines, environment, file paths, "
                        "logs"),
         false},
    };
}

RemoteInfo remoteInfoFor(const RemoteOptions &options,
                         const RemoteReportState &state, qint64 nowMs,
                         const RemoteIdentity &identity) {
    Q_UNUSED(nowMs)
    RemoteInfo info;
    info.enabled = options.enabled;
    info.neverSentLine = remoteNeverSentLine();
    // The Settings row and the card never disagree: with the switch off the row
    // keeps naming the switch, with it on the row is the live verdict.
    info.rowText = options.enabled ? remoteRowText(state)
                                   : remoteSettingsLine(options);
    for (const RemoteFieldGroup &group : remoteFieldGroups()) {
        info.scopeLines << (group.sent ? QStringLiteral("+ ") : QStringLiteral("- "))
                               + group.name;
    }

    // Nothing about the address or the token is shown while the switch is off:
    // a card that lists a relay for a feature that is not running is a claim
    // the machine is not keeping.
    if (!options.enabled) {
        info.tokenLine = QStringLiteral("not set - nothing to send");
        info.spoolLine =
            QStringLiteral("none - the spool only opens while reporting is on");
        info.lastLine = QStringLiteral("never");
        return info;
    }

    info.ingestUrl = remoteIngestUrl(options.url);
    if (options.token.isEmpty()) {
        info.tokenLine = QStringLiteral("not set - the relay will refuse every "
                                        "report ([remote] token=)");
    } else {
        info.tokenLine = QStringLiteral("set (%1 chars, never printed)")
                             .arg(options.token.size());
    }

    if (options.spoolMb <= 0) {
        info.spoolLine = QStringLiteral("off ([remote] spool_mb=0) - an "
                                        "undelivered report is dropped and "
                                        "counted");
    } else if (!state.spoolOn) {
        info.spoolLine = QStringLiteral("%1 - not open yet").arg(remoteSpoolPath());
    } else {
        info.spoolLine = QStringLiteral("%1 - %2 queued of %3, %4 dropped")
                             .arg(remoteSpoolPath())
                             .arg(state.spooled)
                             .arg(state.spoolCapacity)
                             .arg(state.spoolDropped);
    }

    if (!identity.note.isEmpty()) {
        info.identityLine = identity.note;
    } else {
        const QString machineId = options.machineId.trimmed();
        if (machineId.isEmpty()) {
            info.identityLine = remoteIdentityAt(remoteMachineIdPath(), options,
                                                 false)
                                    .note;
        } else {
            const QString name = options.name.trimmed().isEmpty()
                ? remoteShortHostname()
                : options.name.trimmed();
            info.identityLine = QStringLiteral("%1 as %2").arg(machineId, name);
        }
    }

    if (state.stopped) {
        info.lastLine = QStringLiteral("refused: %1")
                            .arg(remoteClassText(state.lastClass,
                                                 state.lastStatus));
    } else if (state.lastClass == RemoteClass::None) {
        info.lastLine =
            state.spooled > 0
                ? QStringLiteral("waiting - %1 report(s) held").arg(state.spooled)
                : QStringLiteral("never");
    } else if (state.lastClass == RemoteClass::Ok) {
        info.lastLine = QStringLiteral("delivered %1 ms at %2")
                            .arg(state.lastLatencyMs)
                            .arg(QDateTime::fromMSecsSinceEpoch(state.lastOkMs)
                                     .toString(QStringLiteral("HH:mm:ss")));
    } else {
        info.lastLine = QStringLiteral("not delivered: %1")
                            .arg(remoteClassText(state.lastClass,
                                                 state.lastStatus));
    }
    return info;
}

QString remoteReportCardText(const RemoteInfo &info) {
    if (!info.enabled) {
        return QStringLiteral("state       off - nothing leaves this machine "
                              "([remote] enabled=false)\n"
                              "token       %1\n"
                              "spool       %2\n"
                              "last        %3\n"
                              "%4")
            .arg(info.tokenLine, info.spoolLine, info.lastLine,
                 info.neverSentLine);
    }
    QStringList lines;
    lines << QStringLiteral("state       on -> %1").arg(info.ingestUrl);
    lines << QStringLiteral("token       %1").arg(info.tokenLine);
    lines << QStringLiteral("identity    %1").arg(info.identityLine);
    lines << QStringLiteral("spool       %1").arg(info.spoolLine);
    lines << QStringLiteral("last        %1").arg(info.lastLine);
    for (int i = 0; i < info.scopeLines.size(); ++i) {
        lines << QStringLiteral("%1%2")
                     .arg(i == 0 ? QStringLiteral("fields      ")
                                 : QStringLiteral("            "),
                          info.scopeLines.at(i));
    }
    lines << info.neverSentLine;
    return lines.join(QLatin1Char('\n'));
}

QString remoteMachineIdPath() {
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath() + QStringLiteral("/syspeek");
    }
    return base + QStringLiteral("/machine-id");
}

QString remoteShortHostname() {
    QString host = QSysInfo::machineHostName().trimmed();
    if (host.isEmpty()) {
        host = QStringLiteral("machine");
    }
    const int dot = host.indexOf(QLatin1Char('.'));
    if (dot > 0) {
        host = host.left(dot);
    }
    return host;
}

QString remotePrimaryMac() {
    // The first interface by name that actually has a hardware address: the
    // loopback and the virtual interfaces come with an empty or all-zero MAC,
    // and the sort makes the choice stable across a reboot.
    HostInfo info = collectHostInfo();
    QList<HostNetworkInfo> interfaces = info.interfaces;
    std::sort(interfaces.begin(), interfaces.end(),
              [](const HostNetworkInfo &a, const HostNetworkInfo &b) {
                  return a.name < b.name;
              });
    for (const HostNetworkInfo &entry : interfaces) {
        const QString mac = entry.macAddress.trimmed().toLower();
        if (mac.isEmpty() || mac == QStringLiteral("00:00:00:00:00:00")
            || entry.name == QStringLiteral("lo")) {
            continue;
        }
        return mac;
    }
    return QString();
}

QString remoteDerivedMachineId(const QString &hostname, const QString &mac) {
    QString host = hostname.trimmed().toLower();
    if (host.isEmpty()) {
        host = QStringLiteral("machine");
    }
    QString safe;
    for (const QChar ch : host) {
        safe += (ch.isLetterOrNumber() || ch == QLatin1Char('-')
                 || ch == QLatin1Char('_') || ch == QLatin1Char('.'))
                    ? ch
                    : QLatin1Char('-');
    }
    const QByteArray digest = QCryptographicHash::hash(
        (host + QLatin1Char('|') + mac.toLower()).toUtf8(),
        QCryptographicHash::Sha256);
    return safe + QLatin1Char('-')
           + QString::fromLatin1(digest.toHex().left(12));
}

bool remoteMachineIdLooksValid(const QString &id) {
    if (id.size() < 8 || id.size() > 64) {
        return false;
    }
    for (const QChar ch : id) {
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('-')
              || ch == QLatin1Char('_') || ch == QLatin1Char('.'))) {
            return false;
        }
    }
    return true;
}

RemoteIdentity remoteIdentityAt(const QString &path,
                                const RemoteOptions &options, bool create) {
    RemoteIdentity identity;
    identity.path = path;
    identity.name = options.name.trimmed().isEmpty() ? remoteShortHostname()
                                                     : options.name.trimmed();

    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QString stored = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
        if (remoteMachineIdLooksValid(stored)) {
            identity.id = stored;
            identity.note =
                QStringLiteral("%1 (name %2)").arg(stored, identity.name);
            return identity;
        }
    }
    if (!create) {
        identity.note = QStringLiteral(
            "not created yet - written on the first run with [remote] "
            "enabled=true");
        return identity;
    }

    // QSaveFile writes a temp file and renames it, so a crash mid-write cannot
    // leave half an id behind - the same reason every config write in this tree
    // uses it.
    const QDir dir = QFileInfo(path).dir();
    if (dir.exists() || dir.mkpath(QStringLiteral("."))) {
        const QString fresh = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSaveFile out(path);
        bool ok = out.open(QIODevice::WriteOnly);
        if (ok) {
            ok = out.write(fresh.toUtf8() + '\n') > 0 && out.commit();
        }
        if (ok) {
            QFile::setPermissions(path, QFileDevice::ReadOwner
                                            | QFileDevice::WriteOwner);
            identity.id = fresh;
            identity.created = true;
            identity.note = QStringLiteral("%1 (name %2, new file at %3)")
                                .arg(fresh, identity.name, path);
            return identity;
        }
    }

    identity.derived = true;
    identity.id =
        remoteDerivedMachineId(remoteShortHostname(), remotePrimaryMac());
    identity.note = QStringLiteral("derived - %1 (the id file is not writable "
                                   "at %2)")
                        .arg(identity.id, path);
    return identity;
}

RemoteIdentity remoteLoadIdentity(const RemoteOptions &options) {
    return remoteIdentityAt(remoteMachineIdPath(), options, options.enabled);
}


