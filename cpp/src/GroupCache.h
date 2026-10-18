#pragma once

#include "ProcessGrouper.h"
#include "ProcessIdentity.h"

#include <QList>

class GroupCache {
public:
    explicit GroupCache(int intervalMs = 5000,
                        SystemBackend &backend = systemBackend());

    const ProcessGroup &pidsFor(const ProcessIdentity &focus, bool force = false);

    int intervalMs() const { return m_intervalMs; }

private:
    ProcessGrouper m_grouper;
    ProcessGroup m_group;
    ProcessIdentity m_key;
    qint64 m_stampMs = 0;
    int m_intervalMs;
};
