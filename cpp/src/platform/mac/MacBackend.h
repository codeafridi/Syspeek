#pragma once

#include "../SystemBackend.h"

class MacBackend : public SystemBackend {
public:
    QString platformName() const override;
    PlatformCapabilities capabilities() const override;

    bool hostCpuTimes(linuxproc::HostCpuTimes &out) override;
    bool hostMem(linuxproc::HostMem &out) override;
    bool hostNet(linuxproc::NetTotals &out) override;
    bool hostLoad(double &l1, double &l5, double &l15) override;
    bool diskUsage(const QString &path, unsigned long long &totalBytes,
                   unsigned long long &freeBytes) override;
    bool hostInterfaces(QList<linuxproc::InterfaceStats> &out) override;
    bool readDiskCounters(QList<DiskCounters> &out) override;
    QList<linuxproc::SocketInfo> readNetSockets() override;
    bool hostSecurityPosture(QList<SecurityFinding> &out) override;
    bool readTcpCounters(TcpCounters &out) override;

    bool exists(int pid) override;
    QString comm(int pid) override;
    QString exePath(int pid) override;
    int parentPid(int pid) override;
    QList<int> allPids() override;
    unsigned long long startTime(int pid) override;
    ProcessIdentity identity(int pid) override;
    bool procCpu(int pid, unsigned long long &ticks,
                 unsigned long long &startTime) override;
    bool totalCpuTicks(unsigned long long &out) override;
    int onlineCpuCount() override;
    bool rssKb(int pid, unsigned long long &out) override;
    bool procMem(int pid, linuxproc::MemInfo &out) override;
    bool procSample(int pid, linuxproc::MemInfo &mem,
                    linuxproc::ProcDetail &detail) override;
    int countFds(int pid) override;
    QList<QString> fdTargets(int pid) override;
    QByteArray processEnv(int pid) override;
    QString processCmdline(int pid) override;

private:
    bool readTcpSysctl(struct tcpstat &stats) const;
    void appendTcpDiagnostics(TcpCounters &out) const;
};
