#include "LinuxBackend.h"

#include "LinuxGpu.h"

#include <QFile>

#include <unistd.h>

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
    caps.gpuClients = true;
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
