#pragma once

#include "DisplaySnapshot.h"

#include <QList>
#include <QString>

// Local metric history export (CSV + JSON). Nothing leaves the machine:
// the file is written where the user asks, or streamed to stdout.
//
// CSV dialect is the shipped sysstat sadf CSV (`sadf -d`), verified live on
// sysstat 12.7.7: a single leading header line beginning with '#', one
// data row per sample, ';' field separator, an interval column in seconds,
// a "YYYY-MM-DD HH:MM:SS UTC" timestamp column, and 2-decimal floats
//   # hostname;interval;timestamp;CPU;%user;%nice;%system;%iowait;...
//   host;1;2026-10-26 12:42:43 UTC;-1;12.56;0.00;5.59;0.09;0.00;81.76
// sadf prints -1 for values that do not apply; we use the same sentinel.
//
// JSON structure mirrors the sadf -j envelope nested the same way
// ({"sysstat": {"hosts": [{"nodename", "statistics": [{"timestamp":
// {"date","time","tz"}, "<domain>": ...}]}]}}) but stays inside our own
// namespace ({"syspeek": ...}) - never impersonating sysstat - and reuses
// the --dump vocabulary (host.cpuPercent, host.memAvailableKb, load1).

struct ExportRow {
    qint64 atMs = 0;
    QString appName;
    QString pid;
    double appCpuPercent = -1.0;
    qint64 appRssKb = -1;
    double hostCpuPercent = -1.0;
    double hostStealPercent = -1.0;
    qint64 hostMemAvailableKb = -1;
    qint64 hostMemTotalKb = -1;
    double load1 = -1.0;
    double diskReadKbPerSec = -1.0;
    double diskWriteKbPerSec = -1.0;
    double diskUtilPercent = -1.0;
    double netRxKbPerSec = -1.0;
    double netTxKbPerSec = -1.0;
    double tcpInSegsPerSec = -1.0;
    double tcpOutSegsPerSec = -1.0;
    double tcpRetransPerSec = -1.0;
    double tcpRetransPercent = -1.0;
    qint64 tcpCurrEstab = -1;
};

struct ExportMeta {
    QString hostname;
    QString platform;
    QString version;
    int intervalMs = 1000;
};

ExportRow exportRowFrom(const DisplaySnapshot &snapshot, qint64 atMs);

QString exportCsv(const QList<ExportRow> &rows, const ExportMeta &meta);
QString exportJson(const QList<ExportRow> &rows, const ExportMeta &meta);

// "csv" / "json" accepted; anything else falls back to csv.
QString normalizedExportFormat(const QString &format);
QString exportRows(const QList<ExportRow> &rows, const ExportMeta &meta,
                   const QString &format);
