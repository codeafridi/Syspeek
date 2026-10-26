#include "platform/win/WindowsBackend.h"

#if defined(Q_OS_WIN)

#include "TcpStatus.h"

#include <QDateTime>
#include <QHash>
#include <QMutexLocker>
#include <QSettings>

#include <algorithm>

#include <cstddef>
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
#include <pdh.h>
#include <tlhelp32.h>

namespace {

constexpr qint64 kSnapshotTtlMs = 600;

// ---- ntdll process depth (M50e), grounded on phnt ntexapi.h (the header
// set shipped with SystemInformer's sources; the shipped SystemInformer
// binary imports NtQuerySystemInformation) ----
// x64 SYSTEM_THREAD_INFORMATION (phnt order; compiler alignment yields
// contextSwitches at 0x40, stride 0x50):
struct NtThreadInfo {
    LARGE_INTEGER kernelTime;
    LARGE_INTEGER userTime;
    LARGE_INTEGER createTime;
    ULONG waitTime;
    PVOID startAddress;
    PVOID clientId[2];
    LONG priority;
    LONG basePriority;
    ULONG contextSwitches;
    ULONG threadState;
    ULONG waitReason;
};
// x64 SYSTEM_PROCESS_INFORMATION (phnt order; threads at 0x100):
struct NtProcInfo {
    ULONG nextEntryOffset;
    ULONG numberOfThreads;
    ULONGLONG workingSetPrivateSize;
    ULONG hardFaultCount;
    ULONG numberOfThreadsHighWatermark;
    ULONGLONG cycleTime;
    LARGE_INTEGER createTime;
    LARGE_INTEGER userTime;
    LARGE_INTEGER kernelTime;
    USHORT imageNameLength;
    USHORT imageNameMaximumLength;
    ULONG imageNamePad; // UNICODE_STRING x64: 2+2+pad4+ptr = 16 bytes
    wchar_t *imageNameBuffer;
    LONG basePriority;
    HANDLE uniqueProcessId;
    HANDLE inheritedFromUniqueProcessId;
    ULONG handleCount;
    ULONG sessionId;
    ULONG_PTR uniqueProcessKey;
    SIZE_T peakVirtualSize;
    SIZE_T virtualSize;
    ULONG pageFaultCount;
    SIZE_T peakWorkingSetSize;
    SIZE_T workingSetSize;
    SIZE_T quotaPeakPagedPoolUsage;
    SIZE_T quotaPagedPoolUsage;
    SIZE_T quotaPeakNonPagedPoolUsage;
    SIZE_T quotaNonPagedPoolUsage;
    SIZE_T pagefileUsage;
    SIZE_T peakPagefileUsage;
    SIZE_T privatePageCount;
    LARGE_INTEGER readOperationCount;
    LARGE_INTEGER writeOperationCount;
    LARGE_INTEGER otherOperationCount;
    LARGE_INTEGER readTransferCount;
    LARGE_INTEGER writeTransferCount;
    LARGE_INTEGER otherTransferCount;
    NtThreadInfo threads[1];
};
static_assert(offsetof(NtThreadInfo, contextSwitches) == 0x40,
              "NtThreadInfo x64 layout");
static_assert(offsetof(NtProcInfo, threads) == 0x100,
              "NtProcInfo x64 layout");

constexpr ULONG kSystemProcessInformation = 5; // phnt SYSTEMINFORMATIONCLASS
constexpr LONG kStatusInfoLengthMismatch = 0xC0000004;

using NtQuerySystemInformationFn = LONG(WINAPI *)(ULONG, PVOID, ULONG,
                                                  PULONG);
using NtQueryInformationProcessFn = LONG(WINAPI *)(HANDLE, ULONG, PVOID,
                                                   ULONG, PULONG);

NtQuerySystemInformationFn ntQuerySystemInformation() {
    static NtQuerySystemInformationFn fn = []() {
        const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        return ntdll != nullptr
                   ? reinterpret_cast<NtQuerySystemInformationFn>(
                         ::GetProcAddress(ntdll, "NtQuerySystemInformation"))
                   : nullptr;
    }();
    return fn;
}

NtQueryInformationProcessFn ntQueryInformationProcess() {
    static NtQueryInformationProcessFn fn = []() {
        const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        return ntdll != nullptr
                   ? reinterpret_cast<NtQueryInformationProcessFn>(
                         ::GetProcAddress(ntdll,
                                          "NtQueryInformationProcess"))
                   : nullptr;
    }();
    return fn;
}

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
    closePdhDiskQuery();
    closePdhGpuQuery();
    if (m_wsaReady) {
        ::WSACleanup();
    }
}

bool WindowsBackend::ensurePdhDiskQuery() {
    if (m_pdhQuery != nullptr) {
        return true;
    }

    PDH_HQUERY query = nullptr;
    if (::PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
        return false;
    }

    static const wchar_t *kPaths[] = {
        L"\\PhysicalDisk(*)\\Disk Reads/sec",
        L"\\PhysicalDisk(*)\\Disk Writes/sec",
        L"\\PhysicalDisk(*)\\Disk Read Bytes/sec",
        L"\\PhysicalDisk(*)\\Disk Write Bytes/sec",
        L"\\PhysicalDisk(*)\\Avg. Disk sec/Read",
        L"\\PhysicalDisk(*)\\Avg. Disk sec/Write",
        L"\\PhysicalDisk(*)\\Current Disk Queue Length",
        L"\\PhysicalDisk(*)\\% Idle Time",
        L"\\PhysicalDisk(*)\\Avg. Disk Read Queue Length",
        L"\\PhysicalDisk(*)\\Avg. Disk Write Queue Length",
    };

    QList<void *> handles;
    for (const wchar_t *path : kPaths) {
        PDH_HCOUNTER handle = nullptr;
        if (::PdhAddEnglishCounterW(query, path, 0, &handle)
            != ERROR_SUCCESS) {
            ::PdhCloseQuery(query);
            return false;
        }
        handles.append(static_cast<void *>(handle));
    }

    LONGLONG timeBase = 0;
    if (::PdhGetCounterTimeBase(static_cast<PDH_HCOUNTER>(handles.at(0)),
                                &timeBase)
            != ERROR_SUCCESS
        || timeBase <= 0) {
        timeBase = 10000000; // classic 100 ns perf time base
    }

    if (::PdhCollectQueryData(query) != ERROR_SUCCESS) {
        ::PdhCloseQuery(query);
        return false;
    }

    m_pdhQuery = static_cast<void *>(query);
    m_pdhDiskHandles = handles;
    m_pdhTimeBase = timeBase;
    return true;
}

bool WindowsBackend::ensurePdhGpuQuery() {
    if (m_pdhGpuQuery != nullptr) {
        return true;
    }
    PDH_HQUERY query = nullptr;
    if (::PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
        return false;
    }
    static const wchar_t *kPaths[] = {
        L"\\GPU Engine(*)\\Utilization Percentage",
        L"\\GPU Process Memory(*)\\Dedicated Usage",
        L"\\GPU Adapter Memory(*)\\Dedicated Usage",
    };
    QList<void *> handles;
    for (const wchar_t *path : kPaths) {
        PDH_HCOUNTER handle = nullptr;
        if (::PdhAddEnglishCounterW(query, path, 0, &handle)
            != ERROR_SUCCESS) {
            ::PdhCloseQuery(query);
            return false;
        }
        handles.append(static_cast<void *>(handle));
    }
    ::PdhCollectQueryData(query);
    m_pdhGpuQuery = static_cast<void *>(query);
    m_pdhGpuHandles = handles;
    return true;
}

void WindowsBackend::closePdhGpuQuery() {
    if (m_pdhGpuQuery != nullptr) {
        ::PdhCloseQuery(static_cast<PDH_HQUERY>(m_pdhGpuQuery));
        m_pdhGpuQuery = nullptr;
        m_pdhGpuHandles.clear();
    }
}

GpuReading WindowsBackend::gpuInfo() {
    QMutexLocker locker(&m_pdhGpuMutex);
    GpuReading reading;
    if (!ensurePdhGpuQuery()) {
        reading.note = QStringLiteral(
            "gpu: GPU performance counters are unavailable on this system");
        return reading;
    }
    if (::PdhCollectQueryData(static_cast<PDH_HQUERY>(m_pdhGpuQuery))
        != ERROR_SUCCESS) {
        reading.note = QStringLiteral("gpu: GPU counter collection failed");
        return reading;
    }

    constexpr DWORD kPdhMoreData = 0x800007d2;
    struct Item {
        QString name;
        double value = 0.0;
        long long large = 0;
    };
    auto fetch = [&](int leaf, DWORD format) -> QList<Item> {
        QList<Item> items;
        const PDH_HCOUNTER counter = static_cast<PDH_HCOUNTER>(
            m_pdhGpuHandles.at(leaf));
        DWORD bytes = 0;
        DWORD count = 0;
        if (::PdhGetFormattedCounterArrayW(counter, format, &bytes, &count,
                                           nullptr)
            != kPdhMoreData) {
            return items;
        }
        std::vector<BYTE> buffer(bytes);
        if (::PdhGetFormattedCounterArrayW(
                counter, format, &bytes, &count,
                reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data()))
            != ERROR_SUCCESS) {
            return items;
        }
        auto *rows = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(
            buffer.data());
        for (DWORD i = 0; i < count; ++i) {
            Item item;
            item.name = QString::fromWCharArray(rows[i].szName).trimmed();
            if (format == PDH_FMT_LARGE) {
                item.large = rows[i].FmtValue.largeValue;
                item.value = double(item.large);
            } else {
                item.value = rows[i].FmtValue.doubleValue;
            }
            items.append(item);
        }
        return items;
    };

    struct AdapterAgg {
        double util = -1.0;
        quint64 mem = 0;
    };
    struct ProcessAgg {
        double util = -1.0;
        quint64 mem = 0;
    };
    QHash<QString, AdapterAgg> adapters;
    QHash<int, ProcessAgg> processes;

    auto luidOf = [](const QString &instance) -> QString {
        const int at = instance.indexOf(QStringLiteral("_luid_"));
        if (at < 0) {
            return QString();
        }
        const int phys = instance.indexOf(QStringLiteral("_phys_"),
                                          at + 6);
        return instance.mid(at + 6,
                            (phys < 0 ? instance.size() : phys) - at - 6);
    };

    for (const Item &item : fetch(0, PDH_FMT_DOUBLE)) {
        const int pid = gpuPidFromPdhInstance(item.name);
        const QString luid = luidOf(item.name);
        if (luid.isEmpty()) {
            continue;
        }
        AdapterAgg &adapter = adapters[luid];
        const double value = qBound(0.0, item.value, 100.0);
        adapter.util = qMax(adapter.util, value);
        if (pid > 0) {
            ProcessAgg &process = processes[pid];
            process.util = qMax(process.util, value);
        }
    }
    for (const Item &item : fetch(1, PDH_FMT_LARGE)) {
        const int pid = gpuPidFromPdhInstance(item.name);
        const QString luid = luidOf(item.name);
        if (pid <= 0) {
            continue;
        }
        if (item.large > 0) {
            processes[pid].mem += static_cast<quint64>(item.large);
        }
        if (!luid.isEmpty() && item.large > 0) {
            adapters[luid].mem += static_cast<quint64>(item.large);
        }
    }
    for (const Item &item : fetch(2, PDH_FMT_LARGE)) {
        const QString luid = luidOf(item.name);
        if (luid.isEmpty() || item.large <= 0) {
            continue;
        }
        AdapterAgg &adapter = adapters[luid];
        if (adapter.mem == 0) {
            adapter.mem = static_cast<quint64>(item.large);
        }
    }

    int index = 0;
    for (auto it = adapters.constBegin(); it != adapters.constEnd(); ++it) {
        GpuDevice device;
        device.index = index++;
        device.vendor = QStringLiteral("unknown");
        device.driver = QStringLiteral("pdh");
        device.name = QStringLiteral("adapter %1").arg(device.index);
        device.utilPercent = it->util;
        device.memUsed = it->mem;
        reading.devices.append(device);
    }
    for (auto it = processes.constBegin(); it != processes.constEnd(); ++it) {
        if (it->util < 0.0 && it->mem == 0) {
            continue;
        }
        GpuProcess process;
        process.pid = it.key();
        process.name = comm(it.key());
        if (process.name.isEmpty()) {
            process.name = QStringLiteral("pid %1").arg(it.key());
        }
        process.busyPercent = it->util;
        process.memBytes = it->mem;
        reading.processes.append(process);
    }
    std::sort(reading.processes.begin(), reading.processes.end(),
              [](const GpuProcess &a, const GpuProcess &b) {
        const double busyA = a.busyPercent < 0.0 ? 0.0 : a.busyPercent;
        const double busyB = b.busyPercent < 0.0 ? 0.0 : b.busyPercent;
        if (busyA != busyB) {
            return busyA > busyB;
        }
        return a.memBytes > b.memBytes;
    });

    reading.ok = !reading.devices.isEmpty();
    if (!reading.ok) {
        reading.note = QStringLiteral(
            "gpu: no GPU adapter is exposing performance counters");
    } else {
        reading.vendor = QStringLiteral("unknown");
    }
    return reading;
}

void WindowsBackend::closePdhDiskQuery() {
    // PDH counters are owned by the query; one close releases all of them.
    if (m_pdhQuery != nullptr) {
        ::PdhCloseQuery(static_cast<PDH_HQUERY>(m_pdhQuery));
        m_pdhQuery = nullptr;
        m_pdhDiskHandles.clear();
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
    caps.sbom = true;
    caps.perProcessDisk = true;
    caps.gpu = true;
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

bool WindowsBackend::processDiskCounters(int pid, long long &readBytes,
                                         long long &writeBytes) {
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return false;
    }
    IO_COUNTERS io = {};
    const bool ok = ::GetProcessIoCounters(handle, &io) != FALSE;
    ::CloseHandle(handle);
    if (!ok) {
        return false;
    }
    readBytes = static_cast<long long>(io.ReadTransferCount);
    writeBytes = static_cast<long long>(io.WriteTransferCount);
    return true;
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

    // Cumulative context switches: SystemProcessInformation walk (per-thread
    // ContextSwitches summed); Darwin has no voluntary split either, so the
    // mac precedent (voluntary = total) applies.
    unsigned long long ctxTotal = 0;
    if (systemContextSwitches(pid, ctxTotal)) {
        detail.voluntaryCtx = ctxTotal;
        detail.involuntaryCtx = 0;
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
                                          reinterpret_cast<BYTE *>(
                                              buffer.data()),
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
            rec.state = int(rows[i].ServiceStatusProcess.dwCurrentState);
            rec.exitCode = rows[i].ServiceStatusProcess.dwWin32ExitCode;
            rec.processId = int(rows[i].ServiceStatusProcess.dwProcessId);
            out.append(rec);
        }
    }

    ::CloseServiceHandle(mgr);
    return out;
}

bool WindowsBackend::readDiskCounters(QList<DiskCounters> &out) {
    QMutexLocker locker(&m_pdhDiskMutex);
    if (!ensurePdhDiskQuery()) {
        return false;
    }

    enum Leaf {
        kReads = 0,
        kWrites = 1,
        kReadBytes = 2,
        kWriteBytes = 3,
        kAvgSecRead = 4,
        kAvgSecWrite = 5,
        kQueue = 6,
        kIdle = 7,
        kQueueRead = 8,
        kQueueWrite = 9,
    };

    if (::PdhCollectQueryData(static_cast<PDH_HQUERY>(m_pdhQuery))
        != ERROR_SUCCESS) {
        return false;
    }

    const PDH_HCOUNTER queueCounter = static_cast<PDH_HCOUNTER>(
        m_pdhDiskHandles.at(kQueue));

    // Wildcard handle array pass: collects the per-instance names.
    DWORD bytes = 0;
    DWORD itemCount = 0;
    std::vector<BYTE> buffer;
    // pdhmsg.h (line 28): #define PDH_MORE_DATA 0x800007d2 - MSVC pdh.h
    // does not pull pdhmsg.h in, so the constant is spelled literally.
    constexpr DWORD kPdhMoreData = 0x800007d2;
    if (::PdhGetFormattedCounterArrayW(queueCounter, PDH_FMT_DOUBLE, &bytes,
                                       &itemCount, nullptr)
        != kPdhMoreData) {
        return false;
    }
    buffer.resize(bytes);
    if (::PdhGetFormattedCounterArrayW(queueCounter, PDH_FMT_DOUBLE, &bytes,
                                       &itemCount,
                                       reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(
                                           buffer.data()))
        != ERROR_SUCCESS) {
        return false;
    }

    auto *items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(
        buffer.data());
    const double msPerTick =
        m_pdhTimeBase > 0 ? 1000.0 / double(m_pdhTimeBase) : 0.0001;
    const double upNowMs = double(::GetTickCount64());

    QList<DiskCounters> rows;
    for (DWORD i = 0; i < itemCount; ++i) {
        const QString name =
            QString::fromWCharArray(items[i].szName).trimmed();
        if (name.isEmpty()) {
            continue;
        }
        const wchar_t *instance =
            reinterpret_cast<const wchar_t *>(items[i].szName);

        auto rawFirst = [this](int leaf, const wchar_t *inst) -> LONGLONG {
            // PdhGetRawCounterValue(H, LPDWORD type, PPDH_RAW_COUNTER);
            // PDH_RAW_COUNTER = {CStatus, FILETIME TimeStamp,
            //                    LONGLONG FirstValue, SecondValue,
            //                    DWORD MultiCount}.
            PDH_RAW_COUNTER raw;
            DWORD type = 0;
            ZeroMemory(&raw, sizeof(raw));
            if (::PdhGetRawCounterValue(
                    static_cast<PDH_HCOUNTER>(m_pdhDiskHandles.at(leaf)),
                    &type, &raw)
                != ERROR_SUCCESS) {
                return -1;
            }
            return raw.FirstValue;
        };

        const LONGLONG reads = rawFirst(kReads, instance);
        const LONGLONG readsBytes = rawFirst(kReadBytes, instance);
        const LONGLONG writes = rawFirst(kWrites, instance);
        const LONGLONG writesBytes = rawFirst(kWriteBytes, instance);
        const LONGLONG msRead = rawFirst(kAvgSecRead, instance);
        const LONGLONG msWrite = rawFirst(kAvgSecWrite, instance);
        const LONGLONG queueNow = rawFirst(kQueue, instance);
        const LONGLONG idle = rawFirst(kIdle, instance);
        const LONGLONG queueRead = rawFirst(kQueueRead, instance);
        const LONGLONG queueWrite = rawFirst(kQueueWrite, instance);
        if (reads < 0 || writes < 0 || idle < 0) {
            continue;
        }

        DiskCounters row;
        row.name = name;
        row.reads = static_cast<quint64>(qMax<LONGLONG>(reads, 0));
        row.sectorsRead =
            static_cast<quint64>(qMax<LONGLONG>(readsBytes, 0) / 512);
        row.writes = static_cast<quint64>(qMax<LONGLONG>(writes, 0));
        row.sectorsWritten =
            static_cast<quint64>(qMax<LONGLONG>(writesBytes, 0) / 512);
        row.msReading =
            static_cast<quint64>(qMax<LONGLONG>(0, msRead) * msPerTick);
        row.msWriting =
            static_cast<quint64>(qMax<LONGLONG>(0, msWrite) * msPerTick);
        row.iosInProgress = static_cast<quint64>(qMax<LONGLONG>(0, queueNow));
        const double idleCumMs = qMax<LONGLONG>(0, idle) * msPerTick;
        row.msDoingIo =
            static_cast<quint64>(qMax(0.0, upNowMs - idleCumMs));
        const double weightedMs = qMax<LONGLONG>(0, queueRead) * msPerTick
            + qMax<LONGLONG>(0, queueWrite) * msPerTick;
        row.weightedMsDoingIo = static_cast<quint64>(weightedMs);
        rows.append(row);
    }

    if (rows.isEmpty()) {
        return false;
    }
    out = rows;
    return true;
}

bool WindowsBackend::hostSecurityPosture(QList<SecurityFinding> &out) {
    // Key set grounded on the shipped osquery 5.23.1 windows MSI's own
    // windows_hardening.conf pack (shipped artifact content): each check
    // below is a registry probe the pack itself performs, with the weak
    // state it flags (EnableLUA data=0, SecureBoot State value, CFG
    // EnableCfg, ASLR moveImages, SEHOP KernelSEHOPEnabled, DeveloperMode
    // AppModelUnlock). Missing values follow the pack's semantics: absent
    // EnableLUA/moveImages are defaults-on (hardened), values that cannot
    // be read at all are skipped like the mac tool-failure rule.
    out.clear();

    auto dwordValue = [](const QString &keyPath,
                         const QString &valueName) -> QVariant {
        QSettings settings(keyPath, QSettings::NativeFormat);
        return settings.value(valueName);
    };

    {
        const QVariant lua = dwordValue(
            QStringLiteral(
                "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows"
                "\\CurrentVersion\\Policies\\System"),
            QStringLiteral("EnableLUA"));
        // Pack semantics: EnableLUA data=0 is the weak state; absent = on.
        if (!lua.isNull() && lua.toUInt() == 0) {
            out.append({QStringLiteral("user account control"), 8.0, false});
        } else {
            out.append({QStringLiteral("user account control"), 0.0, true});
        }
    }

    {
        const QVariant secureBoot = dwordValue(
            QStringLiteral(
                "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control"
                "\\SecureBoot\\State"),
            QStringLiteral("UEFISecureBootEnabled"));
        if (!secureBoot.isNull()) {
            if (secureBoot.toUInt() != 0) {
                out.append({QStringLiteral("secure boot"), 0.0, true});
            } else {
                out.append({QStringLiteral("secure boot"), 8.0, false});
            }
        }
    }

    {
        const QVariant cfg = dwordValue(
            QStringLiteral(
                "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control"
                "\\Session Manager\\Memory Management"),
            QStringLiteral("EnableCfg"));
        if (!cfg.isNull()) {
            if (cfg.toUInt() != 0) {
                out.append({QStringLiteral("control flow guard"), 0.0, true});
            } else {
                out.append({QStringLiteral("control flow guard"), 8.0, false});
            }
        }
    }

    {
        const QVariant moveImages = dwordValue(
            QStringLiteral(
                "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control"
                "\\Session Manager\\Memory Management"),
            QStringLiteral("moveImages"));
        // ASLR: moveImages non-zero disables mandatory relocation.
        if (!moveImages.isNull() && moveImages.toUInt() != 0) {
            out.append({QStringLiteral("aslr image relocation"), 8.0, false});
        } else {
            out.append({QStringLiteral("aslr image relocation"), 0.0, true});
        }
    }

    {
        const QVariant sehop = dwordValue(
            QStringLiteral(
                "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control"
                "\\Session Manager\\Kernel"),
            QStringLiteral("KernelSEHOPEnabled"));
        if (!sehop.isNull()) {
            if (sehop.toUInt() != 0) {
                out.append({QStringLiteral("sehop"), 0.0, true});
            } else {
                out.append({QStringLiteral("sehop"), 6.0, false});
            }
        }
    }

    {
        const QVariant devMode = dwordValue(
            QStringLiteral(
                "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows"
                "\\CurrentVersion\\AppModelUnlock"),
            QStringLiteral("AllowDevelopmentWithoutDevLicense"));
        const QVariant trustedApps = dwordValue(
            QStringLiteral(
                "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows"
                "\\CurrentVersion\\AppModelUnlock"),
            QStringLiteral("AllowAllTrustedApps"));
        const bool unlocked = (!devMode.isNull() && devMode.toUInt() != 0)
            || (!trustedApps.isNull() && trustedApps.toUInt() != 0);
        if (unlocked) {
            out.append({QStringLiteral("developer mode"), 6.0, false});
        } else {
            out.append({QStringLiteral("developer mode"), 0.0, true});
        }
    }

    return !out.isEmpty();
}

bool WindowsBackend::systemContextSwitches(int pid, unsigned long long &out) {
    const NtQuerySystemInformationFn query = ntQuerySystemInformation();
    if (query == nullptr) {
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QMutexLocker locker(&m_mutex);
        if (m_ctxMapAt != 0 && now - m_ctxMapAt < 1000) {
            const auto cached = m_ctxMap.constFind(pid);
            if (cached != m_ctxMap.constEnd()) {
                out = cached.value();
                return true;
            }
            return false;
        }
    }

    QByteArray buffer(1 << 20, Qt::Uninitialized);
    LONG status = 0;
    for (int attempt = 0; attempt < 6; ++attempt) {
        ULONG needed = 0;
        status = query(kSystemProcessInformation, buffer.data(),
                       static_cast<ULONG>(buffer.size()), &needed);
        if (status == 0) {
            break;
        }
        if (status != kStatusInfoLengthMismatch) {
            return false;
        }
        buffer.resize(buffer.size() * 2);
    }
    if (status != 0) {
        return false;
    }

    QHash<int, unsigned long long> map;
    const BYTE *cursor = reinterpret_cast<const BYTE *>(buffer.constData());
    const BYTE *end = cursor + buffer.size();
    // The chain terminates with nextEntryOffset == 0 on the FINAL record;
    // that record must still be processed before breaking.
    while (cursor + sizeof(NtProcInfo) <= end) {
        auto *record = reinterpret_cast<const NtProcInfo *>(cursor);
        const int recordPid =
            static_cast<int>(reinterpret_cast<ULONG_PTR>(
                record->uniqueProcessId));
        unsigned long long sum = 0;
        for (ULONG t = 0; t < record->numberOfThreads; ++t) {
            sum += record->threads[t].contextSwitches;
        }
        if (recordPid != 0) {
            map.insert(recordPid, sum);
        }
        if (record->nextEntryOffset == 0) {
            break;
        }
        cursor += record->nextEntryOffset;
    }

    QMutexLocker locker(&m_mutex);
    m_ctxMap = map;
    m_ctxMapAt = now;
    const auto found = map.constFind(pid);
    if (found == map.constEnd()) {
        return false;
    }
    out = found.value();
    return true;
}

// M67: per-thread rows (pHnt SYSTEM_THREAD_INFORMATION verbatim fields:
// KernelTime/UserTime 100-ns intervals, state/waitReason) — grounded in
// phnt ntexapi.h/ntkeapi.h + windows_exporter 0.30.2 KernelTime100ns.
bool WindowsBackend::processThreadSamples(int pid, QList<ThreadSample> &out,
                                          long &tickHz) {
    out.clear();
    tickHz = 0;
    if (pid <= 0) {
        return false;
    }
    const NtQuerySystemInformationFn query = ntQuerySystemInformation();
    if (query == nullptr) {
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QMutexLocker locker(&m_mutex);
        tickHz = 10000000;
        if (m_threadCacheAt != 0 && now - m_threadCacheAt < 1000) {
            const auto cached = m_threadCache.constFind(pid);
            if (cached != m_threadCache.constEnd()) {
                out = cached.value();
                return !out.isEmpty();
            }
            return false;
        }
    }

    QByteArray buffer(1 << 20, Qt::Uninitialized);
    LONG status = 0;
    for (int attempt = 0; attempt < 6; ++attempt) {
        ULONG needed = 0;
        status = query(kSystemProcessInformation, buffer.data(),
                       static_cast<ULONG>(buffer.size()), &needed);
        if (status == 0) {
            break;
        }
        if (status != kStatusInfoLengthMismatch) {
            return false;
        }
        buffer.resize(buffer.size() * 2);
    }
    if (status != 0) {
        return false;
    }

    QHash<int, QList<ThreadSample>> map;
    const BYTE *cursor = reinterpret_cast<const BYTE *>(buffer.constData());
    const BYTE *end = cursor + buffer.size();
    while (cursor + sizeof(NtProcInfo) <= end) {
        auto *record = reinterpret_cast<const NtProcInfo *>(cursor);
        const int recordPid =
            static_cast<int>(reinterpret_cast<ULONG_PTR>(
                record->uniqueProcessId));
        QList<ThreadSample> rows;
        if (recordPid > 0 && record->numberOfThreads > 0) {
            QString imageName;
            if (record->imageNameLength > 0
                && record->imageNameBuffer != nullptr) {
                imageName = QString::fromWCharArray(
                    record->imageNameBuffer, record->imageNameLength);
                if (imageName.endsWith(QStringLiteral(".exe"),
                                       Qt::CaseInsensitive)) {
                    imageName.chop(4);
                }
            }
            rows.reserve(record->numberOfThreads);
            for (ULONG t = 0; t < record->numberOfThreads; ++t) {
                const NtThreadInfo &thread = record->threads[t];
                const int tid = static_cast<int>(
                    reinterpret_cast<ULONG_PTR>(thread.clientId[1]));
                if (tid <= 0) {
                    continue;
                }
                ThreadSample sample;
                sample.tid = tid;
                sample.comm = imageName;
                sample.state = threadStateName(thread.threadState);
                sample.waitChannel = thread.threadState == 5
                                         ? threadWaitReasonName(
                                               thread.waitReason)
                                         : QString();
                if (thread.userTime.QuadPart >= 0
                    && thread.kernelTime.QuadPart >= 0) {
                    sample.utimeTicks =
                        static_cast<quint64>(thread.userTime.QuadPart);
                    sample.stimeTicks =
                        static_cast<quint64>(thread.kernelTime.QuadPart);
                }
                rows.append(sample);
            }
            map.insert(recordPid, rows);
        }
        if (record->nextEntryOffset == 0) {
            break;
        }
        cursor += record->nextEntryOffset;
    }

    QMutexLocker locker(&m_mutex);
    m_threadCache = map;
    m_threadCacheAt = now;
    const auto found = map.constFind(pid);
    if (found == map.constEnd()) {
        return false;
    }
    tickHz = 10000000;
    out = found.value();
    return !out.isEmpty();
}


QByteArray WindowsBackend::processEnv(int pid) {
    // PEB walk (cmdline/environment), same-user only: ReadProcessMemory
    // fails gracefully on elevated targets -> empty result, the UI keeps
    // whatever it had (matches the mac procargs2 strategy).
    const NtQueryInformationProcessFn query = ntQueryInformationProcess();
    if (query == nullptr) {
        return QByteArray();
    }
    const HANDLE handle = ::OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
        static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return QByteArray();
    }

    // PROCESS_BASIC_INFORMATION (winternl.h): PebBaseAddress.
    struct NtBasicInfo {
        PVOID reserved1;
        PVOID pebBaseAddress;
        PVOID reserved2[2];
        ULONG_PTR uniqueProcessId;
        PVOID reserved3;
    };
    NtBasicInfo pbi = {};
    if (query(handle, 0 /* ProcessBasicInformation */, &pbi, sizeof(pbi),
              nullptr)
        != 0) {
        ::CloseHandle(handle);
        return QByteArray();
    }

    // x64 PEB: ProcessParameters at 0x20 (winternl.h PEB field order).
    struct NtPebShell {
        BYTE reserved1[2];
        BYTE beingDebugged;
        BYTE reserved2[1];
        PVOID reserved3[2];
        PVOID ldr;
        PVOID processParameters;
    };
    static_assert(offsetof(NtPebShell, processParameters) == 0x20,
                  "PEB x64 ProcessParameters offset");
    constexpr ULONG_PTR kPebParamsOffset =
        offsetof(NtPebShell, processParameters);

    ULONG_PTR paramsAddress = 0;
    SIZE_T got = 0;
    if (!::ReadProcessMemory(
            handle, reinterpret_cast<LPCVOID>(reinterpret_cast<ULONG_PTR>(
                            pbi.pebBaseAddress)
                        + kPebParamsOffset),
            &paramsAddress, sizeof(paramsAddress), &got)
        || paramsAddress == 0) {
        ::CloseHandle(handle);
        return QByteArray();
    }

    // RTL_USER_PROCESS_PARAMETERS (winternl.h order) + the x64 Environment
    // pointer that follows CommandLine (0x80).
    struct NtRtlParams {
        BYTE reserved1[16];
        PVOID reserved2[10];
        USHORT imagePathLength;
        USHORT imagePathMaxLength;
        ULONG imagePathPad; // UNICODE_STRING x64 = 16 bytes
        wchar_t *imagePathBuffer;
        USHORT cmdLineLength;
        USHORT cmdLineMaxLength;
        ULONG cmdLinePad;
        wchar_t *cmdLineBuffer;
        PVOID environment;
    };
    static_assert(offsetof(NtRtlParams, environment) == 0x80,
                  "RTL_USER_PROCESS_PARAMETERS x64 Environment offset");

    NtRtlParams params = {};
    if (!::ReadProcessMemory(handle,
                             reinterpret_cast<LPCVOID>(paramsAddress),
                             &params, sizeof(params), &got)
        || params.environment == nullptr) {
        ::CloseHandle(handle);
        return QByteArray();
    }

    QByteArray envUtf16(32768, Qt::Uninitialized);
    if (!::ReadProcessMemory(handle, params.environment, envUtf16.data(),
                             static_cast<SIZE_T>(envUtf16.size()), &got)) {
        got = 0;
    }
    ::CloseHandle(handle);
    if (got == 0) {
        return QByteArray();
    }

    // UTF-16 multi-sz block -> "K=V" lines joined with \n.
    const auto *begin = reinterpret_cast<const wchar_t *>(envUtf16.constData());
    const size_t count = got / sizeof(wchar_t);
    QStringList pairs;
    size_t start = 0;
    for (size_t i = 0; i < count; ++i) {
        if (begin[i] != L'\0') {
            continue;
        }
        if (i == start) {
            break; // double NUL terminator
        }
        pairs.append(QString::fromWCharArray(begin + start, int(i - start)));
        start = i + 1;
    }
    return pairs.join(QLatin1Char('\n')).toUtf8();
}

QString WindowsBackend::processCmdline(int pid) {
    // Same PEB walk; CommandLine UNICODE_STRING (Length in bytes).
    const NtQueryInformationProcessFn query = ntQueryInformationProcess();
    if (query == nullptr) {
        return QString();
    }
    const HANDLE handle = ::OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
        static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return QString();
    }

    struct NtBasicInfo {
        PVOID reserved1;
        PVOID pebBaseAddress;
        PVOID reserved2[2];
        ULONG_PTR uniqueProcessId;
        PVOID reserved3;
    };
    NtBasicInfo pbi = {};
    if (query(handle, 0 /* ProcessBasicInformation */, &pbi, sizeof(pbi),
              nullptr)
        != 0) {
        ::CloseHandle(handle);
        return QString();
    }

    struct NtPebShell {
        BYTE reserved1[2];
        BYTE beingDebugged;
        BYTE reserved2[1];
        PVOID reserved3[2];
        PVOID ldr;
        PVOID processParameters;
    };
    ULONG_PTR paramsAddress = 0;
    SIZE_T got = 0;
    if (!::ReadProcessMemory(
            handle, reinterpret_cast<LPCVOID>(reinterpret_cast<ULONG_PTR>(
                            pbi.pebBaseAddress)
                        + 0x20), // PEB.ProcessParameters (rx_winternl.h order)
            &paramsAddress, sizeof(paramsAddress), &got)
        || paramsAddress == 0) {
        ::CloseHandle(handle);
        return QString();
    }

    struct NtRtlCmd {
        BYTE reserved1[16];
        PVOID reserved2[10];
        USHORT imagePathLength;
        USHORT imagePathMaxLength;
        ULONG imagePathPad; // UNICODE_STRING x64 = 16 bytes
        wchar_t *imagePathBuffer;
        USHORT cmdLineLength;
        USHORT cmdLineMaxLength;
        ULONG cmdLinePad;
        wchar_t *cmdLineBuffer;
    };
    static_assert(offsetof(NtRtlCmd, cmdLineLength) == 0x70,
                  "RTL_USER_PROCESS_PARAMETERS x64 CommandLine offset");
    static_assert(offsetof(NtRtlCmd, cmdLineBuffer) == 0x78,
                  "RTL_USER_PROCESS_PARAMETERS x64 CommandLine.Buffer offset");

    NtRtlCmd params = {};
    if (!::ReadProcessMemory(handle,
                             reinterpret_cast<LPCVOID>(paramsAddress),
                             &params, sizeof(params), &got)
        || params.cmdLineBuffer == nullptr) {
        ::CloseHandle(handle);
        return QString();
    }

    const int bytes = qMin<USHORT>(params.cmdLineLength, 32768);
    QByteArray cmdUtf16(bytes == 0 ? 2 : bytes, Qt::Uninitialized);
    if (!::ReadProcessMemory(handle, params.cmdLineBuffer,
                             cmdUtf16.data(),
                             static_cast<SIZE_T>(cmdUtf16.size()), &got)) {
        ::CloseHandle(handle);
        return QString();
    }
    ::CloseHandle(handle);

    return QString::fromWCharArray(
        reinterpret_cast<const wchar_t *>(cmdUtf16.constData()),
        int(got / sizeof(wchar_t)));
}
