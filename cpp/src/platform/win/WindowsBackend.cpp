#include "platform/win/WindowsBackend.h"

#if defined(Q_OS_WIN)

#include "TcpStatus.h"

#include <QDateTime>
#include <QHash>
#include <QMutexLocker>

#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>

namespace {

constexpr qint64 kSnapshotTtlMs = 600;

ULONGLONG fileTimeQuad(const FILETIME &ft) {
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

unsigned long long fileTimeToEpoch(const FILETIME &ft) {
    constexpr ULONGLONG kEpochDelta100ns = 116444736000000000ULL;
    const ULONGLONG quad = fileTimeQuad(ft);
    if (quad < kEpochDelta100ns) {
        return 0;
    }
    return (quad - kEpochDelta100ns) / 10000000ULL;
}

QString wideToQString(const wchar_t *value, size_t maxLength) {
    const size_t len = wcsnlen_s(value, maxLength);
    return QString::fromWCharArray(value, static_cast<int>(len));
}

bool ipToString(int family, const void *addr, QString &out) {
    char buf[64] = {};
    if (InetNtopA(family, const_cast<void *>(addr), buf, sizeof(buf))
        == nullptr) {
        return false;
    }
    out = QString::fromLatin1(buf);
    return true;
}

QString tcpStateName(int state) {
    switch (state) {
    case MIB_TCP_STATE_CLOSED:
        return QStringLiteral("CLOSE");
    case MIB_TCP_STATE_LISTEN:
        return QStringLiteral("LISTEN");
    case MIB_TCP_STATE_SYN_SENT:
        return QStringLiteral("SYN_SENT");
    case MIB_TCP_STATE_SYN_RCVD:
        return QStringLiteral("SYN_RECV");
    case MIB_TCP_STATE_ESTAB:
        return QStringLiteral("ESTABLISHED");
    case MIB_TCP_STATE_FIN_WAIT1:
        return QStringLiteral("FIN_WAIT1");
    case MIB_TCP_STATE_FIN_WAIT2:
        return QStringLiteral("FIN_WAIT2");
    case MIB_TCP_STATE_CLOSE_WAIT:
        return QStringLiteral("CLOSE_WAIT");
    case MIB_TCP_STATE_CLOSING:
        return QStringLiteral("CLOSING");
    case MIB_TCP_STATE_LAST_ACK:
        return QStringLiteral("LAST_ACK");
    case MIB_TCP_STATE_TIME_WAIT:
        return QStringLiteral("TIME_WAIT");
    default:
        return QStringLiteral("UNKNOWN");
    }
}

bool appendTcp(bool v6, QList<linuxproc::SocketInfo> &out) {
    const DWORD family = v6 ? AF_INET6 : AF_INET;
    DWORD size = 0;
    if (::GetExtendedTcpTable(nullptr, &size, FALSE, family,
                              TCP_TABLE_OWNER_PID_ALL, 0)
        != ERROR_MORE_DATA
        || size == 0) {
        return false;
    }
    std::vector<BYTE> buffer(size);
    if (::GetExtendedTcpTable(buffer.data(), &size, FALSE, family,
                              TCP_TABLE_OWNER_PID_ALL, 0)
        != NO_ERROR) {
        return false;
    }
    const auto pidPort = [](DWORD value) -> quint16 {
        return ntohs(static_cast<WORD>(value));
    };
    if (v6) {
        const auto *table =
            reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_TCP6ROW_OWNER_PID &row = table->table[i];
            linuxproc::SocketInfo info;
            info.proto = QStringLiteral("tcp6");
            ipToString(AF_INET6, row.ucLocalAddr, info.localAddr);
            info.localPort = pidPort(row.dwLocalPort);
            ipToString(AF_INET6, row.ucRemoteAddr, info.remoteAddr);
            info.remotePort = pidPort(row.dwRemotePort);
            info.state = tcpStateName(static_cast<int>(row.dwState));
            out.append(info);
        }
    } else {
        const auto *table =
            reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_TCPROW_OWNER_PID &row = table->table[i];
            linuxproc::SocketInfo info;
            info.proto = QStringLiteral("tcp");
            ipToString(AF_INET, &row.dwLocalAddr, info.localAddr);
            info.localPort = pidPort(row.dwLocalPort);
            ipToString(AF_INET, &row.dwRemoteAddr, info.remoteAddr);
            info.remotePort = pidPort(row.dwRemotePort);
            info.state = tcpStateName(static_cast<int>(row.dwState));
            out.append(info);
        }
    }
    return true;
}

bool appendUdp(bool v6, QList<linuxproc::SocketInfo> &out) {
    const DWORD family = v6 ? AF_INET6 : AF_INET;
    DWORD size = 0;
    if (::GetExtendedUdpTable(nullptr, &size, FALSE, family,
                              UDP_TABLE_OWNER_PID, 0)
        != ERROR_MORE_DATA
        || size == 0) {
        return false;
    }
    std::vector<BYTE> buffer(size);
    if (::GetExtendedUdpTable(buffer.data(), &size, FALSE, family,
                              UDP_TABLE_OWNER_PID, 0)
        != NO_ERROR) {
        return false;
    }
    const auto pidPort = [](DWORD value) -> quint16 {
        return ntohs(static_cast<WORD>(value));
    };
    if (v6) {
        const auto *table =
            reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_UDP6ROW_OWNER_PID &row = table->table[i];
            linuxproc::SocketInfo info;
            info.proto = QStringLiteral("udp6");
            ipToString(AF_INET6, row.ucLocalAddr, info.localAddr);
            info.localPort = pidPort(row.dwLocalPort);
            info.state = QStringLiteral("CLOSE");
            out.append(info);
        }
    } else {
        const auto *table =
            reinterpret_cast<const MIB_UDPTABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_UDPROW_OWNER_PID &row = table->table[i];
            linuxproc::SocketInfo info;
            info.proto = QStringLiteral("udp");
            ipToString(AF_INET, &row.dwLocalAddr, info.localAddr);
            info.localPort = pidPort(row.dwLocalPort);
            info.state = QStringLiteral("CLOSE");
            out.append(info);
        }
    }
    return true;
}

} // namespace

WindowsBackend::WindowsBackend() {
    WSADATA data = {};
    m_wsaReady = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    TcpCounters probe;
    m_tcpCountersOk = readTcpCounters(probe) && !tcpCountersRedacted(probe);
}

WindowsBackend::~WindowsBackend() {
    if (m_wsaReady) {
        ::WSACleanup();
    }
}

QString WindowsBackend::platformName() const {
    return QStringLiteral("windows");
}

PlatformCapabilities WindowsBackend::capabilities() const {
    PlatformCapabilities caps;
    caps.fim = true;
    caps.systemServices = true;
    caps.journal = true;
    caps.tcpCounters = m_tcpCountersOk;
    return caps;
}

bool WindowsBackend::hostCpuTimes(linuxproc::HostCpuTimes &out) {
    FILETIME idleFt = {}, kernelFt = {}, userFt = {};
    if (!::GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
        return false;
    }
    const ULONGLONG idle = fileTimeQuad(idleFt);
    const ULONGLONG kernel = fileTimeQuad(kernelFt);
    const ULONGLONG user = fileTimeQuad(userFt);
    // GetSystemTimes' kernel time includes idle; subtract so "idle" obeys
    // the /proc/stat convention (idle counted within total, subtracted to
    // compute busy, matching LinuxBackend::hostCpuTimes semantics).
    const ULONGLONG idleClamped = qMin<ULONGLONG>(idle, kernel);
    out.idle = idleClamped;
    out.total = idleClamped + (kernel - idleClamped) + user;
    return out.total > 0;
}

bool WindowsBackend::totalCpuTicks(unsigned long long &out) {
    FILETIME idleFt = {}, kernelFt = {}, userFt = {};
    if (!::GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
        return false;
    }
    // Same 100 ns units as GetProcessTimes so the per-process percent in
    // CpuSampler divides two values with identical scaling.
    out = fileTimeQuad(idleFt) + fileTimeQuad(kernelFt)
          + fileTimeQuad(userFt);
    return true;
}

int WindowsBackend::onlineCpuCount() {
    SYSTEM_INFO info = {};
    ::GetNativeSystemInfo(&info);
    const int count = static_cast<int>(info.dwNumberOfProcessors);
    return count > 0 ? count : 1;
}

bool WindowsBackend::hostMem(linuxproc::HostMem &out) {
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);
    if (!::GlobalMemoryStatusEx(&status)) {
        return false;
    }
    out.totalKb = status.ullTotalPhys >> 10;
    out.availableKb = status.ullAvailPhys >> 10;
    out.freeKb = status.ullAvailPhys >> 10;
    return out.totalKb > 0;
}

bool WindowsBackend::hostNet(linuxproc::NetTotals &out) {
    out = linuxproc::NetTotals{};
    PMIB_IF_TABLE2 table = nullptr;
    if (::GetIfTable2(&table) != NO_ERROR || table == nullptr) {
        return false;
    }
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2 &row = table->Table[i];
        out.rxBytes += row.InOctets;
        out.txBytes += row.OutOctets;
    }
    ::FreeMibTable(table);
    return true;
}

bool WindowsBackend::hostInterfaces(QList<linuxproc::InterfaceStats> &out) {
    out.clear();
    PMIB_IF_TABLE2 table = nullptr;
    if (::GetIfTable2(&table) != NO_ERROR || table == nullptr) {
        return false;
    }
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2 &row = table->Table[i];
        linuxproc::InterfaceStats stats;
        stats.name = wideToQString(row.Alias, IF_MAX_STRING_SIZE + 1);
        if (stats.name.isEmpty()) {
            stats.name = wideToQString(row.Description,
                                       IF_MAX_STRING_SIZE + 1);
        }
        if (stats.name.isEmpty()) {
            stats.name = QStringLiteral("if%1").arg(row.InterfaceIndex);
        }
        stats.rxBytes = row.InOctets;
        stats.txBytes = row.OutOctets;
        stats.rxPackets = row.InUcastPkts + row.InNUcastPkts;
        stats.txPackets = row.OutUcastPkts + row.OutNUcastPkts;
        stats.rxErrs = row.InErrors;
        stats.txErrs = row.OutErrors;
        stats.rxDrop = row.InDiscards;
        stats.txDrop = row.OutDiscards;
        out.append(stats);
    }
    ::FreeMibTable(table);
    return true;
}

bool WindowsBackend::hostLoad(double &l1, double &l5, double &l15) {
    Q_UNUSED(l1);
    Q_UNUSED(l5);
    Q_UNUSED(l15);
    // Windows has no load average; the host strip renders "--".
    return false;
}

bool WindowsBackend::diskUsage(const QString &path, unsigned long long &totalBytes,
                               unsigned long long &freeBytes) {
    const wchar_t *dir = nullptr;
    std::wstring wide;
    if (!path.isEmpty() && !path.startsWith(QLatin1Char('/'))) {
        wide = path.toStdWString();
        dir = wide.c_str();
    }
    ULARGE_INTEGER total = {}, availFree = {}, totalFree = {};
    if (!::GetDiskFreeSpaceExW(dir, &availFree, &total, &totalFree)) {
        return false;
    }
    totalBytes = total.QuadPart;
    freeBytes = totalFree.QuadPart;
    return true;
}

WindowsBackend::Snapshot WindowsBackend::snapshot() const {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker locker(&m_mutex);
    if (!m_snapshot.pids.isEmpty()
        && (now - m_snapshot.builtAt) <= kSnapshotTtlMs) {
        return m_snapshot;
    }

    Snapshot fresh;
    fresh.builtAt = now;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (::Process32FirstW(snap, &entry)) {
            do {
                const int pid = static_cast<int>(entry.th32ProcessID);
                if (pid <= 0) {
                    continue;
                }
                fresh.pids.append(pid);
                fresh.ppid.insert(pid, entry.th32ParentProcessID);
                fresh.threads.insert(pid, entry.cntThreads);
                fresh.image.insert(pid,
                                   wideToQString(entry.szExeFile, MAX_PATH));
            } while (::Process32NextW(snap, &entry));
        }
        ::CloseHandle(snap);
    }
    m_snapshot = std::move(fresh);
    return m_snapshot;
}

bool WindowsBackend::exists(int pid) {
    if (pid <= 0) {
        return false;
    }
    HANDLE handle = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_ACCESS_DENIED || err == ERROR_INVALID_PARAMETER) {
            return err == ERROR_ACCESS_DENIED;
        }
        return false;
    }
    ::CloseHandle(handle);
    return true;
}

QString WindowsBackend::comm(int pid) {
    const Snapshot snap = snapshot();
    const auto it = snap.image.constFind(pid);
    return it != snap.image.constEnd() ? it.value() : QString();
}

int WindowsBackend::parentPid(int pid) {
    const Snapshot snap = snapshot();
    const auto it = snap.ppid.constFind(pid);
    return it != snap.ppid.constEnd()
               ? static_cast<int>(it.value())
               : -1;
}

QList<int> WindowsBackend::allPids() {
    return snapshot().pids;
}

unsigned long long WindowsBackend::startTime(int pid) {
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return 0;
    }
    FILETIME creation = {}, exitFt = {}, kernelFt = {}, userFt = {};
    if (!::GetProcessTimes(handle, &creation, &exitFt, &kernelFt, &userFt)) {
        ::CloseHandle(handle);
        return 0;
    }
    ::CloseHandle(handle);
    return fileTimeToEpoch(creation);
}

ProcessIdentity WindowsBackend::identity(int pid) {
    ProcessIdentity result;
    result.pid = pid;
    result.startTime = startTime(pid);
    return result;
}

QString WindowsBackend::exePath(int pid) {
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return QString();
    }
    wchar_t buffer[1024] = {};
    DWORD size = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    const QString path =
        ::QueryFullProcessImageNameW(handle, 0, buffer, &size)
            ? QString::fromWCharArray(buffer, static_cast<int>(size))
            : QString();
    ::CloseHandle(handle);
    return path;
}

bool WindowsBackend::procCpu(int pid, unsigned long long &ticks,
                             unsigned long long &start) {
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return false;
    }
    FILETIME creation = {}, exitFt = {}, kernelFt = {}, userFt = {};
    if (!::GetProcessTimes(handle, &creation, &exitFt, &kernelFt, &userFt)) {
        ::CloseHandle(handle);
        return false;
    }
    ::CloseHandle(handle);
    // Same 100 ns units as totalCpuTicks above.
    ticks = fileTimeQuad(kernelFt) + fileTimeQuad(userFt);
    start = fileTimeToEpoch(creation);
    return start > 0;
}

bool WindowsBackend::rssKb(int pid, unsigned long long &out) {
    linuxproc::MemInfo mem;
    if (!procMem(pid, mem) || mem.rssKb == 0) {
        return false;
    }
    out = mem.rssKb;
    return true;
}

bool WindowsBackend::procMem(int pid, linuxproc::MemInfo &out) {
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return false;
    }
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    const BOOL ok = ::GetProcessMemoryInfo(
        handle, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
        sizeof(counters));
    ::CloseHandle(handle);
    if (!ok) {
        return false;
    }
    out.rssKb = counters.WorkingSetSize >> 10;
    // PrivateUsage is the committed private bytes (PROCESS_MEMORY_COUNTERS_EX).
    out.vmsKb = counters.PrivateUsage > 0
                    ? static_cast<unsigned long long>(counters.PrivateUsage)
                          >> 10
                    : 0;
    return out.rssKb > 0;
}

bool WindowsBackend::procSample(int pid, linuxproc::MemInfo &mem,
                                linuxproc::ProcDetail &detail) {
    bool ok = procMem(pid, mem);

    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  static_cast<DWORD>(pid));
    if (handle != nullptr) {
        DWORD handleCount = 0;
        if (::GetProcessHandleCount(handle, &handleCount)) {
            detail.fdCount = static_cast<int>(handleCount);
        }
        IO_COUNTERS io = {};
        if (::GetProcessIoCounters(handle, &io)) {
            detail.rchar = static_cast<long long>(io.ReadTransferCount);
            detail.wchar = static_cast<long long>(io.WriteTransferCount);
            detail.readBytes = static_cast<long long>(io.ReadTransferCount);
            detail.writeBytes =
                static_cast<long long>(io.WriteTransferCount);
        }
        ::CloseHandle(handle);
        ok = true;
    }

    const Snapshot snap = snapshot();
    const auto threads = snap.threads.constFind(pid);
    if (threads != snap.threads.constEnd()) {
        detail.threads = static_cast<int>(threads.value());
    }
    return ok;
}

int WindowsBackend::countFds(int pid) {
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return -1;
    }
    DWORD handleCount = 0;
    const BOOL ok = ::GetProcessHandleCount(handle, &handleCount);
    ::CloseHandle(handle);
    return ok ? static_cast<int>(handleCount) : -1;
}

QList<unsigned long long> WindowsBackend::socketInodes(int pid) {
    Q_UNUSED(pid);
    // Windows fd->socket attribution uses handles, not inodes; the FD
    // instrumentation path stays Linux/mac-only.
    return QList<unsigned long long>();
}

QList<linuxproc::SocketInfo> WindowsBackend::readNetSockets() {
    QList<linuxproc::SocketInfo> out;
    appendTcp(false, out);
    appendTcp(true, out);
    appendUdp(false, out);
    appendUdp(true, out);
    return out;
}

bool WindowsBackend::readTcpCounters(TcpCounters &out) {
    out = TcpCounters{};
    bool any = false;
    for (DWORD family : {AF_INET, AF_INET6}) {
        MIB_TCPSTATS stats = {};
        if (::GetTcpStatisticsEx(&stats, family) != NO_ERROR) {
            continue;
        }
        const auto acc = [&out](const char *key, DWORD value) {
            out.tcp.insert(QLatin1String(key),
                           out.tcp.value(QLatin1String(key), 0)
                               + static_cast<quint64>(value));
        };
        acc("RtoAlgorithm", stats.dwRtoAlgorithm);
        acc("ActiveOpens", stats.dwActiveOpens);
        acc("PassiveOpens", stats.dwPassiveOpens);
        acc("AttemptFails", stats.dwAttemptFails);
        acc("EstabResets", stats.dwEstabResets);
        acc("CurrEstab", stats.dwCurrEstab);
        acc("InSegs", stats.dwInSegs);
        acc("OutSegs", stats.dwOutSegs);
        acc("RetransSegs", stats.dwRetransSegs);
        acc("InErrs", stats.dwInErrs);
        acc("OutRsts", stats.dwOutRsts);
        any = true;
    }
    return any;
}
#endif

QList<ServiceRecord> WindowsBackend::readServices() {
    QList<ServiceRecord> out;

    const SC_HANDLE mgr = ::OpenSCManagerW(nullptr, nullptr,
                                           SC_MANAGER_ENUMERATE_SERVICE);
    if (mgr == nullptr) {
        return out;
    }

    DWORD bytes = 0;
    DWORD count = 0;
    DWORD resume = 0;
    BOOL success = FALSE;
    QByteArray buffer;

    for (int attempt = 0; attempt < 4 && !success; ++attempt) {
        const DWORD need = (attempt > 0) ? bytes : 0;
        buffer.resize(need > 0 ? int(need) : 65536);
        count = 0;
        resume = 0;
        success = ::EnumServicesStatusExW(mgr, SC_ENUM_PROCESS_INFO,
                                          SERVICE_WIN32, SERVICE_STATE_ALL,
                                          buffer.data(),
                                          DWORD(buffer.size()), &bytes, &count,
                                          &resume, nullptr);
    }

    if (success) {
        auto *rows = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW *>(
            buffer.data());
        for (DWORD i = 0; i < count; ++i) {
            ServiceRecord rec;
            rec.name = QString::fromWCharArray(
                rows[i].lpServiceName != nullptr ? rows[i].lpServiceName
                                                 : L"");
            rec.displayName = QString::fromWCharArray(
                rows[i].lpDisplayName != nullptr ? rows[i].lpDisplayName
                                                 : L"");
            rec.state = int(rows[i].ServiceStatus.dwCurrentState);
            rec.exitCode = rows[i].ServiceStatus.dwWin32ExitCode;
            rec.processId = int(rows[i].ServiceStatus.dwProcessId);
            out.append(rec);
        }
    }

    ::CloseServiceHandle(mgr);
    return out;
}
