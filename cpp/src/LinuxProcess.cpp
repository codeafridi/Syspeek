#include "LinuxProcess.h"

#include "platform/PortCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#if defined(Q_OS_WIN)
#include <io.h>
#include <process.h>
#else
#include <unistd.h>

#include <sys/statvfs.h>
#endif

namespace linuxproc {

namespace {

QString procPath(int pid, const char *leaf) {
    return QStringLiteral("/proc/%1/%2").arg(pid).arg(QLatin1String(leaf));
}

bool readStatTail(int pid, QStringList &fields) {
    QFile file(procPath(pid, "stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QString text = QString::fromUtf8(file.readAll());
    const int close = text.lastIndexOf(QLatin1Char(')'));
    if (close < 0 || close + 2 > text.size()) {
        return false;
    }

    fields = text.mid(close + 2).trimmed().split(QLatin1Char(' '),
                                                 Qt::SkipEmptyParts);
    return true;
}

} // namespace

bool exists(int pid) {
    if (pid <= 0) {
        return false;
    }
    return QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid));
}

QString comm(int pid) {
    QFile file(procPath(pid, "comm"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

QString exePath(int pid) {
    const QString path = procPath(pid, "exe");
    return QFile::symLinkTarget(path);
}

int parentPid(int pid) {
    QStringList fields;
    if (!readStatTail(pid, fields) || fields.size() <= 1) {
        return -1;
    }

    bool ok = false;
    const int ppid = fields.at(1).toInt(&ok);
    return ok ? ppid : -1;
}

QList<int> allPids() {
    QList<int> pids;
    const QStringList entries =
        QDir(QStringLiteral("/proc"))
            .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        const int pid = entry.toInt(&ok);
        if (ok) {
            pids.append(pid);
        }
    }
    return pids;
}

unsigned long long startTime(int pid) {
    QStringList fields;
    if (!readStatTail(pid, fields) || fields.size() <= 19) {
        return 0;
    }

    bool ok = false;
    const unsigned long long value = fields.at(19).toULongLong(&ok);
    return ok ? value : 0;
}

ProcessIdentity identity(int pid) {
    ProcessIdentity id;
    if (!exists(pid)) {
        return id;
    }

    id.pid = pid;
    id.startTime = startTime(pid);
    if (id.startTime == 0) {
        id.pid = -1;
    }
    return id;
}

bool cpuTicks(int pid, unsigned long long &out) {
    QStringList fields;
    if (!readStatTail(pid, fields) || fields.size() <= 12) {
        return false;
    }

    bool okU = false;
    bool okS = false;
    const unsigned long long utime = fields.at(11).toULongLong(&okU);
    const unsigned long long stime = fields.at(12).toULongLong(&okS);
    if (!okU || !okS) {
        return false;
    }

    out = utime + stime;
    return true;
}

bool procCpu(int pid, unsigned long long &ticks, unsigned long long &startTime) {
    QStringList fields;
    if (!readStatTail(pid, fields) || fields.size() <= 19) {
        return false;
    }

    bool okU = false;
    bool okS = false;
    bool okT = false;
    const unsigned long long utime = fields.at(11).toULongLong(&okU);
    const unsigned long long stime = fields.at(12).toULongLong(&okS);
    const unsigned long long start = fields.at(19).toULongLong(&okT);
    if (!okU || !okS || !okT || start == 0) {
        return false;
    }

    ticks = utime + stime;
    startTime = start;
    return true;
}

int onlineCpuCount() {
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    // procfs files report size 0, so atEnd()/readLine() loops are unreliable;
    // read the whole pseudo-file once and split it.
    const QByteArray data = file.readAll();
    int count = 0;
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        if (line.size() >= 4 && line.startsWith(QByteArrayLiteral("cpu"))
            && line.at(3) >= '0' && line.at(3) <= '9') {
            ++count;
        }
    }
    return count;
}

bool totalCpuTicks(unsigned long long &out) {
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QByteArray line = file.readLine().simplified();
    const QList<QByteArray> parts = line.split(' ');
    if (parts.isEmpty() || parts.first() != QByteArrayLiteral("cpu")) {
        return false;
    }

    constexpr int countedFields = 8;
    unsigned long long sum = 0;
    int counted = 0;
    for (int i = 1; i < parts.size() && counted < countedFields; ++i, ++counted) {
        bool ok = false;
        const unsigned long long value = parts.at(i).toULongLong(&ok);
        if (!ok) {
            break;
        }
        sum += value;
    }

    out = sum;
    return true;
}

bool rssKb(int pid, unsigned long long &out) {
    QFile file(procPath(pid, "status"));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    return parseVmRssKb(file.readAll(), out);
}

bool parseVmRssKb(const QByteArray &statusContent, unsigned long long &out) {
    const QList<QByteArray> lines = statusContent.split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("VmRSS:")) {
            continue;
        }

        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() < 2) {
            return false;
        }

        bool ok = false;
        const unsigned long long kb = parts.at(1).toULongLong(&ok);
        if (!ok) {
            return false;
        }

        out = kb;
        return true;
    }

    return false;
}

namespace {

unsigned long long parseKbValue(const QByteArray &line, bool &ok) {
    const QList<QByteArray> parts = line.simplified().split(' ');
    if (parts.size() < 2) {
        ok = false;
        return 0;
    }
    return parts.at(1).toULongLong(&ok);
}

} // namespace

bool parseStatusMem(const QByteArray &statusContent, MemInfo &out) {
    bool foundRss = false;

    const QList<QByteArray> lines = statusContent.split('\n');
    for (const QByteArray &line : lines) {
        bool ok = false;
        if (line.startsWith("VmRSS:")) {
            const unsigned long long kb = parseKbValue(line, ok);
            if (ok) {
                out.rssKb = kb;
                foundRss = true;
            }
        } else if (line.startsWith("VmSize:")) {
            const unsigned long long kb = parseKbValue(line, ok);
            if (ok) {
                out.vmsKb = kb;
            }
        } else if (line.startsWith("VmSwap:")) {
            const unsigned long long kb = parseKbValue(line, ok);
            if (ok) {
                out.swapKb = kb;
            }
        }
    }

    return foundRss;
}

bool parseStatusDetail(const QByteArray &statusContent, ProcDetail &out) {
    const QList<QByteArray> lines = statusContent.split('\n');
    for (const QByteArray &line : lines) {
        bool ok = false;
        if (line.startsWith("Threads:")) {
            const unsigned long long value = parseKbValue(line, ok);
            if (ok) {
                out.threads = static_cast<int>(value);
            }
        } else if (line.startsWith("voluntary_ctxt_switches:")) {
            const unsigned long long value = parseKbValue(line, ok);
            if (ok) {
                out.voluntaryCtx = value;
            }
        } else if (line.startsWith("nonvoluntary_ctxt_switches:")) {
            const unsigned long long value = parseKbValue(line, ok);
            if (ok) {
                out.involuntaryCtx = value;
            }
        }
    }
    return true;
}

bool parseProcIo(const QByteArray &content, ProcDetail &out) {
    bool found = false;
    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 2) {
            continue;
        }
        bool ok = false;
        const long long value = fields.at(1).toLongLong(&ok);
        if (!ok) {
            continue;
        }
        const QByteArray key = fields.at(0);
        if (key == "rchar:") {
            out.rchar = value;
            found = true;
        } else if (key == "wchar:") {
            out.wchar = value;
            found = true;
        } else if (key == "read_bytes:") {
            out.readBytes = value;
        } else if (key == "write_bytes:") {
            out.writeBytes = value;
        }
    }
    return found;
}

bool sharedLibraries(int pid, QList<QString> &out) {
    out.clear();
    QFile maps(procPath(pid, "maps"));
    if (!maps.open(QIODevice::ReadOnly)) {
        return false;
    }

    QSet<QString> seen;
    const QList<QByteArray> lines = maps.readAll().split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 6) {
            continue;
        }
        const QString path = QString::fromUtf8(fields.mid(5).join(' '));
        if (!path.contains(QStringLiteral(".so"))) {
            continue;
        }
        if (seen.contains(path)) {
            continue;
        }
        seen.insert(path);
        out.append(path);
        if (out.size() >= 200) {
            break;
        }
    }
    return !out.isEmpty();
}

int countFds(int pid) {
    const QString path = procPath(pid, "fd");
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir() || !info.isReadable()) {
        return -1;
    }
    const QStringList entries = QDir(path).entryList(QDir::NoDotAndDotDot);
    return entries.size();
}

namespace {

QString socketStateName(int code) {
    switch (code) {
    case 0x01:
        return QStringLiteral("ESTABLISHED");
    case 0x02:
        return QStringLiteral("SYN_SENT");
    case 0x03:
        return QStringLiteral("SYN_RECV");
    case 0x04:
        return QStringLiteral("FIN_WAIT1");
    case 0x05:
        return QStringLiteral("FIN_WAIT2");
    case 0x06:
        return QStringLiteral("TIME_WAIT");
    case 0x07:
        return QStringLiteral("CLOSE");
    case 0x08:
        return QStringLiteral("CLOSE_WAIT");
    case 0x09:
        return QStringLiteral("LAST_ACK");
    case 0x0A:
        return QStringLiteral("LISTEN");
    case 0x0B:
        return QStringLiteral("CLOSING");
    default:
        return QStringLiteral("UNKNOWN");
    }
}

QString ipv4FromHex(const QString &hex) {
    if (hex.size() != 8) {
        return hex;
    }
    bool ok = false;
    const quint32 value = hex.toUInt(&ok, 16);
    if (!ok) {
        return hex;
    }
    return QStringLiteral("%1.%2.%3.%4")
        .arg(value & 0xFF)
        .arg((value >> 8) & 0xFF)
        .arg((value >> 16) & 0xFF)
        .arg((value >> 24) & 0xFF);
}

QString ipv6FromHex(const QString &hex) {
    if (hex.size() != 32) {
        return hex;
    }

    quint16 groups[8];
    for (int w = 0; w < 4; ++w) {
        bool ok = false;
        const quint32 word = hex.mid(w * 8, 8).toUInt(&ok, 16);
        if (!ok) {
            return hex;
        }
        groups[w * 2] = static_cast<quint16>(((word & 0xFF) << 8)
                                             | ((word >> 8) & 0xFF));
        groups[w * 2 + 1] = static_cast<quint16>(((word >> 16) & 0xFF) << 8
                                                 | ((word >> 24) & 0xFF));
    }

    int bestStart = -1;
    int bestLen = 0;
    for (int i = 0; i < 8;) {
        if (groups[i] != 0) {
            ++i;
            continue;
        }
        int j = i;
        while (j < 8 && groups[j] == 0) {
            ++j;
        }
        if (j - i > bestLen) {
            bestLen = j - i;
            bestStart = i;
        }
        i = j;
    }

    QString out;
    for (int i = 0; i < 8; ++i) {
        if (i == bestStart && bestLen > 1) {
            out += QStringLiteral("::");
            i += bestLen - 1;
            continue;
        }
        if (!out.isEmpty() && !out.endsWith(QLatin1Char(':'))) {
            out += QLatin1Char(':');
        }
        out += QString::number(groups[i], 16);
    }
    return out.isEmpty() ? QStringLiteral("::") : out;
}

QByteArray readFileBytes(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

} // namespace

QList<unsigned long long> socketInodes(int pid) {
    QList<unsigned long long> inodes;
#if defined(Q_OS_WIN)
    Q_UNUSED(pid);
    return inodes;
#else
    const QString dir = procPath(pid, "fd");
    const QStringList entries =
        QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        // QFile::symLinkTarget() rewrites a relative target against the link's
        // directory ("socket:[123]" -> "/proc/<pid>/fd/socket:[123]"), so it
        // never matches on procfs. readlink() returns the raw target.
        const QByteArray path = (dir + QLatin1Char('/') + entry).toLocal8Bit();
        char buffer[128];
        const ssize_t length =
            ::readlink(path.constData(), buffer, sizeof(buffer) - 1);
        if (length <= 0) {
            continue;
        }
        const QByteArray target(buffer, static_cast<int>(length));
        if (!target.startsWith("socket:[") || !target.endsWith(']')) {
            continue;
        }
        bool ok = false;
        const unsigned long long inode =
            target.mid(8, target.size() - 9).toULongLong(&ok);
        if (ok) {
            inodes.append(inode);
        }
    }
    return inodes;
#endif
}

QList<QString> fdTargets(int pid) {
    QList<QString> targets;
#if defined(Q_OS_WIN)
    Q_UNUSED(pid);
    return targets;
#else
    const QString dir = procPath(pid, "fd");
    const QStringList entries =
        QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        const QByteArray path = (dir + QLatin1Char('/') + entry).toLocal8Bit();
        char buffer[512];
        const ssize_t length =
            ::readlink(path.constData(), buffer, sizeof(buffer) - 1);
        if (length <= 0) {
            continue;
        }
        targets.append(QString::fromLocal8Bit(buffer, static_cast<int>(length)));
        if (targets.size() >= 64) {
            break;
        }
    }
    return targets;
#endif
}

bool parseNetTable(const QByteArray &content, const QString &proto,
                   QList<SocketInfo> &out) {
    const QList<QByteArray> lines = content.split('\n');
    if (lines.isEmpty()) {
        return false;
    }

    const bool ipv6 = proto.endsWith(QLatin1Char('6'));
    for (int i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> fields = lines.at(i).simplified().split(' ');
        if (fields.size() < 10) {
            continue;
        }

        const QString local = QString::fromLatin1(fields.at(1));
        const QString remote = QString::fromLatin1(fields.at(2));
        const int localColon = local.indexOf(QLatin1Char(':'));
        const int remoteColon = remote.indexOf(QLatin1Char(':'));
        if (localColon < 0 || remoteColon < 0) {
            continue;
        }

        SocketInfo info;
        info.proto = proto;
        info.localAddr = ipv6 ? ipv6FromHex(local.left(localColon))
                              : ipv4FromHex(local.left(localColon));
        info.localPort = local.mid(localColon + 1).toUShort(nullptr, 16);
        info.remoteAddr = ipv6 ? ipv6FromHex(remote.left(remoteColon))
                               : ipv4FromHex(remote.left(remoteColon));
        info.remotePort = remote.mid(remoteColon + 1).toUShort(nullptr, 16);

        bool ok = false;
        const int state = fields.at(3).toInt(&ok, 16);
        info.state = ok ? socketStateName(state) : QStringLiteral("UNKNOWN");
        info.inode = fields.at(9).toULongLong();
        out.append(info);
    }
    return true;
}

QList<SocketInfo> readNetSockets() {
    QList<SocketInfo> all;
    const char *const files[] = {"tcp", "tcp6", "udp", "udp6"};
    for (const char *file : files) {
        QFile handle(QStringLiteral("/proc/net/%1").arg(QLatin1String(file)));
        if (!handle.open(QIODevice::ReadOnly)) {
            continue;
        }
        QList<SocketInfo> part;
        parseNetTable(handle.readAll(), QString::fromLatin1(file), part);
        all += part;
    }
    return all;
}

bool parsePidCgroup(const QByteArray &content, QString &version, QString &path) {
    const QList<QByteArray> lines = content.split('\n');
    bool any = false;
    bool unified = false;
    QString v1Path;

    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const QList<QByteArray> parts = trimmed.split(':');
        if (parts.size() < 3) {
            continue;
        }
        const QByteArray controllers = parts.at(1);
        const QString linePath = QString::fromUtf8(parts.mid(2).join(':'));
        if (controllers.isEmpty()) {
            unified = true;
            path = linePath;
            any = true;
        } else {
            if (v1Path.isEmpty() || controllers.contains("memory")) {
                v1Path = linePath;
            }
            any = true;
        }
    }

    if (!any) {
        return false;
    }
    if (unified) {
        version = QStringLiteral("v2");
        return true;
    }
    version = QStringLiteral("v1");
    path = v1Path;
    return true;
}

bool parseCgroupCpuStat(const QByteArray &content, CgroupInfo &out) {
    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 2) {
            continue;
        }
        bool ok = false;
        const unsigned long long value = fields.at(1).toULongLong(&ok);
        if (!ok) {
            continue;
        }
        const QByteArray key = fields.at(0);
        if (key == "usage_usec") {
            out.cpuUsageUsec = value;
        } else if (key == "user_usec") {
            out.cpuUserUsec = value;
        } else if (key == "system_usec") {
            out.cpuSystemUsec = value;
        } else if (key == "nr_periods") {
            out.nrPeriods = value;
        } else if (key == "nr_throttled") {
            out.nrThrottled = value;
        } else if (key == "throttled_usec") {
            out.throttledUsec = value;
        } else if (key == "throttled_time") {
            out.throttledUsec = value / 1000; // v1 reports nanoseconds
        }
    }
    return true;
}

bool parseCgroupMemoryValue(const QByteArray &content, long long &out) {
    const QByteArray trimmed = content.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    if (trimmed == "max") {
        out = -1;
        return true;
    }
    bool ok = false;
    const long long value = trimmed.toLongLong(&ok);
    if (!ok) {
        return false;
    }
    // v1 reports an enormous sentinel for "unlimited".
    out = (value > (1LL << 60)) ? -1 : value;
    return true;
}

bool parseCgroupMemoryStat(const QByteArray &content, CgroupInfo &out) {
    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 2) {
            continue;
        }
        bool ok = false;
        const unsigned long long value = fields.at(1).toULongLong(&ok);
        if (!ok) {
            continue;
        }
        const QByteArray key = fields.at(0);
        if (key == "anon") {
            out.memAnonBytes = value;
        } else if (key == "file") {
            out.memFileBytes = value;
        } else if (key == "kernel") {
            out.memKernelBytes = value;
        }
    }
    return true;
}

bool cgroupSample(int pid, CgroupInfo &out) {
    QFile cgroup(procPath(pid, "cgroup"));
    if (!cgroup.open(QIODevice::ReadOnly)) {
        return false;
    }

    QString version;
    QString path;
    if (!parsePidCgroup(cgroup.readAll(), version, path)) {
        return false;
    }
    out.version = version;
    out.path = path;

    const QString root = QStringLiteral("/sys/fs/cgroup");
    if (version == QStringLiteral("v2")) {
        const QString base = root + path;
        parseCgroupCpuStat(readFileBytes(base + QStringLiteral("/cpu.stat")), out);
        parseCgroupMemoryStat(readFileBytes(base + QStringLiteral("/memory.stat")), out);

        long long current = 0;
        if (parseCgroupMemoryValue(
                readFileBytes(base + QStringLiteral("/memory.current")), current)
            && current > 0) {
            out.memUsageBytes = static_cast<unsigned long long>(current);
        }
        long long limit = -1;
        if (parseCgroupMemoryValue(
                readFileBytes(base + QStringLiteral("/memory.max")), limit)) {
            out.memLimitBytes = limit;
        }
        out.available = true;
    } else {
        const QString memBase = root + QStringLiteral("/memory") + path;
        const QString cpuBase = root + QStringLiteral("/cpu,cpuacct") + path;

        parseCgroupCpuStat(readFileBytes(cpuBase + QStringLiteral("/cpu.stat")), out);
        const QByteArray usage =
            readFileBytes(cpuBase + QStringLiteral("/cpuacct.usage"));
        if (!usage.isEmpty()) {
            bool ok = false;
            const unsigned long long ns = usage.trimmed().toULongLong(&ok);
            if (ok) {
                out.cpuUsageUsec = ns / 1000;
            }
        }

        parseCgroupMemoryStat(readFileBytes(memBase + QStringLiteral("/memory.stat")), out);
        long long current = 0;
        if (parseCgroupMemoryValue(
                readFileBytes(memBase + QStringLiteral("/memory.usage_in_bytes")),
                current)
            && current > 0) {
            out.memUsageBytes = static_cast<unsigned long long>(current);
        }
        long long limit = -1;
        if (parseCgroupMemoryValue(
                readFileBytes(memBase + QStringLiteral("/memory.limit_in_bytes")),
                limit)) {
            out.memLimitBytes = limit;
        }
        out.available = true;
    }

    return true;
}

bool parseHostCpu(const QByteArray &statContent, HostCpuTimes &out) {
    const QList<QByteArray> lines = statContent.split('\n');
    if (lines.isEmpty()) {
        return false;
    }
    const QList<QByteArray> fields = lines.first().simplified().split(' ');
    if (fields.isEmpty() || fields.first() != QByteArrayLiteral("cpu")) {
        return false;
    }

    unsigned long long total = 0;
    unsigned long long idle = 0;
    for (int i = 1; i < fields.size() && i <= 8; ++i) {
        bool ok = false;
        const unsigned long long value = fields.at(i).toULongLong(&ok);
        if (!ok) {
            break;
        }
        total += value;
        if (i == 4 || i == 5) { // idle + iowait
            idle += value;
        }
    }

    if (total == 0) {
        return false;
    }
    out.total = total;
    out.idle = idle;
    return true;
}

bool parseMemInfo(const QByteArray &content, HostMem &out) {
    bool found = false;
    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray &line : lines) {
        bool ok = false;
        if (line.startsWith("MemTotal:")) {
            const unsigned long long value = parseKbValue(line, ok);
            if (ok) {
                out.totalKb = value;
                found = true;
            }
        } else if (line.startsWith("MemAvailable:")) {
            const unsigned long long value = parseKbValue(line, ok);
            if (ok) {
                out.availableKb = value;
            }
        } else if (line.startsWith("MemFree:")) {
            const unsigned long long value = parseKbValue(line, ok);
            if (ok) {
                out.freeKb = value;
            }
        }
    }
    return found;
}

bool parseNetDev(const QByteArray &content, NetTotals &out) {
    unsigned long long rx = 0;
    unsigned long long tx = 0;
    bool any = false;

    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        const int colon = trimmed.indexOf(':');
        if (colon < 0) {
            continue;
        }
        const QByteArray name = trimmed.left(colon).trimmed();
        if (name.isEmpty() || name == QByteArrayLiteral("lo")) {
            continue;
        }
        const QList<QByteArray> fields =
            trimmed.mid(colon + 1).simplified().split(' ');
        if (fields.size() < 9) {
            continue;
        }
        bool okRx = false;
        bool okTx = false;
        const unsigned long long rxBytes = fields.at(0).toULongLong(&okRx);
        const unsigned long long txBytes = fields.at(8).toULongLong(&okTx);
        if (!okRx || !okTx) {
            continue;
        }
        rx += rxBytes;
        tx += txBytes;
        any = true;
    }

    if (!any) {
        return false;
    }
    out.rxBytes = rx;
    out.txBytes = tx;
    return true;
}

bool parseLoadAvg(const QByteArray &content, double &l1, double &l5, double &l15) {
    const QList<QByteArray> fields = content.simplified().split(' ');
    if (fields.size() < 3) {
        return false;
    }
    bool ok1 = false;
    bool ok5 = false;
    bool ok15 = false;
    l1 = fields.at(0).toDouble(&ok1);
    l5 = fields.at(1).toDouble(&ok5);
    l15 = fields.at(2).toDouble(&ok15);
    return ok1 && ok5 && ok15;
}

bool hostCpuTimes(HostCpuTimes &out) {
    return parseHostCpu(readFileBytes(QStringLiteral("/proc/stat")), out);
}

bool hostMem(HostMem &out) {
    return parseMemInfo(readFileBytes(QStringLiteral("/proc/meminfo")), out);
}

bool hostNet(NetTotals &out) {
    return parseNetDev(readFileBytes(QStringLiteral("/proc/net/dev")), out);
}

bool hostInterfaces(QList<InterfaceStats> &out) {
    out.clear();
    return parseNetDevInterfaces(readFileBytes(QStringLiteral("/proc/net/dev")), out);
}

bool hostLoad(double &l1, double &l5, double &l15) {
    return parseLoadAvg(readFileBytes(QStringLiteral("/proc/loadavg")), l1, l5, l15);
}

bool diskUsage(const QString &path, unsigned long long &totalBytes,
               unsigned long long &freeBytes) {
#if defined(Q_OS_WIN)
    Q_UNUSED(path);
    Q_UNUSED(totalBytes);
    Q_UNUSED(freeBytes);
    // Never used on Windows (the WindowsBackend owns diskUsage there);
    // statvfs has no MinGW/MSVC equivalent in the POSIX form.
    return false;
#else
    struct statvfs stats;
    if (::statvfs(path.toLocal8Bit().constData(), &stats) != 0) {
        return false;
    }
    const unsigned long long fragment =
        stats.f_frsize != 0 ? stats.f_frsize : stats.f_bsize;
    totalBytes = static_cast<unsigned long long>(stats.f_blocks) * fragment;
    freeBytes = static_cast<unsigned long long>(stats.f_bavail) * fragment;
    return true;
#endif
}

bool parseNetDevInterfaces(const QByteArray &content, QList<InterfaceStats> &out) {
    const QList<QByteArray> lines = content.split('\n');
    bool any = false;
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        const int colon = trimmed.indexOf(':');
        if (colon < 0) {
            continue;
        }
        const QByteArray name = trimmed.left(colon).trimmed();
        if (name.isEmpty()) {
            continue;
        }
        const QList<QByteArray> fields =
            trimmed.mid(colon + 1).simplified().split(' ');
        if (fields.size() < 12) {
            continue;
        }

        bool ok = true;
        auto number = [&ok](const QByteArray &field) {
            bool parsed = false;
            const unsigned long long value = field.toULongLong(&parsed);
            ok = ok && parsed;
            return value;
        };

        InterfaceStats stats;
        stats.name = QString::fromUtf8(name);
        stats.rxBytes = number(fields.at(0));
        stats.rxPackets = number(fields.at(1));
        stats.rxErrs = number(fields.at(2));
        stats.rxDrop = number(fields.at(3));
        stats.txBytes = number(fields.at(8));
        stats.txPackets = number(fields.at(9));
        stats.txErrs = number(fields.at(10));
        stats.txDrop = number(fields.at(11));
        if (!ok) {
            continue;
        }
        out.append(stats);
        any = true;
    }
    return any;
}

double hostCpuBusyPercent(const HostCpuTimes &prev, const HostCpuTimes &cur) {
    const long long deltaTotal =
        static_cast<long long>(cur.total) - static_cast<long long>(prev.total);
    const long long deltaIdle =
        static_cast<long long>(cur.idle) - static_cast<long long>(prev.idle);
    if (deltaTotal <= 0) {
        return 0.0;
    }
    double busy = static_cast<double>(deltaTotal - deltaIdle)
                  / static_cast<double>(deltaTotal) * 100.0;
    if (busy < 0.0) {
        busy = 0.0;
    }
    if (busy > 100.0) {
        busy = 100.0;
    }
    return busy;
}

double netRatePerSec(unsigned long long prev, unsigned long long cur,
                     double seconds) {
    if (seconds <= 0.0 || cur < prev) {
        return 0.0;
    }
    return static_cast<double>(cur - prev) / seconds;
}

QString serviceForPort(quint16 port) {
    return portcatalog::serviceForPort(port);
}

bool portIsDatabase(quint16 port) {
    return portcatalog::portIsDatabase(port);
}

QString endpointExposure(const QString &addr) {
    return portcatalog::endpointExposure(addr);
}

bool parseInstrumentation(const QByteArray &envData, const QByteArray &maps,
                          QList<QString> &agents) {
    QSet<QString> found;

    const QList<QByteArray> entries = envData.split('\0');
    for (const QByteArray &entry : entries) {
        const int equals = entry.indexOf('=');
        if (equals <= 0) {
            continue;
        }
        const QByteArray key = entry.left(equals);
        if (key.startsWith("DD_TRACE") || key.startsWith("DD_SERVICE")
            || key.startsWith("DD_ENV") || key.startsWith("DD_AGENT")
            || key.startsWith("DD_PROFILING") || key.startsWith("DD_RUM")
            || key.startsWith("DD_LOGS")) {
            found.insert(QStringLiteral("datadog"));
        } else if (key.startsWith("OTEL_")) {
            found.insert(QStringLiteral("opentelemetry"));
        } else if (key.startsWith("ELASTIC_APM_")) {
            found.insert(QStringLiteral("elastic-apm"));
        } else if (key.startsWith("NEW_RELIC_")) {
            found.insert(QStringLiteral("newrelic"));
        } else if (key == "SENTRY_DSN") {
            found.insert(QStringLiteral("sentry"));
        }
    }

    const QString libs = QString::fromLatin1(maps);
    if (libs.contains(QStringLiteral("libdatadog"))
        || libs.contains(QStringLiteral("libddtrace"))
        || libs.contains(QStringLiteral("libdd_profiling"))) {
        found.insert(QStringLiteral("datadog"));
    }
    if (libs.contains(QStringLiteral("libopentelemetry"))) {
        found.insert(QStringLiteral("opentelemetry"));
    }
    if (libs.contains(QStringLiteral("libnewrelic"))) {
        found.insert(QStringLiteral("newrelic"));
    }
    if (libs.contains(QStringLiteral("libelasticapm"))) {
        found.insert(QStringLiteral("elastic-apm"));
    }
    if (libs.contains(QStringLiteral("libsentry"))) {
        found.insert(QStringLiteral("sentry"));
    }
    if (libs.contains(QStringLiteral("libpyroscope"))) {
        found.insert(QStringLiteral("pyroscope"));
    }

    agents = found.values();
    std::sort(agents.begin(), agents.end());
    return !agents.isEmpty();
}

bool processInstrumentation(int pid, QList<QString> &agents) {
    agents.clear();
    const QByteArray envData = readFileBytes(procPath(pid, "environ"));
    const QByteArray maps = readFileBytes(procPath(pid, "maps"));
    return parseInstrumentation(envData, maps, agents);
}

bool parseStatmShared(const QByteArray &statmContent, unsigned long long pageSize,
                      unsigned long long &sharedKb) {
    const QList<QByteArray> parts = statmContent.simplified().split(' ');
    if (parts.size() < 3) {
        return false;
    }

    bool ok = false;
    const unsigned long long pages = parts.at(2).toULongLong(&ok);
    if (!ok) {
        return false;
    }

    sharedKb = pages * pageSize / 1024;
    return true;
}

bool procMem(int pid, MemInfo &out) {
    QFile status(procPath(pid, "status"));
    if (!status.open(QIODevice::ReadOnly)) {
        return false;
    }
    if (!parseStatusMem(status.readAll(), out)) {
        return false;
    }

    QFile statm(procPath(pid, "statm"));
    if (statm.open(QIODevice::ReadOnly)) {
#if defined(Q_OS_WIN)
        const long page = 4096;
#else
        const long page = ::sysconf(_SC_PAGESIZE);
#endif
        const unsigned long long pageSize =
            page > 0 ? static_cast<unsigned long long>(page) : 4096ULL;
        unsigned long long shared = 0;
        if (parseStatmShared(statm.readAll(), pageSize, shared)) {
            out.sharedKb = shared;
        }
    }

    return true;
}

bool procSample(int pid, MemInfo &mem, ProcDetail &detail) {
    QFile status(procPath(pid, "status"));
    if (!status.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray content = status.readAll();
    if (!parseStatusMem(content, mem)) {
        return false;
    }
    parseStatusDetail(content, detail);

    QFile io(procPath(pid, "io"));
    if (io.open(QIODevice::ReadOnly)) {
        parseProcIo(io.readAll(), detail);
    }

    QFile statm(procPath(pid, "statm"));
    if (statm.open(QIODevice::ReadOnly)) {
#if defined(Q_OS_WIN)
        const long page = 4096;
#else
        const long page = ::sysconf(_SC_PAGESIZE);
#endif
        const unsigned long long pageSize =
            page > 0 ? static_cast<unsigned long long>(page) : 4096ULL;
        unsigned long long shared = 0;
        if (parseStatmShared(statm.readAll(), pageSize, shared)) {
            mem.sharedKb = shared;
        }
    }

    detail.fdCount = countFds(pid);
    return true;
}

bool sumRssKbWith(const QList<int> &pids, const RssReader &reader, unsigned long long &out) {
    unsigned long long total = 0;
    bool any = false;

    for (int pid : pids) {
        unsigned long long kb = 0;
        if (reader && reader(pid, kb)) {
            total += kb;
            any = true;
        }
    }

    if (!any) {
        return false;
    }

    out = total;
    return true;
}

bool sumRssKb(const QList<int> &pids, unsigned long long &out) {
    return sumRssKbWith(
        pids, [](int pid, unsigned long long &kb) { return rssKb(pid, kb); }, out);
}

} // namespace linuxproc
