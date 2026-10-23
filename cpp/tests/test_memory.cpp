#include "LinuxProcess.h"
#include "MemoryFormat.h"
#include "HostSampler.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QList>

#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

#if !defined(Q_OS_WIN)
#include <sys/socket.h>
#endif

#if defined(Q_OS_LINUX)
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <sys/socket.h>
#endif

#include <QtEndian>

#include <cmath>
#include <cstdio>
#if defined(Q_OS_WIN)
int main() {
    std::printf("skip: POSIX-only test\n");
    return 0;
}
#else
#if defined(Q_OS_WIN)
#define GETPID() ::_getpid()
#else
#define GETPID() ::getpid()
#endif

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    int failures = 0;
    auto check = [&failures](bool condition, const char *message) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition) {
            ++failures;
        }
    };
    auto near = [](double a, double b) { return std::fabs(a - b) < 1e-6; };

    // ---- format ----
    check(formatMemoryKb(0) == QStringLiteral("0 MB"), "0 KB -> 0 MB");
    check(formatMemoryKb(512ULL * 1024) == QStringLiteral("512 MB"),
          "512 MiB -> 512 MB");
    check(formatMemoryKb(1024ULL * 1024) == QStringLiteral("1.0 GB"),
          "1 GiB -> 1.0 GB");
    check(formatMemoryKb(2ULL * 1024 * 1024 + 512ULL * 1024)
              == QStringLiteral("2.5 GB"),
          "2.5 GiB -> 2.5 GB");

    // ---- VmRSS parser edge cases ----
    {
        unsigned long long kb = 0;
        check(linuxproc::parseVmRssKb("Name:\tcat\nVmRSS:\t   2576 kB\n", kb)
                  && kb == 2576,
              "parser: valid VmRSS line");
        check(!linuxproc::parseVmRssKb("Name:\tcat\nVmSize:\t100 kB\n", kb),
              "parser: missing VmRSS -> false");
        check(!linuxproc::parseVmRssKb("VmRSS:\tabc kB\n", kb),
              "parser: non-numeric VmRSS -> false");
        check(!linuxproc::parseVmRssKb("", kb), "parser: empty content -> false");
        check(!linuxproc::parseVmRssKb("VmRSS:\n", kb),
              "parser: VmRSS without value -> false");
        check(linuxproc::parseVmRssKb("VmRSS:\t0 kB\n", kb) && kb == 0,
              "parser: VmRSS 0 is valid (not absence)");
        check(linuxproc::parseVmRssKb("VmRSS:  12345   kB\n", kb) && kb == 12345,
              "parser: extra whitespace tolerated");
        check(linuxproc::parseVmRssKb("VmRSS:\t100 kB\r\n", kb) && kb == 100,
              "parser: CRLF line ending tolerated");
        check(linuxproc::parseVmRssKb("VmRSS:\t4096 kB\n", kb) && kb == 4096,
              "parser: tab separator tolerated");
        check(linuxproc::parseVmRssKb("VmRSS:\t18446744073709551615 kB\n", kb)
                  && kb == 18446744073709551615ULL,
              "parser: 64-bit max value tolerated");
    }

    // ---- status memory struct ----
    {
        linuxproc::MemInfo m;
        check(linuxproc::parseStatusMem(
                  "Name:\tcat\nVmSize:\t   9320 kB\nVmRSS:\t   2576 kB\nVmSwap:\t0 kB\n",
                  m)
                  && m.rssKb == 2576 && m.vmsKb == 9320 && m.swapKb == 0,
              "parseStatusMem: all fields");
        check(!linuxproc::parseStatusMem("VmSize:\t100 kB\n", m),
              "parseStatusMem: missing VmRSS -> false");
    }
    {
        linuxproc::MemInfo m;
        check(linuxproc::parseStatusMem("VmRSS:\t500 kB\n", m) && m.rssKb == 500
                  && m.vmsKb == 0 && m.swapKb == 0,
              "parseStatusMem: VmRSS only, others default 0");
    }
    {
        linuxproc::MemInfo m;
        check(!linuxproc::parseStatusMem("VmRSS:\tabc kB\n", m),
              "parseStatusMem: non-numeric VmRSS -> false");
    }
    {
        linuxproc::MemInfo m;
        check(linuxproc::parseStatusMem("VmRSS:\t100 kB\r\nVmSize:\t200 kB\r\n", m)
                  && m.rssKb == 100 && m.vmsKb == 200,
              "parseStatusMem: CRLF tolerated");
    }

    // ---- statm shared ----
    {
        unsigned long long shared = 0;
        check(linuxproc::parseStatmShared("4062 1823 1495 1208 0 139 0\n", 4096,
                                          shared)
                  && shared == 1495ULL * 4096 / 1024,
              "parseStatmShared: pages -> kB (1495p -> 5980 kB)");
        check(linuxproc::parseStatmShared("1 2 0\n", 4096, shared) && shared == 0,
              "parseStatmShared: zero shared is valid");
        check(!linuxproc::parseStatmShared("1 2\n", 4096, shared),
              "parseStatmShared: too few fields -> false");
        check(!linuxproc::parseStatmShared("1 2 abc\n", 4096, shared),
              "parseStatmShared: non-numeric -> false");
    }

    // ---- status detail (threads + ctx switches) ----
    {
        linuxproc::ProcDetail d;
        check(linuxproc::parseStatusDetail(
                  "Name:\tcat\nThreads:\t4\nvoluntary_ctxt_switches:\t12\n"
                  "nonvoluntary_ctxt_switches:\t3\n",
                  d)
                  && d.threads == 4 && d.voluntaryCtx == 12
                  && d.involuntaryCtx == 3,
              "parseStatusDetail: threads + ctx switches");
        linuxproc::ProcDetail e;
        check(linuxproc::parseStatusDetail("Name:\tcat\n", e) && e.threads == 0
                  && e.voluntaryCtx == 0 && e.involuntaryCtx == 0,
              "parseStatusDetail: missing fields default 0");
    }

    // ---- /proc/net parsing ----
    {
        const QByteArray table =
            "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
            "   0: 0100007F:0035 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 111 1\n"
            "   1: 0100007F:9C4E 0100007F:0035 01 00000000:00000000 00:00000000 00000000  1000        0 222 1\n";
        QList<linuxproc::SocketInfo> socks;
        check(linuxproc::parseNetTable(table, QStringLiteral("tcp"), socks)
                  && socks.size() == 2,
              "parseNetTable: tcp parses both rows");
        check(socks.at(0).localAddr == QStringLiteral("127.0.0.1")
                  && socks.at(0).localPort == 53
                  && socks.at(0).state == QStringLiteral("LISTEN")
                  && socks.at(0).inode == 111,
              "parseNetTable: LISTEN socket decoded");
        check(socks.at(1).remoteAddr == QStringLiteral("127.0.0.1")
                  && socks.at(1).remotePort == 53
                  && socks.at(1).state == QStringLiteral("ESTABLISHED")
                  && socks.at(1).inode == 222,
              "parseNetTable: ESTABLISHED socket decoded");
    }
    {
        const QByteArray table =
            "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
            "   0: 00000000000000000000000001000000:0050 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000 0 0 333 1\n";
        QList<linuxproc::SocketInfo> socks;
        check(linuxproc::parseNetTable(table, QStringLiteral("tcp6"), socks)
                  && socks.size() == 1
                  && socks.at(0).localAddr == QStringLiteral("::1")
                  && socks.at(0).localPort == 80
                  && socks.at(0).state == QStringLiteral("LISTEN"),
              "parseNetTable: IPv6 ::1 decoded");
    }

    // ---- cgroup parsing ----
    {
        QString version;
        QString path;
        check(linuxproc::parsePidCgroup("0::/user.slice/foo.scope\n", version, path)
                  && version == QStringLiteral("v2")
                  && path == QStringLiteral("/user.slice/foo.scope"),
              "parsePidCgroup: v2 unified");
        check(linuxproc::parsePidCgroup(
                  "5:memory:/docker/abc\n4:cpu,cpuacct:/docker/abc\n", version, path)
                  && version == QStringLiteral("v1")
                  && path == QStringLiteral("/docker/abc"),
              "parsePidCgroup: v1 controllers");
        check(!linuxproc::parsePidCgroup("", version, path),
              "parsePidCgroup: empty -> false");
    }
    {
        linuxproc::CgroupInfo info;
        check(linuxproc::parseCgroupCpuStat(
                  "usage_usec 100\nuser_usec 60\nsystem_usec 40\n"
                  "nr_periods 10\nnr_throttled 2\nthrottled_usec 500\n",
                  info)
                  && info.cpuUsageUsec == 100 && info.cpuUserUsec == 60
                  && info.nrPeriods == 10 && info.nrThrottled == 2
                  && info.throttledUsec == 500,
              "parseCgroupCpuStat: v2 fields");
        linuxproc::CgroupInfo v1;
        check(linuxproc::parseCgroupCpuStat(
                  "nr_periods 7\nthrottled_time 1000000\n", v1)
                  && v1.nrPeriods == 7 && v1.throttledUsec == 1000,
              "parseCgroupCpuStat: v1 throttled_time ns -> us");
    }
    {
        long long value = 0;
        check(linuxproc::parseCgroupMemoryValue("max\n", value) && value == -1,
              "parseCgroupMemoryValue: max -> unlimited");
        check(linuxproc::parseCgroupMemoryValue("12345\n", value)
                  && value == 12345,
              "parseCgroupMemoryValue: number");
        check(linuxproc::parseCgroupMemoryValue("9223372036854771712\n", value)
                  && value == -1,
              "parseCgroupMemoryValue: v1 unlimited sentinel");
        linuxproc::CgroupInfo info;
        check(linuxproc::parseCgroupMemoryStat("anon 10\nfile 20\nkernel 30\n", info)
                  && info.memAnonBytes == 10 && info.memFileBytes == 20
                  && info.memKernelBytes == 30,
              "parseCgroupMemoryStat: anon/file/kernel");
    }

    // ---- host metric parsing ----
    {
        linuxproc::HostCpuTimes cpu;
        check(linuxproc::parseHostCpu(
                  "cpu  1000 0 500 3000 200 0 0 0 0 0\ncpu0 1 2 3 4 5 6 7 8\n", cpu)
                  && cpu.total == 4700 && cpu.idle == 3200,
              "parseHostCpu: total 4700, idle 3200");

        linuxproc::HostMem mem;
        check(linuxproc::parseMemInfo(
                  "MemTotal:       1000 kB\nMemFree: 200 kB\nMemAvailable: 600 kB\n",
                  mem)
                  && mem.totalKb == 1000 && mem.freeKb == 200
                  && mem.availableKb == 600,
              "parseMemInfo: total/free/available");

        linuxproc::NetTotals net;
        check(linuxproc::parseNetDev(
                  "Inter-|   Receive                                                |  Transmit\n"
                  " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
                  "    lo: 999 0 0 0 0 0 0 0 888 0 0 0 0 0 0 0\n"
                  "  eth0: 1000 5 0 0 0 0 0 0 2000 7 0 0 0 0 0 0\n",
                  net)
                  && net.rxBytes == 1000 && net.txBytes == 2000,
              "parseNetDev: sums non-loopback rx/tx");

        double l1 = 0, l5 = 0, l15 = 0;
        check(linuxproc::parseLoadAvg("0.50 0.40 0.30 1/234 5678\n", l1, l5, l15)
                  && near(l1, 0.5) && near(l5, 0.4) && near(l15, 0.3),
              "parseLoadAvg: 1/5/15");
    }
    {
        const linuxproc::HostCpuTimes prev{1000, 400};
        const linuxproc::HostCpuTimes cur{2000, 700};
        check(near(linuxproc::hostCpuBusyPercent(prev, cur), 70.0),
              "hostCpuBusyPercent: 70% busy");
        check(near(linuxproc::hostCpuBusyPercent(cur, prev), 0.0),
              "hostCpuBusyPercent: counter reset -> 0");
        check(near(linuxproc::netRatePerSec(1000, 3000, 2.0), 1000.0),
              "netRatePerSec: 1000 B/s");
        check(near(linuxproc::netRatePerSec(3000, 1000, 2.0), 0.0),
              "netRatePerSec: reset -> 0");
    }
    {
        HostSource src;
        linuxproc::HostCpuTimes cpu{1000, 400};
        linuxproc::NetTotals net{1000, 2000};
        src.cpu = [&cpu](linuxproc::HostCpuTimes &o) { o = cpu; return true; };
        src.mem = [](linuxproc::HostMem &o) {
            o.totalKb = 100; o.availableKb = 40; o.freeKb = 10; return true;
        };
        src.net = [&net](linuxproc::NetTotals &o) { o = net; return true; };
        src.load = [](double &a, double &b, double &c) {
            a = 0.5; b = 0.4; c = 0.3; return true;
        };
        src.disk = [](unsigned long long &t, unsigned long long &f) {
            t = 1000; f = 400; return true;
        };

        HostSampler sampler(src);
        const HostSample first = sampler.sample();
        check(first.available && first.warming && first.memAvailable
                  && first.diskAvailable && first.loadAvailable,
              "host sampler: first sample warming + mem/disk/load");

        cpu.total = 2000;
        cpu.idle = 700; // delta total 1000, idle 300 -> 70% busy
        const HostSample second = sampler.sample();
        check(second.available && !second.warming
                  && near(second.cpuPercent, 70.0),
              "host sampler: second sample 70% busy");
        check(near(second.netRxPerSec, 0.0) && near(second.netTxPerSec, 0.0),
              "host sampler: unchanged net -> 0 rate");
        check(second.mem.availableKb == 40 && second.diskFreeBytes == 400,
              "host sampler: mem + disk carried through");
    }

    // ---- /proc/<pid>/io parsing ----
    {
        linuxproc::ProcDetail io;
        check(linuxproc::parseProcIo(
                  "rchar: 10\nwchar: 20\nsyscr: 3\nsyscw: 4\n"
                  "read_bytes: 500\nwrite_bytes: 900\ncancelled_write_bytes: 0\n",
                  io)
                  && io.rchar == 10 && io.wchar == 20 && io.readBytes == 500
                  && io.writeBytes == 900,
              "parseProcIo: rchar/wchar + read/write_bytes");
        linuxproc::ProcDetail io2;
        check(!linuxproc::parseProcIo("syscr: 3\nsyscw: 4\n", io2),
              "parseProcIo: no counters -> false");
    }

    // ---- interface table parsing ----
    {
        QList<linuxproc::InterfaceStats> interfaces;
        check(linuxproc::parseNetDevInterfaces(
                  "Inter-|   Receive                                                |  Transmit\n"
                  " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
                  "    lo: 999 1 2 3 0 0 0 0 888 4 5 6 0 0 0 0\n"
                  "  eth0: 1000 5 0 0 0 0 0 0 2000 7 1 2 0 0 0 0\n",
                  interfaces)
                  && interfaces.size() == 2
                  && interfaces.at(0).name == QStringLiteral("lo")
                  && interfaces.at(0).rxBytes == 999
                  && interfaces.at(0).txBytes == 888
                  && interfaces.at(1).name == QStringLiteral("eth0")
                  && interfaces.at(1).rxBytes == 1000
                  && interfaces.at(1).txBytes == 2000
                  && interfaces.at(1).txErrs == 1
                  && interfaces.at(1).txDrop == 2,
              "parseNetDevInterfaces: per-interface rows incl. lo");
    }

    // ---- services / instrumentation helpers ----
    {
        check(linuxproc::serviceForPort(5432) == QStringLiteral("postgres"),
              "serviceForPort: 5432 -> postgres");
        check(linuxproc::serviceForPort(443) == QStringLiteral("https"),
              "serviceForPort: 443 -> https");
        check(linuxproc::serviceForPort(12345).isEmpty(),
              "serviceForPort: unknown -> empty");
        check(linuxproc::portIsDatabase(6379)
                  && !linuxproc::portIsDatabase(443),
              "portIsDatabase: redis yes, https no");
        check(linuxproc::endpointExposure(QStringLiteral("0.0.0.0"))
                      == QStringLiteral("public")
                  && linuxproc::endpointExposure(QStringLiteral("127.0.0.1"))
                         == QStringLiteral("local")
                  && linuxproc::endpointExposure(QStringLiteral("::1"))
                         == QStringLiteral("local")
                  && linuxproc::endpointExposure(QStringLiteral("::"))
                         == QStringLiteral("public"),
              "endpointExposure: public vs local");
    }
    {
        QByteArray environ;
        environ.append("PATH=/usr/bin");
        environ.append('\0');
        environ.append("DD_TRACE_ENABLED=true");
        environ.append('\0');
        environ.append("HOME=/root");
        environ.append('\0');

        QList<QString> agents;
        check(linuxproc::parseInstrumentation(
                  environ, QByteArrayLiteral("/usr/lib/libopentelemetry_c.so.1\n"),
                  agents)
                  && agents.contains(QStringLiteral("datadog"))
                  && agents.contains(QStringLiteral("opentelemetry")),
              "parseInstrumentation: DD env + OTel lib");

        QList<QString> none;
        check(!linuxproc::parseInstrumentation(QByteArrayLiteral("PATH=/x\0"),
                                               QByteArrayLiteral("/lib/libc.so.6\n"),
                                               none)
                  && none.isEmpty(),
              "parseInstrumentation: clean process -> none");
    }

    // ---- real /proc ----
#ifdef Q_OS_LINUX
    const int self = GETPID();
    unsigned long long rss = 0;
    check(linuxproc::rssKb(self, rss), "rssKb(self) read succeeds");
    check(rss > 0, "rssKb(self) is positive");
    check(!linuxproc::rssKb(1 << 30, rss), "rssKb(bogus) fails");

    {
        linuxproc::MemInfo m;
        check(linuxproc::procMem(self, m) && m.rssKb > 0,
              "procMem(self): reads rss + vms + swap + shared");
    }

    {
        linuxproc::MemInfo m;
        linuxproc::ProcDetail d;
        check(linuxproc::procSample(self, m, d) && m.rssKb > 0
                  && d.threads > 0,
              "procSample(self): memory + threads via one read");
        check(linuxproc::countFds(self) >= 0, "countFds(self) is readable");
        check(linuxproc::countFds(1 << 30) == -1,
              "countFds(bogus) -> -1 (unreadable)");
        check(linuxproc::socketInodes(1 << 30).isEmpty(),
              "socketInodes(bogus) is empty");

        // Regression: QFile::symLinkTarget() rewrote "socket:[N]" against the
        // link's directory, so no sockets were ever detected on procfs.
        int sv[2] = {-1, -1};
        check(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
              "socketpair created for fd scan");
        const QList<unsigned long long> inodes = linuxproc::socketInodes(self);
        check(inodes.size() >= 2,
              "socketInodes finds this process's live sockets");

        check(!linuxproc::fdTargets(self).isEmpty(),
              "fdTargets(self) lists open files");
        check(linuxproc::fdTargets(1 << 30).isEmpty(),
              "fdTargets(bogus) is empty");
        bool sawSocket = false;
        for (const QString &target : linuxproc::fdTargets(self)) {
            if (target.startsWith(QStringLiteral("socket:["))) {
                sawSocket = true;
            }
        }
        check(sawSocket, "fdTargets includes the socketpair sockets");

        QList<QString> libs;
        check(linuxproc::sharedLibraries(self, libs) && !libs.isEmpty(),
              "sharedLibraries(self) lists .so files");
        check(!linuxproc::sharedLibraries(1 << 30, libs),
              "sharedLibraries(bogus) -> false");
        if (sv[0] >= 0) {
            ::close(sv[0]);
        }
        if (sv[1] >= 0) {
            ::close(sv[1]);
        }

        linuxproc::CgroupInfo cg;
        check(linuxproc::cgroupSample(self, cg) && cg.available
                  && !cg.version.isEmpty() && !cg.path.isEmpty(),
              "cgroupSample(self): available with version + path");

        QList<QString> selfAgents;
        linuxproc::processInstrumentation(self, selfAgents);
        check(true, "processInstrumentation(self) does not crash");

        linuxproc::HostCpuTimes hostCpu;
        check(linuxproc::hostCpuTimes(hostCpu) && hostCpu.total > 0,
              "hostCpuTimes: positive total");
        linuxproc::HostMem hostMem;
        check(linuxproc::hostMem(hostMem) && hostMem.totalKb > 0,
              "hostMem: positive total");
        double hl1 = 0, hl5 = 0, hl15 = 0;
        check(linuxproc::hostLoad(hl1, hl5, hl15), "hostLoad: readable");
        unsigned long long diskTotal = 0, diskFree = 0;
        check(linuxproc::diskUsage(QStringLiteral("/"), diskTotal, diskFree)
                  && diskTotal > 0,
              "diskUsage(/): positive total");

        HostSampler hostSampler;
        const HostSample h1 = hostSampler.sample();
        check(h1.available && h1.warming, "host sampler real: first warming");
        const HostSample h2 = hostSampler.sample();
        check(h2.available && !h2.warming && h2.cpuPercent >= 0.0
                  && h2.cpuPercent <= 100.0,
              "host sampler real: second measured 0-100%");
    }
#endif

    // ---- group sum (deterministic seam) ----
    {
        const QHash<int, unsigned long long> table{{10, 100}, {20, 250}};
        linuxproc::RssReader reader = [&table](int pid, unsigned long long &out) {
            const auto it = table.constFind(pid);
            if (it == table.constEnd()) {
                return false;
            }
            out = it.value();
            return true;
        };

        unsigned long long total = 0;
        check(linuxproc::sumRssKbWith(QList<int>{10}, reader, total) && total == 100,
              "sumRssKbWith single pid");
        check(linuxproc::sumRssKbWith(QList<int>{10, 20}, reader, total)
                  && total == 350,
              "sumRssKbWith two pids summed");
        check(linuxproc::sumRssKbWith(QList<int>{10, 999}, reader, total)
                  && total == 100,
              "sumRssKbWith skips unreadable pid, keeps valid");
        check(!linuxproc::sumRssKbWith(QList<int>{999}, reader, total),
              "sumRssKbWith all unreadable -> false");
        check(!linuxproc::sumRssKbWith(QList<int>{}, reader, total),
              "sumRssKbWith empty -> false");
        check(linuxproc::sumRssKbWith(QList<int>{10, 10}, reader, total)
                  && total == 200,
              "sumRssKbWith double-counts shared pages (caveat documented)");
    }

    // ---- M62a: socket queues + netlink tcp diag + kernel limits ----
    {
        const QByteArray queues =
            "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
            "   0: 0100007F:0035 00000000:0000 0A 00000012:00000400 00:00000000 00000000     0        0 111 1\n";
        QList<linuxproc::SocketInfo> socks;
        check(linuxproc::parseNetTable(queues, QStringLiteral("tcp"), socks)
                  && socks.size() == 1,
              "parseNetTable: queued row parses");
        check(socks.at(0).txQueue == 0x12 && socks.at(0).rxQueue == 0x400,
              "parseNetTable: tx/rx queue hex columns decoded");
    }
#if defined(Q_OS_LINUX)
    {
        QByteArray fixtureMessage;
        const char header[4] = {
            static_cast<char>(AF_INET), 0x01, 0x00, 0x00
        };
        fixtureMessage.append(header, 4);
        QByteArray idBytes(48, '\0');
        auto *id = reinterpret_cast<struct inet_diag_sockid *>(idBytes.data());
        qToBigEndian<quint16>(4711, &id->idiag_sport);
        qToBigEndian<quint16>(80, &id->idiag_dport);
        const unsigned char src[4] = {1, 2, 3, 4};
        id->idiag_src[0] = *reinterpret_cast<const __be32 *>(src);
        const unsigned char dst[4] = {93, 184, 216, 34};
        id->idiag_dst[0] = *reinterpret_cast<const __be32 *>(dst);
        id->idiag_cookie[0] = INET_DIAG_NOCOOKIE;
        id->idiag_cookie[1] = INET_DIAG_NOCOOKIE;
        fixtureMessage.append(idBytes);
        const quint32 tails[5] = {0, 33, 44, 1000, 777};
        fixtureMessage.append(reinterpret_cast<const char *>(tails),
                              sizeof(tails));

        QByteArray tiPayload(192, '\0');
        auto *ti = reinterpret_cast<struct tcp_info *>(tiPayload.data());
        ti->tcpi_retransmits = 3;
        ti->tcpi_rtt = 1200;
        ti->tcpi_rttvar = 300;
        ti->tcpi_snd_cwnd = 10;
        ti->tcpi_snd_mss = 1448;
        ti->tcpi_total_retrans = 44;
        ti->tcpi_bytes_acked = 5555;
        ti->tcpi_bytes_received = 6666;
        ti->tcpi_pmtu = 1500;
        const quint16 infoLen = static_cast<quint16>(
            RTA_LENGTH(static_cast<unsigned int>(tiPayload.size())));
        fixtureMessage.append(reinterpret_cast<const char *>(&infoLen), 2);
        const quint16 infoType = INET_DIAG_INFO;
        fixtureMessage.append(reinterpret_cast<const char *>(&infoType), 2);
        fixtureMessage.append(tiPayload);

        const quint16 congLen = static_cast<quint16>(RTA_LENGTH(6));
        fixtureMessage.append(reinterpret_cast<const char *>(&congLen), 2);
        const quint16 congType = INET_DIAG_CONG;
        fixtureMessage.append(reinterpret_cast<const char *>(&congType), 2);
        fixtureMessage.append("cubic", 6);
        fixtureMessage.append(2, '\0');

        const quint32 skmem[SK_MEMINFO_VARS] = {7, 212992, 1, 212992, 0, 4, 0,
                                                0, 5};
        const quint16 skLen = static_cast<quint16>(
            RTA_LENGTH(SK_MEMINFO_VARS * sizeof(quint32)));
        fixtureMessage.append(reinterpret_cast<const char *>(&skLen), 2);
        const quint16 skType = INET_DIAG_SKMEMINFO;
        fixtureMessage.append(reinterpret_cast<const char *>(&skType), 2);
        fixtureMessage.append(reinterpret_cast<const char *>(skmem),
                              sizeof(skmem));

        struct nlmsghdr record = {};
        record.nlmsg_len = NLMSG_LENGTH(
            static_cast<unsigned int>(fixtureMessage.size()));
        record.nlmsg_type = SOCK_DIAG_BY_FAMILY;
        QByteArray content(reinterpret_cast<const char *>(&record), sizeof(record));
        content.append(fixtureMessage);
        content.append(QByteArray(NLMSG_ALIGN(record.nlmsg_len)
                                      - record.nlmsg_len,
                                  '\0'));

        struct nlmsghdr done = {};
        done.nlmsg_len = NLMSG_LENGTH(0);
        done.nlmsg_type = NLMSG_DONE;
        content.append(reinterpret_cast<const char *>(&done), sizeof(done));

        QList<linuxproc::TcpDiagInfo> parsed;
        check(linuxproc::parseTcpDiagDump(content, parsed)
                  && parsed.size() == 1,
              "parseTcpDiagDump: synthetic inet_diag message parses");
        if (parsed.size() == 1) {
            const linuxproc::TcpDiagInfo &info = parsed.first();
            check(info.localAddr == QStringLiteral("1.2.3.4")
                      && info.localPort == 4711
                      && info.remoteAddr == QStringLiteral("93.184.216.34")
                      && info.remotePort == 80
                      && info.state == QStringLiteral("ESTABLISHED")
                      && info.inode == 777
                      && info.rqueue == 33 && info.wqueue == 44
                      && info.uid == 1000,
                  "parseTcpDiagDump: socket identity decoded");
            check(info.hasInfo && info.rttUsec == 1200 && info.rttVarUsec == 300
                      && info.sndCwnd == 10 && info.sndMss == 1448
                      && info.retransmits == 3 && info.totalRetrans == 44
                      && info.bytesAcked == 5555 && info.bytesReceived == 6666
                      && info.pmtu == 1500,
                  "parseTcpDiagDump: tcp_info payload decoded");
            check(info.congestion == QStringLiteral("cubic"),
                  "parseTcpDiagDump: congestion algorithm attr decoded");
            check(info.hasSkmem && info.skRmemAlloc == 7
                      && info.skRcvBuf == 212992 && info.skDrops == 5
                      && info.skWmemQueued == 4,
                  "parseTcpDiagDump: skmeminfo payload decoded");
        }
    }
#endif
    {
        linuxproc::CgroupInfo events;
        check(linuxproc::parseCgroupMemoryEvents(
                  "oom 7\noom_kill 2\nmax 1\n", events)
              && events.memOomEvents == 7 && events.memOomKills == 2,
              "parseCgroupMemoryEvents: oom/oom_kill counters");
    }
    {
        unsigned long long allocated = 0;
        unsigned long long limit2 = 0;
        check(linuxproc::parseFileNr("  34976  0  1048576\n", allocated,
                                     limit2)
                  && allocated == 34976 && limit2 == 1048576,
              "parseFileNr: allocated/limit columns");
        check(!linuxproc::parseFileNr("no data", allocated, limit2),
              "parseFileNr: malformed row rejected");
    }
    {
        QList<QString> algorithms;
        linuxproc::parseCongestionAvailable("cubic bbr2 cubic reno\n",
                                            algorithms);
        check(algorithms.size() == 3
                  && algorithms.at(1) == QStringLiteral("bbr2"),
              "parseCongestionAvailable: split + dedupe");
    }
    {
        int running = -1;
        int blocked2 = -1;
        check(linuxproc::parseHostStatTail(
                  "procs_running 5\nprocs_blocked 2\ncpu 1 2 3\n",
                  running, blocked2)
                  && running == 5 && blocked2 == 2,
              "parseHostStatTail: running/blocked");
    }
    check(formatBytesCompact(97) == QStringLiteral("97 B")
              && formatBytesCompact(2048) == QStringLiteral("2 KB")
              && formatBytesCompact(3 * 1024ULL * 1024ULL)
                     == QStringLiteral("3.0 MB"),
          "formatBytesCompact: B/KB/MB");
    {
        linuxproc::HostCpuTimes stealCpu;
        check(linuxproc::parseHostCpu(
                  "cpu  1000 0 500 3000 200 0 0 700 0 0\ncpu0 1 2 3 4\n",
                  stealCpu)
                  && stealCpu.steal == 700 && stealCpu.idle == 3200,
              "parseHostCpu: steal column decoded");
    }

    // ---- group sum (real /proc) ----
#ifdef Q_OS_LINUX
    {
        unsigned long long total = 0;
        check(linuxproc::sumRssKb(QList<int>{self}, total) && total > 0,
              "sumRssKb(self) reads a positive group total");
        check(!linuxproc::sumRssKb(QList<int>{1 << 30}, total),
              "sumRssKb(all unreadable) -> false");
    }

    std::printf("sample rss(self)=%llu kB -> %s\n", rss,
                qUtf8Printable(formatMemoryKb(rss)));
#endif

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

#endif