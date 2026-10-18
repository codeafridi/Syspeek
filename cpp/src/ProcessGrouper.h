#pragma once

#include "platform/PlatformFactory.h"

#include <QHash>
#include <QList>

struct ProcessGroup {
    int rootPid = -1;
    QList<int> pids;
};

struct ProcNode {
    int pid = -1;
    int ppid = -1;
    int depth = 0;
};

class ProcessGrouper {
public:
    explicit ProcessGrouper(SystemBackend &backend = systemBackend());

    static QList<int> descendants(int rootPid,
                                  const QHash<int, int> &parentOf);

    static QList<ProcNode> orderedTree(int rootPid,
                                       const QHash<int, int> &parentOf);

    ProcessGroup discover(int focusPid) const;

private:
    SystemBackend &m_backend;
};
