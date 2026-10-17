#include "DumpReport.h"

#include "platform/PlatformFactory.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QThread>

#include <algorithm>
#include <unistd.h>

namespace {

constexpr int kSampleIntervalMs = 200;

struct ProcTick {
    unsigned long long ticks = 0;
    unsigned long long start = 0;
    bool valid = false;
};

QHash<int, ProcTick> sampleProcTicks(SystemBackend &backend,
                                     const QList<int> &pids) {
    QHash<int, ProcTick> out;
    out.reserve(pids.size());
    for (int pid : pids) {
        ProcTick t;
        t.valid = backend.procCpu(pid, t.ticks, t.start);
        out.insert(pid, t);
    }
    return out;
}

QJsonObject capabilitiesJson(const PlatformCapabilities &caps) {
    QJsonObject obj;
    obj.insert(QStringLiteral("cgroups"), caps.cgroups);
    obj.insert(QStringLiteral("systemServices"), caps.systemServices);
    obj.insert(QStringLiteral("journal"), caps.journal);
    obj.insert(QStringLiteral("fim"), caps.fim);
    obj.insert(QStringLiteral("sbom"), caps.sbom);
    obj.insert(QStringLiteral("dpkgSbom"), caps.dpkgSbom);
    obj.insert(QStringLiteral("tcpCounters"), caps.tcpCounters);
    obj.insert(QStringLiteral("psi"), caps.psi);
    obj.insert(QStringLiteral("namespaces"), caps.namespaces);
    return obj;
}

} // namespace

QString dumpReportJson(int maxProcs) {
    SystemBackend &backend = systemBackend();

    int cores = backend.onlineCpuCount();
    if (cores < 1) {
        cores = 1;
    }

    linuxproc::HostCpuTimes hostCpu0;
    const bool hostCpuOk = backend.hostCpuTimes(hostCpu0);
    unsigned long long total0 = 0;
    backend.totalCpuTicks(total0);
    linuxproc::NetTotals net0;
    const bool netOk = backend.hostNet(net0);

    const QList<int> pids = backend.allPids();
    const QHash<int, ProcTick> first = sampleProcTicks(backend, pids);

    QThread::msleep(kSampleIntervalMs);

    linuxproc::HostCpuTimes hostCpu1;
    const bool hostCpuOk1 = backend.hostCpuTimes(hostCpu1);
    unsigned long long total1 = 0;
    backend.totalCpuTicks(total1);
    linuxproc::NetTotals net1;
    const bool netOk1 = backend.hostNet(net1);
    const QHash<int, ProcTick> second = sampleProcTicks(backend, pids);

    QJsonObject host;
    if (hostCpuOk && hostCpuOk1) {
        host.insert(QStringLiteral("cpuPercent"),
                    linuxproc::hostCpuBusyPercent(hostCpu0, hostCpu1));
    }
    host.insert(QStringLiteral("cores"), cores);

    linuxproc::HostMem mem;
    if (backend.hostMem(mem)) {
        host.insert(QStringLiteral("memTotalKb"),
                    static_cast<double>(mem.totalKb));
        host.insert(QStringLiteral("memAvailableKb"),
                    static_cast<double>(mem.availableKb));
        host.insert(QStringLiteral("memFreeKb"),
                    static_cast<double>(mem.freeKb));
    }

    double l1 = 0.0;
    double l5 = 0.0;
    double l15 = 0.0;
    if (backend.hostLoad(l1, l5, l15)) {
        host.insert(QStringLiteral("load1"), l1);
        host.insert(QStringLiteral("load5"), l5);
        host.insert(QStringLiteral("load15"), l15);
    }

    unsigned long long diskTotal = 0;
    unsigned long long diskFree = 0;
    if (backend.diskUsage(QStringLiteral("/"), diskTotal, diskFree)) {
        host.insert(QStringLiteral("diskTotalBytes"),
                    static_cast<double>(diskTotal));
        host.insert(QStringLiteral("diskFreeBytes"),
                    static_cast<double>(diskFree));
    }

    if (netOk && netOk1) {
        const double secs = static_cast<double>(kSampleIntervalMs) / 1000.0;
        host.insert(QStringLiteral("netRxPerSec"),
                    linuxproc::netRatePerSec(net0.rxBytes, net1.rxBytes, secs));
        host.insert(QStringLiteral("netTxPerSec"),
                    linuxproc::netRatePerSec(net0.txBytes, net1.txBytes, secs));
    }

    QList<linuxproc::InterfaceStats> ifaces;
    if (backend.hostInterfaces(ifaces)) {
        host.insert(QStringLiteral("ifaceCount"), ifaces.size());
    }

    const long long deltaTotal =
        static_cast<long long>(total1) - static_cast<long long>(total0);

    struct Row {
        QJsonObject obj;
        double cpuPercent = 0.0;
        unsigned long long rssKb = 0;
    };
    QList<Row> rows;
    rows.reserve(pids.size());

    for (int pid : pids) {
        const ProcTick a = first.value(pid);
        const ProcTick b = second.value(pid);
        if (!a.valid || !b.valid || a.start != b.start) {
            continue;
        }

        linuxproc::MemInfo procMem;
        linuxproc::ProcDetail detail;
        if (!backend.procSample(pid, procMem, detail)) {
            continue;
        }

        double cpuPercent = 0.0;
        if (deltaTotal > 0 && b.ticks >= a.ticks) {
            cpuPercent =
                static_cast<double>(b.ticks - a.ticks)
                / static_cast<double>(deltaTotal) * 100.0 * static_cast<double>(cores);
            cpuPercent = std::clamp(cpuPercent, 0.0, 999.0 * cores);
        }

        QJsonObject obj;
        obj.insert(QStringLiteral("pid"), pid);
        obj.insert(QStringLiteral("ppid"), backend.parentPid(pid));
        obj.insert(QStringLiteral("comm"), backend.comm(pid));
        obj.insert(QStringLiteral("exe"), backend.exePath(pid));
        obj.insert(QStringLiteral("rssKb"),
                   static_cast<double>(procMem.rssKb));
        obj.insert(QStringLiteral("threads"), detail.threads);
        obj.insert(QStringLiteral("fds"), detail.fdCount);
        obj.insert(QStringLiteral("cpuPercent"), cpuPercent);

        Row row;
        row.obj = obj;
        row.cpuPercent = cpuPercent;
        row.rssKb = procMem.rssKb;
        rows.append(row);
    }

    std::sort(rows.begin(), rows.end(), [](const Row &lhs, const Row &rhs) {
        if (lhs.cpuPercent != rhs.cpuPercent) {
            return lhs.cpuPercent > rhs.cpuPercent;
        }
        return lhs.rssKb > rhs.rssKb;
    });

    QJsonArray processes;
    const int limit = std::min<int>(maxProcs, rows.size());
    for (int i = 0; i < limit; ++i) {
        processes.append(rows[i].obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("platform"), backend.platformName());
    root.insert(QStringLiteral("selfPid"), static_cast<int>(::getpid()));
    root.insert(QStringLiteral("capabilities"), capabilitiesJson(backend.capabilities()));
    root.insert(QStringLiteral("host"), host);
    root.insert(QStringLiteral("processCount"), pids.size());
    root.insert(QStringLiteral("processes"), processes);

    QJsonObject tcp;
    TcpCounters tcpCounters;
    const bool tcpAvailable = backend.readTcpCounters(tcpCounters);
    tcp.insert(QStringLiteral("available"), tcpAvailable);
    if (tcpAvailable) {
        tcp.insert(QStringLiteral("inSegs"),
                   static_cast<qint64>(tcpCounters.tcp.value(
                       QStringLiteral("InSegs"))));
        tcp.insert(QStringLiteral("outSegs"),
                   static_cast<qint64>(tcpCounters.tcp.value(
                       QStringLiteral("OutSegs"))));
        tcp.insert(QStringLiteral("retransSegs"),
                   static_cast<qint64>(tcpCounters.tcp.value(
                       QStringLiteral("RetransSegs"))));
        tcp.insert(QStringLiteral("timeouts"),
                   static_cast<qint64>(tcpCounters.ext.value(
                       QStringLiteral("TCPTimeouts"))));
        for (auto it = tcpCounters.tcp.constBegin();
             it != tcpCounters.tcp.constEnd(); ++it) {
            if (!tcp.contains(it.key())) {
                tcp.insert(it.key(), static_cast<qint64>(it.value()));
            }
        }
        for (auto it = tcpCounters.ext.constBegin();
             it != tcpCounters.ext.constEnd(); ++it) {
            tcp.insert(QStringLiteral("ext.%1").arg(it.key()),
                       static_cast<qint64>(it.value()));
        }
    }
    root.insert(QStringLiteral("tcp"), tcp);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
