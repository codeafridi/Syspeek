#include "RemoteSpool.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <array>
#include <cstring>

namespace {

constexpr char kMagic[9] = {'S', 'Y', 'S', 'P', 'K', 'R', 'S', 'P', '1'};
constexpr quint32 kVersion = 1;

// Header offsets. The magic is 9 bytes so the two CRC-less counters that follow
// stay 8-byte aligned, which keeps a hexdump of the file readable.
constexpr int kOffMagic = 0;
constexpr int kOffVersion = 12;
constexpr int kOffRecordBytes = 16;
constexpr int kOffCapacity = 20;
constexpr int kOffCount = 24;
constexpr int kOffHead = 28;
constexpr int kOffDropped = 32;
constexpr int kOffTotal = 40;

// Record offsets.
constexpr int kOffIdLen = 0;
constexpr int kOffId = 4;
constexpr int kOffSentAt = kOffId + kRemoteSpoolIdBytes;
constexpr int kOffBodyLen = kOffSentAt + 8;
constexpr int kOffBody = kOffBodyLen + 4;
constexpr int kOffCrc = kRemoteSpoolRecordBytes - 4;

static_assert(kOffCrc == kOffBody + kRemoteSpoolBodyBytes,
              "record layout must end with the CRC");

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

const quint32 *crcTable() {
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> built{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            built[i] = c;
        }
        return built;
    }();
    return table.data();
}

quint32 crc32Of(const char *data, qsizetype size) {
    const quint32 *table = crcTable();
    quint32 crc = 0xFFFFFFFFu;
    for (qsizetype i = 0; i < size; ++i) {
        crc = table[(crc ^ quint8(data[i])) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

}  // namespace

RemoteSpool::~RemoteSpool() {
    close();
}

bool RemoteSpool::open(const QString &path, qint64 bytesBudget) {
    close();
    if (path.isEmpty()) {
        return false;
    }
    const int capacity = remoteSpoolCapacityFor(bytesBudget);
    if (capacity <= 0) {
        return false;
    }

    m_path = path;
    m_budget = bytesBudget;
    m_capacity = capacity;

    const QDir dir = QFileInfo(path).dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        m_path.clear();
        m_budget = 0;
        m_capacity = 0;
        return false;
    }

    m_file = new QFile(path);
    if (!m_file->open(QIODevice::ReadWrite)) {
        delete m_file;
        m_file = nullptr;
        m_path.clear();
        m_budget = 0;
        m_capacity = 0;
        return false;
    }

    if (m_file->size() < kRemoteSpoolHeaderBytes || !loadHeader()) {
        if (!createFile()) {
            close();
            return false;
        }
    }
    return true;
}

void RemoteSpool::close() {
    if (m_file != nullptr) {
        m_file->flush();
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    m_count = 0;
    m_head = 0;
    m_dropped = 0;
    m_oversized = 0;
    m_total = 0;
}

bool RemoteSpool::createFile() {
    if (m_file == nullptr) {
        return false;
    }
    m_count = 0;
    m_head = 0;
    m_dropped = 0;
    m_oversized = 0;
    m_total = 0;
    if (!m_file->resize(fileBytes())) {
        return false;
    }
    return writeHeader() && m_file->flush();
}

bool RemoteSpool::loadHeader() {
    if (m_file == nullptr) {
        return false;
    }
    if (!m_file->seek(0)) {
        return false;
    }
    const QByteArray raw = m_file->read(kRemoteSpoolHeaderBytes);
    if (raw.size() < kRemoteSpoolHeaderBytes) {
        return false;
    }
    if (std::memcmp(raw.constData() + kOffMagic, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }
    if (readU32(raw, kOffVersion) != kVersion
        || readU32(raw, kOffRecordBytes) != quint32(kRemoteSpoolRecordBytes)
        || int(readU32(raw, kOffCapacity)) != m_capacity) {
        return false;
    }
    if (m_file->size() != fileBytes()) {
        return false;
    }
    const int count = int(readU32(raw, kOffCount));
    const int head = int(readU32(raw, kOffHead));
    if (count < 0 || count > m_capacity || head < 0 || head >= m_capacity) {
        return false;
    }
    m_count = count;
    m_head = head;
    m_dropped = qint64(readU64(raw, kOffDropped));
    m_total = qint64(readU64(raw, kOffTotal));
    if (m_dropped < 0 || m_total < 0) {
        return false;
    }
    return true;
}

bool RemoteSpool::writeHeader() const {
    if (m_file == nullptr) {
        return false;
    }
    QByteArray raw(kRemoteSpoolHeaderBytes, '\0');
    std::memcpy(raw.data() + kOffMagic, kMagic, sizeof(kMagic));
    writeU32(raw, kOffVersion, kVersion);
    writeU32(raw, kOffRecordBytes, quint32(kRemoteSpoolRecordBytes));
    writeU32(raw, kOffCapacity, quint32(m_capacity));
    writeU32(raw, kOffCount, quint32(m_count));
    writeU32(raw, kOffHead, quint32(m_head));
    writeU64(raw, kOffDropped, quint64(m_dropped));
    writeU64(raw, kOffTotal, quint64(m_total));
    if (!m_file->seek(0)) {
        return false;
    }
    return m_file->write(raw) == kRemoteSpoolHeaderBytes;
}

qint64 RemoteSpool::fileBytes() const {
    return qint64(kRemoteSpoolHeaderBytes)
           + qint64(m_capacity) * kRemoteSpoolRecordBytes;
}

bool RemoteSpool::writeRecord(int slot, const QString &id, qint64 sentAtMs,
                              const QByteArray &body) {
    if (m_file == nullptr || slot < 0 || slot >= m_capacity) {
        return false;
    }
    const QByteArray idBytes = id.toUtf8().left(kRemoteSpoolIdBytes);
    QByteArray raw(kRemoteSpoolRecordBytes, '\0');
    writeU32(raw, kOffIdLen, quint32(idBytes.size()));
    std::memcpy(raw.data() + kOffId, idBytes.constData(), size_t(idBytes.size()));
    writeU64(raw, kOffSentAt, quint64(sentAtMs));
    writeU32(raw, kOffBodyLen, quint32(body.size()));
    std::memcpy(raw.data() + kOffBody, body.constData(), size_t(body.size()));
    writeU32(raw, kOffCrc, crc32Of(raw.constData(), kOffCrc));

    const qint64 offset =
        qint64(kRemoteSpoolHeaderBytes) + qint64(slot) * kRemoteSpoolRecordBytes;
    if (!m_file->seek(offset)) {
        return false;
    }
    return m_file->write(raw) == kRemoteSpoolRecordBytes;
}

bool RemoteSpool::readRecord(int slot, RemoteSpoolRecord &out) const {
    if (m_file == nullptr || slot < 0 || slot >= m_capacity) {
        return false;
    }
    const qint64 offset =
        qint64(kRemoteSpoolHeaderBytes) + qint64(slot) * kRemoteSpoolRecordBytes;
    if (!m_file->seek(offset)) {
        return false;
    }
    const QByteArray raw = m_file->read(kRemoteSpoolRecordBytes);
    if (raw.size() < kRemoteSpoolRecordBytes) {
        return false;
    }
    if (readU32(raw, kOffCrc) != crc32Of(raw.constData(), kOffCrc)) {
        return false;
    }
    const quint32 idLen = readU32(raw, kOffIdLen);
    const quint32 bodyLen = readU32(raw, kOffBodyLen);
    if (idLen == 0 || idLen > quint32(kRemoteSpoolIdBytes)
        || bodyLen > quint32(kRemoteSpoolBodyBytes)) {
        return false;
    }
    out.id = QString::fromUtf8(raw.constData() + kOffId, int(idLen));
    out.sentAtMs = qint64(readU64(raw, kOffSentAt));
    out.body = raw.mid(kOffBody, int(bodyLen));
    return !out.id.isEmpty();
}

bool RemoteSpool::append(const QString &id, qint64 sentAtMs,
                         const QByteArray &body) {
    if (m_file == nullptr || m_capacity <= 0) {
        return false;
    }
    if (id.isEmpty() || id.toUtf8().size() > kRemoteSpoolIdBytes
        || body.isEmpty() || body.size() > kRemoteSpoolBodyBytes) {
        ++m_oversized;
        return false;
    }
    if (m_count == m_capacity) {
        // The write below lands on the oldest record: that report is gone, and
        // the count is what keeps the loss from being silent.
        ++m_dropped;
    }
    if (!writeRecord(m_head, id, sentAtMs, body)) {
        return false;
    }
    m_head = (m_head + 1) % m_capacity;
    if (m_count < m_capacity) {
        ++m_count;
    }
    ++m_total;
    return writeHeader() && m_file->flush();
}

bool RemoteSpool::peekOldest(RemoteSpoolRecord &out) const {
    return readAt(0, out);
}

bool RemoteSpool::readAt(int index, RemoteSpoolRecord &out) const {
    if (m_file == nullptr || index < 0 || index >= m_count) {
        return false;
    }
    const int slot = (m_head + m_capacity - m_count + index) % m_capacity;
    return readRecord(slot, out);
}

bool RemoteSpool::ackOldest() {
    if (m_file == nullptr || m_count <= 0) {
        return false;
    }
    // count is the only thing that marks a record as gone: the oldest is
    // derived as (head - count), so one decrement advances the front and the
    // freed slot is reused when the ring wraps around to it.
    --m_count;
    return writeHeader() && m_file->flush();
}

bool RemoteSpool::clear() {
    if (m_file == nullptr) {
        return false;
    }
    return createFile();
}

int remoteSpoolCapacityFor(qint64 bytesBudget) {
    if (bytesBudget <= 0) {
        return 0;
    }
    // A budget smaller than one record plus the header cannot hold a single
    // report, which is why 0 records means "spooling off" rather than a file
    // that is opened and then always full.
    const qint64 fit =
        (bytesBudget - kRemoteSpoolHeaderBytes) / kRemoteSpoolRecordBytes;
    if (fit <= 0) {
        return 0;
    }
    return int(qMin<qint64>(fit, kRemoteSpoolMaxRecords));
}

QString remoteSpoolSummaryText(int count, int capacity, qint64 dropped,
                               qint64 bytes, bool on) {
    if (!on) {
        return QStringLiteral("spooling off - an undelivered report is dropped "
                              "and counted");
    }
    if (capacity <= 0) {
        return QStringLiteral("spooling off - the byte budget is too small for "
                              "one report");
    }
    QString line = QStringLiteral("%1/%2 reports held, %3 on disk")
                       .arg(count)
                       .arg(capacity)
                       .arg(bytes >= 0 ? QStringLiteral("%1 MB").arg(
                                             double(bytes) / (1024.0 * 1024.0),
                                             0, 'f', 1)
                                       : QStringLiteral("--"));
    if (dropped > 0) {
        line += QStringLiteral(" - %1 dropped at the ceiling").arg(dropped);
    }
    return line;
}
