#pragma once

#include "CpuSampler.h"
#include "Config.h"
#include "DiskSampler.h"
#include "DisplaySnapshot.h"
#include "FocusSampler.h"
#include "GroupCache.h"
#include "HealthEvaluator.h"
#include "HostSampler.h"
#include "IoRateTracker.h"
#include "LinuxProcess.h"
#include "NetRateTracker.h"
#include "ProcessIdentity.h"
#include "ProcessDiskStatus.h"
#include "ProcessNetStatus.h"
#include "SbomSource.h"
#include "TcpSampler.h"
#include "platform/PlatformFactory.h"

#include <QObject>
#include <QString>

#include <memory>

class FocusedWindowProvider;
class QTimer;

class SamplerWorker : public QObject {
    Q_OBJECT

public:
    explicit SamplerWorker(FocusedWindowProvider *provider,
                           const Config &config = Config{},
                           std::unique_ptr<SystemBackend> backend = nullptr,
                           QObject *parent = nullptr);

public slots:
    void start();
    void stop();
    void sampleNow();

signals:
    void snapshotReady(const DisplaySnapshot &snapshot);

private:
    DisplaySnapshot buildSnapshot(const FocusSample &focus);
    void fillHostTail(DisplaySnapshot &snapshot,
                      const QList<linuxproc::SocketInfo> &sockets);
    void fillProcessNet(DisplaySnapshot &snapshot,
                        const QList<linuxproc::SocketInfo> &sockets);
    void fillProcessDisk(DisplaySnapshot &snapshot);

    FocusedWindowProvider *m_provider;
    std::unique_ptr<SystemBackend> m_backend;
    FocusSampler m_sampler;
    CpuSampler m_cpuSampler;
    HostSampler m_hostSampler;
    DiskSampler m_diskSampler;
    TcpSampler m_tcpSampler;
    SbomSource m_sbomSource;
    ProfileSampler m_profileSampler;
    HostInfo m_hostInfo;
    bool m_hostInfoReady = false;
    SloTracker m_slo;
    SloSpec m_sloSpec;
    IoRateTracker m_ioTracker;
    NetRateTracker m_netTracker;
#if defined(Q_OS_LINUX)
    struct DiagPrior {
        quint64 bytesAcked = 0;
        quint64 totalRetrans = 0;
        quint32 skDrops = 0;
        bool seen = false;
    };
    QHash<unsigned long long, DiagPrior> m_diagPrior;
#endif
    QHash<unsigned long long, ProcessNetPrior> m_netPrior;
    QHash<unsigned long long, int> m_netInodePid;
    qint64 m_netMapStampMs = 0;
    qint64 m_netRateStampMs = 0;
    QHash<int, ProcessDiskCounters> m_rowDiskCounters;
    QHash<int, ProcessDiskCounters> m_rowDiskPrior;
    qint64 m_rowDiskStampMs = 0;
    QHash<int, ProcessDiskCounters> m_scanDiskPrior;
    qint64 m_scanDiskStampMs = 0;
    QList<ProcessDiskConsumer> m_scanDiskTop;
    double m_scanDiskReadPerSec = -1.0;
    double m_scanDiskWritePerSec = -1.0;
    int m_scanDiskTracked = 0;
    int m_scanDiskUnreadable = 0;
    GroupCache m_groupCache;
    QTimer *m_timer = nullptr;
    int m_intervalMs = 1000;
    quint64 m_sequence = 0;
    QString m_cpuText = QStringLiteral("--");
    QString m_memText = QStringLiteral("--");
    ProcessIdentity m_cpuIdentity;
    ProcessIdentity m_healthIdentity;
    HealthEvaluator m_health;
};
