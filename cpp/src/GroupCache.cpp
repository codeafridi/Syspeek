#include "GroupCache.h"

#include <QDateTime>

GroupCache::GroupCache(int intervalMs, SystemBackend &backend)
    : m_grouper(backend)
    , m_intervalMs(intervalMs > 0 ? intervalMs : 5000) {
}

const ProcessGroup &GroupCache::pidsFor(const ProcessIdentity &focus, bool force) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    const bool stale =
        m_stampMs == 0 || (now - m_stampMs) > m_intervalMs;
    const bool changed = focus != m_key;

    if (force || changed || stale || !m_key.isValid()) {
        m_group = m_grouper.discover(focus.pid);
        m_key = focus;
        m_stampMs = now;
    }

    return m_group;
}
