#include "SamplerWorker.h"

#include "FocusedWindowProvider.h"
#include "MemoryFormat.h"
#include "ProcessGrouper.h"
#include "platform/PortCatalog.h"

#include <QDateTime>
#include <QFile>
#include <QSet>
#include <QTimer>

#include <algorithm>

SamplerWorker::SamplerWorker(FocusedWindowProvider *provider,
                             const Config &config,
                             std::unique_ptr<SystemBackend> backend,
                             QObject *parent)
    : QObject(parent)
    , m_provider(provider)
    , m_backend(backend ? std::move(backend) : createSystemBackend())
    , m_sampler(provider, *m_backend)
    , m_cpuSampler(CpuSource::fromBackend(*m_backend))
    , m_hostSampler(HostSource::fromBackend(*m_backend))
    , m_diskSampler(DiskSource::fromBackend(*m_backend))
    , m_tcpSampler(TcpSource::fromBackend(*m_backend))
    , m_profileSampler(m_backend.get())
    , m_groupCache(5000, *m_backend)
    , m_health(config.health) {
    m_intervalMs = config.sampleIntervalMs;
    m_sloSpec.name = config.slo.name;
    m_sloSpec.objective = config.slo.objective;
    m_sloSpec.windowHours = config.slo.windowHours;
    m_slo = SloTracker(config.slo.windowHours);
}

void SamplerWorker::start() {
    if (!m_hostInfoReady) {
        m_hostInfo = collectHostInfo();
        m_hostInfoReady = true;
    }
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setInterval(m_intervalMs);
        connect(m_timer, &QTimer::timeout, this, &SamplerWorker::sampleNow);
    }
    m_timer->start();
    sampleNow();
}

void SamplerWorker::stop() {
    if (m_timer != nullptr) {
        m_timer->stop();
    }
}

void SamplerWorker::sampleNow() {
    const FocusSample focus = m_sampler.sample();
    DisplaySnapshot snapshot = buildSnapshot(focus);
    snapshot.sequence = ++m_sequence;
    emit snapshotReady(snapshot);
}

DisplaySnapshot SamplerWorker::buildSnapshot(const FocusSample &focus) {
    DisplaySnapshot snap;

    if (!focus.available) {
        // M58: no early return. Desktops without any focus provider
        // (LXQt/XFCE/Cinnamon plain X sessions) used to leave EVERY
        // card at "--" because the host/disks/tcp tail was skipped
        // wholesale. Without focus the per-app card stays "--" and the
        // health/SLO bookkeeping resets, but the host cards keep
        // updating every tick.
        snap.status = QStringLiteral("no-focus");
        m_cpuText = QStringLiteral("--");
        m_memText = QStringLiteral("--");
        m_cpuIdentity = ProcessIdentity{};
        m_healthIdentity = ProcessIdentity{};
        m_health.reset();
        m_slo.record(false, QDateTime::currentMSecsSinceEpoch());
        snap.slo = m_slo.report(m_sloSpec);
        const QList<linuxproc::SocketInfo> sockets =
            m_backend->capabilities().perProcessNet
                ? m_backend->readNetSockets()
                : QList<linuxproc::SocketInfo>{};
        fillProcessNet(snap, sockets);
        fillHostTail(snap, sockets);
        return snap;
    }

    if (focus.identity != m_healthIdentity) {
        m_health.reset();
        m_healthIdentity = focus.identity;
    }

    snap.appName = focus.appName;
    snap.pid = QString::number(focus.identity.pid);

    const ProcessGroup &group = m_groupCache.pidsFor(focus.identity);
    const CpuSample cpu = m_cpuSampler.sample(group.pids, focus.identity);
    const bool cpuValid = cpu.available && !cpu.warming;

    if (!cpu.available) {
        m_cpuText = QStringLiteral("--");
    } else if (cpu.warming) {
        m_cpuText = QStringLiteral("Warming up");
    } else {
        m_cpuText = QStringLiteral("%1%").arg(qRound(cpu.percent));
    }

    unsigned long long rss = 0;
    const bool rssValid = m_backend->sumRssKb(group.pids, rss);
    m_memText = rssValid ? formatMemoryKb(rss) : QStringLiteral("--");

    QHash<int, int> parentOf;
    for (int pid : group.pids) {
        const int ppid = m_backend->parentPid(pid);
        if (ppid > 0) {
            parentOf.insert(pid, ppid);
        }
    }

    const QList<ProcNode> tree =
        ProcessGrouper::orderedTree(group.rootPid, parentOf);

    unsigned long long rssTotal = 0;
    unsigned long long vmsTotal = 0;
    unsigned long long swapTotal = 0;
    unsigned long long sharedTotal = 0;
    QList<ProcessRow> rows;
    linuxproc::ProcDetail rootDetail;
    int rootPid = -1;
    for (const ProcNode &node : tree) {
        linuxproc::MemInfo info;
        linuxproc::ProcDetail detail;
        if (!m_backend->procSample(node.pid, info, detail)) {
            continue;
        }
        rssTotal += info.rssKb;
        vmsTotal += info.vmsKb;
        swapTotal += info.swapKb;
        sharedTotal += info.sharedKb;

        if (node.depth == 0) {
            rootPid = node.pid;
            rootDetail = detail;
        }

        ProcessRow row;
        row.pid = node.pid;
        row.depth = node.depth;
        row.threads = detail.threads;
        row.fds = detail.fdCount;
        row.cpuPercent = cpu.perPidPercent.value(node.pid, 0.0);
        row.name = m_backend->comm(node.pid);
        if (row.name.isEmpty()) {
            row.name = QString::number(node.pid);
        }
        row.memory = formatMemoryKb(info.rssKb);
        rows.append(row);
    }

    if (rootPid > 0) {
        const IoRateTracker::Rate rate =
            m_ioTracker.update(rootPid, rootDetail.rchar, rootDetail.wchar);
        snap.focusThreads = rootDetail.threads;
        snap.focusFds = rootDetail.fdCount;
        snap.focusVolCtx = rootDetail.voluntaryCtx;
        snap.focusInvolCtx = rootDetail.involuntaryCtx;
        snap.focusIoReadPerSec = rate.readPerSec;
        snap.focusIoWritePerSec = rate.writePerSec;
        snap.focusOpenFiles = m_backend->fdTargets(rootPid);
        m_backend->sharedLibraries(rootPid, snap.dependencies);

        QStringList sbomPaths = snap.dependencies;
        const QString exe = m_backend->exePath(rootPid);
        if (!exe.isEmpty()) {
            sbomPaths.append(exe);
        }
        snap.sbom = m_sbomSource.resolve(sbomPaths);
    }

    snap.processes = rows;
    snap.rssKb = rssTotal;
    snap.vmsKb = vmsTotal;
    snap.swapKb = swapTotal;
    snap.sharedKb = sharedTotal;

    QSet<unsigned long long> inodeSet;
    for (const ProcNode &node : tree) {
        const QList<unsigned long long> inodes =
            m_backend->socketInodes(node.pid);
        for (unsigned long long inode : inodes) {
            inodeSet.insert(inode);
        }
    }

    QList<ConnectionRow> connections;
    QList<QString> listening;
    QList<EndpointRow> endpoints;
    QList<QString> dbConnections;
    QHash<QString, int> stateCounts;
    QSet<QString> listenSeen;
    const QList<linuxproc::SocketInfo> sockets = m_backend->readNetSockets();
#if defined(Q_OS_LINUX)
    QHash<unsigned long long, linuxproc::TcpDiagInfo> tcpDiag;
    if (!inodeSet.isEmpty()) {
        const QList<linuxproc::TcpDiagInfo> diag = m_backend->queryTcpDiag();
        for (const linuxproc::TcpDiagInfo &info : diag) {
            if (info.inode != 0) {
                tcpDiag.insert(info.inode, info);
            }
        }
    }
    if (m_diagPrior.size() > 4096) {
        m_diagPrior.clear();
    }
#endif
    for (const linuxproc::SocketInfo &socket : sockets) {
        stateCounts[socket.state] += 1;

        const bool isEndpoint =
            socket.state == QStringLiteral("LISTEN")
            || (socket.proto.startsWith(QStringLiteral("udp"))
                && socket.remotePort == 0);
        if (isEndpoint) {
            const QString endpoint = QStringLiteral("%1 %2:%3")
                                         .arg(socket.proto, socket.localAddr)
                                         .arg(socket.localPort);
            if (!listenSeen.contains(endpoint)) {
                listenSeen.insert(endpoint);
                listening.append(endpoint);

                EndpointRow entry;
                entry.proto = socket.proto;
                entry.addr = socket.localAddr;
                entry.port = socket.localPort;
                entry.exposure = portcatalog::endpointExposure(socket.localAddr);
                entry.service = portcatalog::serviceForPort(socket.localPort);
                entry.database = portcatalog::portIsDatabase(socket.localPort);
                // LISTEN rx_queue is the kernel's live accept backlog
                // (iproute2 prints it as Recv-Q on a LISTEN socket).
                entry.backlog = static_cast<int>(qMin<unsigned long long>(
                    socket.rxQueue, 2147483647ULL));
                endpoints.append(entry);
            }
        }

        if (!inodeSet.contains(socket.inode) || connections.size() >= 50) {
            continue;
        }
        ConnectionRow row;
        row.proto = socket.proto;
        row.local =
            QStringLiteral("%1:%2").arg(socket.localAddr).arg(socket.localPort);
        row.remote =
            (socket.remotePort != 0
             || socket.state == QStringLiteral("ESTABLISHED"))
                ? QStringLiteral("%1:%2")
                      .arg(socket.remoteAddr)
                      .arg(socket.remotePort)
                : QStringLiteral("*");
        row.state = socket.state;
#if defined(Q_OS_LINUX)
        if (socket.proto.startsWith(QStringLiteral("tcp"))
            && socket.inode != 0) {
            const auto it = tcpDiag.find(socket.inode);
            if (it != tcpDiag.end() && it->hasInfo) {
                row.hasTcpDiag = true;
                row.hasSkmem = it->hasSkmem;
                row.skDrops = it->skDrops;
                row.congestion = it->congestion;
                const linuxproc::TcpDiagInfo &diagInfo = it.value();
                const uint64_t key = socket.inode;
                auto prior = m_diagPrior.find(key);
                if (prior != m_diagPrior.end() && prior->seen) {
                    row.retransDelta = static_cast<quint32>(
                        diagInfo.totalRetrans > prior->totalRetrans
                            ? diagInfo.totalRetrans - prior->totalRetrans
                            : 0);
                    row.skdropDelta =
                        diagInfo.skDrops > prior->skDrops
                            ? diagInfo.skDrops - prior->skDrops
                            : 0;
                    row.bytesAckedDelta =
                        diagInfo.bytesAcked > prior->bytesAcked
                            ? diagInfo.bytesAcked - prior->bytesAcked
                            : 0;
                } else {
                    row.retransDelta = 0;
                    row.skdropDelta = 0;
                    row.bytesAckedDelta = 0;
                }
                DiagPrior next;
                next.bytesAcked = diagInfo.bytesAcked;
                next.totalRetrans = diagInfo.totalRetrans;
                next.skDrops = it->skDrops;
                next.seen = true;
                m_diagPrior.insert(key, next);
                row.rttUsec = it->rttUsec;
                row.rttVarUsec = it->rttVarUsec;
                row.rtoUsec = it->rtoUsec;
                row.cwnd = it->sndCwnd;
                row.sndMss = it->sndMss;
                row.retransmits = it->retransmits;
                row.totalRetrans = it->totalRetrans;
                row.segsOut = static_cast<quint32>(it->segsOut);
                row.segsIn = static_cast<quint32>(it->segsIn);
                row.pmtu = it->pmtu;
                row.bytesSent = it->bytesSent;
                row.bytesAcked = it->bytesAcked;
                row.bytesReceived = it->bytesReceived;
            }
        }
#endif
        connections.append(row);

        if (portcatalog::portIsDatabase(socket.remotePort)) {
            dbConnections.append(
                QStringLiteral("%1:%2 -> %3:%4 (%5)")
                    .arg(socket.localAddr)
                    .arg(socket.localPort)
                    .arg(socket.remoteAddr)
                    .arg(socket.remotePort)
                    .arg(portcatalog::serviceForPort(socket.remotePort)));
        }
    }
    snap.connections = connections;
    snap.listeningPorts = listening;
    snap.endpoints = endpoints;
    snap.dbConnections = dbConnections;

    fillProcessNet(snap, sockets);

    m_backend->processInstrumentation(focus.identity.pid, snap.instrumentation);

    SecurityInput securityInput;
    if (readSecurityInput(focus.identity.pid, securityInput)) {
        snap.security = evaluateSecurity(securityInput);
    } else if (QList<SecurityFinding> hostFindings;
               m_backend->hostSecurityPosture(hostFindings)) {
        snap.security = postureFromFindings(hostFindings);
    }

    snap.runtime = detectRuntime(focus.identity.pid, *m_backend);

    snap.profile = m_profileSampler.sample(focus.identity.pid);

    QList<QPair<QString, int>> counts;
    for (auto it = stateCounts.constBegin(); it != stateCounts.constEnd(); ++it) {
        counts.append(qMakePair(it.key(), it.value()));
    }
    std::sort(counts.begin(), counts.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  return a.first < b.first;
              });
    snap.socketStateCounts = counts;

    QList<linuxproc::InterfaceStats> interfaceStats;
    if (m_backend->hostInterfaces(interfaceStats)) {
        QList<InterfaceRow> interfaceRows;
        for (const linuxproc::InterfaceStats &stats : interfaceStats) {
            InterfaceRow row;
            row.name = stats.name;
            row.rxBytes = stats.rxBytes;
            row.txBytes = stats.txBytes;
            const NetRateTracker::Rate rate =
                m_netTracker.update(stats.name, stats.rxBytes, stats.txBytes);
            row.rxPerSec = rate.rxPerSec;
            row.txPerSec = rate.txPerSec;
            interfaceRows.append(row);
        }
        snap.interfaces = interfaceRows;
    }

    linuxproc::CgroupInfo cgroup;
    if (m_backend->capabilities().cgroups
        && m_backend->cgroupSample(focus.identity.pid, cgroup)
        && cgroup.available) {
        linuxproc::OomInfo oom;
        m_backend->processOom(focus.identity.pid, oom);
        snap.focusOom = oom;
        snap.cgroupAvailable = true;
        snap.cgroupVersion = cgroup.version;
        snap.cgroupPath = cgroup.path;
        snap.cgroupMemUsage = cgroup.memUsageBytes;
        snap.cgroupMemLimit = cgroup.memLimitBytes;
        snap.cgroupAnon = cgroup.memAnonBytes;
        snap.cgroupFile = cgroup.memFileBytes;
        snap.cgroupKernel = cgroup.memKernelBytes;
        snap.cgroupThrottled = cgroup.nrThrottled;
        snap.cgroupPeriods = cgroup.nrPeriods;
        snap.cgroupThrottledUsec = cgroup.throttledUsec;
        snap.cgroupOomEvents = cgroup.memOomEvents;
        snap.cgroupOomKills = cgroup.memOomKills;
        snap.cgroupMemPeakBytes = cgroup.memPeakBytes;
        snap.cgroupHasMemPeak = cgroup.hasMemPeak;
        snap.cgroupPidsCurrent = cgroup.pidsCurrent;
        snap.cgroupPidsMax = cgroup.pidsMax;
    }

    m_cpuIdentity = focus.identity;

    snap.cpu = m_cpuText;
    snap.memory = m_memText;
    snap.cpuValid = cpuValid;
    snap.cpuPercent = cpuValid ? cpu.percent : 0.0;
    snap.status = HealthEvaluator::text(
        m_health.update(cpuValid, cpuValid ? cpu.percent : 0.0, rssValid, rss));

    m_slo.record(snap.status == QStringLiteral("OK"),
                 QDateTime::currentMSecsSinceEpoch());
    snap.slo = m_slo.report(m_sloSpec);

    fillHostTail(snap, sockets);

    return snap;
}

void SamplerWorker::fillHostTail(DisplaySnapshot &snapshot,
                                 const QList<linuxproc::SocketInfo> &sockets) {
#if defined(Q_OS_LINUX)
    linuxproc::KernelLimits kernel;
    if (m_backend->kernelLimits(kernel) && kernel.available) {
        snapshot.kernelLimits = kernel;
        snapshot.hostRun = kernel.runningProcs;
        snapshot.hostBlocked = kernel.blockedProcs;
    }
#endif
    const HostSample host = m_hostSampler.sample();
    if (host.available) {
        snapshot.hostAvailable = true;
        snapshot.hostWarming = host.warming;
        snapshot.hostCpuPercent = host.cpuPercent;
        snapshot.hostStealPercent = host.stealPercent;
        snapshot.hostMemTotalKb = host.mem.totalKb;
        snapshot.hostMemAvailableKb = host.mem.availableKb;
        snapshot.hostDiskTotalBytes = host.diskTotalBytes;
        snapshot.hostDiskFreeBytes = host.diskFreeBytes;
        snapshot.hostNetRxPerSec = host.netRxPerSec;
        snapshot.hostNetTxPerSec = host.netTxPerSec;
        snapshot.hostLoad1 = host.load1;
    }

    snapshot.hostInfo = m_hostInfo;

    const DiskSnapshot disk = m_diskSampler.sample();
    snapshot.disksAvailable = disk.available;
    snapshot.disksWarming = disk.warming;
    snapshot.disks = disk.disks;
    snapshot.ioPressure = disk.pressure;
    snapshot.ioPressureAvailable = disk.pressure.available;
    snapshot.diskIntervalMs = disk.intervalMs;

    const TcpHealth tcp = m_tcpSampler.sample();
    snapshot.tcpHealth = tcp;
    snapshot.tcpHealthAvailable = tcp.available;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    if (tcp.available) {
        int established = 0;
        for (const linuxproc::SocketInfo &socket : sockets) {
            if (socket.proto.startsWith(QStringLiteral("tcp"))
                && socket.state == QStringLiteral("ESTABLISHED")) {
                ++established;
            }
        }
        snapshot.tcpHealth.currEstab = established;
    }
#endif
}

void SamplerWorker::fillProcessNet(
    DisplaySnapshot &snapshot, const QList<linuxproc::SocketInfo> &sockets) {

    if (!m_backend->capabilities().perProcessNet) {
        return;
    }
    snapshot.processNetAvailable = true;

    // The inode -> pid map costs one /proc/<pid>/fd walk per refresh, so it
    // is rebuilt on a 2 s cadence instead of every tick (a socket that
    // appears in between is attributed on the next refresh).
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_netInodePid.isEmpty() || now - m_netMapStampMs >= 2000) {
        m_netInodePid = processNetInodePid(*m_backend, m_backend->allPids());
        m_netMapStampMs = now;
    }

    qint64 windowMs = m_intervalMs;
    if (m_netRateStampMs > 0) {
        windowMs = qBound<qint64>(qint64(100), now - m_netRateStampMs,
                                  qint64(60000));
    }
    m_netRateStampMs = now;

    QHash<int, QList<unsigned long long>> inodesOfPid;
    for (auto it = m_netInodePid.constBegin(); it != m_netInodePid.constEnd();
         ++it) {
        inodesOfPid[it.value()].append(it.key());
    }

    QList<linuxproc::TcpDiagInfo> diag;
#if defined(Q_OS_LINUX)
    diag = m_backend->queryTcpDiag();
#endif
    const QHash<unsigned long long, ProcessNetRate> socketRates =
        processNetSocketRates(diag, windowMs, m_netPrior);
    const QHash<int, ProcessNetPidRate> pidRates =
        processNetPidRates(socketRates, m_netInodePid);

    double focusRx = 0.0;
    double focusTx = 0.0;
    bool focusKnown = false;
    for (ProcessRow &row : snapshot.processes) {
        const auto it = pidRates.constFind(row.pid);
        if (it != pidRates.constEnd()) {
            row.netRxPerSec = it->rxPerSec;
            row.netTxPerSec = it->txPerSec;
            if (it->rxPerSec >= 0.0) {
                focusRx += it->rxPerSec;
                focusTx += it->txPerSec;
                focusKnown = true;
            }
        }
        row.listenPorts =
            processNetListenPorts(sockets, inodesOfPid.value(row.pid));
    }
    if (focusKnown) {
        snapshot.focusNetRxPerSec = focusRx;
        snapshot.focusNetTxPerSec = focusTx;
    }

    int tracked = 0;
    int unattributed = 0;
    bool anyKnown = false;
    for (const linuxproc::TcpDiagInfo &info : diag) {
        if (info.inode == 0) {
            continue;
        }
        if (m_netInodePid.contains(info.inode)) {
            ++tracked;
        } else {
            ++unattributed;
        }
    }
    for (auto it = pidRates.constBegin(); it != pidRates.constEnd(); ++it) {
        if (it->rxPerSec >= 0.0) {
            anyKnown = true;
            break;
        }
    }
    snapshot.processNetTracked = tracked;
    snapshot.processNetUnattributed = unattributed;
    snapshot.processNetWarming = !anyKnown;

    QList<ProcessNetTalker> talkers = processNetTopTalkers(pidRates, 8);
    for (ProcessNetTalker &talker : talkers) {
        talker.name = m_backend->comm(talker.pid);
        if (talker.name.isEmpty()) {
            talker.name = QStringLiteral("pid %1").arg(talker.pid);
        }
        talker.listenPorts =
            processNetListenPorts(sockets, inodesOfPid.value(talker.pid));
    }
    snapshot.processNetTalkers = talkers;
}
