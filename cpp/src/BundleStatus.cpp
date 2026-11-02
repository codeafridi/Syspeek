#include "BundleStatus.h"

#include "QtCompat.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

#include <algorithm>

namespace {

QJsonObject hostBlock(const DisplaySnapshot &s) {
    QJsonObject host;
    host.insert(QStringLiteral("available"), s.hostAvailable);
    host.insert(QStringLiteral("warming"), s.hostWarming);
    host.insert(QStringLiteral("cpuPercent"), s.hostCpuPercent);
    host.insert(QStringLiteral("stealPercent"), s.hostStealPercent);
    host.insert(QStringLiteral("memTotalKb"),
                static_cast<double>(s.hostMemTotalKb));
    host.insert(QStringLiteral("memAvailableKb"),
                static_cast<double>(s.hostMemAvailableKb));
    host.insert(QStringLiteral("diskTotalBytes"),
                static_cast<double>(s.hostDiskTotalBytes));
    host.insert(QStringLiteral("diskFreeBytes"),
                static_cast<double>(s.hostDiskFreeBytes));
    host.insert(QStringLiteral("load1"), s.hostLoad1);
    host.insert(QStringLiteral("run"), s.hostRun);
    host.insert(QStringLiteral("blocked"), s.hostBlocked);
    host.insert(QStringLiteral("netRxBps"), s.hostNetRxPerSec);
    host.insert(QStringLiteral("netTxBps"), s.hostNetTxPerSec);
    return host;
}

QJsonObject focusBlock(const DisplaySnapshot &s) {
    QJsonObject focus;
    focus.insert(QStringLiteral("app"), s.appName);
    focus.insert(QStringLiteral("pid"), s.pid);   // already a display string
    focus.insert(QStringLiteral("cpuPercent"), s.cpuPercent);
    focus.insert(QStringLiteral("rssKb"), static_cast<double>(s.rssKb));
    focus.insert(QStringLiteral("threads"), s.focusThreads);
    focus.insert(QStringLiteral("fds"), s.focusFds);
    focus.insert(QStringLiteral("netRxBps"), s.focusNetRxPerSec);
    focus.insert(QStringLiteral("netTxBps"), s.focusNetTxPerSec);
    focus.insert(QStringLiteral("ioReadBps"), s.focusIoReadPerSec);
    focus.insert(QStringLiteral("ioWriteBps"), s.focusIoWritePerSec);
    return focus;
}

QJsonArray processBlock(const DisplaySnapshot &s, int cap) {
    QList<ProcessRow> rows = s.processes;
    std::sort(rows.begin(), rows.end(),
              [](const ProcessRow &a, const ProcessRow &b) {
                  return a.cpuPercent > b.cpuPercent;
              });
    QJsonArray out;
    for (int i = 0; i < rows.size() && i < cap; ++i) {
        const ProcessRow &row = rows.at(i);
        QJsonObject entry;
        entry.insert(QStringLiteral("pid"), row.pid);
        entry.insert(QStringLiteral("name"), row.name);
        entry.insert(QStringLiteral("cpuPercent"), row.cpuPercent);
        entry.insert(QStringLiteral("rssKb"), row.rssKb);
        entry.insert(QStringLiteral("threads"), row.threads);
        out.append(entry);
    }
    return out;
}

QJsonObject sampleJson(const ExportRow &row) {
    // The same vocabulary the CSV/JSON export uses, so one reader knows both.
    QJsonObject entry;
    entry.insert(QStringLiteral("at"), double(row.atMs));
    entry.insert(QStringLiteral("app"), row.appName);
    entry.insert(QStringLiteral("appCpu"), row.appCpuPercent);
    entry.insert(QStringLiteral("appRssKb"), double(row.appRssKb));
    entry.insert(QStringLiteral("hostCpu"), row.hostCpuPercent);
    entry.insert(QStringLiteral("hostSteal"), row.hostStealPercent);
    entry.insert(QStringLiteral("memAvailableKb"),
                 double(row.hostMemAvailableKb));
    entry.insert(QStringLiteral("load1"), row.load1);
    entry.insert(QStringLiteral("diskReadKbps"), row.diskReadKbPerSec);
    entry.insert(QStringLiteral("diskWriteKbps"), row.diskWriteKbPerSec);
    entry.insert(QStringLiteral("diskUtil"), row.diskUtilPercent);
    entry.insert(QStringLiteral("netRxKbps"), row.netRxKbPerSec);
    entry.insert(QStringLiteral("netTxKbps"), row.netTxKbPerSec);
    entry.insert(QStringLiteral("tcpRetrans"), row.tcpRetransPerSec);
    return entry;
}

QJsonObject eventJson(const TimelineEvent &event) {
    QJsonObject entry;
    entry.insert(QStringLiteral("at"), double(event.atMs));
    entry.insert(QStringLiteral("severity"),
                 timelineSeverityName(event.severity));
    entry.insert(QStringLiteral("kind"), timelineKindName(event.kind));
    entry.insert(QStringLiteral("subject"), event.subject);
    entry.insert(QStringLiteral("title"), event.title);
    if (!event.detail.isEmpty()) {
        entry.insert(QStringLiteral("detail"), event.detail);
    }
    if (!event.tags.isEmpty()) {
        entry.insert(QStringLiteral("tags"),
                     QJsonArray::fromStringList(event.tags));
    }
    return entry;
}

QJsonObject incidentJson(const Incident &incident) {
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), incident.id);
    entry.insert(QStringLiteral("group"), incident.groupKey);
    entry.insert(QStringLiteral("title"), incident.title);
    entry.insert(QStringLiteral("severity"), incident.severity);
    entry.insert(QStringLiteral("status"), incident.status);
    if (!incident.owner.isEmpty()) {
        entry.insert(QStringLiteral("owner"), incident.owner);
    }
    entry.insert(QStringLiteral("openedAt"), double(incident.openedAt));
    entry.insert(QStringLiteral("resolvedAt"), double(incident.resolvedAt));
    entry.insert(QStringLiteral("count"), incident.count);
    entry.insert(QStringLiteral("flapping"), incident.flapping);
    entry.insert(QStringLiteral("rules"),
                 QJsonArray::fromStringList(incident.rules));
    return entry;
}

QJsonObject monitorJson(const AlertRule &rule) {
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), rule.id);
    entry.insert(QStringLiteral("title"), rule.title);
    entry.insert(QStringLiteral("severity"), rule.severity);
    entry.insert(QStringLiteral("threshold"), rule.threshold);
    entry.insert(QStringLiteral("above"), rule.above);
    entry.insert(QStringLiteral("sustained"), rule.sustained);
    entry.insert(QStringLiteral("unit"), rule.unit);
    if (!rule.group.isEmpty()) {
        entry.insert(QStringLiteral("group"), rule.group);
    }
    if (!rule.owner.isEmpty()) {
        entry.insert(QStringLiteral("owner"), rule.owner);
    }
    return entry;
}

QString isoUtc(qint64 ms) {
    if (ms <= 0) {
        return QString();
    }
    return qtcompat::fromMSecsSinceEpochUtc(ms).toString(Qt::ISODate);
}

} // namespace

QJsonObject bundleBody(const DisplaySnapshot &snapshot,
                       const QList<TimelineEvent> &events,
                       const QList<ExportRow> &history,
                       const QList<Incident> &incidents,
                       const QList<AlertRule> &monitors,
                       const BundleLimits &limits, qint64 nowMs) {
    const qint64 when =
        nowMs > 0 ? nowMs : QDateTime::currentMSecsSinceEpoch();

    QJsonObject body;
    body.insert(QStringLiteral("createdAt"), double(when));
    body.insert(QStringLiteral("createdAtIso"), isoUtc(when));

    QJsonObject host = hostBlock(snapshot);
    host.insert(QStringLiteral("hostname"),
                snapshot.hostInfo.platform.hostname);
    host.insert(QStringLiteral("platform"), snapshot.hostInfo.platform.os);
    host.insert(QStringLiteral("kernel"),
                QStringLiteral("%1 %2")
                    .arg(snapshot.hostInfo.platform.kernelName,
                         snapshot.hostInfo.platform.kernelRelease));
    host.insert(QStringLiteral("machine"),
                snapshot.hostInfo.platform.machine);
    body.insert(QStringLiteral("host"), host);
    body.insert(QStringLiteral("focus"), focusBlock(snapshot));
    body.insert(QStringLiteral("processes"),
                processBlock(snapshot, limits.maxProcesses));

    QJsonObject health;
    health.insert(QStringLiteral("processes"), snapshot.processes.size());
    health.insert(QStringLiteral("threads"), snapshot.focusThreads);
    health.insert(QStringLiteral("tcpAvailable"), snapshot.tcpHealthAvailable);
    if (snapshot.tcpHealthAvailable) {
        health.insert(QStringLiteral("tcpEstab"),
                      static_cast<double>(snapshot.tcpHealth.currEstab));
        health.insert(QStringLiteral("tcpRetransPercent"),
                      snapshot.tcpHealth.retransPercent);
    }
    // Containers and Kubernetes land in the HUD, not in the snapshot, so a
    // bundle taken from the sampler states them as not captured rather than
    // inventing a zero.
    health.insert(QStringLiteral("disks"), snapshot.disks.size());
    health.insert(QStringLiteral("disksAvailable"), snapshot.disksAvailable);
    body.insert(QStringLiteral("health"), health);

    QJsonArray eventRows;
    // Newest first, bounded: the reader wants the minutes around the incident,
    // not the whole session.
    QList<TimelineEvent> sorted = events;
    std::sort(sorted.begin(), sorted.end(),
              [](const TimelineEvent &a, const TimelineEvent &b) {
                  return a.atMs > b.atMs;
              });
    for (int i = 0; i < sorted.size() && i < limits.maxEvents; ++i) {
        eventRows.append(eventJson(sorted.at(i)));
    }
    body.insert(QStringLiteral("events"), eventRows);

    QJsonArray sampleRows;
    const int first = qMax(0, history.size() - limits.maxSamples);
    for (int i = first; i < history.size(); ++i) {
        sampleRows.append(sampleJson(history.at(i)));
    }
    body.insert(QStringLiteral("samples"), sampleRows);

    QJsonArray incidentRows;
    for (const Incident &incident : incidents) {
        incidentRows.append(incidentJson(incident));
    }
    body.insert(QStringLiteral("incidents"), incidentRows);

    QJsonArray monitorRows;
    for (const AlertRule &rule : monitors) {
        monitorRows.append(monitorJson(rule));
    }
    body.insert(QStringLiteral("monitors"), monitorRows);

    return body;
}

QString bundleDigest(const QJsonObject &body) {
    // QJsonObject keeps its keys sorted, so the compact encoding is canonical:
    // the same body always hashes to the same digest, on any platform.
    const QByteArray canonical = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(canonical, QCryptographicHash::Sha256)
            .toHex());
}

QByteArray bundleEncode(const QJsonObject &body, const QString &toolVersion) {
    QJsonObject envelope;
    envelope.insert(QStringLiteral("schema"), 1);
    envelope.insert(QStringLiteral("tool"), QStringLiteral("Syspeek"));
    envelope.insert(QStringLiteral("version"), toolVersion);
    envelope.insert(QStringLiteral("digest"), bundleDigest(body));
    envelope.insert(QStringLiteral("body"), body);
    return QJsonDocument(envelope).toJson(QJsonDocument::Indented);
}

bool bundleDecode(const QByteArray &json, QJsonObject *bundle,
                  QString *error) {
    if (error != nullptr) {
        error->clear();
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("not a bundle: %1")
                         .arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject envelope = doc.object();
    if (!envelope.contains(QStringLiteral("body"))
        || !envelope.contains(QStringLiteral("digest"))) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "not a Syspeek bundle: no body/digest - is this another tool's "
                "JSON?");
        }
        return false;
    }
    const int schema = envelope.value(QStringLiteral("schema")).toInt();
    if (schema > 1) {
        if (error != nullptr) {
            *error = QStringLiteral(
                         "bundle schema %1 is newer than this build - update "
                         "Syspeek to read it")
                         .arg(schema);
        }
        return false;
    }
    const QString expected = envelope.value(QStringLiteral("digest")).toString();
    const QString actual =
        bundleDigest(envelope.value(QStringLiteral("body")).toObject());
    if (expected != actual) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "the digest does not match - the file was edited or truncated "
                "in transit");
        }
        return false;
    }
    if (bundle != nullptr) {
        *bundle = envelope;
    }
    return true;
}

QString bundleSummaryText(const QJsonObject &bundle) {
    const QJsonObject body = bundle.value(QStringLiteral("body")).toObject();
    if (body.isEmpty()) {
        return QStringLiteral("empty bundle");
    }
    const QJsonObject host = body.value(QStringLiteral("host")).toObject();
    const QJsonObject focus = body.value(QStringLiteral("focus")).toObject();
    const QJsonArray events = body.value(QStringLiteral("events")).toArray();
    const QJsonArray samples = body.value(QStringLiteral("samples")).toArray();
    const QJsonArray incidents =
        body.value(QStringLiteral("incidents")).toArray();

    int errors = 0;
    int warnings = 0;
    for (const QJsonValue &value : events) {
        const QString severity =
            value.toObject().value(QStringLiteral("severity")).toString();
        if (severity == QLatin1String("error")) {
            ++errors;
        } else if (severity == QLatin1String("warn")) {
            ++warnings;
        }
    }

    QStringList lines;
    lines << QStringLiteral("%1 (%2 %3)")
                 .arg(host.value(QStringLiteral("hostname")).toString(
                          QStringLiteral("this machine")),
                      host.value(QStringLiteral("platform")).toString(
                          QStringLiteral("unknown")),
                      host.value(QStringLiteral("kernel")).toString());
    lines << QStringLiteral("captured  %1  by Syspeek %2")
                 .arg(body.value(QStringLiteral("createdAtIso")).toString(),
                      bundle.value(QStringLiteral("version")).toString());
    lines << QStringLiteral("focus     %1 (pid %2)  cpu %3%  rss %4 MB")
                 .arg(focus.value(QStringLiteral("app")).toString(
                          QStringLiteral("--")))
                 .arg(focus.value(QStringLiteral("pid")).toString())
                 .arg(focus.value(QStringLiteral("cpuPercent")).toDouble(), 0,
                      'f', 1)
                 .arg(focus.value(QStringLiteral("rssKb")).toDouble() / 1024.0,
                      0, 'f', 0);
    lines << QStringLiteral("host      cpu %1%  load %2  avail %3 MB")
                 .arg(host.value(QStringLiteral("cpuPercent")).toDouble(), 0,
                      'f', 1)
                 .arg(host.value(QStringLiteral("load1")).toDouble(), 0, 'f',
                      2)
                 .arg(host.value(QStringLiteral("memAvailableKb")).toDouble()
                          / 1024.0,
                      0, 'f', 0);
    lines << QStringLiteral("captured  %1 event(s) (%2 error, %3 warn)  "
                            "%4 sample(s)  %5 incident(s)")
                 .arg(events.size())
                 .arg(errors)
                 .arg(warnings)
                 .arg(samples.size())
                 .arg(incidents.size());
    if (samples.size() > 1) {
        const qint64 first = static_cast<qint64>(
            samples.first().toObject().value(QStringLiteral("at")).toDouble());
        const qint64 last = static_cast<qint64>(
            samples.last().toObject().value(QStringLiteral("at")).toDouble());
        if (last > first) {
            lines << QStringLiteral("window    %1s of samples")
                         .arg((last - first) / 1000);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString bundleDetailText(const QJsonObject &bundle) {
    const QJsonObject body = bundle.value(QStringLiteral("body")).toObject();
    QStringList lines;
    const QJsonArray incidents =
        body.value(QStringLiteral("incidents")).toArray();
    for (const QJsonValue &value : incidents) {
        const QJsonObject incident = value.toObject();
        const qint64 at = static_cast<qint64>(
            incident.value(QStringLiteral("openedAt")).toDouble());
        lines << QStringLiteral("INCIDENT  %1  [%2]  %3%4")
                     .arg(isoUtc(at),
                          incident.value(QStringLiteral("severity")).toString(),
                          incident.value(QStringLiteral("title")).toString(),
                          incident.value(QStringLiteral("owner"))
                                  .toString()
                                  .isEmpty()
                              ? QString()
                              : QStringLiteral("  @%1").arg(
                                    incident
                                        .value(QStringLiteral("owner"))
                                        .toString()));
    }
    const QJsonArray events = body.value(QStringLiteral("events")).toArray();
    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        const qint64 at = static_cast<qint64>(
            event.value(QStringLiteral("at")).toDouble());
        lines << QStringLiteral("%1  %2  %3")
                     .arg(isoUtc(at),
                          event.value(QStringLiteral("severity"))
                              .toString()
                              .toUpper()
                              .leftJustified(5, QLatin1Char(' ')),
                          event.value(QStringLiteral("title")).toString());
    }
    if (lines.isEmpty()) {
        return QStringLiteral(
            "no events were captured in this window - the bundle still carries "
            "the host, the focused app and the samples around the moment");
    }
    return lines.join(QLatin1Char('\n'));
}

QString bundleFileName(qint64 nowMs) {
    const qint64 when =
        nowMs > 0 ? nowMs : QDateTime::currentMSecsSinceEpoch();
    return QStringLiteral("syspeek-bundle-%1.json")
        .arg(qtcompat::fromMSecsSinceEpochUtc(when)
                 .toString(QStringLiteral("yyyyMMdd-HHmmss")));
}
