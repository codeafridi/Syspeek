#include "DumpReport.h"

#include "FleetSource.h"
#include "FleetStatus.h"

#include "CloudProbe.h"
#include "CloudStatus.h"
#include "Config.h"
#include "HistoryLog.h"
#include "RemoteStatus.h"
#include "RemoteSpool.h"
#include "TickBudget.h"
#include "MetricsEndpoint.h"
#include "CloudFleetSource.h"
#include "CloudSpendSource.h"
#include "CloudPricing.h"

#include "OverlayStatus.h"
#include "ProcessDiskStatus.h"

#include "DiskStatus.h"
#include "ProcessActions.h"
#include "ProcessNetStatus.h"
#include "SbomStatus.h"
#include "HostInfoStatus.h"
#include "platform/PlatformFactory.h"
#include "platform/linux/LinuxOverlay.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QList>
#include <QThread>

#include <algorithm>
#if defined(Q_OS_WIN)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

int dumpPid() {
#if defined(Q_OS_WIN)
    return static_cast<int>(::_getpid());
#else
    return static_cast<int>(::getpid());
#endif
}

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
#if defined(Q_OS_LINUX)
    linuxproc::KernelLimits kernel;
    if (backend.kernelLimits(kernel) && kernel.available) {
        host.insert(QStringLiteral("fdAllocated"),
                    static_cast<double>(kernel.fdAllocated));
        host.insert(QStringLiteral("fdMax"),
                    static_cast<double>(kernel.fdMax));
        host.insert(QStringLiteral("somaxconn"),
                    static_cast<double>(kernel.somaxconn));
        if (!kernel.congestion.isEmpty()) {
            host.insert(QStringLiteral("congestion"), kernel.congestion);
        }
        host.insert(QStringLiteral("runningProcs"), kernel.runningProcs);
        host.insert(QStringLiteral("blockedProcs"), kernel.blockedProcs);
        host.insert(QStringLiteral("swappiness"), kernel.swappiness);
        host.insert(QStringLiteral("overcommit"), kernel.overcommit);
    }
#endif

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
#if defined(Q_OS_WIN)
    const int selfPid = static_cast<int>(::_getpid());
    root.insert(QStringLiteral("selfPid"), selfPid);
#else
    root.insert(QStringLiteral("selfPid"), static_cast<int>(::getpid()));
#endif
    root.insert(QStringLiteral("capabilities"), capabilitiesJson(backend.capabilities()));
    root.insert(QStringLiteral("host"), host);
    root.insert(QStringLiteral("processCount"), pids.size());
    root.insert(QStringLiteral("processes"), processes);
#if defined(Q_OS_LINUX)
    {
        QList<linuxproc::TcpDiagInfo> diag;
        const bool queried = linuxproc::queryTcpDiag(diag);
        QJsonObject tcpDiag;
        tcpDiag.insert(QStringLiteral("queried"), queried);
        int withInfo = 0;
        for (const linuxproc::TcpDiagInfo &info : diag) {
            if (info.hasInfo) {
                ++withInfo;
            }
        }
        tcpDiag.insert(QStringLiteral("count"), diag.size());
        tcpDiag.insert(QStringLiteral("withInfo"), withInfo);
        QJsonObject first;
        if (!diag.isEmpty()) {
            const linuxproc::TcpDiagInfo &info = diag.first();
            first.insert(QStringLiteral("local"),
                         QStringLiteral("%1:%2")
                             .arg(info.localAddr)
                             .arg(info.localPort));
            first.insert(QStringLiteral("remote"),
                         QStringLiteral("%1:%2")
                             .arg(info.remoteAddr)
                             .arg(info.remotePort));
            first.insert(QStringLiteral("state"), info.state);
            if (info.hasInfo) {
                first.insert(QStringLiteral("rttMs"),
                             info.rttUsec / 1000.0);
                first.insert(QStringLiteral("cwnd"),
                             static_cast<double>(info.sndCwnd));
                first.insert(QStringLiteral("totalRetrans"),
                             static_cast<double>(info.totalRetrans));
            }
        }
        tcpDiag.insert(QStringLiteral("first"), first);
        root.insert(QStringLiteral("tcpDiag"), tcpDiag);
    }
#endif

    {
        QJsonObject processNet;
        const bool supported = backend.capabilities().perProcessNet;
        processNet.insert(QStringLiteral("available"), supported);
        if (supported) {
#if defined(Q_OS_LINUX)
            const QHash<unsigned long long, int> inodePid =
                processNetInodePid(backend, pids);
            QHash<unsigned long long, ProcessNetPrior> prior;
            const QList<linuxproc::TcpDiagInfo> baseline = backend.queryTcpDiag();
            processNetSocketRates(baseline, 250, prior);
            QThread::msleep(250);
            const QList<linuxproc::TcpDiagInfo> current = backend.queryTcpDiag();
            const QHash<unsigned long long, ProcessNetRate> socketRates =
                processNetSocketRates(current, 250, prior);
            int tracked = 0;
            int unattributed = 0;
            for (const linuxproc::TcpDiagInfo &info : current) {
                if (info.inode == 0) {
                    continue;
                }
                if (inodePid.contains(info.inode)) {
                    ++tracked;
                } else {
                    ++unattributed;
                }
            }
            processNet.insert(QStringLiteral("tracked"), tracked);
            processNet.insert(QStringLiteral("unattributed"), unattributed);
            processNet.insert(QStringLiteral("socketsWithRate"),
                              socketRates.size());
            QList<ProcessNetTalker> talkers = processNetTopTalkers(
                processNetPidRates(socketRates, inodePid), 5);
            QJsonArray rows;
            for (ProcessNetTalker &talker : talkers) {
                QJsonObject row;
                row.insert(QStringLiteral("pid"), talker.pid);
                row.insert(QStringLiteral("name"), backend.comm(talker.pid));
                row.insert(QStringLiteral("sockets"), talker.sockets);
                row.insert(QStringLiteral("rxBps"), talker.rxPerSec);
                row.insert(QStringLiteral("txBps"), talker.txPerSec);
                rows.append(row);
            }
            processNet.insert(QStringLiteral("talkers"), rows);
#endif
        }
        root.insert(QStringLiteral("processNet"), processNet);
    }

    {
        QJsonObject processDisk;
        const bool supported = backend.capabilities().perProcessDisk;
        processDisk.insert(QStringLiteral("available"), supported);
        processDisk.insert(QStringLiteral("scan"),
                           backend.capabilities().perProcessDiskScan);
        if (supported) {
            QHash<int, ProcessDiskCounters> counterSet;
            const QList<int> diskPids = backend.allPids();
            int readable = 0;
            for (int pid : diskPids) {
                long long readBytes = -1;
                long long writeBytes = -1;
                if (backend.processDiskCounters(pid, readBytes, writeBytes)) {
                    counterSet.insert(
                        pid, ProcessDiskCounters{readBytes, writeBytes});
                    ++readable;
                }
            }
            processDisk.insert(QStringLiteral("readable"), readable);
            processDisk.insert(QStringLiteral("pids"), diskPids.size());
            QHash<int, ProcessDiskCounters> prior;
            processDiskRates(counterSet, 250, prior);
            QThread::msleep(250);
            QHash<int, ProcessDiskCounters> second;
            for (int pid : diskPids) {
                long long readBytes = -1;
                long long writeBytes = -1;
                if (backend.processDiskCounters(pid, readBytes, writeBytes)) {
                    second.insert(pid, ProcessDiskCounters{readBytes, writeBytes});
                }
            }
            const QHash<int, ProcessDiskRate> rates =
                processDiskRates(second, 250, prior);
            QList<ProcessDiskConsumer> top =
                processDiskTopConsumers(rates, {}, 5);
            QJsonArray rows;
            for (ProcessDiskConsumer &consumer : top) {
                QJsonObject row;
                row.insert(QStringLiteral("pid"), consumer.pid);
                QString name = backend.comm(consumer.pid);
                if (name.isEmpty()) {
                    name = QStringLiteral("pid %1").arg(consumer.pid);
                }
                row.insert(QStringLiteral("name"), name);
                row.insert(QStringLiteral("readBps"), consumer.readPerSec);
                row.insert(QStringLiteral("writeBps"), consumer.writePerSec);
                rows.append(row);
            }
            processDisk.insert(QStringLiteral("top"), rows);
            processDisk.insert(QStringLiteral("readBps"),
                               processDiskTotal(rates, true));
            processDisk.insert(QStringLiteral("writeBps"),
                               processDiskTotal(rates, false));
        }
        root.insert(QStringLiteral("processDisk"), processDisk);
    }

    {
        QJsonObject gpu;
        const PlatformCapabilities caps = backend.capabilities();
        gpu.insert(QStringLiteral("capable"), caps.gpu);
        gpu.insert(QStringLiteral("clients"), caps.gpuClients);
        gpu.insert(QStringLiteral("processData"), caps.gpuProcessData);
        const GpuReading reading = backend.gpuInfo();
        gpu.insert(QStringLiteral("ok"), reading.ok);
        if (!reading.note.isEmpty()) {
            gpu.insert(QStringLiteral("note"), reading.note);
        }
        QJsonArray devices;
        for (const GpuDevice &device : reading.devices) {
            QJsonObject row;
            row.insert(QStringLiteral("index"), device.index);
            row.insert(QStringLiteral("vendor"), device.vendor);
            row.insert(QStringLiteral("name"), device.name);
            row.insert(QStringLiteral("driver"), device.driver);
            row.insert(QStringLiteral("pciId"), device.pciId);
            if (device.utilPercent >= 0.0) {
                row.insert(QStringLiteral("utilPercent"), device.utilPercent);
            }
            row.insert(QStringLiteral("utilFromClients"),
                       device.utilFromClients);
            row.insert(QStringLiteral("memUsed"), double(device.memUsed));
            row.insert(QStringLiteral("memTotal"), double(device.memTotal));
            if (device.tempC >= 0.0) {
                row.insert(QStringLiteral("tempC"), device.tempC);
            }
            if (device.powerW >= 0.0) {
                row.insert(QStringLiteral("powerW"), device.powerW);
            }
            if (device.clockMhz > 0) {
                row.insert(QStringLiteral("clockMhz"), device.clockMhz);
            }
            devices.append(row);
        }
        gpu.insert(QStringLiteral("devices"), devices);

        QJsonArray processes;
        for (const GpuProcess &process : reading.processes) {
            QJsonObject row;
            row.insert(QStringLiteral("pid"), process.pid);
            row.insert(QStringLiteral("name"), process.name);
            if (process.busyPercent >= 0.0) {
                row.insert(QStringLiteral("busyPercent"), process.busyPercent);
            }
            row.insert(QStringLiteral("memBytes"), double(process.memBytes));
            if (!process.engines.isEmpty()) {
                row.insert(QStringLiteral("engines"), process.engines);
            }
            processes.append(row);
        }
        if (caps.gpuClients) {
            const QList<int> pids = backend.allPids();
            QList<GpuEngineSample> clients;
            backend.gpuClients(pids, clients);
            gpu.insert(QStringLiteral("clientsFound"), clients.size());
            QThread::msleep(250);
            QList<GpuEngineSample> second;
            backend.gpuClients(pids, second);
            QHash<quint64, GpuClientPrior> prior;
            gpuClientBusy(clients, 250, prior);
            const QHash<quint64, GpuClientBusy> busy =
                gpuClientBusy(second, 250, prior);
            const QList<GpuProcess> merged = gpuMergeClients(second, busy, 5);
            for (const GpuProcess &process : merged) {
                QJsonObject row;
                row.insert(QStringLiteral("pid"), process.pid);
                row.insert(QStringLiteral("name"), process.name);
                if (process.busyPercent >= 0.0) {
                    row.insert(QStringLiteral("busyPercent"),
                               process.busyPercent);
                }
                row.insert(QStringLiteral("memBytes"),
                           double(process.memBytes));
                processes.append(row);
            }
        }
        gpu.insert(QStringLiteral("processes"), processes);
        root.insert(QStringLiteral("gpu"), gpu);
    }

    {
        QJsonObject sensors;
        const PlatformCapabilities caps = backend.capabilities();
        sensors.insert(QStringLiteral("thermalCapable"), caps.sensors);
        sensors.insert(QStringLiteral("batteryCapable"), caps.battery);
        const SensorsSnapshot reading = backend.sensors();
        sensors.insert(QStringLiteral("available"), reading.available);
        if (!reading.note.isEmpty()) {
            sensors.insert(QStringLiteral("note"), reading.note);
        }
        QJsonArray chips;
        int readings = 0;
        for (const SensorChip &chip : reading.chips) {
            QJsonObject row;
            row.insert(QStringLiteral("name"), chip.name);
            row.insert(QStringLiteral("readings"), chip.readings.size());
            chips.append(row);
            readings += chip.readings.size();
        }
        sensors.insert(QStringLiteral("chips"), chips);
        sensors.insert(QStringLiteral("readings"), readings);
        if (reading.hottestCelsius >= 0.0) {
            sensors.insert(QStringLiteral("hottestCelsius"),
                           reading.hottestCelsius);
            sensors.insert(QStringLiteral("hottestLabel"), reading.hottestLabel);
            sensors.insert(QStringLiteral("hottestSeverity"),
                           reading.hottestSeverity);
        }
        if (reading.maxFanRpm >= 0.0) {
            sensors.insert(QStringLiteral("maxFanRpm"), reading.maxFanRpm);
        }
        sensors.insert(QStringLiteral("fans"), reading.fanCount);
        if (reading.totalPowerW >= 0.0) {
            sensors.insert(QStringLiteral("totalPowerW"), reading.totalPowerW);
        }
        QJsonArray batteries;
        for (const BatteryReading &battery : reading.batteries) {
            QJsonObject row;
            row.insert(QStringLiteral("name"), battery.name);
            if (!battery.model.isEmpty()) {
                row.insert(QStringLiteral("model"), battery.model);
            }
            if (!battery.manufacturer.isEmpty()) {
                row.insert(QStringLiteral("manufacturer"),
                           battery.manufacturer);
            }
            if (!battery.technology.isEmpty()) {
                row.insert(QStringLiteral("technology"), battery.technology);
            }
            row.insert(QStringLiteral("state"), batteryStateText(battery.state));
            if (battery.capacityPercent >= 0) {
                row.insert(QStringLiteral("chargePercent"),
                           battery.capacityPercent);
            }
            if (battery.energyNowWh >= 0.0) {
                row.insert(QStringLiteral("energyNowWh"), battery.energyNowWh);
            }
            if (battery.energyFullWh >= 0.0) {
                row.insert(QStringLiteral("energyFullWh"), battery.energyFullWh);
            }
            if (battery.energyDesignWh > 0.0) {
                row.insert(QStringLiteral("energyDesignWh"),
                           battery.energyDesignWh);
            }
            const int health = batteryHealthPercent(battery);
            if (health >= 0) {
                row.insert(QStringLiteral("healthPercent"), health);
            }
            if (battery.powerW >= 0.0) {
                row.insert(QStringLiteral("powerW"), battery.powerW);
            }
            if (battery.voltageV > 0.0) {
                row.insert(QStringLiteral("voltageV"), battery.voltageV);
            }
            if (battery.cycleCount >= 0) {
                row.insert(QStringLiteral("cycleCount"), battery.cycleCount);
            }
            if (battery.timeToEmptyMin >= 0) {
                row.insert(QStringLiteral("timeToEmptyMin"),
                           battery.timeToEmptyMin);
            }
            if (battery.temperatureC >= 0.0) {
                row.insert(QStringLiteral("temperatureC"), battery.temperatureC);
            }
            batteries.append(row);
        }
        sensors.insert(QStringLiteral("batteries"), batteries);
        QJsonArray adapters;
        for (const AdapterReading &adapter : reading.adapters) {
            QJsonObject row;
            row.insert(QStringLiteral("name"), adapter.name);
            row.insert(QStringLiteral("online"), adapter.online);
            if (adapter.wattsW >= 0.0) {
                row.insert(QStringLiteral("watts"), adapter.wattsW);
            }
            adapters.append(row);
        }
        sensors.insert(QStringLiteral("adapters"), adapters);
        root.insert(QStringLiteral("sensors"), sensors);
    }

    {
        // M82: instance identity + accrued cost. CI runners are not cloud
        // instances, so the block carries deterministic self-tests beside the
        // probe result and both are asserted (a gated run must still prove the
        // math, not just fail to find a metadata service).
        QJsonObject cloud;
        CloudProbeOptions options;
        options.timeoutMs = 600;
        const CloudProbeResult probe = probeCloudIdentity(options);
        // [cloud] identity= is honoured here too, through the same resolver the
        // sampler uses: --dump must describe the identity the running app
        // would use, or a host without IMDS could never prove the fallback.
        QString configError;
        const Config dumpConfig = loadConfig(defaultConfigPath(), &configError);
        QString fallbackNote;
        QString identityError;
        const CloudIdentity identity = cloudIdentityResolved(
            probe.identity, dumpConfig.cloud.identityOverride, &fallbackNote,
            &identityError);
        const bool available = identity.provider != CloudProvider::None;
        cloud.insert(QStringLiteral("available"), available);
        cloud.insert(QStringLiteral("attempted"), probe.attempted);
        cloud.insert(QStringLiteral("provider"),
                     cloudProviderName(identity.provider));
        cloud.insert(QStringLiteral("identityOverride"),
                     dumpConfig.cloud.identityOverride);
        cloud.insert(QStringLiteral("identityFallback"),
                     !fallbackNote.isEmpty());
        // The precedence rule, asserted in CI: a metadata answer always beats
        // the config fallback, so a stale file cannot relabel a real instance.
        cloud.insert(QStringLiteral("identityPrecedenceOk"),
                     cloudIdentityResolved(
                         CloudIdentity{}, QString(), nullptr, nullptr)
                             .provider
                         == CloudProvider::None
                         && cloudIdentityResolved(
                                [] {
                                    CloudIdentity real;
                                    real.provider = CloudProvider::Gcp;
                                    real.region = QStringLiteral("us-central1");
                                    return real;
                                }(),
                                QStringLiteral("aws/eu-west-1/123456789012"),
                                nullptr, nullptr)
                                .provider
                                == CloudProvider::Gcp
                         && cloudIdentityResolved(
                                CloudIdentity{},
                                QStringLiteral("aws/eu-west-1/123456789012"),
                                nullptr, nullptr)
                                .provider
                                == CloudProvider::Aws
                         && cloudIdentityResolved(
                                CloudIdentity{}, QStringLiteral("bogus/x"),
                                nullptr, nullptr)
                                .provider
                                == CloudProvider::None);
        if (!available) {
            cloud.insert(QStringLiteral("note"),
                         probe.error.isEmpty() ? cloudUnreachableText()
                                               : probe.error);
        } else {
            const CloudIdentity &id = identity;
            cloud.insert(QStringLiteral("instanceId"), id.instanceId);
            cloud.insert(QStringLiteral("instanceType"), id.instanceType);
            cloud.insert(QStringLiteral("region"), id.region);
            cloud.insert(QStringLiteral("zone"), id.zone);
            cloud.insert(QStringLiteral("spot"), id.spot);
        }

        const int vcpus = QThread::idealThreadCount();
        const qint64 uptime = cloudHostUptimeSeconds();
        cloud.insert(QStringLiteral("vcpus"), vcpus);
        cloud.insert(QStringLiteral("uptimeSec"), double(uptime));

        const QString ratesPath = defaultCloudRatePath();
        CloudRateTable table;
        QFile ratesFile(ratesPath);
        if (ratesFile.open(QIODevice::ReadOnly)) {
            table = parseCloudRateFile(QString::fromUtf8(ratesFile.readAll()),
                                       ratesPath);
            ratesFile.close();
        }
        cloud.insert(QStringLiteral("ratePath"), ratesPath);
        cloud.insert(QStringLiteral("rates"), table.rates.size());
        cloud.insert(QStringLiteral("importDate"), table.importDate);
        cloud.insert(QStringLiteral("staleDays"),
                     cloudRateStaleDays(table.importDate,
                                        QDateTime::currentMSecsSinceEpoch()));

        if (available) {
            const double rate = cloudRateFor(table, identity.provider,
                                             identity.instanceType);
            cloud.insert(QStringLiteral("rateKey"),
                         cloudRateKey(identity.provider,
                                      identity.instanceType));
            cloud.insert(QStringLiteral("ratePerHour"), rate);
            const CloudCostMath math = computeCloudCost(rate, uptime);
            cloud.insert(QStringLiteral("accruedUsd"), math.accruedUsd);
            cloud.insert(QStringLiteral("projectedMonthlyUsd"),
                         math.projectedMonthlyUsd);
            cloud.insert(QStringLiteral("appCostPerHour"),
                         cloudProcessCostPerHour(rate, 25.0, vcpus));
        }

        const CloudRateTable fixture = parseCloudRateFile(
            QStringLiteral("# imported 2020-01-01\n"
                           "aws/c7g.2xlarge = 0.2920\n"
                           "azure/Standard_D2s_v3 = 0.0960\n"));
        cloud.insert(QStringLiteral("rateLookupOk"),
                     qAbs(cloudRateFor(fixture, CloudProvider::Aws,
                                       QStringLiteral("C7G.2XLarge"))
                          - 0.2920) < 1e-9
                         && qAbs(cloudRateFor(fixture,
                                              CloudProvider::Azure,
                                              QStringLiteral("nope"))
                                 + 1.0) < 1e-9);
        const CloudCostMath hourMath = computeCloudCost(1.0, 3600);
        cloud.insert(QStringLiteral("costMathOk"),
                     qAbs(hourMath.accruedUsd - 1.0) < 1e-9
                         && qAbs(hourMath.projectedMonthlyUsd - 730.0) < 1e-9);
        cloud.insert(QStringLiteral("shareOk"),
                     qAbs(cloudProcessCostPerHour(8.0, 800.0, 8) - 8.0) < 1e-9
                         && cloudProcessCostPerHour(8.0, 100.0, 0) < 0.0);
        const QList<CloudProcessSample> rows = {
            {QStringLiteral("small"), 11, 10.0},
            {QStringLiteral("big"), 22, 300.0},
            {QStringLiteral("idle"), 33, 0.0},
        };
        const QList<CloudConsumer> top =
            cloudTopConsumers(rows, 1.0, 4, 2);
        cloud.insert(QStringLiteral("topOk"),
                     top.size() == 2
                         && top.constFirst().pid == 22
                         && qAbs(cloudConsumerTotal(top)
                                 - (0.75 + 0.025)) < 1e-9);
        cloud.insert(QStringLiteral("staleOk"),
                     cloudRateStaleDays(QStringLiteral("2020-01-01"),
                                        QDateTime::currentMSecsSinceEpoch())
                         > 100);

        // M83: the account listing runs only when an instance was identified
        // (with no metadata service there is no provider to ask), so the
        // parsers, the age maths and the self-match carry deterministic
        // self-tests that every runner can assert.
        CloudFleetOptions fleetOptions;
        fleetOptions.timeoutMs = 8000;
        const CloudFleetStatus listing =
            probeCloudFleet(identity, fleetOptions);
        QJsonObject fleet;
        fleet.insert(QStringLiteral("attempted"), listing.attempted);
        fleet.insert(QStringLiteral("available"), listing.available);
        fleet.insert(QStringLiteral("cli"), listing.cliName);
        fleet.insert(QStringLiteral("scope"), listing.scope);
        fleet.insert(QStringLiteral("note"), fleetSummaryText(listing));
        fleet.insert(QStringLiteral("rows"), listing.rows.size());
        fleet.insert(QStringLiteral("running"), listing.running);
        fleet.insert(QStringLiteral("selfIndex"), listing.selfIndex);

        const qint64 fleetNow = 1'700'000'000'000LL;
        const CloudFleetStatus awsFixture = parseAwsDescribeInstances(
            QByteArrayLiteral(
                "[{\"InstanceId\":\"i-0aaa\",\"InstanceType\":\"c7g.2xlarge\","
                "\"State\":{\"Name\":\"running\"},"
                "\"Placement\":{\"AvailabilityZone\":\"eu-west-1a\"},"
                "\"LaunchTime\":\"2023-06-01T10:00:00Z\","
                "\"Tags\":[{\"Key\":\"Name\",\"Value\":\"web-1\"}]},"
                "{\"InstanceId\":\"i-0bbb\",\"InstanceType\":\"t4g.small\","
                "\"State\":{\"Name\":\"stopped\"},"
                "\"Placement\":{\"AvailabilityZone\":\"eu-west-1b\"}}]"));
        const CloudFleetStatus azureFixture = parseAzureVmList(
            QByteArrayLiteral(
                "[{\"name\":\"vm-a\",\"location\":\"westus\","
                "\"powerState\":\"VM deallocated\","
                "\"hardwareProfile\":{\"vmSize\":\"Standard_D2s_v3\"}}]"));
        fleet.insert(QStringLiteral("fleetParseOk"),
                     awsFixture.rows.size() == 2
                         && awsFixture.rows.at(0).name == QStringLiteral("web-1")
                         && awsFixture.rows.at(0).state == QStringLiteral("running")
                         && awsFixture.rows.at(1).state == QStringLiteral("stopped")
                         && awsFixture.rows.at(0).zone == QStringLiteral("eu-west-1a")
                         && awsFixture.rows.at(0).region == QStringLiteral("eu-west-1"));
        fleet.insert(QStringLiteral("fleetAzureOk"),
                     azureFixture.rows.size() == 1
                         && azureFixture.rows.at(0).state == QStringLiteral("stopped")
                         && azureFixture.rows.at(0).instanceType
                                == QStringLiteral("Standard_D2s_v3")
                         && azureFixture.rows.at(0).launchedAtMs < 0);
        fleet.insert(QStringLiteral("fleetAgeOk"),
                     fleetAgeSeconds(fleetNow - 3 * 86400LL * 1000LL,
                                     fleetNow) == 3 * 86400LL
                         && fleetAgeSeconds(fleetNow, fleetNow) == 0
                         && fleetAgeSeconds(fleetNow + 1000, fleetNow) < 0);
        CloudFleetStatus own = awsFixture;
        CloudIdentity ownIdentity;
        ownIdentity.provider = CloudProvider::Aws;
        ownIdentity.instanceId = QStringLiteral("i-0bbb");
        const int selfAfter = fleetMarkSelf(own, ownIdentity);
        fleet.insert(QStringLiteral("fleetSelfOk"),
                     selfAfter == 0 && own.rows.at(0).isSelf
                         && own.rows.at(0).instanceId == QStringLiteral("i-0bbb")
                         && !own.rows.at(1).isSelf);
        CloudFleetStatus pricedFleet = awsFixture;
        fleetApplyRates(pricedFleet, fixture, 730.0);
        fleet.insert(QStringLiteral("fleetCostOk"),
                     qAbs(pricedFleet.rows.at(0).monthlyUsd
                          - 0.2920 * 730.0) < 1e-9
                         && pricedFleet.rows.at(1).monthlyUsd < 0.0
                         && pricedFleet.running == 1
                         && pricedFleet.unpriced == 0
                         && qAbs(pricedFleet.monthlyUsd - 0.2920 * 730.0) < 1e-9);
        cloud.insert(QStringLiteral("fleet"), fleet);

        // M85: the billed-spend query. Same shape as the inventory: the real
        // call only happens where a provider was identified, and the parsers
        // plus the arithmetic carry self-tests every runner can assert.
        CloudSpendOptions spendOptions;
        spendOptions.timeoutMs = 12000;
        const CloudSpendStatus spend =
            probeCloudSpend(identity, spendOptions);
        QJsonObject spendJson;
        spendJson.insert(QStringLiteral("attempted"), spend.attempted);
        spendJson.insert(QStringLiteral("available"), spend.available);
        spendJson.insert(QStringLiteral("provider"),
                         cloudProviderName(spend.provider));
        spendJson.insert(QStringLiteral("cli"), spend.cliName);
        spendJson.insert(QStringLiteral("scope"), spend.scope);
        spendJson.insert(QStringLiteral("note"),
                         cloudSpendSummaryText(spend));
        spendJson.insert(QStringLiteral("periodStart"), spend.periodStart);
        spendJson.insert(QStringLiteral("monthToDateUsd"), spend.monthToDateUsd);
        spendJson.insert(QStringLiteral("forecastUsd"), spend.forecastUsd);
        spendJson.insert(QStringLiteral("services"), spend.services.size());
        spendJson.insert(QStringLiteral("days"), spend.days.size());

        const QByteArray spendFixture = QByteArrayLiteral(
            "{\"ResultsByTime\":["
            "{\"TimePeriod\":{\"Start\":\"2026-10-01\",\"End\":\"2026-10-02\"},"
            "\"Total\":{},\"Groups\":["
            "{\"Keys\":[\"Amazon Elastic Compute Cloud - Compute\"],"
            "\"Metrics\":{\"UnblendedCost\":{\"Amount\":\"12.5\",\"Unit\":\"USD\"}}},"
            "{\"Keys\":[\"Amazon Simple Storage Service\"],"
            "\"Metrics\":{\"UnblendedCost\":{\"Amount\":\"1.5\",\"Unit\":\"USD\"}}}],"
            "\"Estimated\":false},"
            "{\"TimePeriod\":{\"Start\":\"2026-10-02\",\"End\":\"2026-10-03\"},"
            "\"Total\":{},\"Groups\":["
            "{\"Keys\":[\"Amazon Elastic Compute Cloud - Compute\"],"
            "\"Metrics\":{\"UnblendedCost\":{\"Amount\":\"10.0\",\"Unit\":\"USD\"}}}],"
            "\"Estimated\":true}]}");
        QString spendError;
        CloudSpendStatus awsSpend =
            parseAwsCostAndUsage(spendFixture, &spendError);
        spendApplyForecast(&awsSpend, 50.0);
        spendJson.insert(QStringLiteral("spendAwsOk"),
                         awsSpend.available
                             && qAbs(awsSpend.monthToDateUsd - 24.0) < 1e-9
                             && qAbs(awsSpend.forecastUsd - 74.0) < 1e-9
                             && awsSpend.days.size() == 2
                             && awsSpend.services.size() == 2
                             && awsSpend.services.at(0).name
                                    == QStringLiteral("Amazon Elastic Compute "
                                                      "Cloud - Compute")
                             && qAbs(awsSpend.services.at(0).usd - 22.5) < 1e-9
                             && awsSpend.estimated);
        const CloudSpendStatus azureSpend = parseAzureUsageList(
            QByteArrayLiteral(
                "[{\"properties\":{\"pretaxCost\":3.25,"
                "\"product\":\"Virtual Machines\","
                "\"usageStart\":\"2026-10-01T00:00:00Z\"}},"
                "{\"properties\":{\"pretaxCost\":1.75,"
                "\"product\":\"Storage\","
                "\"usageStart\":\"2026-10-02T00:00:00Z\"}}]"),
            &spendError);
        spendJson.insert(QStringLiteral("spendAzureOk"),
                         azureSpend.available
                             && qAbs(azureSpend.monthToDateUsd - 5.0) < 1e-9
                             && azureSpend.days.size() == 2
                             && azureSpend.services.size() == 2);
        QString forecastError;
        const CloudSpendStatus forecast = parseAwsCostForecast(
            QByteArrayLiteral(
                "{\"Total\":{\"Amount\":\"118.33\",\"Unit\":\"USD\"},"
                "\"ForecastResultsByTime\":[]}"),
            &forecastError);
        spendJson.insert(QStringLiteral("spendForecastOk"),
                         forecast.available
                             && qAbs(forecast.forecastRemainingUsd - 118.33)
                                    < 1e-9);
        spendJson.insert(
            QStringLiteral("spendPeriodOk"),
            cloudSpendPeriodStart(1'700'000'000'000LL)
                    == QStringLiteral("2023-11-01")
                && cloudSpendPeriodEnd(1'700'000'000'000LL)
                       == QStringLiteral("2023-11-15")
                && cloudSpendForecastEnd(1'700'000'000'000LL)
                       == QStringLiteral("2023-12-01"));
        CloudIdentity gcpOnly;
        gcpOnly.provider = CloudProvider::Gcp;
        spendJson.insert(
            QStringLiteral("spendGateOk"),
            cloudSpendGateText(CloudProvider::Gcp).contains(
                QStringLiteral("BigQuery"))
                && probeCloudSpend(gcpOnly, spendOptions)
                       .note.contains(QStringLiteral("BigQuery")));
        cloud.insert(QStringLiteral("spend"), spendJson);
        root.insert(QStringLiteral("cloud"), cloud);
    }

    {
        // M84: the sampler's self-cost model. No GUI is needed to prove it,
        // and the same numbers drive the Settings rows, so a runner asserts
        // the maths the page shows.
        QList<TickSection> sections;
        sections.append(TickSection{QStringLiteral("focus"), 3.5});
        sections.append(TickSection{QStringLiteral("processes"), 41.2});
        sections.append(TickSection{QStringLiteral("gpu"), 0.01});
        const TickCost tickCost =
            tickbudget::summarize(sections, 62.0,
                                  QList<double>{40.0, 62.0, 84.0});
        QJsonObject budget;
        budget.insert(QStringLiteral("sections"), tickCost.sections.size());
        budget.insert(QStringLiteral("slowest"),
                      tickCost.sections.isEmpty()
                          ? QString()
                          : tickCost.sections.constFirst().name);
        budget.insert(QStringLiteral("avgMs"), tickCost.avgMs);
        budget.insert(QStringLiteral("worstMs"), tickCost.worstMs);
        budget.insert(QStringLiteral("sortedOk"),
                      tickCost.sections.size() == 3
                          && tickCost.sections.constFirst().name
                                 == QStringLiteral("processes")
                          && tickCost.sections.constLast().name
                                 == QStringLiteral("gpu"));
        budget.insert(QStringLiteral("windowOk"),
                      qAbs(tickCost.avgMs - 62.0) < 1e-9
                          && qAbs(tickCost.worstMs - 84.0) < 1e-9);
        budget.insert(QStringLiteral("unmeasuredOk"),
                      tickbudget::tickCostText(TickCost{})
                          == QStringLiteral("measuring"));
        budget.insert(
            QStringLiteral("selfTextOk"),
            tickbudget::selfCostText(SelfCost{})
                    == QStringLiteral("not measurable on this platform")
                && !tickbudget::tickSectionText(tickCost).isEmpty());
        root.insert(QStringLiteral("budget"), budget);
    }

    {
        QJsonObject history;
        const QString probePath =
            QDir::tempPath()
            + QStringLiteral("/crowsnest-history-probe-%1.ring").arg(dumpPid());
        HistoryLog log;
        constexpr int kCap = 8;
        const qint64 base = QDateTime::currentMSecsSinceEpoch();
        bool appended = log.open(probePath, kCap);
        history.insert(QStringLiteral("capacity"), kCap);
        if (appended) {
            for (int i = 0; i < 11; ++i) {
                HistoryRecord record;
                record.atMs = base + i * 1000;
                record.appName = QStringLiteral("probe");
                record.pid = 4242;
                record.hostCpuPercent = 10.0 + i;
                record.hostMemAvailableKb = 100000 + i;
                record.load1 = 1.25;
                if (!log.append(record)) {
                    appended = false;
                    break;
                }
            }
        }
        history.insert(QStringLiteral("ok"), appended && log.isOpen());
        history.insert(QStringLiteral("count"), log.count());
        history.insert(QStringLiteral("total"), double(log.totalWritten()));
        history.insert(QStringLiteral("bytes"), double(log.fileBytes()));
        history.insert(QStringLiteral("wrapped"), log.totalWritten() > kCap);
        HistoryRecord oldest;
        HistoryRecord newest;
        if (log.oldest(oldest) && log.newest(newest)) {
            history.insert(QStringLiteral("oldestAt"), double(oldest.atMs));
            history.insert(QStringLiteral("newestAt"), double(newest.atMs));
            history.insert(QStringLiteral("spanMs"), double(log.spanMs()));
            history.insert(QStringLiteral("oldestCpu"), oldest.hostCpuPercent);
            history.insert(QStringLiteral("newestCpu"), newest.hostCpuPercent);
            history.insert(QStringLiteral("ordered"),
                           oldest.atMs < newest.atMs);
            history.insert(QStringLiteral("app"), oldest.appName);
            history.insert(QStringLiteral("pid"), oldest.pid);
        }
        log.close();

        HistoryLog reopened;
        const bool resumed = reopened.open(probePath, kCap);
        history.insert(QStringLiteral("reopened"), resumed);
        history.insert(QStringLiteral("reopenedCount"), reopened.count());
        HistoryRecord resumedOldest;
        history.insert(QStringLiteral("resumedOrdered"),
                       resumed && reopened.oldest(resumedOldest)
                           && resumedOldest.atMs == oldest.atMs);
        const bool differentCapacity = reopened.open(probePath, kCap + 4);
        history.insert(QStringLiteral("capacityChangeResets"),
                       differentCapacity && reopened.count() == 0);
        reopened.close();
        QFile::remove(probePath);
        root.insert(QStringLiteral("history"), history);
    }

    {
        // M78: exercise every verb against a child this process owns, so the
        // dump proves the seam end to end (capability, syscall, refusal).
        QJsonObject actions;
        const PlatformCapabilities caps = systemBackend().capabilities();
        actions.insert(QStringLiteral("signals"), caps.processSignals);
        actions.insert(QStringLiteral("renice"), caps.processRenice);
        actions.insert(QStringLiteral("ionice"), caps.processIonice);
        actions.insert(QStringLiteral("oomScore"), caps.processOomScore);
        actions.insert(QStringLiteral("pause"), caps.processPause);

        QProcess child;
#if defined(Q_OS_WIN)
        child.start(QStringLiteral("ping"),
                    {QStringLiteral("-n"), QStringLiteral("60"),
                     QStringLiteral("127.0.0.1")});
#else
        child.start(QStringLiteral("sleep"), {QStringLiteral("60")});
#endif
        bool spawned = child.waitForStarted(3000);
        const int childPid = spawned ? int(child.processId()) : 0;
        actions.insert(QStringLiteral("childPid"), childPid);

        bool reniceOk = false;
        bool ioniceOk = false;
        bool oomOk = false;
        bool signalOk = false;
        QString error;

        ProcessAction action;
        action.pid = childPid;
        action.name = QStringLiteral("dump-probe");

        if (childPid > 0 && caps.processRenice) {
            action.kind = ProcessActionKind::Renice;
            action.niceValue = 0;
            action.relative = true;
            reniceOk = systemBackend().applyProcessAction(action, &error);
        }
        if (childPid > 0 && caps.processIonice) {
            action.kind = ProcessActionKind::Ionice;
            action.ioClass = IoPriorityClass::Idle;
            action.ioLevel = 0;
            ioniceOk = systemBackend().applyProcessAction(action, &error);
        }
        if (childPid > 0 && caps.processOomScore) {
            action.kind = ProcessActionKind::OomScore;
            action.oomScoreAdj = 0;
            action.relative = false;
            oomOk = systemBackend().applyProcessAction(action, &error);
        }
        if (childPid > 0 && caps.processSignals) {
            action.kind = ProcessActionKind::Signal;
            action.signal = ProcessSignal::Term;
            signalOk = systemBackend().applyProcessAction(action, &error);
            if (signalOk) {
                child.waitForFinished(3000);
            }
        }
        if (child.state() != QProcess::NotRunning) {
            child.kill();
            child.waitForFinished(2000);
        }
        actions.insert(QStringLiteral("spawned"), spawned);
        actions.insert(QStringLiteral("reniceOk"), reniceOk);
        actions.insert(QStringLiteral("ioniceOk"), ioniceOk);
        actions.insert(QStringLiteral("oomOk"), oomOk);
        actions.insert(QStringLiteral("signalOk"), signalOk);
        actions.insert(QStringLiteral("childExited"),
                       spawned && child.state() == QProcess::NotRunning);
        // A refused verb must state why, not fail silently.
        ProcessAction bad;
        bad.pid = -1;
        bad.kind = ProcessActionKind::Signal;
        bad.signal = ProcessSignal::Term;
        QString refusedError;
        const bool badOk = systemBackend().applyProcessAction(bad, &refusedError);
        actions.insert(QStringLiteral("invalidPidRefused"), !badOk);
        actions.insert(QStringLiteral("invalidPidError"), refusedError);
        root.insert(QStringLiteral("actions"), actions);
    }

    {
        // M80: the overlay's window-server contract. Nothing here needs a
        // QApplication: the session comes from the environment and the X11
        // facts from libX11 directly, so `--dump` stays headless.
        QJsonObject overlay;
        OverlaySession session = OverlaySession::Unknown;
        QString wmName;
        QRect workArea;
        bool shapeAvailable = false;
        bool stickySupported = false;
        const QString platform = backend.platformName();
        if (platform == QStringLiteral("linux")) {
            const QString wayland =
                QString::fromLocal8Bit(qgetenv("WAYLAND_DISPLAY"));
            const QString x11 = QString::fromLocal8Bit(qgetenv("DISPLAY"));
            // What the GUI will actually use wins over what the session
            // claims: QT_QPA_PLATFORM=xcb on a Wayland desktop runs the X11
            // path, and the dump has to describe that, not the session.
            QString declared = QString::fromLocal8Bit(qgetenv("QT_QPA_PLATFORM"));
            if (declared.isEmpty()) {
                declared = QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE"));
            }
            session = detectOverlaySession(
                declared.isEmpty() ? (wayland.isEmpty() ? QString() : wayland)
                                   : declared,
                wayland, x11);
            if (session == OverlaySession::X11) {
                const linuxoverlay::X11Probe probe = linuxoverlay::probe();
                wmName = probe.wmName;
                workArea = probe.workArea;
                shapeAvailable = probe.shapeAvailable;
                stickySupported = probe.stickySupported;
            }
        } else {
            session = detectOverlaySession(platform, QString(), QString());
        }

        overlay.insert(QStringLiteral("session"),
                       overlaySessionName(session));
        if (!wmName.isEmpty()) {
            overlay.insert(QStringLiteral("wm"), wmName);
        }
        if (workArea.isValid()) {
            overlay.insert(QStringLiteral("workArea"), overlayGeometryText(workArea));
        }
        overlay.insert(QStringLiteral("shapeAvailable"), shapeAvailable);
        overlay.insert(QStringLiteral("stickySupported"), stickySupported);
        overlay.insert(QStringLiteral("clickThroughCapable"),
                       session == OverlaySession::X11 && shapeAvailable);
        overlay.insert(QStringLiteral("note"),
                       overlayClickThroughNote(
                           session,
                           session == OverlaySession::X11 && shapeAvailable));
        root.insert(QStringLiteral("overlay"), overlay);
    }

    {
        QJsonObject metrics;
        const MetricsEndpoint endpoint =
            resolveMetricsEndpoint(MetricsConfig{});
        metrics.insert(QStringLiteral("bind"), endpoint.bindAddress);
        metrics.insert(QStringLiteral("port"), endpoint.port);
        metrics.insert(QStringLiteral("authRequired"), endpoint.authRequired);
        metrics.insert(QStringLiteral("downgraded"), endpoint.bindDowngraded);

        MetricsConfig exposed;
        exposed.bindAddress = QStringLiteral("0.0.0.0");
        const MetricsEndpoint refused = resolveMetricsEndpoint(exposed);
        metrics.insert(QStringLiteral("refusedNonLoopback"),
                       refused.bindDowngraded && !refused.authRequired
                           && refused.bindAddress
                                  == QStringLiteral("127.0.0.1"));

        exposed.token = QStringLiteral("probe-token-0123456789");
        const MetricsEndpoint guarded = resolveMetricsEndpoint(exposed);
        metrics.insert(QStringLiteral("tokenGuarded"),
                       guarded.authRequired && !guarded.bindDowngraded
                           && guarded.bindAddress
                                  == QStringLiteral("0.0.0.0"));

        const QByteArray token = exposed.token.toUtf8();
        metrics.insert(
            QStringLiteral("headerAccepted"),
            metricsRequestAuthorized(
                QByteArrayLiteral("GET /metrics HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Authorization: Bearer probe-token-0123456789"
                                  "\r\n\r\n"),
                token));
        metrics.insert(
            QStringLiteral("headerRejected"),
            !metricsRequestAuthorized(
                QByteArrayLiteral("GET /metrics HTTP/1.1\r\n"
                                  "Authorization: Bearer probe-token-0\r\n"
                                  "\r\n"),
                token)
                && !metricsRequestAuthorized(
                    QByteArrayLiteral("GET /metrics HTTP/1.1\r\n\r\n"),
                    token));
        metrics.insert(QStringLiteral("loopbackOnlyText"),
                       metricsEndpointText(endpoint).contains(
                           QStringLiteral("loopback only")));
        root.insert(QStringLiteral("metrics"), metrics);
    }

    {
        // M94: the fleet contract. A runner with no peers still proves the
        // maths: emit a document, read it back, refuse a newer schema, and
        // fail a peer that is not answering - all deterministic, no network
        // beyond one refused connect to a closed loopback port.
        QJsonObject fleet;
        const Config dumpConfig = loadConfig(defaultConfigPath(), nullptr);
        fleet.insert(QStringLiteral("enabled"), dumpConfig.fleet.enabled);
        fleet.insert(QStringLiteral("share"), dumpConfig.fleet.share);
        fleet.insert(QStringLiteral("schema"), kFleetSchemaVersion);

        DisplaySnapshot sample;
        sample.appName = QStringLiteral("probe-app");
        sample.pid = QStringLiteral("4242");
        sample.cpuValid = true;
        sample.cpuPercent = 42.5;
        sample.rssKb = 1024;
        sample.hostAvailable = true;
        sample.hostCpuPercent = 17.25;
        sample.hostMemTotalKb = 8192000;
        sample.hostMemAvailableKb = 4096000;
        sample.hostLoad1 = 1.5;
        sample.sequence = 7;
        const QByteArray document =
            fleetSnapshotJson(sample, QStringLiteral("probe-host"),
                              QStringLiteral("probe"), QStringLiteral("0.0.0"),
                              QStringLiteral("linux"));
        FleetPeer readBack;
        QString parseError;
        const bool parseOk = parseFleetSnapshotJson(document, &readBack,
                                                    &parseError)
            && readBack.schema == kFleetSchemaVersion
            && readBack.name == QStringLiteral("probe-host")
            && qAbs(readBack.cpuPercent - 17.25) < 0.001
            && readBack.memAvailableKb == 4096000
            && qAbs(readBack.load1 - 1.5) < 0.001;
        fleet.insert(QStringLiteral("parseOk"), parseOk);
        if (!parseOk) {
            fleet.insert(QStringLiteral("parseError"), parseError);
        }

        QJsonObject newer = QJsonDocument::fromJson(document).object();
        newer.insert(QStringLiteral("schema"), kFleetSchemaVersion + 1);
        FleetPeer refused;
        QString newerError;
        fleet.insert(
            QStringLiteral("refuseNewerOk"),
            !parseFleetSnapshotJson(QJsonDocument(newer).toJson(), &refused,
                                    &newerError)
                && newerError.contains(QStringLiteral("update Crowsnest")));

        const QString normalized = fleetNormalizePeerUrl(
            QStringLiteral("http://build-01:9107/api/v1/snapshot"));
        const QList<FleetPeer> namedSpec =
            parseFleetPeers({QStringLiteral("db=build-02")});
        fleet.insert(QStringLiteral("urlOk"),
                     normalized
                         == QStringLiteral(
                             "http://build-01:9107/api/v1/snapshot")
                         && namedSpec.size() == 1
                         && namedSpec.first().name == QStringLiteral("db")
                         && namedSpec.first().url
                                == QStringLiteral(
                                    "http://build-02:9107/api/v1/snapshot"));

        FleetPollOptions offlineOptions;
        offlineOptions.enabled = true;
        offlineOptions.token = QStringLiteral("probe-token");
        offlineOptions.peers = {QStringLiteral("closed=127.0.0.1:1")};
        offlineOptions.pollMs = kFleetDefaultPollMs;
        offlineOptions.timeoutMs = 400;
        const FleetStatus offline =
            probeFleet(offlineOptions, QDateTime::currentMSecsSinceEpoch());
        fleet.insert(QStringLiteral("offlineOk"),
                     offline.peers.size() == 1
                         && !offline.peers.first().online
                         && !offline.peers.first().error.isEmpty()
                         && fleetOnlineCount(offline) == 0
                         && fleetDownCount(offline) == 1);

        FleetStatus withSelf = offline;
        FleetPeer self = fleetSelfPeer(sample, QStringLiteral("this-box"),
                                       QDateTime::currentMSecsSinceEpoch());
        withSelf = fleetWithSelf(withSelf, self);
        fleet.insert(QStringLiteral("selfOk"),
                     withSelf.peers.size() == 2 && withSelf.peers.first().isSelf
                         && fleetOnlineCount(withSelf) == 0
                         && fleetDownCount(withSelf) == 1);
        fleet.insert(QStringLiteral("summaryOk"),
                     !fleetSummaryText(withSelf,
                                       QDateTime::currentMSecsSinceEpoch())
                          .isEmpty()
                         && !fleetNoteText(withSelf).isEmpty());
        fleet.insert(QStringLiteral("peersConfigured"),
                     parseFleetPeers(dumpConfig.fleet.peers).size());
        root.insert(QStringLiteral("fleet"), fleet);
    }

    {
        // M95: the reporter's config surface, with no transport. The URL rules
        // are asserted here so a runner with no relay address still proves
        // them: https for a bare host, the port kept, cleartext only when the
        // user asked for it, and a reason for every refusal.
        QJsonObject remote;
        const Config dumpConfig = loadConfig(defaultConfigPath(), nullptr);
        QString urlError;
        const QString normalized =
            remoteNormalizeUrl(dumpConfig.remote.url, &urlError);
        remote.insert(QStringLiteral("enabled"), dumpConfig.remote.enabled);
        remote.insert(QStringLiteral("url"), dumpConfig.remote.url);
        remote.insert(QStringLiteral("normalizedUrl"), normalized);
        remote.insert(QStringLiteral("urlError"), urlError);
        remote.insert(QStringLiteral("ingestUrl"),
                      remoteIngestUrl(dumpConfig.remote.url));
        remote.insert(QStringLiteral("intervalMs"), dumpConfig.remote.intervalMs);
        remote.insert(QStringLiteral("timeoutMs"), dumpConfig.remote.timeoutMs);
        remote.insert(QStringLiteral("spoolMb"), dumpConfig.remote.spoolMb);
        remote.insert(QStringLiteral("spoolPath"), remoteSpoolPath());
        remote.insert(QStringLiteral("hasToken"),
                      !dumpConfig.remote.token.isEmpty());
        remote.insert(QStringLiteral("valid"),
                      remoteConfigUsable(dumpConfig.remote));
        remote.insert(QStringLiteral("note"),
                      remoteConfigNote(dumpConfig.remote));

        remote.insert(QStringLiteral("urlOk"),
                      remoteNormalizeUrl(QStringLiteral("relay.example.test"))
                              == QStringLiteral("https://relay.example.test")
                          && remoteNormalizeUrl(
                                 QStringLiteral("http://127.0.0.1:8080/"))
                                 == QStringLiteral("http://127.0.0.1:8080")
                          && remoteIngestUrl(
                                 QStringLiteral("https://relay.example.test"))
                                 == QStringLiteral(
                                     "https://relay.example.test/api/v1/ingest")
                          && remoteIngestUrl(QStringLiteral("[::1]:8443"))
                                 == QStringLiteral(
                                     "https://[::1]:8443/api/v1/ingest"));

        QString reason;
        const bool refuseScheme =
            remoteNormalizeUrl(QStringLiteral("ftp://relay.example.test"),
                               &reason)
                .isEmpty()
            && !reason.isEmpty();
        reason.clear();
        const bool refuseUserinfo =
            remoteNormalizeUrl(
                QStringLiteral("https://user:pw@relay.example.test"), &reason)
                .isEmpty()
            && !reason.isEmpty();
        reason.clear();
        const bool refuseEmpty = remoteNormalizeUrl(QString(), &reason).isEmpty()
                                 && !reason.isEmpty();
        remote.insert(QStringLiteral("urlOk"),
                      remote["urlOk"].toBool() && refuseScheme
                          && refuseUserinfo && refuseEmpty);

        RemoteOptions probe;
        probe.intervalMs = 1;
        probe.timeoutMs = 1;
        probe.spoolMb = -1;
        remoteValidate(probe);
        const bool floorOk = probe.intervalMs == kRemoteIntervalMinMs
                             && probe.timeoutMs == kRemoteTimeoutMinMs
                             && probe.spoolMb == kRemoteSpoolMinMb;
        probe.intervalMs = 999999;
        probe.timeoutMs = 999999;
        probe.spoolMb = 999;
        remoteValidate(probe);
        const bool ceilingOk = probe.intervalMs == kRemoteIntervalMaxMs
                               && probe.timeoutMs == kRemoteTimeoutMaxMs
                               && probe.spoolMb == kRemoteSpoolMaxMb;
        remote.insert(QStringLiteral("clampOk"), floorOk && ceilingOk);

        // M97: the transport's answers, proven without a relay. `--dump` never
        // sends a report - a diagnostic must not become a report - so it states
        // that and proves the taxonomy the runner uses.
        remote.insert(QStringLiteral("attempted"), false);
        remote.insert(QStringLiteral("ok"), false);
        remote.insert(QStringLiteral("classification"),
                      remoteClassName(RemoteClass::None));
        remote.insert(QStringLiteral("latencyMs"), -1);
        remote.insert(QStringLiteral("transportNote"),
                      QStringLiteral("remote: --dump never sends a report - the "
                                     "console or `serve` does"));
        const bool classifyOk =
            remoteClassify(200) == RemoteClass::Ok
            && remoteClassify(204) == RemoteClass::Ok
            && remoteClassify(401) == RemoteClass::Auth
            && remoteClassify(403) == RemoteClass::Auth
            && remoteClassify(404) == RemoteClass::NotFound
            && remoteClassify(413) == RemoteClass::Rejected
            && remoteClassify(422) == RemoteClass::Rejected
            && remoteClassify(429) == RemoteClass::Throttled
            && remoteClassify(500) == RemoteClass::Server
            && remoteClassify(503) == RemoteClass::Server
            && remoteClassify(0) == RemoteClass::Transport
            && remoteClassIsTerminal(RemoteClass::Auth)
            && remoteClassIsTerminal(RemoteClass::NotFound)
            && remoteClassIsTerminal(RemoteClass::Rejected)
            && !remoteClassIsTerminal(RemoteClass::Server)
            && !remoteClassIsTerminal(RemoteClass::Throttled)
            && !remoteClassIsTerminal(RemoteClass::Transport)
            && !remoteClassIsTerminal(RemoteClass::Ok)
            && !remoteClassIsTerminal(RemoteClass::None);
        remote.insert(QStringLiteral("classifyOk"), classifyOk);
        const bool backoffOk = remoteBackoffMs(1, 15000) == 2000
                               && remoteBackoffMs(2, 15000) == 4000
                               && remoteBackoffMs(3, 15000) == 8000
                               && remoteBackoffMs(4, 15000) == 15000
                               && remoteBackoffMs(9, 15000) == 15000
                               && remoteBackoffMs(2, 5000) == 4000
                               && remoteBackoffMs(0, 15000) == 15000;
        remote.insert(QStringLiteral("backoffOk"), backoffOk);
        const bool retryAfterOk =
            remoteRetryAfterMs(QByteArrayLiteral(
                                   "HTTP/1.1 429 Too Many Requests\r\n"
                                   "Retry-After: 1\r\n")) == 1000
            && remoteRetryAfterMs(QByteArrayLiteral("retry-after: 7")) == 7000
            && remoteRetryAfterMs(QByteArrayLiteral("Retry-After: sooner"))
                   == -1
            && remoteRetryAfterMs(QByteArrayLiteral("X-Nothing: 3")) == -1;
        remote.insert(QStringLiteral("retryAfterOk"), retryAfterOk);

        // M98: the spool, proven on a temp file - never the real cache, and
        // never a report. Eight records and eleven appends, so the wrap, the
        // drop count and the oldest-first order are all exercised.
        const QString spoolProbePath =
            QDir::tempPath()
            + QStringLiteral("/crowsnest-remote-probe-%1.spool").arg(dumpPid());
        const qint64 probeBudget =
            kRemoteSpoolHeaderBytes + 8 * kRemoteSpoolRecordBytes;
        bool spoolOpened = false;
        bool spoolWrapped = false;
        bool spoolAcked = false;
        bool spoolCorruptSkipped = false;
        int spoolCount = 0;
        int spoolCapacity = 0;
        qint64 spoolDropped = 0;
        qint64 spoolBytes = 0;
        {
            RemoteSpool spool;
            spoolOpened = spool.open(spoolProbePath, probeBudget);
            if (spoolOpened) {
                for (int i = 0; i < 11; ++i) {
                    spool.append(QStringLiteral("probe-%1").arg(i),
                                 qint64(1000 + i),
                                 QByteArrayLiteral("{\"n\":")
                                     + QByteArray::number(i) + '}');
                }
                RemoteSpoolRecord oldest;
                RemoteSpoolRecord newest;
                spoolWrapped =
                    spool.count() == 8 && spool.dropped() == 3
                    && spool.peekOldest(oldest)
                    && oldest.id == QStringLiteral("probe-3")
                    && spool.readAt(7, newest)
                    && newest.id == QStringLiteral("probe-10")
                    && newest.sentAtMs == 1010;
                spoolAcked = spool.ackOldest() && spool.count() == 7
                             && spool.peekOldest(oldest)
                             && oldest.id == QStringLiteral("probe-4");
                spoolCount = spool.count();
                spoolCapacity = spool.capacity();
                spoolDropped = spool.dropped();
                spoolBytes = spool.fileBytes();
            }

            // A byte flipped inside one record must make exactly that record
            // unreadable and leave its neighbours alone: a corrupt slot is
            // skipped, never decoded blind. Slot 0 holds the oldest live report
            // here (7 of 8 slots are in use), and 200 is inside its body.
            if (spoolOpened) {
                QFile raw(spoolProbePath);
                const qint64 offset = qint64(kRemoteSpoolHeaderBytes) + 200;
                if (raw.open(QIODevice::ReadWrite) && raw.seek(offset)) {
                    const QByteArray byte = raw.read(1);
                    const char flipped = char(quint8(byte.at(0)) ^ 0xFFu);
                    raw.seek(offset);
                    if (raw.write(&flipped, 1) == 1) {
                        raw.flush();
                    }
                }
                raw.close();
                int readable = 0;
                int unreadable = 0;
                for (int i = 0; i < spool.count(); ++i) {
                    RemoteSpoolRecord probe;
                    if (spool.readAt(i, probe)) {
                        ++readable;
                    } else {
                        ++unreadable;
                    }
                }
                spoolCorruptSkipped = unreadable == 1
                                      && readable == spool.count() - 1;
            }
            spool.close();
        }
        RemoteSpool spoolReopened;
        const bool spoolReset =
            spoolReopened.open(spoolProbePath, probeBudget * 2)
            && spoolReopened.count() == 0
            && spoolReopened.capacity() == 16;
        spoolReopened.close();
        QFile::remove(spoolProbePath);
        remote.insert(QStringLiteral("spoolCount"), spoolCount);
        remote.insert(QStringLiteral("spoolDropped"), spoolDropped);
        remote.insert(QStringLiteral("spoolBytes"), spoolBytes);
        remote.insert(QStringLiteral("spoolCapacity"), spoolCapacity);
        remote.insert(QStringLiteral("spoolOpenOk"), spoolOpened);
        remote.insert(QStringLiteral("spoolWrapOk"), spoolWrapped);
        remote.insert(QStringLiteral("spoolAckOk"), spoolAcked);
        remote.insert(QStringLiteral("spoolCorruptOk"), spoolCorruptSkipped);
        remote.insert(QStringLiteral("spoolResetOk"), spoolReset);
        remote.insert(QStringLiteral("spoolBudgetOk"),
                      remoteSpoolCapacityFor(0) == 0
                          && remoteSpoolCapacityFor(probeBudget) == 8
                          && remoteSpoolCapacityFor(qint64(256) * 1024 * 1024)
                                 == kRemoteSpoolMaxRecords);

        // M100: the machine identity. `--dump` reports what is on disk and
        // writes nothing at the real path; the maths is proven on a temp id so
        // a runner that never enabled the tier still exercises it.
        const RemoteIdentity liveIdentity =
            remoteIdentityAt(remoteMachineIdPath(), dumpConfig.remote, false);
        remote.insert(QStringLiteral("machineId"), liveIdentity.id);
        remote.insert(QStringLiteral("machineIdName"), liveIdentity.name);
        remote.insert(QStringLiteral("machineIdPath"), remoteMachineIdPath());
        remote.insert(QStringLiteral("machineIdDerived"),
                      liveIdentity.derived);
        remote.insert(QStringLiteral("machineIdNote"), liveIdentity.note);

        const QString idDirPath =
            QDir::tempPath()
            + QStringLiteral("/crowsnest-id-probe-%1").arg(dumpPid());
        QDir idDir(idDirPath);
        idDir.removeRecursively();
        idDir.mkpath(QStringLiteral("."));
        const QString idPath = idDirPath + QStringLiteral("/machine-id");
        RemoteOptions idOptions;
        idOptions.name = QStringLiteral("probe-box");
        const RemoteIdentity firstId =
            remoteIdentityAt(idPath, idOptions, true);
        const RemoteIdentity secondId =
            remoteIdentityAt(idPath, idOptions, true);
        const bool idStable =
            firstId.id.size() == 36 && !firstId.derived && firstId.created
            && secondId.id == firstId.id && !secondId.created;
        QFile idFile(idPath);
        bool idModeOk = idFile.open(QIODevice::ReadOnly);
        if (idModeOk) {
            const QFileDevice::Permissions perms = idFile.permissions();
            idFile.close();
#if defined(Q_OS_WIN)
            // No POSIX modes on Windows: Qt reports a coarse owner/other set for
            // every file, and the id lives in the user's own profile ACL. The
            // check here is the one the OS can answer.
            idModeOk = true;
#else
            idModeOk = !(perms & (QFileDevice::ReadGroup
                                  | QFileDevice::WriteGroup
                                  | QFileDevice::ReadOther
                                  | QFileDevice::WriteOther));
#endif
        }
        remote.insert(QStringLiteral("identityModePosix"), [] {
#if defined(Q_OS_WIN)
            return false;
#else
            return true;
#endif
        }());
        QFile::remove(idPath);
        const RemoteIdentity renewedId =
            remoteIdentityAt(idPath, idOptions, true);
        const bool idRenews =
            !renewedId.id.isEmpty() && renewedId.id != firstId.id;
        QFile::remove(idPath);
        // A path under a regular file cannot be created on any OS, and unlike a
        // chmod 0500 directory it also holds for a root runner.
        const QString blocker = idDirPath + QStringLiteral("/blocker");
        {
            QFile file(blocker);
            if (file.open(QIODevice::WriteOnly)) {
                file.write("x", 1);
            }
        }
        const RemoteIdentity derivedId = remoteIdentityAt(
            blocker + QStringLiteral("/machine-id"), idOptions, true);
        // The derived id is hostname + sha256(mac), never the [remote] name:
        // a display name the user can change must not renumber the machine.
        bool derivedHex = derivedId.id.size() > 13;
        for (const QChar ch : derivedId.id.right(12)) {
            if (!(ch.isDigit() || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')))) {
                derivedHex = false;
            }
        }
        const bool idDerivedOk =
            derivedId.derived && !derivedId.id.isEmpty() && derivedHex
            && derivedId.id.startsWith(
                remoteShortHostname().toLower().left(4))
            && !derivedId.id.startsWith(QStringLiteral("probe-box"))
            && derivedId.note.contains(QStringLiteral("not writable"));
        const RemoteIdentity noWrite =
            remoteIdentityAt(idPath, idOptions, false);
        const bool idNoWriteOk = noWrite.id.isEmpty() && !noWrite.created
                                 && !QFile::exists(idPath);
        // A provisioned id ([remote] machine_id=) is written as-is on the first
        // run and is the file's value from then on.
        const RemoteIdentity seededId = remoteIdentityAt(
            idPath, idOptions, true, QStringLiteral("seeded-id-1234"));
        const RemoteIdentity reseeded = remoteIdentityAt(
            idPath, idOptions, true, QStringLiteral("other-id-5678"));
        const bool idSeedOk = seededId.id == QStringLiteral("seeded-id-1234")
                              && seededId.note.contains(QStringLiteral(
                                     "machine_id="))
                              && reseeded.id == seededId.id;
        QFile::remove(idPath);
        // A hostname longer than the id budget is cut before the hash, so a
        // derived id always fits the 64-byte field it is stored in (a macOS
        // runner found this: the id was 70 chars and nothing accepted it).
        const QString longHost(80, QLatin1Char('h'));
        const QString longId = remoteDerivedMachineId(longHost,
                                                      QStringLiteral("aa:bb"));
        const bool idLongHostOk = longId.size() <= kRemoteMachineIdMaxChars
                                  && remoteMachineIdLooksValid(longId);
        idDir.removeRecursively();
        remote.insert(QStringLiteral("identitySeedOk"), idSeedOk);
        remote.insert(QStringLiteral("identityLongHostOk"), idLongHostOk);
        remote.insert(QStringLiteral("identityOk"),
                      idStable && idRenews && idDerivedOk && idNoWriteOk
                          && idSeedOk && idLongHostOk
                          && remoteMachineIdLooksValid(firstId.id));
        remote.insert(QStringLiteral("identityStable"), idStable);
        remote.insert(QStringLiteral("identityRenews"), idRenews);
        remote.insert(QStringLiteral("identityDerivedOk"), idDerivedOk);
        remote.insert(QStringLiteral("identityModeOk"), idModeOk);
        remote.insert(QStringLiteral("identityNoWriteOk"), idNoWriteOk);

        const QByteArray rendered =
            QJsonDocument(remote).toJson(QJsonDocument::Compact);
        const bool tokenLeaked = !dumpConfig.remote.token.isEmpty()
                                 && QString::fromUtf8(rendered).contains(
                                     dumpConfig.remote.token);
        remote.insert(QStringLiteral("tokenOk"), !tokenLeaked);
        root.insert(QStringLiteral("remote"), remote);
    }

    QJsonObject services;
    const QList<ServiceRecord> serviceRecords = backend.readServices();
    QList<UnitRow> serviceRows;
    serviceRecordsToUnits(serviceRecords, serviceRows);
    services.insert(QStringLiteral("count"), serviceRecords.size());
    services.insert(QStringLiteral("running"),
                    servicesRunningCount(serviceRows));
    services.insert(QStringLiteral("failed"),
                    servicesFailedCount(serviceRows));
    root.insert(QStringLiteral("services"), services);

    {
        QJsonObject disks;
        QList<DiskCounters> first;
        if (backend.readDiskCounters(first)) {
            QThread::msleep(250);
            QList<DiskCounters> second;
            if (backend.readDiskCounters(second)) {
                const QList<DiskRow> rates =
                    computeDiskRates(first, second, 250.0);
                disks.insert(QStringLiteral("instances"), rates.size());
                int hottest = 0;
                int busiest = 0;
                for (const DiskRow &row : rates) {
                    hottest = qMax(hottest,
                                   qRound(row.readPerSec + row.writePerSec));
                    busiest = qMax(busiest, qRound(row.utilPercent));
                }
                disks.insert(QStringLiteral("ioPerSec"), hottest);
                disks.insert(QStringLiteral("utilPercent"), busiest);
            }
        } else {
            disks.insert(QStringLiteral("instances"), 0);
        }
        root.insert(QStringLiteral("disks"), disks);
    }

    {
        QJsonObject security;
        QList<SecurityFinding> findings;
        if (backend.hostSecurityPosture(findings)) {
            const SecurityPosture graded = postureFromFindings(findings);
            security.insert(QStringLiteral("findings"), findings.size());
            security.insert(QStringLiteral("exposed"), graded.exposedCount);
            security.insert(QStringLiteral("exposure"), graded.exposure);
            security.insert(QStringLiteral("grade"), graded.grade);
        } else {
            security.insert(QStringLiteral("findings"), 0);
        }
        root.insert(QStringLiteral("security"), security);
    }

    {
        // Per-filesystem capacity (same source the Host inventory card uses).
        QJsonArray filesystems;
        const HostInfo info = collectHostInfo();
        for (const HostFsInfo &fs : info.filesystems) {
            QJsonObject row;
            row.insert(QStringLiteral("on"), fs.mountedOn);
            row.insert(QStringLiteral("fs"), fs.name);
            row.insert(QStringLiteral("totalG"),
                       fs.kbSize / (1024.0 * 1024.0));
            if (fs.hasKbFree) {
                row.insert(QStringLiteral("freeG"),
                           fs.kbFree / (1024.0 * 1024.0));
            }
            filesystems.append(row);
        }
        QJsonObject fsobj;
        fsobj.insert(QStringLiteral("count"), filesystems.size());
        fsobj.insert(QStringLiteral("entries"), filesystems);
        root.insert(QStringLiteral("filesystems"), fsobj);
    }

#if defined(Q_OS_WIN)
    {
        // Self-process depth: NtQuerySystemInformation ctx walk +
        // PEB cmdline/environment (same-process ReadProcessMemory).
        QJsonObject depth;
        linuxproc::MemInfo memOut;
        linuxproc::ProcDetail detailOut;
        if (backend.procSample(selfPid, memOut, detailOut)) {
            depth.insert(QStringLiteral("ctxSwitches"),
                         static_cast<qlonglong>(detailOut.voluntaryCtx));
        }
        const QString cmdline = backend.processCmdline(selfPid);
        depth.insert(QStringLiteral("cmdlineLength"), cmdline.size());
        const QByteArray envData = backend.processEnv(selfPid);
        depth.insert(QStringLiteral("envPairs"),
                     envData.isEmpty()
                         ? 0
                         : envData.count('\n') + 1);
        QList<ThreadSample> threadRows;
        long threadTickHz = 0;
        if (backend.processThreadSamples(selfPid, threadRows, threadTickHz)) {
            depth.insert(QStringLiteral("threadRows"),
                         static_cast<qlonglong>(threadRows.size()));
        }
        root.insert(QStringLiteral("selfDepth"), depth);
        QJsonObject software;
        software.insert(QStringLiteral("programs"),
                        static_cast<qlonglong>(
                            enumerateWindowsSoftware().size()));
        root.insert(QStringLiteral("software"), software);
    }
#endif

    QJsonObject tcp;
    TcpCounters tcpCounters;
    const bool tcpAvailable = backend.readTcpCounters(tcpCounters);    tcp.insert(QStringLiteral("available"), tcpAvailable);
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
        for (auto it = tcpCounters.ext.constBegin();
             it != tcpCounters.ext.constEnd(); ++it) {
            tcp.insert(QStringLiteral("ext.%1").arg(it.key()),
                       static_cast<qint64>(it.value()));
        }
    }
    root.insert(QStringLiteral("tcp"), tcp);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
