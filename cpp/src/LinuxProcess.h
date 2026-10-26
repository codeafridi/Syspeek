#pragma once

#include "ProcessIdentity.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <functional>

namespace linuxproc {

struct MemInfo {
    unsigned long long rssKb = 0;
    unsigned long long vmsKb = 0;
    unsigned long long swapKb = 0;
    unsigned long long sharedKb = 0;
};

struct ProcDetail {
    int threads = 0;
    unsigned long long voluntaryCtx = 0;
    unsigned long long involuntaryCtx = 0;
    int fdCount = -1;
    long long rchar = -1;
    long long wchar = -1;
    long long readBytes = -1;
    long long writeBytes = -1;
    long long cancelledWriteBytes = -1;
};

struct SocketInfo {
    QString proto;
    QString localAddr;
    quint16 localPort = 0;
    QString remoteAddr;
    quint16 remotePort = 0;
    QString state;
    unsigned long long inode = 0;
    unsigned long long rxQueue = 0;
    unsigned long long txQueue = 0;
};

struct InterfaceStats {
    QString name;
    unsigned long long rxBytes = 0;
    unsigned long long txBytes = 0;
    unsigned long long rxPackets = 0;
    unsigned long long txPackets = 0;
    unsigned long long rxErrs = 0;
    unsigned long long txErrs = 0;
    unsigned long long rxDrop = 0;
    unsigned long long txDrop = 0;
};

struct CgroupInfo {
    bool available = false;
    QString version;
    QString path;
    unsigned long long cpuUsageUsec = 0;
    unsigned long long cpuUserUsec = 0;
    unsigned long long cpuSystemUsec = 0;
    unsigned long long nrPeriods = 0;
    unsigned long long nrThrottled = 0;
    unsigned long long throttledUsec = 0;
    unsigned long long memUsageBytes = 0;
    long long memLimitBytes = -1;
    long long pidsCurrent = -1;
    long long pidsMax = -1;
    unsigned long long memAnonBytes = 0;
    unsigned long long memFileBytes = 0;
    unsigned long long memKernelBytes = 0;
    unsigned long long memOomEvents = 0;
    unsigned long long memOomKills = 0;
    unsigned long long memPeakBytes = 0;
    bool hasMemPeak = false;
};

struct KernelLimits {
    bool available = false;
    unsigned long long fdAllocated = 0;
    unsigned long long fdMax = 0;
    bool fdPressure = false;
    unsigned long long somaxconn = 0;
    QString congestion;
    QList<QString> congestionAvailable;
    unsigned long long inotifyWatchesMax = 0;
    unsigned long long pidMax = 0;
    int swappiness = -1;
    int overcommit = -1;
    int runningProcs = -1;
    int blockedProcs = -1;
};

struct HostCpuTimes {
    unsigned long long total = 0;
    unsigned long long idle = 0;
    unsigned long long steal = 0;
};

struct HostMem {
    unsigned long long totalKb = 0;
    unsigned long long availableKb = 0;
    unsigned long long freeKb = 0;
};

struct NetTotals {
    unsigned long long rxBytes = 0;
    unsigned long long txBytes = 0;
};

bool exists(int pid);
QString comm(int pid);
QString exePath(int pid);
int parentPid(int pid);
QList<int> allPids();
unsigned long long startTime(int pid);
ProcessIdentity identity(int pid);

bool cpuTicks(int pid, unsigned long long &out);
bool procCpu(int pid, unsigned long long &ticks, unsigned long long &startTime);
bool totalCpuTicks(unsigned long long &out);
int onlineCpuCount();
bool rssKb(int pid, unsigned long long &out);
bool parseVmRssKb(const QByteArray &statusContent, unsigned long long &out);
bool parseStatusMem(const QByteArray &statusContent, MemInfo &out);
bool parseStatmShared(const QByteArray &statmContent, unsigned long long pageSize,
                      unsigned long long &sharedKb);
bool procMem(int pid, MemInfo &out);
bool procSample(int pid, MemInfo &mem, ProcDetail &detail);
bool parseStatusDetail(const QByteArray &statusContent, ProcDetail &out);
bool parseProcIo(const QByteArray &content, ProcDetail &out);
bool procDiskCounters(int pid, long long &readBytes, long long &writeBytes);
QList<QString> fdTargets(int pid);
bool sharedLibraries(int pid, QList<QString> &out);
int countFds(int pid);
QList<unsigned long long> socketInodes(int pid);
bool parseNetTable(const QByteArray &content, const QString &proto,
                   QList<SocketInfo> &out);
QList<SocketInfo> readNetSockets();
struct TcpDiagInfo {
    QString localAddr;
    quint16 localPort = 0;
    QString remoteAddr;
    quint16 remotePort = 0;
    QString state;
    QString congestion;
    bool hasInfo = false;
    quint8 retransmits = 0;
    quint32 rttUsec = 0;
    quint32 rttVarUsec = 0;
    quint32 rtoUsec = 0;
    quint32 sndMss = 0;
    quint32 sndCwnd = 0;
    quint32 sndSsthresh = 0;
    quint32 totalRetrans = 0;
    quint32 pmtu = 0;
    quint64 segsOut = 0;
    quint64 segsIn = 0;
    quint64 bytesAcked = 0;
    quint64 bytesReceived = 0;
    quint64 bytesSent = 0;
    quint64 bytesRetrans = 0;
    quint32 rqueue = 0;
    quint32 wqueue = 0;
    quint32 uid = 0;
    quint32 inode = 0;
    quint32 skRmemAlloc = 0;
    quint32 skRcvBuf = 0;
    quint32 skWmemAlloc = 0;
    quint32 skSndBuf = 0;
    quint32 skFwdAlloc = 0;
    quint32 skWmemQueued = 0;
    quint32 skOptmem = 0;
    quint32 skBacklog = 0;
    quint32 skDrops = 0;
    bool hasSkmem = false;
};

struct OomInfo {
    int score = -1;
    int scoreAdj = 0;
};
#if defined(Q_OS_LINUX)
bool parseTcpDiagDump(const QByteArray &content, QList<TcpDiagInfo> &out);
bool queryTcpDiag(QList<TcpDiagInfo> &out);
#endif
bool parseNetDevInterfaces(const QByteArray &content, QList<InterfaceStats> &out);
bool parsePidCgroup(const QByteArray &content, QString &version, QString &path);
bool parseCgroupCpuStat(const QByteArray &content, CgroupInfo &out);
bool processOom(int pid, OomInfo &out);
bool parseCgroupMemoryValue(const QByteArray &content, long long &out);
bool parseCgroupMemoryStat(const QByteArray &content, CgroupInfo &out);
bool parseCgroupMemoryEvents(const QByteArray &content, CgroupInfo &out);
bool parseFileNr(const QByteArray &content, unsigned long long &allocated,
                 unsigned long long &limit);
void parseCongestionAvailable(const QByteArray &content, QList<QString> &out);
bool parseHostStatTail(const QByteArray &content, int &running, int &blocked);
bool readKernelLimits(KernelLimits &out);
bool cgroupSample(int pid, CgroupInfo &out);
QString serviceForPort(quint16 port);
bool portIsDatabase(quint16 port);
QString endpointExposure(const QString &addr);
bool parseInstrumentation(const QByteArray &envData, const QByteArray &maps,
                          QList<QString> &agents);
bool processInstrumentation(int pid, QList<QString> &agents);
bool parseHostCpu(const QByteArray &statContent, HostCpuTimes &out);
bool parseMemInfo(const QByteArray &content, HostMem &out);
bool parseNetDev(const QByteArray &content, NetTotals &out);
bool parseLoadAvg(const QByteArray &content, double &l1, double &l5, double &l15);
bool hostCpuTimes(HostCpuTimes &out);
bool hostMem(HostMem &out);
bool hostNet(NetTotals &out);
bool hostInterfaces(QList<InterfaceStats> &out);
bool hostLoad(double &l1, double &l5, double &l15);
bool diskUsage(const QString &path, unsigned long long &totalBytes,
               unsigned long long &freeBytes);
double hostCpuBusyPercent(const HostCpuTimes &prev, const HostCpuTimes &cur);
double netRatePerSec(unsigned long long prev, unsigned long long cur,
                     double seconds);
using RssReader = std::function<bool(int, unsigned long long &)>;
bool sumRssKbWith(const QList<int> &pids, const RssReader &reader, unsigned long long &out);
bool sumRssKb(const QList<int> &pids, unsigned long long &out);

} // namespace linuxproc
