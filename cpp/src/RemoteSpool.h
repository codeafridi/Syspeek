#pragma once

#include <QByteArray>
#include <QString>

class QFile;

// M98: the offline spool. A report that cannot be delivered is written here
// before the attempt and removed only when the relay has accepted it (or
// refused the body outright), so a closed laptop or a dead relay costs nothing
// and the buffer can never grow past a stated byte budget.
//
// File layout (single file, hard cap = header + capacity * record):
//   0   char magic[9]   "SYSPKRSP1"
//   9   u8[3]  pad
//   12  u32 version     1
//   16  u32 recordBytes kRemoteSpoolRecordBytes
//   20  u32 capacity    records (derived from the byte budget)
//   24  u32 count       records currently stored (<= capacity)
//   28  u32 head        index of the next write slot
//   32  u64 dropped     records lost to the ring (drop-oldest)
//   40  u64 total       records ever appended
//   48  u64 reserved
//   56  u64 reserved
//   64+ records, oldest = (head - count) mod capacity, so the ring wraps in
//       place and never rewrites a record it is keeping.
//
// Record (kRemoteSpoolRecordBytes): u32 idLen, id[64], i64 sentAtMs,
// u32 bodyLen, body[16384], u32 crc32 over everything before the CRC. The body
// is the envelope exactly as it goes on the wire, so a replay after a restart
// is byte-identical - which is what makes the relay's id-based dedupe work.
constexpr int kRemoteSpoolIdBytes = 64;
constexpr int kRemoteSpoolBodyBytes = 16384;
constexpr qint64 kRemoteSpoolRecordBytes =
    4 + kRemoteSpoolIdBytes + 8 + 4 + kRemoteSpoolBodyBytes + 4;
constexpr int kRemoteSpoolHeaderBytes = 64;
constexpr int kRemoteSpoolMaxRecords = 512;

// The gap between two backlog records: fast enough to drain an hour of buffered
// reports in minutes, slow enough that a reopened laptop does not arrive at the
// relay as one burst.
constexpr int kRemoteBacklogStepMs = 250;

struct RemoteSpoolRecord {
    QString id;
    qint64 sentAtMs = 0;
    QByteArray body;
};

class RemoteSpool {
public:
    RemoteSpool() = default;
    ~RemoteSpool();

    RemoteSpool(const RemoteSpool &) = delete;
    RemoteSpool &operator=(const RemoteSpool &) = delete;

    // bytesBudget <= 0 keeps the spool off (the card says so). A different
    // budget than the file on disk, or a header that does not read back as
    // ours, re-creates the file: the format is ours and a silent
    // reinterpretation of old bytes would lie about what is buffered.
    bool open(const QString &path, qint64 bytesBudget);
    void close();

    bool isOpen() const { return m_file != nullptr; }
    QString path() const { return m_path; }
    int capacity() const { return m_capacity; }
    int count() const { return m_count; }
    qint64 dropped() const { return m_dropped; }
    qint64 oversized() const { return m_oversized; }
    qint64 appended() const { return m_total; }
    qint64 fileBytes() const;
    qint64 budgetBytes() const { return m_budget; }

    // A body larger than one record cannot be stored without cutting it, and a
    // cut body is not a report - it is counted and refused instead.
    bool append(const QString &id, qint64 sentAtMs, const QByteArray &body);

    // index 0 = oldest stored report, count()-1 = newest.
    bool peekOldest(RemoteSpoolRecord &out) const;
    bool readAt(int index, RemoteSpoolRecord &out) const;
    bool ackOldest();
    bool clear();

private:
    bool loadHeader();
    bool writeHeader() const;
    bool createFile();
    bool readRecord(int slot, RemoteSpoolRecord &out) const;
    bool writeRecord(int slot, const QString &id, qint64 sentAtMs,
                     const QByteArray &body);

    QFile *m_file = nullptr;
    QString m_path;
    qint64 m_budget = 0;
    int m_capacity = 0;
    int m_count = 0;
    int m_head = 0;
    qint64 m_dropped = 0;
    qint64 m_oversized = 0;
    qint64 m_total = 0;
};

// Budget -> records. The header is reserved before any record is counted so the
// file is never larger than the stated budget.
int remoteSpoolCapacityFor(qint64 bytesBudget);

QString remoteSpoolSummaryText(int count, int capacity, qint64 dropped,
                               qint64 bytes, bool on);
