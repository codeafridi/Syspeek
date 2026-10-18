#include "ProcessGrouper.h"

#include <QSet>

#include <algorithm>

ProcessGrouper::ProcessGrouper(SystemBackend &backend)
    : m_backend(backend) {
}

QList<int> ProcessGrouper::descendants(int rootPid,
                                       const QHash<int, int> &parentOf) {
    QList<int> result;
    QSet<int> inGroup;

    if (rootPid <= 0) {
        return result;
    }

    result.append(rootPid);
    inGroup.insert(rootPid);

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = parentOf.constBegin(); it != parentOf.constEnd(); ++it) {
            if (inGroup.contains(it.value()) && !inGroup.contains(it.key())) {
                inGroup.insert(it.key());
                result.append(it.key());
                changed = true;
            }
        }
    }

    return result;
}

QList<ProcNode> ProcessGrouper::orderedTree(int rootPid,
                                            const QHash<int, int> &parentOf) {
    QList<ProcNode> result;
    if (rootPid <= 0) {
        return result;
    }

    QHash<int, QList<int>> children;
    for (auto it = parentOf.constBegin(); it != parentOf.constEnd(); ++it) {
        children[it.value()].append(it.key());
    }
    for (auto it = children.begin(); it != children.end(); ++it) {
        std::sort(it.value().begin(), it.value().end());
    }

    ProcNode root;
    root.pid = rootPid;
    root.ppid = parentOf.value(rootPid, -1);
    root.depth = 0;
    result.append(root);

    for (int index = 0; index < result.size(); ++index) {
        const ProcNode node = result.at(index);
        const auto kids = children.constFind(node.pid);
        if (kids == children.constEnd()) {
            continue;
        }
        for (int child : kids.value()) {
            ProcNode childNode;
            childNode.pid = child;
            childNode.ppid = node.pid;
            childNode.depth = node.depth + 1;
            result.append(childNode);
        }
    }

    return result;
}

ProcessGroup ProcessGrouper::discover(int focusPid) const {
    ProcessGroup group;

    if (focusPid <= 0 || !m_backend.exists(focusPid)) {
        return group;
    }

    const QString focusedExe = m_backend.exePath(focusPid);
    const QString focusedName = m_backend.comm(focusPid);

    int root = focusPid;
    int current = focusPid;
    while (true) {
        const int parent = m_backend.parentPid(current);
        if (parent <= 1) {
            break;
        }

        const QString parentExe = m_backend.exePath(parent);
        const bool sameExe =
            !focusedExe.isEmpty() && parentExe == focusedExe;
        const bool sameName = m_backend.comm(parent) == focusedName;

        if (!sameExe && !sameName) {
            break;
        }

        root = parent;
        current = parent;
    }

    QHash<int, int> parentOf;
    const QList<int> pids = m_backend.allPids();
    for (int pid : pids) {
        const int parent = m_backend.parentPid(pid);
        if (parent > 0) {
            parentOf.insert(pid, parent);
        }
    }

    group.rootPid = root;
    group.pids = descendants(root, parentOf);
    return group;
}
