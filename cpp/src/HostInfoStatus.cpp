#include "HostInfoStatus.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QSettings>

#if defined(Q_OS_MACOS)
#include <net/if_dl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#endif

#if defined(Q_OS_WIN)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#endif

#if !defined(Q_OS_WIN)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#include <vector>

namespace {

bool macSysctlString(const char *key, QString &out) {
#if defined(Q_OS_MACOS)
    size_t size = 0;
    if (sysctlbyname(key, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return false;
    }
    QByteArray buf(static_cast<int>(size) + 1, '\0');
    if (sysctlbyname(key, buf.data(), &size, nullptr, 0) != 0) {
        return false;
    }
    out = QString::fromLatin1(buf.data());
    return true;
#else
    Q_UNUSED(key);
    Q_UNUSED(out);
    return false;
#endif
}

bool macSysctlInt(const char *key, int &out) {
#if defined(Q_OS_MACOS)
    int value = 0;
    size_t size = sizeof(value);
    if (sysctlbyname(key, &value, &size, nullptr, 0) != 0) {
        return false;
    }
    out = value;
    return true;
#else
    Q_UNUSED(key);
    Q_UNUSED(out);
    return false;
#endif
}

bool macSysctlCaches(const char *key, unsigned long long &out) {
#if defined(Q_OS_MACOS)
    unsigned long long value = 0;
    size_t size = sizeof(value);
    if (sysctlbyname(key, &value, &size, nullptr, 0) != 0) {
        return false;
    }
    out = value;
    return true;
#else
    Q_UNUSED(key);
    Q_UNUSED(out);
    return false;
#endif
}

QString macSwapUsageTotalKb() {
#if defined(Q_OS_MACOS)
    QString usage;
    if (!macSysctlString("vm.swapusage", usage)) {
        return QString();
    }
    const QRegularExpression pattern(
        QStringLiteral("total\\s*=\\s*([0-9.]+)([KMG])"));
    const QRegularExpressionMatch match = pattern.match(usage);
    if (!match.hasMatch()) {
        return QString();
    }
    const double value = match.captured(1).toDouble();
    const QString unit = match.captured(2);
    double kb = value;
    if (unit == QStringLiteral("G")) {
        kb *= 1024.0 * 1024.0;
    } else if (unit == QStringLiteral("M")) {
        kb *= 1024.0;
    }
    return QString::number(static_cast<unsigned long long>(kb));
#else
    return QString();
#endif
}

} // namespace

namespace {

QString firstValue(const QByteArray &data, const QString &key) {
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromLatin1(raw);
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            continue;
        }
        if (line.left(colon).trimmed() == key) {
            return line.mid(colon + 1).trimmed();
        }
    }
    return QString();
}

bool isPseudoFs(const QString &fs) {
    static const QSet<QString> pseudo = {
        QStringLiteral("proc"),      QStringLiteral("sysfs"),
        QStringLiteral("cgroup"),    QStringLiteral("cgroup2"),
        QStringLiteral("devpts"),    QStringLiteral("mqueue"),
        QStringLiteral("debugfs"),   QStringLiteral("securityfs"),
        QStringLiteral("pstore"),    QStringLiteral("bpf"),
        QStringLiteral("tracefs"),   QStringLiteral("configfs"),
        QStringLiteral("fusectl"),   QStringLiteral("autofs"),
        QStringLiteral("hugetlbfs"), QStringLiteral("rpc_pipefs"),
        QStringLiteral("nsfs"),      QStringLiteral("devtmpfs"),
        QStringLiteral("binfmt_misc")};
    return pseudo.contains(fs);
}

} // namespace

HostCpuInfo parseCpuInfo(const QByteArray &cpuinfo) {
    HostCpuInfo info;
    const QList<QByteArray> lines = cpuinfo.split('\n');
    int logical = 0;
    int coresPerPackage = 0;
    double mhzSum = 0.0;
    int mhzCount = 0;
    QSet<QString> packageIds;
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromLatin1(raw);
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            continue;
        }
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QStringLiteral("processor")) {
            ++logical;
        } else if (key == QStringLiteral("vendor_id")) {
            info.vendorId = value;
        } else if (key == QStringLiteral("model name")) {
            info.modelName = value;
        } else if (key == QStringLiteral("physical id")) {
            packageIds.insert(value);
        } else if (key == QStringLiteral("cpu cores")) {
            coresPerPackage = value.toInt();
        } else if (key == QStringLiteral("cpu MHz")) {
            mhzSum += value.toDouble();
            ++mhzCount;
        } else if (key == QStringLiteral("cache size")) {
            info.cacheSize = value;
        }
    }
    info.logicalProcessors = logical;
    info.packages = packageIds.isEmpty() ? 1 : packageIds.size();
    info.cores = coresPerPackage > 0 ? coresPerPackage * info.packages
                                     : info.logicalProcessors;
    info.mhz = mhzCount > 0 ? mhzSum / mhzCount : 0.0;
    return info;
}

QList<HostFsInfo> parseMounts(const QByteArray &mounts) {
    QList<HostFsInfo> out;
    const QList<QByteArray> lines = mounts.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromLatin1(raw).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList parts =
            line.split(QRegularExpression(QStringLiteral("\\s+")),
                       Qt::SkipEmptyParts);
        if (parts.size() < 3) {
            continue;
        }
        if (isPseudoFs(parts.at(2))) {
            continue;
        }
        HostFsInfo fs;
        fs.name = parts.at(0);
        fs.mountedOn = parts.at(1);
        out.append(fs);
    }
    return out;
}

QString parseOsRelease(const QByteArray &osRelease) {
    QString name;
    const QList<QByteArray> lines = osRelease.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromLatin1(raw).trimmed();
        if (line.startsWith(QStringLiteral("PRETTY_NAME="))) {
            QString value = line.mid(12);
            if (value.startsWith(QLatin1Char('"'))
                && value.endsWith(QLatin1Char('"'))) {
                value = value.mid(1, value.size() - 2);
            }
            return value;
        }
        if (line.startsWith(QStringLiteral("NAME="))) {
            name = line.mid(5);
        }
    }
    if (name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"'))) {
        name = name.mid(1, name.size() - 2);
    }
    return name;
}

#if defined(Q_OS_MACOS)
HostInfo collectHostInfoMac() {
    // Datadog pkg/gohai darwin model (cpu_darwin.go, platform_darwin.go,
    // memory_darwin.go — source reference in /home/nullchan/Research/
    // datadog-agent, previously REA'd on the agent binary for M39):
    //   cpu: machdep.cpu.vendor / machdep.cpu.brand_string, hw.packages,
    //        hw.physicalcpu / hw.logicalcpu, hw.l1dcachesize /
    //        hw.l2cachesize / hw.l3cachesize, hw.cpufrequency
    //   mem: hw.memsize (+ vm.swapusage)
    //   platform: uname fields; Rosetta via sysctl.proc_translated
    //   filesystems: getfsstat (local mounts)
    //   network: getifaddrs with AF_LINK MAC (sockaddr_dl), v4/v6 CIDR
    HostInfo info;
    info.available = true;

    struct utsname uts {};
    if (uname(&uts) == 0) {
        info.platform.kernelName = QString::fromLatin1(uts.sysname);
        info.platform.kernelRelease = QString::fromLatin1(uts.release);
        info.platform.kernelVersion = QString::fromLatin1(uts.version);
        info.platform.machine = QString::fromLatin1(uts.machine);
        info.platform.os = QString::fromLatin1(uts.sysname);
    }
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
        info.platform.hostname = QString::fromLatin1(hostname);
    }

    int translated = 0;
    if (macSysctlInt("sysctl.proc_translated", translated)
        && translated == 1) {
        info.virtualization = QStringLiteral("rosetta2");
        info.platform.machine = QStringLiteral("arm64");
    }

    macSysctlString("machdep.cpu.brand_string", info.cpu.modelName);
    macSysctlString("machdep.cpu.vendor", info.cpu.vendorId);
    int cores = 0;
    if (macSysctlInt("hw.physicalcpu", cores) && cores > 0) {
        info.cpu.cores = cores;
    }
    if (macSysctlInt("hw.logicalcpu", cores) && cores > 0) {
        info.cpu.logicalProcessors = cores;
    }
    if (macSysctlInt("hw.packages", cores) && cores > 0) {
        info.cpu.packages = cores;
    }
    unsigned long long cache = 0;
    QString cacheText;
    if (macSysctlCaches("hw.l1dcachesize", cache)) {
        cacheText += QStringLiteral("L1d %1KB").arg(cache / 1024ULL);
    }
    if (macSysctlCaches("hw.l2cachesize", cache)) {
        cacheText += (cacheText.isEmpty() ? QString()
                                          : QStringLiteral(" + "))
                     + QStringLiteral("L2 %1KB").arg(cache / 1024ULL);
    }
    if (macSysctlCaches("hw.l3cachesize", cache)) {
        cacheText += (cacheText.isEmpty() ? QString()
                                          : QStringLiteral(" + "))
                     + QStringLiteral("L3 %1KB").arg(cache / 1024ULL);
    }
    info.cpu.cacheSize = cacheText;

    unsigned long long memsize = 0;
    size_t size = sizeof(memsize);
    if (sysctlbyname("hw.memsize", &memsize, &size, nullptr, 0) == 0) {
        info.memTotalKb = memsize / 1024ULL;
    }
    const QString swapKb = macSwapUsageTotalKb();
    if (!swapKb.isEmpty()) {
        info.swapTotalKb = swapKb.toULongLong();
    }

    int fsCount = getfsstat(nullptr, 0, MNT_NOWAIT);
    if (fsCount > 0) {
        std::vector<struct statfs> stats(static_cast<size_t>(fsCount));
        fsCount = getfsstat(stats.data(),
                            static_cast<int>(stats.size()
                                                 * sizeof(struct statfs)),
                            MNT_NOWAIT);
        for (int i = 0; i < fsCount; ++i) {
            const struct statfs &st = stats[static_cast<size_t>(i)];
            if ((st.f_flags & MNT_LOCAL) == 0) {
                continue;
            }
            const QString mountOn =
                QString::fromLatin1(st.f_mntonname);
            if (mountOn.startsWith(QStringLiteral("/System/"))) {
                continue;
            }
            HostFsInfo fs;
            fs.name = QString::fromLatin1(st.f_mntfromname);
            fs.mountedOn = mountOn;
            fs.kbSize = static_cast<unsigned long long>(st.f_blocks)
                        * st.f_bsize / 1024ULL;
            info.filesystems.append(fs);
        }
    }

    QHash<QString, HostNetworkInfo> byName;
    struct ifaddrs *addrs = nullptr;
    if (getifaddrs(&addrs) == 0) {
        for (struct ifaddrs *ifa = addrs; ifa != nullptr;
             ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) {
                continue;
            }
            const QString name = QString::fromLatin1(ifa->ifa_name);
            if (name == QStringLiteral("lo0")) {
                continue;
            }
            HostNetworkInfo &entry = byName[name];
            entry.name = name;
            const int family = ifa->ifa_addr->sa_family;
            if (family == AF_LINK) {
                const auto *sdl = reinterpret_cast<const sockaddr_dl *>(
                    ifa->ifa_addr);
                if (sdl->sdl_nlen == name.size()
                    && sdl->sdl_alen == 6) {
                    const unsigned char *mac =
                        reinterpret_cast<const unsigned char *>(
                            sdl->sdl_data + sdl->sdl_nlen);
                    entry.macAddress =
                        QStringLiteral("%1:%2:%3:%4:%5:%6")
                            .arg(mac[0], 2, 16, QChar('0'))
                            .arg(mac[1], 2, 16, QChar('0'))
                            .arg(mac[2], 2, 16, QChar('0'))
                            .arg(mac[3], 2, 16, QChar('0'))
                            .arg(mac[4], 2, 16, QChar('0'))
                            .arg(mac[5], 2, 16, QChar('0'));
                }
                continue;
            }
            char buf[INET6_ADDRSTRLEN];
            int prefix = 0;
            if (ifa->ifa_netmask != nullptr) {
                if (family == AF_INET) {
                    const auto *mask =
                        reinterpret_cast<const sockaddr_in *>(ifa->ifa_netmask);
                    prefix = __builtin_popcount(
                        ntohl(mask->sin_addr.s_addr));
                } else if (family == AF_INET6) {
                    const auto *mask =
                        reinterpret_cast<sockaddr_in6 const *>(ifa->ifa_netmask);
                    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(
                        &mask->sin6_addr.s6_addr[0]);
                    for (int i = 0; i < 16; ++i) {
                        prefix += __builtin_popcount(bytes[i]);
                    }
                }
            }
            if (family == AF_INET) {
                inet_ntop(AF_INET,
                          &reinterpret_cast<const sockaddr_in *>(
                               ifa->ifa_addr)
                               ->sin_addr,
                          buf, sizeof(buf));
                entry.ipv4 = QStringLiteral("%1/%2")
                                 .arg(QString::fromLatin1(buf))
                                 .arg(prefix);
            } else if (family == AF_INET6) {
                inet_ntop(AF_INET6,
                          &reinterpret_cast<const sockaddr_in6 *>(
                               ifa->ifa_addr)
                               ->sin6_addr,
                          buf, sizeof(buf));
                entry.ipv6 = QStringLiteral("%1/%2")
                                 .arg(QString::fromLatin1(buf))
                                 .arg(prefix);
            }
        }
        freeifaddrs(addrs);
    }
    for (const HostNetworkInfo &entry : byName) {
        info.interfaces.append(entry);
    }

    return info;
}
#endif

#if defined(Q_OS_WIN)
HostInfo collectHostInfoWin() {
    // Datadog gohai-style inventory; API/registry grounds (kept PEs):
    // agent.exe carries the strings HARDWARE\DESCRIPTION\System\
    // CentralProcessor\0 + ProcessorNameString + GetActiveProcessorCount
    // + GetLogicalDriveStringsW + GetVolumeInformation + GetAdaptersAddresses;
    // windows_exporter adds LogicalDisk / "Free Space" / Hypervisor /
    // RtlGetVersion; SystemInformer quotes RtlGetVersion. All Win32/registry,
    // nothing WMI/PowerShell.
    HostInfo info;
    info.available = true;

    using RtlGetVersionFn = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
    RtlGetVersionFn rtlGetVersion = []() -> RtlGetVersionFn {
        const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        return ntdll != nullptr
                   ? reinterpret_cast<RtlGetVersionFn>(
                         ::GetProcAddress(ntdll, "RtlGetVersion"))
                   : nullptr;
    }();

    RTL_OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtlGetVersion != nullptr && rtlGetVersion(&version) == 0) {
        info.platform.kernelName = QStringLiteral("Windows");
        info.platform.kernelRelease =
            QStringLiteral("%1.%2 build %3")
                .arg(version.dwMajorVersion)
                .arg(version.dwMinorVersion)
                .arg(version.dwBuildNumber);
        info.platform.kernelVersion =
            QString::fromWCharArray(version.szCSDVersion);
        info.platform.os =
            QStringLiteral("Windows %1.%2.%3")
                .arg(version.dwMajorVersion)
                .arg(version.dwMinorVersion)
                .arg(version.dwBuildNumber);
    }

    info.platform.hostname = QSysInfo::machineHostName();

    SYSTEM_INFO sysInfo = {};
    ::GetNativeSystemInfo(&sysInfo);
    switch (sysInfo.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        info.platform.machine = QStringLiteral("x86_64");
        break;
    case PROCESSOR_ARCHITECTURE_ARM64:
        info.platform.machine = QStringLiteral("arm64");
        break;
    default:
        info.platform.machine = QStringLiteral("unknown");
        break;
    }

    {
        QSettings cpuKey(
            QStringLiteral("HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION"
                           "\System\CentralProcessor\0"),
            QSettings::NativeFormat);
        info.cpu.modelName = cpuKey.value(QStringLiteral("ProcessorNameString"))
                                .toString();
        info.cpu.vendorId =
            cpuKey.value(QStringLiteral("VendorIdentifier")).toString();
        info.cpu.mhz = cpuKey.value(QStringLiteral("~MHz")).toDouble();
        info.cpu.logicalProcessors =
            static_cast<int>(sysInfo.dwNumberOfProcessors);
        info.cpu.cores = info.cpu.logicalProcessors;
    }

    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (::GlobalMemoryStatusEx(&mem)) {
        info.memTotalKb = mem.ullTotalPhys / 1024;
        info.swapTotalKb = mem.ullTotalPageFile / 1024;
    }

    // Fixed drives: GetLogicalDriveStringsW + GetVolumeInformationW +
    // GetDiskFreeSpaceExW (M49 grounded).
    wchar_t driveList[512] = {};
    if (::GetLogicalDriveStringsW(DWORD(std::size(driveList) - 1),
                                  driveList)
        > 0) {
        const wchar_t *cur = driveList;
        while (*cur != L'\0') {
            const QString drive = QString::fromWCharArray(cur);
            const UINT type = ::GetDriveTypeW(cur);
            cur += drive.size() + 1;
            if (type != DRIVE_FIXED) {
                continue;
            }
            wchar_t fsName[MAX_PATH + 1] = {};
            if (::GetVolumeInformationW(reinterpret_cast<const wchar_t *>(
                                            drive.utf16()),
                                        nullptr, 0, nullptr, nullptr,
                                        nullptr, fsName, MAX_PATH)) {
                ULARGE_INTEGER total = {};
                ULARGE_INTEGER freeBytes = {};
                ::GetDiskFreeSpaceExW(reinterpret_cast<const wchar_t *>(
                                          drive.utf16()),
                                      nullptr, &total, &freeBytes);
                HostFsInfo fs;
                fs.name = drive;
                fs.mountedOn = QString::fromWCharArray(fsName);
                fs.kbSize = total.QuadPart / 1024;
                info.filesystems.append(fs);
            }
        }
    }

    // Interfaces: GetAdaptersAddresses (MAC + prefix-length CIDRs).
    ULONG adaptersSize = 16 * 1024;
    QByteArray adapterBuffer;
    for (int attempt = 0; attempt < 3; ++attempt) {
        adapterBuffer.resize(int(adaptersSize));
        auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(
            adapterBuffer.data());
        const ULONG result = ::GetAdaptersAddresses(
            AF_UNSPEC, 0, nullptr, adapters,
            &adaptersSize);
        if (result == ERROR_SUCCESS) {
            break;
        }
        if (result != ERROR_BUFFER_OVERFLOW) {
            adapterBuffer.clear();
            break;
        }
    }
    if (!adapterBuffer.isEmpty()) {
        auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(
            adapterBuffer.data());
        for (auto *a = adapters; a != nullptr; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp
                || a->PhysicalAddressLength == 0) {
                continue;
            }
            HostNetworkInfo net;
            net.name = QString::fromWCharArray(a->FriendlyName);
            QByteArray macBytes(reinterpret_cast<const char *>(
                                    a->PhysicalAddress),
                                int(a->PhysicalAddressLength));
            net.macAddress = QString::fromLatin1(macBytes.toHex(':'));
            for (auto *u = a->FirstUnicastAddress; u != nullptr;
                 u = u->Next) {
                const sockaddr *sa = u->Address.lpSockaddr;
                if (sa == nullptr) {
                    continue;
                }
                char ip[64] = {};
                const int prefixLen = int(u->OnLinkPrefixLength);
                if (sa->sa_family == AF_INET) {
                    ::InetNtopA(AF_INET,
                                &reinterpret_cast<const sockaddr_in *>(sa)
                                     ->sin_addr,
                                ip, sizeof(ip));
                    net.ipv4 = QStringLiteral("%1/%2")
                                   .arg(QString::fromLatin1(ip))
                                   .arg(prefixLen);
                } else if (sa->sa_family == AF_INET6
                           && net.ipv6.isEmpty()) {
                    ::InetNtopA(AF_INET6,
                                reinterpret_cast<const void *>(
                                    &reinterpret_cast<const sockaddr_in6 *>(
                                        sa)
                                         ->sin6_addr),
                                ip, sizeof(ip));
                    net.ipv6 = QStringLiteral("%1/%2")
                                   .arg(QString::fromLatin1(ip),
                                        QString::number(prefixLen));
                }
            }
            info.interfaces.append(net);
        }
    }

    return info;
}
#endif

HostInfo collectHostInfo() {
#if defined(Q_OS_MACOS)
    return collectHostInfoMac();
#elif defined(Q_OS_WIN)
    return collectHostInfoWin();
#else
    HostInfo info;
    info.available = true;

    struct utsname uts {};
    if (uname(&uts) == 0) {
        info.platform.kernelName = QString::fromLatin1(uts.sysname);
        info.platform.kernelRelease = QString::fromLatin1(uts.release);
        info.platform.kernelVersion = QString::fromLatin1(uts.version);
        info.platform.machine = QString::fromLatin1(uts.machine);
    }
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
        info.platform.hostname = QString::fromLatin1(hostname);
    }
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (osRelease.open(QIODevice::ReadOnly)) {
        info.platform.os = parseOsRelease(osRelease.readAll());
    }

    QFile cpuinfo(QStringLiteral("/proc/cpuinfo"));
    if (cpuinfo.open(QIODevice::ReadOnly)) {
        info.cpu = parseCpuInfo(cpuinfo.readAll());
    }
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (meminfo.open(QIODevice::ReadOnly)) {
        const QByteArray data = meminfo.readAll();
        info.memTotalKb = firstValue(data, QStringLiteral("MemTotal"))
                              .split(QLatin1Char(' '))
                              .value(0)
                              .toULongLong();
        info.swapTotalKb = firstValue(data, QStringLiteral("SwapTotal"))
                               .split(QLatin1Char(' '))
                               .value(0)
                               .toULongLong();
    }

    QFile mounts(QStringLiteral("/proc/mounts"));
    if (mounts.open(QIODevice::ReadOnly)) {
        info.filesystems = parseMounts(mounts.readAll());
        for (HostFsInfo &fs : info.filesystems) {
            struct statvfs stats {};
            if (statvfs(fs.mountedOn.toUtf8().constData(), &stats) == 0) {
                fs.kbSize = static_cast<unsigned long long>(stats.f_blocks)
                            * stats.f_frsize / 1024ULL;
            }
        }
    }

    QHash<QString, HostNetworkInfo> byName;
    struct ifaddrs *addrs = nullptr;
    if (getifaddrs(&addrs) == 0) {
        for (struct ifaddrs *ifa = addrs; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) {
                continue;
            }
            const QString name = QString::fromLatin1(ifa->ifa_name);
            if (name == QStringLiteral("lo")) {
                continue;
            }
            HostNetworkInfo &entry = byName[name];
            entry.name = name;
            if (entry.macAddress.isEmpty()) {
                QFile mac(QStringLiteral("/sys/class/net/") + name
                          + QStringLiteral("/address"));
                if (mac.open(QIODevice::ReadOnly)) {
                    entry.macAddress =
                        QString::fromLatin1(mac.readAll()).trimmed();
                }
            }
            const int family = ifa->ifa_addr->sa_family;
            auto prefixLength = [](const void *maskPtr, int bytes) {
                const auto *mask =
                    static_cast<const unsigned char *>(maskPtr);
                int bits = 0;
                for (int i = 0; i < bytes; ++i) {
                    unsigned char b = mask[i];
                    while ((b & 0x80) != 0) {
                        ++bits;
                        b = static_cast<unsigned char>(b << 1);
                    }
                }
                return bits;
            };
            if (family == AF_INET) {
                const auto *sin =
                    reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
                char buf[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
                int prefix = 32;
                if (ifa->ifa_netmask != nullptr) {
                    prefix = prefixLength(
                        &reinterpret_cast<struct sockaddr_in *>(ifa->ifa_netmask)
                             ->sin_addr,
                        4);
                }
                entry.ipv4 = QString::fromLatin1(buf)
                             + QStringLiteral("/%1").arg(prefix);
            } else if (family == AF_INET6) {
                const auto *sin6 =
                    reinterpret_cast<struct sockaddr_in6 *>(ifa->ifa_addr);
                char buf[INET6_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf));
                const QString ip = QString::fromLatin1(buf);
                if (!ip.startsWith(QStringLiteral("fe80"))) {
                    int prefix = 128;
                    if (ifa->ifa_netmask != nullptr) {
                        prefix = prefixLength(
                            &reinterpret_cast<struct sockaddr_in6 *>(
                                 ifa->ifa_netmask)
                                 ->sin6_addr,
                            16);
                    }
                    entry.ipv6 = ip + QStringLiteral("/%1").arg(prefix);
                }
            }
        }
        freeifaddrs(addrs);
    }
    for (auto it = byName.constBegin(); it != byName.constEnd(); ++it) {
        info.interfaces.append(it.value());
    }

    if (QFile::exists(QStringLiteral("/.dockerenv"))) {
        info.virtualization = QStringLiteral("docker");
    } else {
        QFile cgroup(QStringLiteral("/proc/1/cgroup"));
        QString cg;
        if (cgroup.open(QIODevice::ReadOnly)) {
            cg = QString::fromLatin1(cgroup.readAll());
        }
        QFile product(QStringLiteral("/sys/class/dmi/id/product_name"));
        QString prod;
        if (product.open(QIODevice::ReadOnly)) {
            prod = QString::fromLatin1(product.readAll()).trimmed();
        }
        if (cg.contains(QStringLiteral("lxc"))) {
            info.virtualization = QStringLiteral("lxc");
        } else if (prod.contains(QStringLiteral("KVM"))
                   || prod.contains(QStringLiteral("QEMU"))) {
            info.virtualization = QStringLiteral("kvm");
        } else if (prod.contains(QStringLiteral("VMware"))) {
            info.virtualization = QStringLiteral("vmware");
        } else if (prod.contains(QStringLiteral("VirtualBox"))) {
            info.virtualization = QStringLiteral("virtualbox");
        } else if (prod.contains(QStringLiteral("Xen"))) {
            info.virtualization = QStringLiteral("xen");
        } else {
            info.virtualization = QStringLiteral("none");
        }
    }
    return info;
#endif
}

QString hostInfoSummaryText(const HostInfo &info) {
    if (!info.available) {
        return QStringLiteral("host: unavailable");
    }
    return QStringLiteral("%1  %2 %3  %4 cores / %5 GB  %6")
        .arg(info.platform.hostname,
             info.platform.kernelName,
             info.platform.kernelRelease)
        .arg(info.cpu.logicalProcessors)
        .arg(QString::number(info.memTotalKb / (1024.0 * 1024.0), 'f', 1),
             info.virtualization);
}

QString hostInfoPlatformText(const HostInfo &info) {
    return QStringLiteral("os       %1\nhost     %2\nkernel   %3 %4\narch     %5")
        .arg(info.platform.os.isEmpty() ? QStringLiteral("-") : info.platform.os,
             info.platform.hostname, info.platform.kernelRelease,
             info.platform.kernelVersion.left(40), info.platform.machine);
}

QString hostInfoCpuText(const HostInfo &info) {
    return QStringLiteral("model    %1\nvendor   %2\ncores    %3 physical / %4 logical / %5 pkg\nmhz      %6\ncache    %7")
        .arg(info.cpu.modelName.isEmpty() ? QStringLiteral("-") : info.cpu.modelName,
             info.cpu.vendorId.isEmpty() ? QStringLiteral("-") : info.cpu.vendorId)
        .arg(info.cpu.cores)
        .arg(info.cpu.logicalProcessors)
        .arg(info.cpu.packages)
        .arg(QString::number(info.cpu.mhz, 'f', 0),
             info.cpu.cacheSize.isEmpty() ? QStringLiteral("-") : info.cpu.cacheSize);
}

QString hostInfoFilesystemsText(const HostInfo &info) {
    if (info.filesystems.isEmpty()) {
        return QStringLiteral("--");
    }
    QString text;
    int shown = 0;
    for (const HostFsInfo &fs : info.filesystems) {
        if (shown++ >= 12) {
            text += QStringLiteral("...\n");
            break;
        }
        text += QStringLiteral("%1  %2  %3 GB\n")
                    .arg(fs.mountedOn.left(28).leftJustified(28, ' '),
                         fs.name.left(20).leftJustified(20, ' '),
                         QString::number(fs.kbSize / (1024.0 * 1024.0), 'f', 1));
    }
    return text.trimmed();
}

QString hostInfoNetworkText(const HostInfo &info) {
    if (info.interfaces.isEmpty()) {
        return QStringLiteral("--");
    }
    QString text;
    for (const HostNetworkInfo &iface : info.interfaces) {
        text += QStringLiteral("%1  %2  %3%4\n")
                    .arg(iface.name.leftJustified(10, ' '),
                         iface.macAddress)
                    .arg(iface.ipv4.isEmpty() ? QStringLiteral("-") : iface.ipv4,
                         iface.ipv6.isEmpty()
                             ? QString()
                             : QStringLiteral("  ") + iface.ipv6);
    }
    return text.trimmed();
}
