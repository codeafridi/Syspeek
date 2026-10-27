#include "MetricsExport.h"

#include <QDateTime>
#include <QTimeZone>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>

namespace {

constexpr int kDecimals = 2;

QString fixed(double value) {
    if (value < 0.0) {
        return QStringLiteral("-1");
    }
    return QString::number(value, 'f', kDecimals);
}

QString integer(qint64 value) {
    if (value < 0) {
        return QStringLiteral("-1");
    }
    return QString::number(value);
}

QString text(const QString &value) {
    return value.isEmpty() ? QStringLiteral("-1") : value;
}

QJsonValue numberOrNull(double value) {
    if (value < 0.0) {
        return QJsonValue(QJsonValue::Null);
    }
    return QJsonValue(value);
}

QJsonValue integerOrNull(qint64 value) {
    if (value < 0) {
        return QJsonValue(QJsonValue::Null);
    }
    return QJsonValue(value);
}

QJsonValue stringOrNull(const QString &value) {
    if (value.isEmpty()) {
        return QJsonValue(QJsonValue::Null);
    }
    return QJsonValue(value);
}

QDateTime utc(qint64 ms) {
    return QDateTime::fromMSecsSinceEpoch(ms > 0 ? ms : 0, QTimeZone::UTC);
}

QString intervalSeconds(int intervalMs) {
    if (intervalMs <= 0) {
        return QStringLiteral("-1");
    }
    const double seconds = double(intervalMs) / 1000.0;
    if (qFuzzyCompare(seconds, qRound64(seconds))) {
        return QString::number(qRound64(seconds));
    }
    return QString::number(seconds, 'f', 1);
}

} // namespace

ExportRow exportRowFrom(const DisplaySnapshot &snapshot, qint64 atMs) {
    ExportRow row;
    row.atMs = atMs;

    const bool focused = !snapshot.appName.isEmpty()
                         && snapshot.appName != QStringLiteral("--");
    if (focused) {
        row.appName = snapshot.appName;
        row.pid = snapshot.pid;
    }
    if (focused && snapshot.cpuValid) {
        row.appCpuPercent = snapshot.cpuPercent;
    }
    if (focused && snapshot.rssKb > 0) {
        row.appRssKb = qint64(snapshot.rssKb);
    }

    if (snapshot.hostAvailable && !snapshot.hostWarming) {
        row.hostCpuPercent = snapshot.hostCpuPercent;
        row.hostStealPercent = snapshot.hostStealPercent;
        row.hostMemAvailableKb = qint64(snapshot.hostMemAvailableKb);
        row.hostMemTotalKb = qint64(snapshot.hostMemTotalKb);
        row.load1 = snapshot.hostLoad1;
        row.netRxKbPerSec = snapshot.hostNetRxPerSec / 1024.0;
        row.netTxKbPerSec = snapshot.hostNetTxPerSec / 1024.0;
    }

    if (snapshot.disksAvailable && !snapshot.disksWarming
        && !snapshot.disks.isEmpty()) {
        double readKb = 0.0;
        double writeKb = 0.0;
        double util = 0.0;
        for (const DiskRow &disk : snapshot.disks) {
            readKb += disk.readKbPerSec;
            writeKb += disk.writeKbPerSec;
            util = std::max(util, disk.utilPercent);
        }
        row.diskReadKbPerSec = readKb;
        row.diskWriteKbPerSec = writeKb;
        row.diskUtilPercent = util;
    }

    if (snapshot.tcpHealthAvailable) {
        row.tcpInSegsPerSec = snapshot.tcpHealth.inSegsPerSec;
        row.tcpOutSegsPerSec = snapshot.tcpHealth.outSegsPerSec;
        row.tcpRetransPerSec = snapshot.tcpHealth.retransPerSec;
        row.tcpRetransPercent = snapshot.tcpHealth.retransPercent;
        row.tcpCurrEstab = qint64(snapshot.tcpHealth.currEstab);
    }

    return row;
}

QString exportCsv(const QList<ExportRow> &rows, const ExportMeta &meta) {
    QString out;
    out += QStringLiteral(
        "# hostname;interval;timestamp;APP;app_pid;app_cpu_percent;app_rss_kb;"
        "host_cpu_percent;host_steal_percent;host_mem_available_kb;"
        "host_mem_total_kb;load_1m;disk_read_kb_s;disk_write_kb_s;"
        "disk_util_percent;net_rx_kb_s;net_tx_kb_s;tcp_in_segs_s;"
        "tcp_out_segs_s;tcp_retrans_segs_s;tcp_retrans_percent;"
        "tcp_curr_estab\n");

    const QString hostname = meta.hostname.isEmpty()
                                 ? QStringLiteral("-1")
                                 : meta.hostname;
    const QString interval = intervalSeconds(meta.intervalMs);
    for (const ExportRow &row : rows) {
        QStringList fields;
        fields << hostname;
        fields << interval;
        fields << utc(row.atMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                      + QStringLiteral(" UTC");
        fields << text(row.appName);
        fields << text(row.pid);
        fields << fixed(row.appCpuPercent);
        fields << integer(row.appRssKb);
        fields << fixed(row.hostCpuPercent);
        fields << fixed(row.hostStealPercent);
        fields << integer(row.hostMemAvailableKb);
        fields << integer(row.hostMemTotalKb);
        fields << fixed(row.load1);
        fields << fixed(row.diskReadKbPerSec);
        fields << fixed(row.diskWriteKbPerSec);
        fields << fixed(row.diskUtilPercent);
        fields << fixed(row.netRxKbPerSec);
        fields << fixed(row.netTxKbPerSec);
        fields << fixed(row.tcpInSegsPerSec);
        fields << fixed(row.tcpOutSegsPerSec);
        fields << fixed(row.tcpRetransPerSec);
        fields << fixed(row.tcpRetransPercent);
        fields << integer(row.tcpCurrEstab);
        out += fields.join(QLatin1Char(';'));
        out += QLatin1Char('\n');
    }
    return out;
}

QString exportJson(const QList<ExportRow> &rows, const ExportMeta &meta) {
    QJsonArray samples;
    for (const ExportRow &row : rows) {
        const QDateTime stamp = utc(row.atMs);
        QJsonObject app{
            {QStringLiteral("name"), stringOrNull(row.appName)},
            {QStringLiteral("pid"), stringOrNull(row.pid)},
            {QStringLiteral("cpuPercent"), numberOrNull(row.appCpuPercent)},
            {QStringLiteral("rssKb"), integerOrNull(row.appRssKb)}};
        QJsonObject host{
            {QStringLiteral("cpuPercent"), numberOrNull(row.hostCpuPercent)},
            {QStringLiteral("stealPercent"), numberOrNull(row.hostStealPercent)},
            {QStringLiteral("memAvailableKb"),
             integerOrNull(row.hostMemAvailableKb)},
            {QStringLiteral("memTotalKb"), integerOrNull(row.hostMemTotalKb)},
            {QStringLiteral("load1"), numberOrNull(row.load1)}};
        QJsonObject disk{
            {QStringLiteral("readKbPerSec"),
             numberOrNull(row.diskReadKbPerSec)},
            {QStringLiteral("writeKbPerSec"),
             numberOrNull(row.diskWriteKbPerSec)},
            {QStringLiteral("utilPercent"), numberOrNull(row.diskUtilPercent)}};
        QJsonObject network{
            {QStringLiteral("rxKbPerSec"), numberOrNull(row.netRxKbPerSec)},
            {QStringLiteral("txKbPerSec"), numberOrNull(row.netTxKbPerSec)}};
        QJsonObject tcp{
            {QStringLiteral("inSegsPerSec"),
             numberOrNull(row.tcpInSegsPerSec)},
            {QStringLiteral("outSegsPerSec"),
             numberOrNull(row.tcpOutSegsPerSec)},
            {QStringLiteral("retransSegsPerSec"),
             numberOrNull(row.tcpRetransPerSec)},
            {QStringLiteral("retransPercent"),
             numberOrNull(row.tcpRetransPercent)},
            {QStringLiteral("currEstab"), integerOrNull(row.tcpCurrEstab)}};

        QJsonObject sample{
            {QStringLiteral("timestamp"),
             QJsonObject{{QStringLiteral("date"),
                          stamp.toString(QStringLiteral("yyyy-MM-dd"))},
                         {QStringLiteral("time"),
                          stamp.toString(QStringLiteral("HH:mm:ss"))},
                         {QStringLiteral("tz"), QStringLiteral("UTC")}}},
            {QStringLiteral("app"), app},
            {QStringLiteral("host"), host},
            {QStringLiteral("disk"), disk},
            {QStringLiteral("network"), network},
            {QStringLiteral("tcp"), tcp}};
        samples.append(sample);
    }

    QJsonObject body{
        {QStringLiteral("version"), meta.version},
        {QStringLiteral("platform"), meta.platform},
        {QStringLiteral("hostname"), meta.hostname},
        {QStringLiteral("intervalMs"), meta.intervalMs},
        {QStringLiteral("samples"), samples}};
    QJsonObject root{{QStringLiteral("syspeek"), body}};
    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString normalizedExportFormat(const QString &format) {
    return format.trimmed().toLower() == QLatin1String("json")
               ? QStringLiteral("json")
               : QStringLiteral("csv");
}

QString exportRows(const QList<ExportRow> &rows, const ExportMeta &meta,
                   const QString &format) {
    return normalizedExportFormat(format) == QLatin1String("json")
               ? exportJson(rows, meta)
               : exportCsv(rows, meta);
}
