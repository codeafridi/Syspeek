#include "HistoryLog.h"

#include "MemoryFormat.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtEndian>

#include <cstring>

namespace {

constexpr char kMagic[8] = {'S', 'Y', 'S', 'P', 'K', 'H', 'S', '1'};
constexpr int kHeaderBytes = 64;
constexpr quint32 kVersion = 1;

quint32 readU32(const QByteArray &raw, int offset) {
    quint32 value = 0;
    std::memcpy(&value, raw.constData() + offset, 4);
    return qFromLittleEndian(value);
}

void writeU32(QByteArray &raw, int offset, quint32 value) {
    const quint32 le = qToLittleEndian(value);
    std::memcpy(raw.data() + offset, &le, 4);
}

quint64 readU64(const QByteArray &raw, int offset) {
    quint64 value = 0;
    std::memcpy(&value, raw.constData() + offset, 8);
    return qFromLittleEndian(value);
}

void writeU64(QByteArray &raw, int offset, quint64 value) {
    const quint64 le = qToLittleEndian(value);
    std::memcpy(raw.data() + offset, &le, 8);
}

double dblAt(const QByteArray &raw, int offset) {
    const quint32 bits = readU32(raw, offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, 4);
    return double(value);
}

void putDbl(QByteArray &raw, int offset, double value) {
    const float stored = float(value);
    quint32 bits = 0;
    std::memcpy(&bits, &stored, 4);
    writeU32(raw, offset, bits);
}

qint32 s32At(const QByteArray &raw, int offset) {
    return qint32(readU32(raw, offset));
}

void putS32(QByteArray &raw, int offset, qint64 value) {
    const qint32 clamped = qint32(qBound<qint64>(-2147483647LL, value,
                                                2147483647LL));
    writeU32(raw, offset, quint32(clamped));
}

QString dashIf(double value, int decimals = 1) {
    if (value < 0.0) {
        return QStringLiteral("--");
    }
    return QString::number(value, 'f', decimals);
}

QString dashIfKb(qint64 kb) {
    if (kb < 0) {
        return QStringLiteral("--");
    }
    return formatMemoryKb(quint64(kb));
}

QString dashIfInt(qint64 value) {
    if (value < 0) {
        return QStringLiteral("--");
    }
    return QString::number(value);
}

QString clockOf(qint64 atMs) {
    return QDateTime::fromMSecsSinceEpoch(atMs).toString(
        QStringLiteral("HH:mm:ss"));
}

}  // namespace

QByteArray encodeHistoryRecord(const HistoryRecord &record) {
    QByteArray raw(kHistoryRecordBytes, '\0');
    writeU64(raw, 0, quint64(record.atMs));
    const QByteArray name =
        record.appName.left(23).toUtf8();
    std::memcpy(raw.data() + 8, name.constData(),
                size_t(qMin(name.size(), 23)));
    putS32(raw, 32, record.pid);
    putDbl(raw, 36, record.appCpuPercent);
    putS32(raw, 40, record.appRssKb);
    putDbl(raw, 44, record.hostCpuPercent);
    putDbl(raw, 48, record.hostStealPercent);
    putS32(raw, 52, record.hostMemAvailableKb);
    putS32(raw, 56, record.hostMemTotalKb);
    putDbl(raw, 60, record.load1);
    putDbl(raw, 64, record.diskReadKbPerSec);
    putDbl(raw, 68, record.diskWriteKbPerSec);
    putDbl(raw, 72, record.diskUtilPercent);
    putDbl(raw, 76, record.netRxKbPerSec);
    putDbl(raw, 80, record.netTxKbPerSec);
    putDbl(raw, 84, record.tcpInSegsPerSec);
    putDbl(raw, 88, record.tcpOutSegsPerSec);
    putDbl(raw, 92, record.tcpRetransPerSec);
    putDbl(raw, 96, record.tcpRetransPercent);
    putS32(raw, 100, record.tcpCurrEstab);
    return raw;
}

bool decodeHistoryRecord(const QByteArray &bytes, HistoryRecord &record) {
    if (bytes.size() < kHistoryRecordBytes) {
        return false;
    }
    record.atMs = qint64(readU64(bytes, 0));
    const QByteArray name = bytes.mid(8, 24);
    const int end = name.indexOf('\0');
    record.appName = QString::fromUtf8(end < 0 ? name : name.left(end));
    record.pid = s32At(bytes, 32);
    record.appCpuPercent = dblAt(bytes, 36);
    record.appRssKb = s32At(bytes, 40);
    record.hostCpuPercent = dblAt(bytes, 44);
    record.hostStealPercent = dblAt(bytes, 48);
    record.hostMemAvailableKb = s32At(bytes, 52);
    record.hostMemTotalKb = s32At(bytes, 56);
    record.load1 = dblAt(bytes, 60);
    record.diskReadKbPerSec = dblAt(bytes, 64);
    record.diskWriteKbPerSec = dblAt(bytes, 68);
    record.diskUtilPercent = dblAt(bytes, 72);
    record.netRxKbPerSec = dblAt(bytes, 76);
    record.netTxKbPerSec = dblAt(bytes, 80);
    record.tcpInSegsPerSec = dblAt(bytes, 84);
    record.tcpOutSegsPerSec = dblAt(bytes, 88);
    record.tcpRetransPerSec = dblAt(bytes, 92);
    record.tcpRetransPercent = dblAt(bytes, 96);
    record.tcpCurrEstab = s32At(bytes, 100);
    return record.atMs > 0;
}

HistoryRecord historyRecordFrom(const ExportRow &row) {
    HistoryRecord record;
    record.atMs = row.atMs;
    record.appName = row.appName;
    bool ok = false;
    record.pid = row.pid.toInt(&ok);
    if (!ok) {
        record.pid = -1;
    }
    record.appCpuPercent = row.appCpuPercent;
    record.appRssKb = row.appRssKb;
    record.hostCpuPercent = row.hostCpuPercent;
    record.hostStealPercent = row.hostStealPercent;
    record.hostMemAvailableKb = row.hostMemAvailableKb;
    record.hostMemTotalKb = row.hostMemTotalKb;
    record.load1 = row.load1;
    record.diskReadKbPerSec = row.diskReadKbPerSec;
    record.diskWriteKbPerSec = row.diskWriteKbPerSec;
    record.diskUtilPercent = row.diskUtilPercent;
    record.netRxKbPerSec = row.netRxKbPerSec;
    record.netTxKbPerSec = row.netTxKbPerSec;
    record.tcpInSegsPerSec = row.tcpInSegsPerSec;
    record.tcpOutSegsPerSec = row.tcpOutSegsPerSec;
    record.tcpRetransPerSec = row.tcpRetransPerSec;
    record.tcpRetransPercent = row.tcpRetransPercent;
    record.tcpCurrEstab = row.tcpCurrEstab;
    return record;
}

ExportRow exportRowFromHistory(const HistoryRecord &record) {
    ExportRow row;
    row.atMs = record.atMs;
    row.appName = record.appName;
    row.pid = record.pid >= 0 ? QString::number(record.pid) : QString();
    row.appCpuPercent = record.appCpuPercent;
    row.appRssKb = record.appRssKb;
    row.hostCpuPercent = record.hostCpuPercent;
    row.hostStealPercent = record.hostStealPercent;
    row.hostMemAvailableKb = record.hostMemAvailableKb;
    row.hostMemTotalKb = record.hostMemTotalKb;
    row.load1 = record.load1;
    row.diskReadKbPerSec = record.diskReadKbPerSec;
    row.diskWriteKbPerSec = record.diskWriteKbPerSec;
    row.diskUtilPercent = record.diskUtilPercent;
    row.netRxKbPerSec = record.netRxKbPerSec;
    row.netTxKbPerSec = record.netTxKbPerSec;
    row.tcpInSegsPerSec = record.tcpInSegsPerSec;
    row.tcpOutSegsPerSec = record.tcpOutSegsPerSec;
    row.tcpRetransPerSec = record.tcpRetransPerSec;
    row.tcpRetransPercent = record.tcpRetransPercent;
    row.tcpCurrEstab = record.tcpCurrEstab;
    return row;
}

HistoryRecord historyRecordFromSnapshot(const DisplaySnapshot &snapshot,
                                        qint64 atMs) {
    return historyRecordFrom(exportRowFrom(snapshot, atMs));
}

HistoryLog::~HistoryLog() {
    close();
}

bool HistoryLog::open(const QString &path, int capacity) {
    close();
    if (path.isEmpty() || capacity <= 0) {
        return false;
    }
    m_path = path;
    m_capacity = capacity;

    const QFileInfo info(path);
    const QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        m_path.clear();
        m_capacity = 0;
        return false;
    }

    m_file = new QFile(path);
    if (!m_file->open(QIODevice::ReadWrite)) {
        delete m_file;
        m_file = nullptr;
        m_path.clear();
        m_capacity = 0;
        return false;
    }

    if (m_file->size() < kHeaderBytes) {
        if (!createFile()) {
            close();
            return false;
        }
        return true;
    }
    if (!loadHeader()) {
        if (!createFile()) {
            close();
            return false;
        }
    }
    return true;
}

void HistoryLog::close() {
    if (m_file != nullptr) {
        m_file->flush();
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    m_count = 0;
    m_head = 0;
    m_total = 0;
}

bool HistoryLog::createFile() {
    if (m_file == nullptr) {
        return false;
    }
    m_count = 0;
    m_head = 0;
    m_total = 0;
    m_createdMs = QDateTime::currentMSecsSinceEpoch();
    m_intervalMs = 1000;
    if (!m_file->resize(0)) {
        return false;
    }
    return writeHeader();
}

bool HistoryLog::loadHeader() {
    if (m_file == nullptr) {
        return false;
    }
    m_file->seek(0);
    const QByteArray raw = m_file->read(kHeaderBytes);
    if (raw.size() < kHeaderBytes) {
        return false;
    }
    if (std::memcmp(raw.constData(), kMagic, 8) != 0) {
        return false;
    }
    if (readU32(raw, 8) != kVersion
        || readU32(raw, 12) != quint32(kHistoryRecordBytes)) {
        return false;
    }
    if (int(readU32(raw, 16)) != m_capacity) {
        return false;
    }
    const int count = int(readU32(raw, 20));
    const int head = int(readU32(raw, 44));
    if (count < 0 || count > m_capacity || head < 0 || head >= m_capacity) {
        return false;
    }
    m_count = count;
    m_head = head;
    m_total = qint64(readU64(raw, 24));
    m_createdMs = qint64(readU64(raw, 32));
    m_intervalMs = qint64(readU32(raw, 40));
    if (m_intervalMs <= 0) {
        m_intervalMs = 1000;
    }
    if (qint64(m_file->size()) < fileBytes()) {
        return false;
    }
    return true;
}

bool HistoryLog::writeHeader() {
    if (m_file == nullptr) {
        return false;
    }
    QByteArray raw(kHeaderBytes, '\0');
    std::memcpy(raw.data(), kMagic, 8);
    writeU32(raw, 8, kVersion);
    writeU32(raw, 12, quint32(kHistoryRecordBytes));
    writeU32(raw, 16, quint32(m_capacity));
    writeU32(raw, 20, quint32(m_count));
    writeU64(raw, 24, quint64(m_total));
    writeU64(raw, 32, quint64(m_createdMs));
    writeU32(raw, 40, quint32(m_intervalMs));
    writeU32(raw, 44, quint32(m_head));
    if (!m_file->seek(0)) {
        return false;
    }
    return m_file->write(raw) == kHeaderBytes;
}

qint64 HistoryLog::fileBytes() const {
    return qint64(kHeaderBytes) + qint64(m_capacity) * kHistoryRecordBytes;
}

bool HistoryLog::append(const HistoryRecord &record) {
    if (m_file == nullptr || m_capacity <= 0) {
        return false;
    }
    const qint64 offset =
        qint64(kHeaderBytes) + qint64(m_head) * kHistoryRecordBytes;
    if (!m_file->seek(offset)) {
        return false;
    }
    if (m_file->write(encodeHistoryRecord(record)) != kHistoryRecordBytes) {
        return false;
    }
    m_head = (m_head + 1) % m_capacity;
    if (m_count < m_capacity) {
        ++m_count;
    }
    ++m_total;
    if (m_total == 1) {
        m_createdMs = record.atMs;
    }
    if (!writeHeader()) {
        return false;
    }
    return m_file->flush();
}

bool HistoryLog::readAt(int index, HistoryRecord &out) const {
    if (m_file == nullptr || index < 0 || index >= m_count) {
        return false;
    }
    const int slot = (m_head + m_capacity - m_count + index) % m_capacity;
    const qint64 offset =
        qint64(kHeaderBytes) + qint64(slot) * kHistoryRecordBytes;
    if (!m_file->seek(offset)) {
        return false;
    }
    return decodeHistoryRecord(m_file->read(kHistoryRecordBytes), out);
}

int HistoryLog::readRange(int from, int max, QList<HistoryRecord> &out) const {
    out.clear();
    if (m_file == nullptr || max <= 0) {
        return 0;
    }
    const int start = qBound(0, from, m_count);
    const int wanted = qMin(max, m_count - start);
    if (wanted <= 0) {
        return 0;
    }
    const int firstSlot = (m_head + m_capacity - m_count + start) % m_capacity;
    int remaining = wanted;
    int slot = firstSlot;
    while (remaining > 0) {
        const int chunk = qMin(remaining, m_capacity - slot);
        const qint64 offset =
            qint64(kHeaderBytes) + qint64(slot) * kHistoryRecordBytes;
        if (!m_file->seek(offset)) {
            break;
        }
        const QByteArray raw = m_file->read(qint64(chunk) * kHistoryRecordBytes);
        const int got = raw.size() / kHistoryRecordBytes;
        for (int i = 0; i < got; ++i) {
            HistoryRecord record;
            if (decodeHistoryRecord(raw.mid(i * kHistoryRecordBytes,
                                            kHistoryRecordBytes),
                                    record)) {
                out.append(record);
            }
        }
        if (got < chunk) {
            break;
        }
        remaining -= chunk;
        slot = 0;
    }
    return out.size();
}

bool HistoryLog::readLast(int max, QList<HistoryRecord> &out) const {
    const int start = qMax(0, m_count - qMax(0, max));
    return readRange(start, max, out) > 0;
}

qint64 HistoryLog::spanMs() const {
    if (m_count < 2) {
        return 0;
    }
    HistoryRecord firstRecord;
    HistoryRecord lastRecord;
    if (!readAt(0, firstRecord) || !readAt(m_count - 1, lastRecord)) {
        return 0;
    }
    return qMax<qint64>(0, lastRecord.atMs - firstRecord.atMs);
}

bool HistoryLog::newest(HistoryRecord &out) const {
    return readAt(m_count - 1, out);
}

bool HistoryLog::oldest(HistoryRecord &out) const {
    return readAt(0, out);
}

QString historyDefaultPath() {
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath() + QStringLiteral("/syspeek");
    }
    return base + QStringLiteral("/history.ring");
}

QString historySummaryText(int count, int capacity, qint64 spanMs,
                           qint64 bytes, qint64 intervalMs) {
    if (capacity <= 0) {
        return QStringLiteral("history: disabled");
    }
    if (count <= 0) {
        return QStringLiteral("history: no samples stored yet (capacity %1)")
            .arg(capacity);
    }
    const int percent = int(qRound(100.0 * double(count) / double(capacity)));
    const qint64 seconds = spanMs / 1000;
    QString span;
    if (seconds < 90) {
        span = QStringLiteral("%1 s").arg(seconds);
    } else if (seconds < 5400) {
        span = QStringLiteral("%1 min").arg(seconds / 60);
    } else {
        span = QStringLiteral("%1.%2 h")
                   .arg(seconds / 3600)
                   .arg((seconds % 3600) / 360);
    }
    return QStringLiteral("history: %1/%2 samples (%3%) - span %4 - %5 on "
                          "disk - %6 s interval")
        .arg(count)
        .arg(capacity)
        .arg(percent)
        .arg(span)
        .arg(formatBytesCompact(quint64(qMax<qint64>(0, bytes))))
        .arg(intervalMs > 0 ? intervalMs / 1000 : 1);
}

QString historyPositionText(int index, int count, const HistoryRecord &record,
                            bool live) {
    if (count <= 0) {
        return QStringLiteral("no stored sample selected");
    }
    const QString when = clockOf(record.atMs);
    if (live) {
        return QStringLiteral("sample %1/%2 - %3 (LIVE)")
            .arg(index + 1)
            .arg(count)
            .arg(when);
    }
    return QStringLiteral("sample %1/%2 - %3 (rewound)")
        .arg(index + 1)
        .arg(count)
        .arg(when);
}

QString historyRecordText(const HistoryRecord &record) {
    const QString app =
        record.appName.isEmpty()
            ? QStringLiteral("--")
            : QStringLiteral("%1 %2").arg(record.appName,
                                          record.pid >= 0
                                              ? QString::number(record.pid)
                                              : QStringLiteral("--"));
    return QStringLiteral(
               "%1  app %2  cpu %3%  rss %4  |  host cpu %5%  steal %6%  "
               "mem avail %7  load %8  |  disk %9/%10 KB/s  %11%  |  "
               "net %12/%13 KB/s  |  tcp %14/%15 segs  retrans %16%  estab %17")
        .arg(clockOf(record.atMs))
        .arg(app)
        .arg(dashIf(record.appCpuPercent))
        .arg(dashIfKb(record.appRssKb))
        .arg(dashIf(record.hostCpuPercent))
        .arg(dashIf(record.hostStealPercent))
        .arg(dashIfKb(record.hostMemAvailableKb))
        .arg(dashIf(record.load1, 2))
        .arg(dashIf(record.diskReadKbPerSec, 0))
        .arg(dashIf(record.diskWriteKbPerSec, 0))
        .arg(dashIf(record.diskUtilPercent, 0))
        .arg(dashIf(record.netRxKbPerSec, 0))
        .arg(dashIf(record.netTxKbPerSec, 0))
        .arg(dashIf(record.tcpInSegsPerSec, 0))
        .arg(dashIf(record.tcpOutSegsPerSec, 0))
        .arg(dashIf(record.tcpRetransPercent, 2))
        .arg(dashIfInt(record.tcpCurrEstab));
}
