#pragma once

#include "CloudProbe.h"
#include "CloudStatus.h"
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
#include "TickBudget.h"
#include "platform/PlatformFactory.h"

#include <QObject>
#include <QString>

#include <memory>

class FocusedWindowProvider;
class CloudFleetRunner;
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
    void fillGpu(DisplaySnapshot &snapshot);
    void fillSensors(DisplaySnapshot &snapshot);
    void fillCloud(DisplaySnapshot &snapshot);

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
    // GPU: one backend probe per 2 s window plus an incremental fdinfo
    // client scan (known clients + a rotating pid slice, ~80 pids/tick)
    // so the per-tick cost stays bounded on big machines.
    GpuReading m_gpuReading;
    QHash<quint64, GpuEngineSample> m_gpuClients;
    QHash<quint64, GpuClientPrior> m_gpuPrior;
    QList<GpuProcess> m_gpuProcesses;
    qint64 m_gpuStampMs = 0;
    qint64 m_gpuWindowMs = 0;
    int m_gpuRotate = 0;
    bool m_gpuWarming = true;
    // Sensors: temperatures/fans/power and battery state change slowly, so
    // one probe per 2-5 s window is plenty (Stats polls the battery at 5 s).
    SensorsSnapshot m_sensors;
    qint64 m_sensorsStampMs = 0;
    // Cloud (M82): the IMDS probe is an outbound-interface read, so it runs on
    // a 5 min cadence when an instance answered and 15 min when none does;
    // costs are recomputed every tick from the cached identity + rate.
    CloudConfig m_cloud;
    CloudSnapshot m_cloudBase;
    CloudRateTable m_cloudRates;
    QString m_cloudRatesPath;
    qint64 m_cloudProbeStampMs = 0;
    qint64 m_cloudRatesStampMs = 0;
    // M84: measured cost of the last IMDS sweep (ms) and how many requests it
    // issued; both surface on the CLOUD FOOTPRINT card.
    qint64 m_cloudSweepMs = -1;
    int m_cloudSweepRequests = 0;
    int m_cloudTicks = 0;
    bool m_cloudProbed = false;
    // M83: one polled CLI listing per 10 min; the sampler never waits on it.
    CloudFleetRunner *m_cloudFleet = nullptr;
    GroupCache m_groupCache;
    QTimer *m_timer = nullptr;
    int m_intervalMs = 1000;
    quint64 m_sequence = 0;
    QString m_cpuText = QStringLiteral("--");
    QString m_memText = QStringLiteral("--");
    ProcessIdentity m_cpuIdentity;
    ProcessIdentity m_healthIdentity;
    HealthEvaluator m_health;
    // M84: the HUD measures its own cost. `m_selfCpu` reuses the exact
    // procCpu/totalCpu delta maths the focused app gets, so the number in
    // Settings is the same arithmetic the top row shows.
    CpuSampler m_selfCpu;
    SelfCost m_selfCost;
    TickCost m_tickCost;
    QList<TickSection> m_tickSections;
    QList<double> m_tickTotals;
    void updateSelfCost();
};
