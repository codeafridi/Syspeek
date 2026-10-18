#pragma once

#include "DiskStatus.h"
#include "LinuxProcess.h"
#include "ProcessIdentity.h"
#include "SecurityStatus.h"
#include "ServicesStatus.h"
#include "TcpStatus.h"

#include <QByteArray>
#include <QList>
#include <QString>

struct PlatformCapabilities {
    bool cgroups = false;
    bool systemServices = false;
    bool journal = false;
    bool fim = false;
    bool dpkgSbom = false;
    bool sbom = false;
    bool tcpCounters = false;
    bool psi = false;
    bool namespaces = false;
};

class SystemBackend {
public:
    virtual ~SystemBackend() = default;

    virtual QString platformName() const = 0;
    virtual PlatformCapabilities capabilities() const = 0;

    // ---- host ----
    virtual bool hostCpuTimes(linuxproc::HostCpuTimes &out) {
        Q_UNUSED(out);
        return false;
    }
    virtual bool hostMem(linuxproc::HostMem &out) {
        Q_UNUSED(out);
        return false;
    }
    virtual bool hostNet(linuxproc::NetTotals &out) {
        Q_UNUSED(out);
        return false;
    }
    virtual bool hostLoad(double &l1, double &l5, double &l15) {
        Q_UNUSED(l1);
        Q_UNUSED(l5);
        Q_UNUSED(l15);
        return false;
    }
    virtual bool diskUsage(const QString &path, unsigned long long &totalBytes,
                           unsigned long long &freeBytes) {
        Q_UNUSED(path);
        Q_UNUSED(totalBytes);
        Q_UNUSED(freeBytes);
        return false;
    }
    virtual bool hostInterfaces(QList<linuxproc::InterfaceStats> &out) {
        Q_UNUSED(out);
        return false;
    }
    virtual bool readDiskCounters(QList<DiskCounters> &out) {
        Q_UNUSED(out);
        return false;
    }

    // ---- processes ----
    virtual bool exists(int pid) {
        Q_UNUSED(pid);
        return false;
    }
    virtual QString comm(int pid) {
        Q_UNUSED(pid);
        return QString();
    }
    virtual QString exePath(int pid) {
        Q_UNUSED(pid);
        return QString();
    }
    virtual int parentPid(int pid) {
        Q_UNUSED(pid);
        return -1;
    }
    virtual QList<int> allPids() { return QList<int>(); }
    virtual unsigned long long startTime(int pid) {
        Q_UNUSED(pid);
        return 0;
    }
    virtual ProcessIdentity identity(int pid) {
        Q_UNUSED(pid);
        return ProcessIdentity{};
    }
    virtual bool procCpu(int pid, unsigned long long &ticks,
                         unsigned long long &startTime) {
        Q_UNUSED(pid);
        Q_UNUSED(ticks);
        Q_UNUSED(startTime);
        return false;
    }
    virtual bool totalCpuTicks(unsigned long long &out) {
        Q_UNUSED(out);
        return false;
    }
    virtual int onlineCpuCount() { return 0; }
    virtual bool rssKb(int pid, unsigned long long &out) {
        Q_UNUSED(pid);
        Q_UNUSED(out);
        return false;
    }
    virtual bool procMem(int pid, linuxproc::MemInfo &out) {
        Q_UNUSED(pid);
        Q_UNUSED(out);
        return false;
    }
    virtual bool procSample(int pid, linuxproc::MemInfo &mem,
                            linuxproc::ProcDetail &detail) {
        Q_UNUSED(pid);
        Q_UNUSED(mem);
        Q_UNUSED(detail);
        return false;
    }
    virtual int countFds(int pid) {
        Q_UNUSED(pid);
        return -1;
    }
    virtual QList<QString> fdTargets(int pid) {
        Q_UNUSED(pid);
        return QList<QString>();
    }
    virtual bool sharedLibraries(int pid, QList<QString> &out) {
        Q_UNUSED(pid);
        Q_UNUSED(out);
        return false;
    }
    virtual QList<unsigned long long> socketInodes(int pid) {
        Q_UNUSED(pid);
        return QList<unsigned long long>();
    }
    virtual bool processInstrumentation(int pid, QList<QString> &agents) {
        Q_UNUSED(pid);
        Q_UNUSED(agents);
        return false;
    }
    virtual QByteArray processEnv(int pid) {
        Q_UNUSED(pid);
        return QByteArray();
    }
    virtual QString processCmdline(int pid) {
        Q_UNUSED(pid);
        return QString();
    }
    virtual bool cgroupSample(int pid, linuxproc::CgroupInfo &out) {
        Q_UNUSED(pid);
        Q_UNUSED(out);
        return false;
    }
    virtual bool sumRssKb(const QList<int> &pids, unsigned long long &out) {
        unsigned long long total = 0;
        bool any = false;
        for (int pid : pids) {
            unsigned long long kb = 0;
            if (rssKb(pid, kb)) {
                total += kb;
                any = true;
            }
        }
        if (!any) {
            return false;
        }
        out = total;
        return true;
    }

    // ---- network ----
    virtual QList<linuxproc::SocketInfo> readNetSockets() {
        return QList<linuxproc::SocketInfo>();
    }
    virtual bool hostSecurityPosture(QList<SecurityFinding> &out) {
        Q_UNUSED(out);
        return false;
    }
    virtual bool readTcpCounters(TcpCounters &out) {
        Q_UNUSED(out);
        return false;
    }

    // ---- services ----
    // Native records (SCM on Windows). Linux/macOS run tools through their
    // own source strategies and do not fill this.
    virtual QList<ServiceRecord> readServices() {
        return QList<ServiceRecord>();
    }
};
