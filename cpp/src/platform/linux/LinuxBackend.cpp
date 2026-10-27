#include "LinuxBackend.h"

#include "LinuxGpu.h"

#include <QFile>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {

// Kernel ABI mirrored from util-linux ionice 2.41 ("ioprio_get/ioprio_set
// failed", "0: none, 1: realtime, 2: best-effort, 3: idle"): the class lives
// in the top 3 bits, the priority in the low 3 (IOPRIO_PRIO_VALUE).
constexpr int kIoprioWhoProcess = 1;

QString errnoText(int code) {
    return QString::fromLocal8Bit(std::strerror(code));
}

} // namespace

QString LinuxBackend::platformName() const {
    return QStringLiteral("linux");
}

PlatformCapabilities LinuxBackend::capabilities() const {
    PlatformCapabilities caps;
    caps.cgroups = true;
    caps.systemServices = true;
    caps.journal = true;
    caps.fim = true;
    caps.dpkgSbom = true;
    caps.sbom = true;
    caps.tcpCounters = true;
    caps.psi = true;
    caps.namespaces = true;
    caps.perProcessNet = true;
    caps.perProcessDisk = true;
    caps.perProcessDiskScan = true;
    caps.gpu = true;
    caps.gpuProcessData = true;
    caps.gpuClients = true;
    caps.processSignals = true;
    caps.processRenice = true;
    caps.processIonice = true;
    caps.processOomScore = true;
    caps.processPause = true;
    return caps;
}

bool LinuxBackend::hostCpuTimes(linuxproc::HostCpuTimes &out) {
    return linuxproc::hostCpuTimes(out);
}

bool LinuxBackend::hostMem(linuxproc::HostMem &out) {
    return linuxproc::hostMem(out);
}

bool LinuxBackend::hostNet(linuxproc::NetTotals &out) {
    return linuxproc::hostNet(out);
}

bool LinuxBackend::hostLoad(double &l1, double &l5, double &l15) {
    return linuxproc::hostLoad(l1, l5, l15);
}

bool LinuxBackend::diskUsage(const QString &path, unsigned long long &totalBytes,
                             unsigned long long &freeBytes) {
    return linuxproc::diskUsage(path, totalBytes, freeBytes);
}

bool LinuxBackend::hostInterfaces(QList<linuxproc::InterfaceStats> &out) {
    return linuxproc::hostInterfaces(out);
}

bool LinuxBackend::readDiskCounters(QList<DiskCounters> &out) {
    QFile file(QStringLiteral("/proc/diskstats"));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    out = parseDiskstats(file.readAll());
    return !out.isEmpty();
}

bool LinuxBackend::exists(int pid) {
    return linuxproc::exists(pid);
}

QString LinuxBackend::comm(int pid) {
    return linuxproc::comm(pid);
}

QString LinuxBackend::exePath(int pid) {
    return linuxproc::exePath(pid);
}

int LinuxBackend::parentPid(int pid) {
    return linuxproc::parentPid(pid);
}

QList<int> LinuxBackend::allPids() {
    return linuxproc::allPids();
}

unsigned long long LinuxBackend::startTime(int pid) {
    return linuxproc::startTime(pid);
}

ProcessIdentity LinuxBackend::identity(int pid) {
    return linuxproc::identity(pid);
}

bool LinuxBackend::processThreadSamples(int pid, QList<ThreadSample> &out,
                                        long &tickHz) {
    if (!collectProcThreadSamples(pid, out)) {
        tickHz = 0;
        return false;
    }
    tickHz = sysconf(_SC_CLK_TCK);
    return true;
}

bool LinuxBackend::procCpu(int pid, unsigned long long &ticks,
                           unsigned long long &startTime) {
    return linuxproc::procCpu(pid, ticks, startTime);
}

bool LinuxBackend::totalCpuTicks(unsigned long long &out) {
    return linuxproc::totalCpuTicks(out);
}

int LinuxBackend::onlineCpuCount() {
    return linuxproc::onlineCpuCount();
}

bool LinuxBackend::rssKb(int pid, unsigned long long &out) {
    return linuxproc::rssKb(pid, out);
}

bool LinuxBackend::procMem(int pid, linuxproc::MemInfo &out) {
    return linuxproc::procMem(pid, out);
}

bool LinuxBackend::procSample(int pid, linuxproc::MemInfo &mem,
                              linuxproc::ProcDetail &detail) {
    return linuxproc::procSample(pid, mem, detail);
}

int LinuxBackend::countFds(int pid) {
    return linuxproc::countFds(pid);
}

QList<QString> LinuxBackend::fdTargets(int pid) {
    return linuxproc::fdTargets(pid);
}

bool LinuxBackend::sharedLibraries(int pid, QList<QString> &out) {
    return linuxproc::sharedLibraries(pid, out);
}

QList<unsigned long long> LinuxBackend::socketInodes(int pid) {
    return linuxproc::socketInodes(pid);
}

bool LinuxBackend::processInstrumentation(int pid, QList<QString> &agents) {
    return linuxproc::processInstrumentation(pid, agents);
}

bool LinuxBackend::cgroupSample(int pid, linuxproc::CgroupInfo &out) {
    return linuxproc::cgroupSample(pid, out);
}

bool LinuxBackend::kernelLimits(linuxproc::KernelLimits &out) {
    return linuxproc::readKernelLimits(out);
}

bool LinuxBackend::processOom(int pid, linuxproc::OomInfo &out) {
    return linuxproc::processOom(pid, out);
}

bool LinuxBackend::processDiskCounters(int pid, long long &readBytes,
                                       long long &writeBytes) {
    return linuxproc::procDiskCounters(pid, readBytes, writeBytes);
}

GpuReading LinuxBackend::gpuInfo() {
    return linuxgpu::collect();
}

bool LinuxBackend::gpuClients(const QList<int> &pids,
                              QList<GpuEngineSample> &out) {
    return linuxgpu::clients(pids, out);
}

bool LinuxBackend::sumRssKb(const QList<int> &pids, unsigned long long &out) {
    return linuxproc::sumRssKb(pids, out);
}

bool LinuxBackend::applyProcessAction(const ProcessAction &action,
                                      QString *error) {
    const auto fail = [error](const QString &what) {
        const int code = errno;
        if (error != nullptr) {
            *error = QStringLiteral("%1: %2").arg(what, errnoText(code));
        }
        return false;
    };
    if (action.pid <= 0) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid pid");
        }
        return false;
    }
    switch (action.kind) {
    case ProcessActionKind::Signal:
        if (::kill(action.pid, processSignalNumber(action.signal)) != 0) {
            return fail(processSignalName(action.signal));
        }
        return true;
    case ProcessActionKind::Renice: {
        int target = clampNice(action.niceValue);
        if (action.relative) {
            errno = 0;
            const int current =
                ::getpriority(PRIO_PROCESS, static_cast<id_t>(action.pid));
            if (current == -1 && errno != 0) {
                return fail(QStringLiteral("getpriority"));
            }
            target = clampNice(current + action.niceValue);
        }
        if (::setpriority(PRIO_PROCESS, static_cast<id_t>(action.pid), target)
            != 0) {
            return fail(QStringLiteral("setpriority"));
        }
        return true;
    }
    case ProcessActionKind::Ionice: {
        const int value = ioPriorityValue(action.ioClass, action.ioLevel);
        if (::syscall(SYS_ioprio_set, kIoprioWhoProcess, action.pid, value)
            != 0) {
            return fail(QStringLiteral("ioprio_set"));
        }
        return true;
    }
    case ProcessActionKind::OomScore: {
        const QString path =
            QStringLiteral("/proc/%1/oom_score_adj").arg(action.pid);
        int target = clampOomScoreAdj(action.oomScoreAdj);
        if (action.relative) {
            QFile readable(path);
            if (!readable.open(QIODevice::ReadOnly)) {
                return fail(QStringLiteral("open oom_score_adj"));
            }
            bool parseOk = false;
            const int current = readable.readAll().trimmed().toInt(&parseOk);
            if (!parseOk) {
                if (error != nullptr) {
                    *error = QStringLiteral("oom_score_adj did not parse");
                }
                return false;
            }
            target = clampOomScoreAdj(current + action.oomScoreAdj);
        }
        QFile adj(path);
        if (!adj.open(QIODevice::WriteOnly)) {
            return fail(QStringLiteral("open oom_score_adj"));
        }
        const QByteArray payload = QByteArray::number(target);
        if (adj.write(payload) != payload.size()) {
            return fail(QStringLiteral("write oom_score_adj"));
        }
        return true;
    }
    }
    if (error != nullptr) {
        *error = QStringLiteral("unknown action");
    }
    return false;
}

QList<linuxproc::SocketInfo> LinuxBackend::readNetSockets() {
    return linuxproc::readNetSockets();
}

#if defined(Q_OS_LINUX)
QList<linuxproc::TcpDiagInfo> LinuxBackend::queryTcpDiag() {
    QList<linuxproc::TcpDiagInfo> out;
    linuxproc::queryTcpDiag(out);
    return out;
}
#endif
