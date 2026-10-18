#pragma once

#include "ProcessIdentity.h"

#include <QHash>
#include <QList>

#include <functional>

struct CpuSample {
    bool available = false;
    bool warming = false;
    double percent = 0.0;
    QHash<int, double> perPidPercent;
};

class SystemBackend;

// Indirection over the raw CPU reads so tests can drive every edge case
// deterministically. The default source reads real /proc.
struct CpuSource {
    std::function<bool(int, unsigned long long &, unsigned long long &)> procCpu;
    std::function<bool(unsigned long long &)> totalCpu;
    std::function<int()> coreCount;

    static CpuSource fromBackend(SystemBackend &backend);
    static CpuSource system();
};

class CpuSampler {
public:
    explicit CpuSampler(CpuSource source = CpuSource::system());

    CpuSample sample(const QList<int> &pids, const ProcessIdentity &key);
    void reset();

private:
    struct Entity {
        ProcessIdentity id;
        unsigned long long ticks = 0;
    };

    CpuSource m_source;
    ProcessIdentity m_key;
    QHash<int, Entity> m_lastByPid;
    unsigned long long m_lastTotalTicks = 0;
    bool m_hasBaseline = false;
};
