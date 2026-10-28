#include "DumpReport.h"

#include "CloudProbe.h"
#include "CloudStatus.h"
#include "HistoryLog.h"
#include "MetricsEndpoint.h"
#include "CloudFleetSource.h"
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
        const bool available = probe.identity.provider != CloudProvider::None;
        cloud.insert(QStringLiteral("available"), available);
        cloud.insert(QStringLiteral("attempted"), probe.attempted);
        cloud.insert(QStringLiteral("provider"),
                     cloudProviderName(probe.identity.provider));
        if (!available) {
            cloud.insert(QStringLiteral("note"),
                         probe.error.isEmpty() ? cloudUnreachableText()
                                               : probe.error);
        } else {
            const CloudIdentity &id = probe.identity;
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
            const double rate = cloudRateFor(table, probe.identity.provider,
                                             probe.identity.instanceType);
            cloud.insert(QStringLiteral("rateKey"),
                         cloudRateKey(probe.identity.provider,
                                      probe.identity.instanceType));
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
            probeCloudFleet(probe.identity, fleetOptions);
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
        root.insert(QStringLiteral("cloud"), cloud);
    }

    {
        QJsonObject history;
        const QString probePath =
            QDir::tempPath()
            + QStringLiteral("/syspeek-history-probe-%1.ring").arg(dumpPid());
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
