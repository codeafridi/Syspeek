#include "MacBackend.h"

#ifdef Q_OS_MACOS

#include <mach/mach.h>
#include <mach/host_info.h>
#include <mach/machine.h>
#include <mach/mach_host.h>
#include <mach/mach_time.h>
#include <mach/processor_info.h>
#include <mach/vm_statistics.h>

#include <libproc.h>

#include <CoreFoundation/CoreFoundation.h>

#if defined(Q_OS_MACOS)
#include <IOKit/IOKitLib.h>
#endif

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/tcp_var.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/proc_info.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <QProcess>

namespace {
// Darwin TCP FSM values (netinet/tcp_fsm.h; not exposed by the SDK
// userspace netinet/tcp.h):
// TCPS_CLOSED 0, TCPS_LISTEN 1, TCPS_SYN_SENT 2, TCPS_SYN_RECEIVED 3,
// TCPS_ESTABLISHED 4, TCPS_CLOSE_WAIT 5, TCPS_FIN_WAIT_1 6,
// TCPS_CLOSING 7, TCPS_LAST_ACK 8, TCPS_FIN_WAIT_2 9, TCPS_TIME_WAIT 10.
constexpr int kTcpStateClosed = 0;
constexpr int kTcpStateListen = 1;
constexpr int kTcpStateSynSent = 2;
constexpr int kTcpStateSynReceived = 3;
constexpr int kTcpStateEstablished = 4;
constexpr int kTcpStateCloseWait = 5;
constexpr int kTcpStateFinWait1 = 6;
constexpr int kTcpStateClosing = 7;
constexpr int kTcpStateLastAck = 8;
constexpr int kTcpStateFinWait2 = 9;
constexpr int kTcpStateTimeWait = 10;
} // namespace
#include <cerrno>
#include <cstring>
#include <vector>

namespace {

constexpr unsigned long long kHostTickNs = 10000000ULL;
constexpr int kProcPidListFds = 1;
constexpr int kProcPidTaskInfo = 4;
constexpr int kProcPidFdVnodePathInfo = 2;
constexpr int kPfRoute = 17;
constexpr int kNetRtIfList2 = 6;
constexpr unsigned char kRtmIfInfo2 = 0x12;

double machTimebaseRatio() {
    mach_timebase_info_data_t tb{};
    if (mach_timebase_info(&tb) != KERN_SUCCESS || tb.denom == 0) {
        return 1.0;
    }
    return static_cast<double>(tb.numer) / static_cast<double>(tb.denom);
}

bool readHostCpuLoad(uint64_t counts[CPU_STATE_MAX]) {
    host_cpu_load_info_data_t info{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        reinterpret_cast<host_info_t>(&info), &count) != KERN_SUCCESS) {
        return false;
    }
    for (int i = 0; i < CPU_STATE_MAX; ++i) {
        counts[i] = info.cpu_ticks[i];
    }
    return true;
}

bool sysctlU64(const char *name, uint64_t &out) {
    size_t size = sizeof(out);
    return sysctlbyname(name, &out, &size, nullptr, 0) == 0;
}

bool sysctlU32(const char *name, uint32_t &out) {
    size_t size = sizeof(out);
    return sysctlbyname(name, &out, &size, nullptr, 0) == 0;
}

bool sysctlU32OrU64(const char *name, uint64_t &out) {
    uint32_t v32 = 0;
    if (sysctlU32(name, v32)) {
        out = v32;
        return true;
    }
    return sysctlU64(name, out);
}

bool readKinfo(int pid, kinfo_proc &kp) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    size_t size = sizeof(kp);
    std::memset(&kp, 0, sizeof(kp));
    if (sysctl(mib, 4, &kp, &size, nullptr, 0) != 0) {
        return false;
    }
    return size >= sizeof(kp) && kp.kp_proc.p_pid == pid;
}

bool readTaskInfo(int pid, proc_taskinfo &pti) {
    std::memset(&pti, 0, sizeof(pti));
    const int rc = proc_pidinfo(pid, kProcPidTaskInfo, 0, &pti, sizeof(pti));
    return rc == static_cast<int>(sizeof(pti));
}

QList<linuxproc::InterfaceStats> readIfList2() {
    QList<linuxproc::InterfaceStats> out;
    int mib[6] = {CTL_NET, kPfRoute, 0, 0, kNetRtIfList2, 0};
    size_t len = 0;
    if (sysctl(mib, 6, nullptr, &len, nullptr, 0) != 0 || len == 0) {
        return out;
    }
    std::vector<char> buf(len);
    if (sysctl(mib, 6, buf.data(), &len, nullptr, 0) != 0) {
        return out;
    }

    const char *p = buf.data();
    const char *end = p + len;
    while (p + 4 <= end) {
        const unsigned short msglen =
            *reinterpret_cast<const unsigned short *>(p);
        if (msglen == 0 || p + msglen > end) {
            break;
        }
        if (static_cast<unsigned char>(p[3]) == kRtmIfInfo2 &&
            p + sizeof(struct if_msghdr2) + sizeof(struct sockaddr_dl) <= end) {
            const auto *hdr = reinterpret_cast<const struct if_msghdr2 *>(p);
            const struct if_data64 &d = hdr->ifm_data;
            const auto *sdl = reinterpret_cast<const struct sockaddr_dl *>(
                p + sizeof(struct if_msghdr2));
            linuxproc::InterfaceStats st;
            st.name = QString::fromUtf8(sdl->sdl_data, sdl->sdl_nlen);
            st.rxBytes = d.ifi_ibytes;
            st.txBytes = d.ifi_obytes;
            st.rxPackets = d.ifi_ipackets;
            st.txPackets = d.ifi_opackets;
            st.rxErrs = d.ifi_ierrors;
            st.txErrs = d.ifi_oerrors;
            st.rxDrop = d.ifi_iqdrops;
            out.append(st);
        }
        p += msglen;
    }
    return out;
}

struct ProcArgs {
    QList<QByteArray> argv;
    QByteArray env;
};

bool readProcArgs(int pid, ProcArgs &out) {
    int argmax = 0;
    size_t size = sizeof(argmax);
    int mibMax[2] = {CTL_KERN, KERN_ARGMAX};
    if (sysctl(mibMax, 2, &argmax, &size, nullptr, 0) != 0 || argmax <= 4) {
        return false;
    }

    std::vector<char> buf(static_cast<size_t>(argmax));
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
    size_t len = buf.size();
    if (sysctl(mib, 3, buf.data(), &len, nullptr, 0) != 0 || len <= sizeof(int)) {
        return false;
    }

    int argc = 0;
    std::memcpy(&argc, buf.data(), sizeof(argc));
    size_t pos = sizeof(int);
    auto skipString = [&](size_t &p) {
        while (p < len && buf[p] != '\0') {
            ++p;
        }
    };

    skipString(pos);
    while (pos < len && buf[pos] == '\0') {
        ++pos;
    }

    for (int i = 0; i < argc && pos < len; ++i) {
        const size_t start = pos;
        skipString(pos);
        out.argv.append(QByteArray(buf.data() + start, static_cast<int>(pos - start)));
        ++pos;
    }

    if (pos < len) {
        out.env = QByteArray(buf.data() + pos, static_cast<int>(len - pos));
    }
    return true;
}

} // namespace

QString MacBackend::platformName() const {
    return QStringLiteral("macos");
}

PlatformCapabilities MacBackend::capabilities() const {
    PlatformCapabilities caps;
    caps.systemServices = true;
    caps.journal = true;
    caps.fim = true;
    caps.sbom = true;
    struct tcpstat probe {};
    caps.tcpCounters = readTcpSysctl(probe)
        && (probe.tcps_rcvtotal != 0 || probe.tcps_sndtotal != 0
            || probe.tcps_connattempt != 0);
    caps.perProcessDisk = true;
    caps.perProcessDiskScan = true;
    return caps;
}

bool MacBackend::hostCpuTimes(linuxproc::HostCpuTimes &out) {
    uint64_t counts[CPU_STATE_MAX] = {};
    if (!readHostCpuLoad(counts)) {
        return false;
    }
    out.idle = counts[CPU_STATE_IDLE];
    out.total = counts[CPU_STATE_USER] + counts[CPU_STATE_SYSTEM] +
                counts[CPU_STATE_IDLE] + counts[CPU_STATE_NICE];
    return true;
}

bool MacBackend::hostMem(linuxproc::HostMem &out) {
    uint64_t memsize = 0;
    if (!sysctlU64("hw.memsize", memsize)) {
        return false;
    }

    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm), &count) != KERN_SUCCESS) {
        return false;
    }

    vm_size_t pageSize = 0;
    if (host_page_size(mach_host_self(), &pageSize) != KERN_SUCCESS || pageSize == 0) {
        pageSize = static_cast<vm_size_t>(getpagesize());
    }
    const unsigned long long page = pageSize;

    const unsigned long long freePages =
        static_cast<unsigned long long>(vm.free_count) + vm.speculative_count;
    const unsigned long long availPages =
        static_cast<unsigned long long>(vm.free_count) + vm.inactive_count +
        vm.speculative_count + vm.purgeable_count;

    out.totalKb = memsize / 1024ULL;
    out.freeKb = freePages * page / 1024ULL;
    out.availableKb = availPages * page / 1024ULL;
    if (out.availableKb > out.totalKb) {
        out.availableKb = out.totalKb;
    }
    return true;
}

bool MacBackend::hostNet(linuxproc::NetTotals &out) {
    unsigned long long rx = 0;
    unsigned long long tx = 0;
    const QList<linuxproc::InterfaceStats> ifaces = readIfList2();
    for (const linuxproc::InterfaceStats &st : ifaces) {
        rx += st.rxBytes;
        tx += st.txBytes;
    }
    out.rxBytes = rx;
    out.txBytes = tx;
    return true;
}

bool MacBackend::hostInterfaces(QList<linuxproc::InterfaceStats> &out) {
    const QList<linuxproc::InterfaceStats> ifaces = readIfList2();
    if (ifaces.isEmpty()) {
        return false;
    }
    out.append(ifaces);
    return true;
}

bool MacBackend::readTcpSysctl(struct tcpstat &stats) const {
    // netinet/tcp_var.h (SDK, quoted exactly):
    //   #define TCPCTL_STATS 4  /* statistics (read-only) */
    //   struct tcpstat { ... u_int32_t tcps_rcvtotal; /* total packets
    //     received */ ... u_int32_t tcps_sndtotal; /* total packets sent
    //     */ ... u_int32_t tcps_sndrexmitpack; /* data packets
    //     retransmitted */ ... u_int32_t tcps_rexmttimeo; /* retransmit
    //     timeouts */ ... u_int32_t tcps_listendrop; /* listen queue
    //     overflows */ ... u_int32_t tcps_sc_retransmitted; /*
    //     syncache entry was retransmitted */ ... }
    // MIB: CTL_NET / PF_INET / IPPROTO_TCP / TCPCTL_STATS (the same
    // read "/proc/net/snmp+netstat" model as netstat -s, Darwin side).
    int mib[4] = {CTL_NET, PF_INET, IPPROTO_TCP, TCPCTL_STATS};
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return false;
    }
    std::vector<char> buf(size);
    if (sysctl(mib, 4, buf.data(), &size, nullptr, 0) != 0) {
        return false;
    }
    std::memset(&stats, 0, sizeof(stats));
    const size_t want = size < sizeof(stats) ? size : sizeof(stats);
    std::memcpy(&stats, buf.data(), want);
    return true;
}

bool MacBackend::readTcpCounters(TcpCounters &out) {
    // Key mapping reuses the pure computeTcpHealth() vocabulary
    // (/proc/net/snmp + /proc/net/netstat names):
    //   InSegs        <- tcps_rcvtotal          /* total packets received */
    //   OutSegs       <- tcps_sndtotal          /* total packets sent */
    //   RetransSegs   <- tcps_sndrexmitpack     /* data packets retransmitted */
    //   InErrs        <- rcvbadsum + rcvbadoff + rcvshort
    //                    (checksum/offset/truncation errors; no pure
    //                    RST-sent counter exists, so OutRsts omitted
    //                    rather than approximated with sndctrl=SYN|FIN|RST)
    //   CurrEstab     <- counted from the same-tick socket snapshot
    //                    (SamplerWorker; tcpstat carries no gauge)
    //   TCPTimeouts   <- tcps_rexmttimeo        /* retransmit timeouts */
    //   TCPSynRetrans <- tcps_sc_retransmitted  /* syncache entry was retransmitted */
    //   ListenDrops   <- tcps_listendrop        /* listen queue overflows */
    struct tcpstat stats {};
    if (!readTcpSysctl(stats)) {
        return false;
    }
    out = TcpCounters{};
    out.tcp.insert(QStringLiteral("InSegs"),
                   static_cast<quint64>(stats.tcps_rcvtotal));
    out.tcp.insert(QStringLiteral("OutSegs"),
                   static_cast<quint64>(stats.tcps_sndtotal));
    out.tcp.insert(QStringLiteral("RetransSegs"),
                   static_cast<quint64>(stats.tcps_sndrexmitpack));
    out.tcp.insert(
        QStringLiteral("InErrs"),
        static_cast<quint64>(stats.tcps_rcvbadsum + stats.tcps_rcvbadoff
                             + stats.tcps_rcvshort));
    out.ext.insert(QStringLiteral("TCPTimeouts"),
                   static_cast<quint64>(stats.tcps_rexmttimeo));
    out.ext.insert(QStringLiteral("TCPSynRetrans"),
                   static_cast<quint64>(stats.tcps_sc_retransmitted));
    out.ext.insert(QStringLiteral("ListenDrops"),
                   static_cast<quint64>(stats.tcps_listendrop));
    if (tcpCountersRedacted(out)) {
        // Verified on macos-14 CI (kernel 23.6.0): unprivileged callers
        // get a correctly sized struct of zeros while sudo netstat -s
        // shows real counters - macOS redacts per-protocol TCP stats.
        // No data unprivileged; the TCP HEALTH card stays gated.
        return false;
    }
    return true;
}

bool MacBackend::hostSecurityPosture(QList<SecurityFinding> &out) {
    // osquery darwin tables ground the fact model: FileVault status
    // (kFileVaultStatusOn/Off/Unknown), Gatekeeper assessment DB
    // ("Cannot open Gatekeeper DB: ") and codesigning_flags; the tools
    // used here follow the standard Apple userland probing set.
    out.clear();

    enum class Check { SIP, Gatekeeper, Firewall, FileVault };

    auto runTool = [](const QString &program,
                      const QStringList &args) -> QString {
        QProcess process;
        process.start(program, args);
        if (!process.waitForStarted(800)
            || !process.waitForFinished(2500)
            || process.exitStatus() != QProcess::NormalExit
            || process.exitCode() != 0) {
            return QString();
        }
        return QString::fromUtf8(process.readAllStandardOutput()).toLower();
    };

    const QString sip =
        runTool(QStringLiteral("/usr/bin/csrutil"),
                QStringList{QStringLiteral("status")});
    if (!sip.isEmpty() && sip.contains(QStringLiteral("enabled"))) {
        out.append({QStringLiteral("system integrity protection"), 0.0, true});
    } else if (!sip.isEmpty()) {
        out.append({QStringLiteral("system integrity protection"), 10.0, false});
    }

    const QString fileVault =
        runTool(QStringLiteral("/usr/bin/fdesetup"),
                QStringList{QStringLiteral("status")});
    if (!fileVault.isEmpty()
        && fileVault.contains(QStringLiteral("filevault is on"))) {
        out.append({QStringLiteral("disk encryption"), 0.0, true});
    } else if (!fileVault.isEmpty()) {
        out.append({QStringLiteral("disk encryption"), 8.0, false});
    }

    const QString gatekeeper =
        runTool(QStringLiteral("/usr/sbin/spctl"),
                QStringList{QStringLiteral("--status")});
    if (!gatekeeper.isEmpty()
        && gatekeeper.contains(QStringLiteral("enabled"))) {
        out.append({QStringLiteral("gatekeeper assessments"), 0.0, true});
    } else if (!gatekeeper.isEmpty()) {
        out.append({QStringLiteral("gatekeeper assessments"), 8.0, false});
    }

    const QString firewall =
        runTool(QStringLiteral(
                    "/usr/libexec/ApplicationFirewall/socketfilterfw"),
                QStringList{QStringLiteral("--getglobalstate")});
    if (!firewall.isEmpty()
        && firewall.contains(QStringLiteral("enabled"))) {
        out.append({QStringLiteral("application firewall"), 0.0, true});
    } else if (!firewall.isEmpty()) {
        out.append({QStringLiteral("application firewall"), 6.0, false});
    }

    return !out.isEmpty();
}

QList<linuxproc::SocketInfo> MacBackend::readNetSockets() {
    QList<linuxproc::SocketInfo> out;

    const int count = proc_listallpids(nullptr, 0);
    if (count <= 0) {
        return out;
    }
    std::vector<pid_t> pids(static_cast<size_t>(count) + 64);
    const int got = proc_listallpids(pids.data(),
                                     static_cast<int>(pids.size()
                                                          * sizeof(pid_t)));
    if (got <= 0) {
        return out;
    }

    for (int i = 0; i < got; ++i) {
        const pid_t pid = pids[i];
        if (pid <= 0) {
            continue;
        }
        const int fdsLen = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, 0, 0);
        if (fdsLen <= 0) {
            continue;
        }
        std::vector<char> fdsBuf(static_cast<size_t>(fdsLen));
        const int readLen = proc_pidinfo(pid, PROC_PIDLISTFDS, 0,
                                         fdsBuf.data(), fdsLen);
        if (readLen <= 0) {
            continue;
        }
        const auto *fds = reinterpret_cast<const struct proc_fdinfo *>(
            fdsBuf.data());
        const int fdCount = readLen / static_cast<int>(sizeof(*fds));

        for (int k = 0; k < fdCount; ++k) {
            if (fds[k].proc_fdtype != PROX_FDTYPE_SOCKET) {
                continue;
            }
            struct socket_fdinfo sfi {};
            const int n = proc_pidinfo(pid, PROC_PIDFDSOCKETINFO,
                                       fds[k].proc_fd, &sfi, sizeof(sfi));
            if (n <= 0) {
                continue;
            }
            const int family = sfi.psi.soi_family;
            const int proto = sfi.psi.soi_protocol;
            if (family != AF_INET && family != AF_INET6) {
                continue;
            }
            if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) {
                continue;
            }

            linuxproc::SocketInfo info;
            const bool v6 = family == AF_INET6;
            info.proto = proto == IPPROTO_TCP
                             ? (v6 ? QStringLiteral("tcp6")
                                   : QStringLiteral("tcp"))
                             : (v6 ? QStringLiteral("udp6")
                                   : QStringLiteral("udp"));

            char addrBuf[INET6_ADDRSTRLEN];
            if (v6) {
                inet_ntop(AF_INET6,
                          &sfi.psi.soi_proto.pri_in.insi_laddr.ina_6,
                          addrBuf, sizeof(addrBuf));
            } else {
                inet_ntop(AF_INET,
                          &sfi.psi.soi_proto.pri_in.insi_laddr.ina_46
                               .i46a_addr4,
                          addrBuf, sizeof(addrBuf));
            }
            info.localAddr = QString::fromLatin1(addrBuf);
            if (v6) {
                inet_ntop(AF_INET6,
                          &sfi.psi.soi_proto.pri_in.insi_faddr.ina_6,
                          addrBuf, sizeof(addrBuf));
            } else {
                inet_ntop(AF_INET,
                          &sfi.psi.soi_proto.pri_in.insi_faddr.ina_46
                               .i46a_addr4,
                          addrBuf, sizeof(addrBuf));
            }
            info.remoteAddr = QString::fromLatin1(addrBuf);
            info.localPort = ntohs(
                sfi.psi.soi_proto.pri_in.insi_lport);
            info.remotePort = ntohs(
                sfi.psi.soi_proto.pri_in.insi_fport);

            if (proto == IPPROTO_TCP) {
                // Darwin TCP FSM (netinet/tcp_fsm.h): TCPS_CLOSED=0 .. TCPS_TIME_WAIT=10.
                switch (sfi.psi.soi_state) {
                case kTcpStateClosed:
                    info.state = QStringLiteral("CLOSE");
                    break;
                case kTcpStateListen:
                    info.state = QStringLiteral("LISTEN");
                    break;
                case kTcpStateSynSent:
                    info.state = QStringLiteral("SYN_SENT");
                    break;
                case kTcpStateSynReceived:
                    info.state = QStringLiteral("SYN_RECV");
                    break;
                case kTcpStateEstablished:
                    info.state = QStringLiteral("ESTABLISHED");
                    break;
                case kTcpStateCloseWait:
                    info.state = QStringLiteral("CLOSE_WAIT");
                    break;
                case kTcpStateFinWait1:
                    info.state = QStringLiteral("FIN_WAIT1");
                    break;
                case kTcpStateClosing:
                    info.state = QStringLiteral("CLOSING");
                    break;
                case kTcpStateLastAck:
                    info.state = QStringLiteral("LAST_ACK");
                    break;
                case kTcpStateFinWait2:
                    info.state = QStringLiteral("FIN_WAIT2");
                    break;
                case kTcpStateTimeWait:
                    info.state = QStringLiteral("TIME_WAIT");
                    break;
                default:
                    info.state = QStringLiteral("UNKNOWN");
                    break;
                }
            } else {
                info.state = QStringLiteral("CLOSE");
            }
            out.append(info);
        }
    }
    return out;
}

bool MacBackend::readDiskCounters(QList<DiskCounters> &out) {
    out.clear();
    CFMutableDictionaryRef matching = IOServiceMatching("IOBlockStorageDriver");
    if (matching == nullptr) {
        return false;
    }
    io_iterator_t iterator = MACH_PORT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching,
                                     &iterator) != KERN_SUCCESS) {
        return false;
    }

    io_registry_entry_t entry = 0;
    while ((entry = IOIteratorNext(iterator)) != 0) {
        io_registry_entry_t media = MACH_PORT_NULL;
        if (IORegistryEntryGetParentEntry(entry, kIOServicePlane,
                                          &media) == KERN_SUCCESS) {
            CFStringRef name = static_cast<CFStringRef>(
                IORegistryEntryCreateCFProperty(media, CFSTR("BSD Name"),
                                                kCFAllocatorDefault, 0));
            CFDictionaryRef stats = static_cast<CFDictionaryRef>(
                IORegistryEntryCreateCFProperty(entry, CFSTR("Statistics"),
                                                kCFAllocatorDefault, 0));
            if (name != nullptr && stats != nullptr) {
                DiskCounters counters;
                counters.name = QString::fromCFString(name);

                auto num = [&stats](CFStringRef key, unsigned long long &v) {
                    CFNumberRef value = static_cast<CFNumberRef>(
                        CFDictionaryGetValue(stats, key));
                    return value != nullptr
                           && CFNumberGetValue(value, kCFNumberLongLongType,
                                               &v);
                };

                unsigned long long reads = 0;
                unsigned long long writes = 0;
                unsigned long long bytesRead = 0;
                unsigned long long bytesWritten = 0;
                unsigned long long readTimeNs = 0;
                unsigned long long writeTimeNs = 0;
                num(CFSTR("Operations (Read)"), reads);
                num(CFSTR("Operations (Write)"), writes);
                num(CFSTR("Bytes (Read)"), bytesRead);
                num(CFSTR("Bytes (Write)"), bytesWritten);
                num(CFSTR("TotalTime (Read)"), readTimeNs);
                num(CFSTR("TotalTime (Write)"), writeTimeNs);

                counters.reads = reads;
                counters.writes = writes;
                counters.sectorsRead = bytesRead / 512ULL;
                counters.sectorsWritten = bytesWritten / 512ULL;
                counters.msReading = readTimeNs / 1000000ULL;
                counters.msWriting = writeTimeNs / 1000000ULL;
                counters.msDoingIo = counters.msReading + counters.msWriting;
                counters.weightedMsDoingIo = counters.msDoingIo;
                out.append(counters);
            }
            if (name != nullptr) {
                CFRelease(name);
            }
            if (stats != nullptr) {
                CFRelease(stats);
            }
            if (media != MACH_PORT_NULL) {
                IOObjectRelease(media);
            }
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);

    return !out.isEmpty();
}

bool MacBackend::hostLoad(double &l1, double &l5, double &l15) {
    struct loadavg la{};
    int mib[2] = {CTL_VM, VM_LOADAVG};
    size_t size = sizeof(la);
    if (sysctl(mib, 2, &la, &size, nullptr, 0) != 0 || la.fscale == 0) {
        return false;
    }
    const double scale = static_cast<double>(la.fscale);
    l1 = static_cast<double>(la.ldavg[0]) / scale;
    l5 = static_cast<double>(la.ldavg[1]) / scale;
    l15 = static_cast<double>(la.ldavg[2]) / scale;
    return true;
}

bool MacBackend::diskUsage(const QString &path, unsigned long long &totalBytes,
                           unsigned long long &freeBytes) {
    struct statfs st{};
    const QByteArray p = path.toUtf8();
    if (statfs(p.constData(), &st) != 0) {
        return false;
    }
    const unsigned long long bsize = st.f_bsize;
    totalBytes = st.f_blocks * bsize;
    freeBytes = st.f_bavail * bsize;
    return true;
}

bool MacBackend::exists(int pid) {
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        return true;
    }
    if (kill(pid, 0) == 0) {
        return true;
    }
    if (errno == EPERM) {
        return true;
    }
    if (errno == ESRCH) {
        return false;
    }
    return true;
}

QString MacBackend::comm(int pid) {
    char buf[2 * MAXCOMLEN + 1] = {};
    if (proc_name(pid, buf, sizeof(buf)) <= 0) {
        return QString();
    }
    return QString::fromUtf8(buf);
}

QString MacBackend::exePath(int pid) {
    char buf[PROC_PIDPATHINFO_MAXSIZE] = {};
    if (proc_pidpath(pid, buf, sizeof(buf)) <= 0) {
        return QString();
    }
    return QString::fromUtf8(buf);
}

int MacBackend::parentPid(int pid) {
    kinfo_proc kp{};
    if (!readKinfo(pid, kp)) {
        return -1;
    }
    return kp.kp_eproc.e_ppid;
}

unsigned long long MacBackend::startTime(int pid) {
    kinfo_proc kp{};
    if (!readKinfo(pid, kp)) {
        return 0;
    }
    return static_cast<unsigned long long>(kp.kp_proc.p_starttime.tv_sec);
}

QList<int> MacBackend::allPids() {
    QList<int> pids;
    const int count = proc_listallpids(nullptr, 0);
    if (count <= 0) {
        return pids;
    }
    std::vector<pid_t> buf(static_cast<size_t>(count) + 64);
    const int got = proc_listallpids(buf.data(),
                                     static_cast<int>(buf.size() * sizeof(pid_t)));
    if (got <= 0) {
        return pids;
    }
    pids.reserve(got);
    for (int i = 0; i < got; ++i) {
        if (buf[i] > 0) {
            pids.append(buf[i]);
        }
    }
    return pids;
}

ProcessIdentity MacBackend::identity(int pid) {
    ProcessIdentity id;
    id.pid = pid;
    id.startTime = startTime(pid);
    return id;
}

bool MacBackend::procCpu(int pid, unsigned long long &ticks,
                         unsigned long long &start) {
    proc_taskinfo pti{};
    if (!readTaskInfo(pid, pti)) {
        return false;
    }
    start = startTime(pid);
    if (start == 0) {
        return false;
    }
    const double ratio = machTimebaseRatio();
    const double ns =
        static_cast<double>(pti.pti_total_user + pti.pti_total_system) * ratio;
    ticks = static_cast<unsigned long long>(ns);
    return true;
}

bool MacBackend::totalCpuTicks(unsigned long long &out) {
    uint64_t counts[CPU_STATE_MAX] = {};
    if (!readHostCpuLoad(counts)) {
        return false;
    }
    const unsigned long long total = counts[CPU_STATE_USER] + counts[CPU_STATE_SYSTEM] +
                                     counts[CPU_STATE_IDLE] + counts[CPU_STATE_NICE];
    out = total * kHostTickNs;
    return true;
}

int MacBackend::onlineCpuCount() {
    uint64_t v = 0;
    if (sysctlU32OrU64("hw.logicalcpu", v) && v > 0) {
        return static_cast<int>(v);
    }
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<int>(n) : 0;
}

bool MacBackend::rssKb(int pid, unsigned long long &out) {
    proc_taskinfo pti{};
    if (!readTaskInfo(pid, pti)) {
        return false;
    }
    out = pti.pti_resident_size / 1024ULL;
    return true;
}

bool MacBackend::procMem(int pid, linuxproc::MemInfo &out) {
    proc_taskinfo pti{};
    if (!readTaskInfo(pid, pti)) {
        return false;
    }
    out.rssKb = pti.pti_resident_size / 1024ULL;
    out.vmsKb = pti.pti_virtual_size / 1024ULL;
    out.swapKb = 0;
    out.sharedKb = 0;
    return true;
}

bool MacBackend::procSample(int pid, linuxproc::MemInfo &mem,
                            linuxproc::ProcDetail &detail) {
    proc_taskinfo pti{};
    if (!readTaskInfo(pid, pti)) {
        return false;
    }
    mem.rssKb = pti.pti_resident_size / 1024ULL;
    mem.vmsKb = pti.pti_virtual_size / 1024ULL;
    mem.swapKb = 0;
    mem.sharedKb = 0;

    detail.threads = pti.pti_threadnum;
    detail.voluntaryCtx = pti.pti_csw;
    detail.fdCount = countFds(pid);

    struct rusage_info_v6 ri{};
    if (proc_pid_rusage(pid, RUSAGE_INFO_V6,
                        reinterpret_cast<rusage_info_t *>(&ri)) == 0) {
        detail.readBytes = static_cast<long long>(ri.ri_diskio_bytesread);
        detail.writeBytes = static_cast<long long>(ri.ri_diskio_byteswritten);
    }
    return true;
}

bool MacBackend::processDiskCounters(int pid, long long &readBytes,
                                     long long &writeBytes) {
    struct rusage_info_v6 ri{};
    if (proc_pid_rusage(pid, RUSAGE_INFO_V6,
                        reinterpret_cast<rusage_info_t *>(&ri)) != 0) {
        return false;
    }
    readBytes = static_cast<long long>(ri.ri_diskio_bytesread);
    writeBytes = static_cast<long long>(ri.ri_diskio_byteswritten);
    return true;
}

int MacBackend::countFds(int pid) {
    const int bytes = proc_pidinfo(pid, kProcPidListFds, 0, nullptr, 0);
    if (bytes <= 0) {
        return -1;
    }
    return bytes / static_cast<int>(sizeof(struct proc_fdinfo));
}

QList<QString> MacBackend::fdTargets(int pid) {
    QList<QString> out;
    const int bytes = proc_pidinfo(pid, kProcPidListFds, 0, nullptr, 0);
    if (bytes <= 0) {
        return out;
    }
    const int capacity = bytes + static_cast<int>(sizeof(struct proc_fdinfo)) * 8;
    std::vector<char> raw(static_cast<size_t>(capacity));
    const int got = proc_pidinfo(pid, kProcPidListFds, 0, raw.data(), capacity);
    if (got <= 0) {
        return out;
    }
    const int n = got / static_cast<int>(sizeof(struct proc_fdinfo));
    const auto *fds = reinterpret_cast<const struct proc_fdinfo *>(raw.data());
    for (int i = 0; i < n; ++i) {
        const int fd = fds[i].proc_fd;
        struct vnode_fdinfowithpath vpi{};
        const int rc = proc_pidfdinfo(pid, fd, kProcPidFdVnodePathInfo, &vpi,
                                      sizeof(vpi));
        if (rc > 0 && vpi.pvip.vip_path[0] != '\0') {
            out.append(QString::fromUtf8(vpi.pvip.vip_path));
        } else {
            out.append(QStringLiteral("fd:%1").arg(fd));
        }
    }
    return out;
}

QByteArray MacBackend::processEnv(int pid) {
    ProcArgs args;
    if (!readProcArgs(pid, args)) {
        return QByteArray();
    }
    return args.env;
}

QString MacBackend::processCmdline(int pid) {
    ProcArgs args;
    if (!readProcArgs(pid, args)) {
        return QString();
    }
    QList<QString> parts;
    parts.reserve(args.argv.size());
    for (const QByteArray &a : args.argv) {
        parts.append(QString::fromUtf8(a));
    }
    return parts.join(QLatin1Char(' '));
}

#endif // Q_OS_MACOS
