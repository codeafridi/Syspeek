#pragma once

#include "platform/SystemBackend.h"

#if defined(Q_OS_WIN)

#include <QHash>
#include <QMutex>

class WindowsBackend final : public SystemBackend {
public:
    WindowsBackend();
    ~WindowsBackend() override;

    QString platformName() const override;
    PlatformCapabilities capabilities() const override;

    bool hostCpuTimes(linuxproc::HostCpuTimes &out) override;
    bool hostMem(linuxproc::HostMem &out) override;
    bool hostNet(linuxproc::NetTotals &out) override;
    bool hostInterfaces(QList<linuxproc::InterfaceStats> &out) override;
    bool hostLoad(double &l1, double &l5, double &l15) override;
    bool diskUsage(const QString &path, unsigned long long &totalBytes,
                   unsigned long long &freeBytes) override;

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
    bool processDiskCounters(int pid, long long &readBytes,
                             long long &writeBytes) override;
    QList<unsigned long long> socketInodes(int pid) override;

    QList<linuxproc::SocketInfo> readNetSockets() override;
    bool readTcpCounters(TcpCounters &out) override;
    bool readDiskCounters(QList<DiskCounters> &out) override;
    QList<ServiceRecord> readServices() override;
    bool hostSecurityPosture(QList<SecurityFinding> &out) override;
    QByteArray processEnv(int pid) override;
    QString processCmdline(int pid) override;

private:
    struct Snapshot {
        QList<int> pids;
        QHash<int, quint32> ppid;
        QHash<int, quint32> threads;
        QHash<int, QString> image;
        qint64 builtAt = 0;
    };

    Snapshot snapshot() const;

    bool m_wsaReady = false;
    mutable QMutex m_mutex;
    mutable Snapshot m_snapshot;
#if defined(Q_OS_WIN)
    // PDH disk sampling (persistent query; raw counter reads).
    bool ensurePdhDiskQuery();
    void closePdhDiskQuery();
    void *m_pdhQuery = nullptr;
    QList<void *> m_pdhDiskHandles;
    long long m_pdhTimeBase = 0;
    QMutex m_pdhDiskMutex;

    // Per-process cumulative context switches (NtQuerySystemInformation
    // SystemProcessInformation walk; 1 s TTL cache).
    bool systemContextSwitches(int pid, unsigned long long &out);
    QHash<int, unsigned long long> m_ctxMap;
    qint64 m_ctxMapAt = 0;

    // M67: per-thread CPU/wait samples from the same walk (1 s TTL cache).
    bool processThreadSamples(int pid, QList<ThreadSample> &out,
                              long &tickHz) override;
    QHash<int, QList<ThreadSample>> m_threadCache;
    qint64 m_threadCacheAt = 0;
#endif
    bool m_tcpCountersOk = false;
};

#endif
