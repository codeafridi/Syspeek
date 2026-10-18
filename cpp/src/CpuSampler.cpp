#include "CpuSampler.h"

#include "platform/PlatformFactory.h"

#include <algorithm>

CpuSource CpuSource::fromBackend(SystemBackend &backend) {
    CpuSource source;
    source.procCpu = [&backend](int pid, unsigned long long &ticks,
                                unsigned long long &start) {
        return backend.procCpu(pid, ticks, start);
    };
    source.totalCpu = [&backend](unsigned long long &out) {
        return backend.totalCpuTicks(out);
    };
    source.coreCount = [&backend]() { return backend.onlineCpuCount(); };
    return source;
}

CpuSource CpuSource::system() {
    return fromBackend(systemBackend());
}

CpuSampler::CpuSampler(CpuSource source)
    : m_source(std::move(source)) {
}

CpuSample CpuSampler::sample(const QList<int> &pids, const ProcessIdentity &key) {
    CpuSample result;

    if (!key.isValid() || pids.isEmpty()) {
        reset();
        return result;
    }

    unsigned long long total = 0;
    if (!m_source.totalCpu || !m_source.totalCpu(total)) {
        reset();
        return result;
    }

    QHash<int, Entity> current;
    for (int pid : pids) {
        unsigned long long ticks = 0;
        unsigned long long start = 0;
        if (m_source.procCpu && m_source.procCpu(pid, ticks, start) && start != 0) {
            Entity entity;
            entity.id = ProcessIdentity{pid, start};
            entity.ticks = ticks;
            current.insert(pid, entity);
        }
    }

    if (current.isEmpty()) {
        reset();
        return result;
    }

    // New focus target or no baseline yet: seed and report warming.
    if (key != m_key || !m_hasBaseline) {
        m_key = key;
        m_lastByPid = current;
        m_lastTotalTicks = total;
        m_hasBaseline = true;
        result.available = true;
        result.warming = true;
        return result;
    }

    // Sum deltas only for entities that existed in both samples with the same
    // identity. New entities seed silently; reused PIDs and per-entity counter
    // resets are re-seeded rather than producing a bogus delta.
    unsigned long long sumDelta = 0;
    bool comparable = false;
    QHash<int, unsigned long long> deltaByPid;
    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        const auto prev = m_lastByPid.constFind(it.key());
        if (prev == m_lastByPid.constEnd()) {
            continue;
        }
        if (prev->id != it->id) {
            continue;
        }
        if (it->ticks < prev->ticks) {
            continue;
        }
        comparable = true;
        const unsigned long long delta = it->ticks - prev->ticks;
        sumDelta += delta;
        deltaByPid.insert(it.key(), delta);
    }

    m_lastByPid = current;
    const long long deltaTotal =
        static_cast<long long>(total) - static_cast<long long>(m_lastTotalTicks);
    m_lastTotalTicks = total;

    if (deltaTotal <= 0 || !comparable) {
        result.available = true;
        result.warming = true;
        return result;
    }

    int cores = m_source.coreCount ? m_source.coreCount() : 0;
    if (cores < 1) {
        cores = 1;
    }

    const double percent = static_cast<double>(sumDelta)
                           / static_cast<double>(deltaTotal) * 100.0
                           * static_cast<double>(cores);

    result.available = true;
    result.warming = false;
    result.percent = std::clamp(percent, 0.0, 999.0 * cores);

    for (auto it = deltaByPid.constBegin(); it != deltaByPid.constEnd(); ++it) {
        const double value = static_cast<double>(it.value())
                             / static_cast<double>(deltaTotal) * 100.0
                             * static_cast<double>(cores);
        result.perPidPercent.insert(it.key(), value);
    }
    return result;
}

void CpuSampler::reset() {
    m_key = ProcessIdentity{};
    m_lastByPid.clear();
    m_lastTotalTicks = 0;
    m_hasBaseline = false;
}
