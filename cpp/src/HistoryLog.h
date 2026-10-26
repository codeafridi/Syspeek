#pragma once

#include "MetricsExport.h"

#include <QByteArray>
#include <QList>
#include <QString>

class QFile;

// Local history ring: a bounded, fixed-record on-disk log of the sampled
// snapshot so the HUD can rewind through recent history instead of only
// showing the live tick.
//
// File layout (single file, hard byte cap = header + capacity * record):
//   0    char magic[8]   "SYSPKHS1"
//   8    u32  version    1
//   12   u32  recordSize kRecordBytes
//   16   u32  capacity   records
//   20   u32  count      records currently stored (<= capacity)
//   24   u64  total      records ever appended (monotonic)
//   32   u64  createdMs  wall clock of the first append
//   40   u32  intervalMs sample interval
//   44   u32  head       index of the next write slot
//   48   u32  reserved   (crc of the header once sealed; 0 now)
//   52   u32  reserved
//   56   u64  reserved
//   64+  records, oldest-first order is derived from head/count, so a
//        full ring wraps in place and never rewrites the file.
//
// Record (108 bytes, little endian) mirrors the exported CSV columns so the
// history and the `--record` export speak the same vocabulary. Doubles are
// stored as float32 (documented lossy: 4 bytes per value keeps a 1 h ring
// under 400 KB) and missing values keep the -1 sentinel.
struct HistoryRecord {
    qint64 atMs = 0;
    QString appName;
    int pid = -1;
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

QByteArray encodeHistoryRecord(const HistoryRecord &record);
bool decodeHistoryRecord(const QByteArray &bytes, HistoryRecord &record);

HistoryRecord historyRecordFrom(const ExportRow &row);
ExportRow exportRowFromHistory(const HistoryRecord &record);
HistoryRecord historyRecordFromSnapshot(const DisplaySnapshot &snapshot,
                                        qint64 atMs);

constexpr int kHistoryRecordBytes = 108;

class HistoryLog {
public:
    HistoryLog() = default;
    ~HistoryLog();

    HistoryLog(const HistoryLog &) = delete;
    HistoryLog &operator=(const HistoryLog &) = delete;

    // Opens (or creates) the ring. An existing file with a different
    // capacity or record size is re-created: the format is ours, and a
    // silent reinterpretation of old bytes would lie about the data.
    bool open(const QString &path, int capacity);
    void close();

    bool isOpen() const { return m_file != nullptr; }
    QString path() const { return m_path; }
    int capacity() const { return m_capacity; }
    int count() const { return m_count; }
    qint64 totalWritten() const { return m_total; }
    qint64 intervalMs() const { return m_intervalMs; }
    qint64 createdMs() const { return m_createdMs; }
    qint64 fileBytes() const;
    bool append(const HistoryRecord &record);

    // index 0 = oldest stored sample, count()-1 = newest.
    bool readAt(int index, HistoryRecord &out) const;
    int readRange(int from, int max, QList<HistoryRecord> &out) const;
    bool readLast(int max, QList<HistoryRecord> &out) const;

    qint64 spanMs() const;
    bool newest(HistoryRecord &out) const;
    bool oldest(HistoryRecord &out) const;

private:
    bool loadHeader();
    bool writeHeader();
    bool createFile();

    QFile *m_file = nullptr;
    QString m_path;
    int m_capacity = 0;
    int m_count = 0;
    int m_head = 0;
    qint64 m_total = 0;
    qint64 m_createdMs = 0;
    qint64 m_intervalMs = 1000;
};

QString historyDefaultPath();
QString historySummaryText(int count, int capacity, qint64 spanMs,
                           qint64 bytes, qint64 intervalMs);
QString historyPositionText(int index, int count, const HistoryRecord &record,
                            bool live);
QString historyRecordText(const HistoryRecord &record);
