#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QtGlobal>

class SystemBackend;

struct ThreadSample {
    int tid = 0;
    QString comm;
    QString state;
    QString waitChannel;
    quint64 utimeTicks = 0;
    quint64 stimeTicks = 0;
};

struct ThreadProfile {
    int tid = 0;
    QString comm;
    QString state;
    QString waitChannel;
    double cpuPercent = 0.0;
};

struct MemoryRegion {
    QString path;
    unsigned long long pssBytes = 0;
};

struct ProfileSnapshot {
    bool available = false;
    bool warming = false;
    QString comm;
    int threadCount = 0;
    double totalCpuPercent = 0.0;
    QList<ThreadProfile> threads;                  // top by CPU
    QList<QPair<QString, int>> waitChannels;       // off-CPU grouping
    QList<QPair<QString, int>> states;             // R/S/D/...
    QList<MemoryRegion> regions;                   // top by PSS
    unsigned long long totalPssBytes = 0;
    qint64 sampledAtMs = 0;
};

// Parse one /proc/<pid>/task/<tid>/stat line (comm may contain spaces/parens).
ThreadSample parseTaskStat(const QByteArray &stat, const QString &wchan,
                           int tid);
// Sum Pss per mapping path from /proc/<pid>/smaps.
QList<MemoryRegion> parseSmaps(const QByteArray &smaps);

// M67 (pure, all-OS tested): phnt KTHREAD_STATE / KWAIT_REASON names —
// the Windows thread-visibility vocabulary (ntkeapi.h).
QString threadStateName(int state);
QString threadWaitReasonName(int reason);

// M67: one-shot /proc per-thread collection (LinuxBackend delegates here;
// the sampler's no-backend fallback keeps old tests working unchanged).
bool collectProcThreadSamples(int pid, QList<ThreadSample> &out);

// Unprivileged profiler: per-thread CPU time deltas + off-CPU wait channels +
// smaps memory regions (Datadog profile-type analogues: cpu/wall/block/memory).
class ProfileSampler {
public:
    explicit ProfileSampler(SystemBackend *backend = nullptr);
    ProfileSnapshot sample(int pid);
    void reset();

private:
    SystemBackend *m_backend = nullptr;
    bool m_hasBaseline = false;
    QHash<int, QPair<quint64, quint64>> m_lastTicks;
    QElapsedTimer m_timer;
};

QString profileSummaryText(const ProfileSnapshot &snapshot);
QString profileThreadsText(const ProfileSnapshot &snapshot);
QString profileOffCpuText(const ProfileSnapshot &snapshot);
QString profileMemoryText(const ProfileSnapshot &snapshot);
