#include "CpuSampler.h"
#include "GroupCache.h"
#include "LinuxProcess.h"
#include "ProcessGrouper.h"
#include "ProcessIdentity.h"

#include <QCoreApplication>
#include <QHash>

#if defined(Q_OS_WIN)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <cstdio>
#if defined(Q_OS_WIN)
#define GETPID() ::_getpid()
#else
#define GETPID() ::getpid()
#endif

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    int failures = 0;
    auto check = [&failures](bool condition, const char *message) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition) {
            ++failures;
        }
    };

    QHash<int, int> parentOf;
    parentOf.insert(2, 1);
    parentOf.insert(3, 1);
    parentOf.insert(4, 2);
    parentOf.insert(5, 4);

    const QList<int> fromRoot1 = ProcessGrouper::descendants(1, parentOf);
    check(fromRoot1.size() == 5, "descendants(1) includes whole tree");
    check(fromRoot1.contains(5), "descendants(1) includes deep descendant");

    const QList<int> fromRoot2 = ProcessGrouper::descendants(2, parentOf);
    check(fromRoot2.size() == 3 && fromRoot2.contains(4)
              && fromRoot2.contains(5) && !fromRoot2.contains(3),
          "descendants(2) includes only its subtree");

    check(ProcessGrouper::descendants(6, parentOf).size() == 1,
          "descendants(unknown) is just itself");
    check(ProcessGrouper::descendants(0, parentOf).isEmpty(),
          "descendants(0) is empty");

    const QList<ProcNode> tree = ProcessGrouper::orderedTree(1, parentOf);
    check(tree.size() == 5, "orderedTree(1) includes whole tree");
    check(tree.at(0).pid == 1 && tree.at(0).depth == 0,
          "orderedTree root first at depth 0");
    check(tree.at(1).pid == 2 && tree.at(1).depth == 1 && tree.at(2).pid == 3
              && tree.at(2).depth == 1,
          "orderedTree level-1 children sorted and at depth 1");
    check(tree.at(3).pid == 4 && tree.at(3).depth == 2,
          "orderedTree grandchild at depth 2");
    check(tree.at(4).pid == 5 && tree.at(4).depth == 3,
          "orderedTree deep descendant at depth 3");
    check(tree.at(4).ppid == 4, "orderedTree records parent pid");

    const QList<ProcNode> subtree = ProcessGrouper::orderedTree(2, parentOf);
    check(subtree.size() == 3 && subtree.at(0).pid == 2 && subtree.at(2).pid == 5,
          "orderedTree(2) is its subtree only");
    check(ProcessGrouper::orderedTree(0, parentOf).isEmpty(),
          "orderedTree(0) is empty");
    check(ProcessGrouper::orderedTree(99, parentOf).size() == 1,
          "orderedTree(unknown) is just the root");

#ifdef Q_OS_LINUX
    const int self = static_cast<int>(GETPID());
    const ProcessGroup group = ProcessGrouper().discover(self);
    check(group.rootPid > 0, "discover(self) has a root");
    check(group.pids.contains(self), "discover(self) contains self");

    bool allExist = true;
    for (int pid : group.pids) {
        if (!linuxproc::exists(pid)) {
            allExist = false;
            break;
        }
    }
    check(allExist, "every group pid exists");

    unsigned long long rss = 0;
    check(linuxproc::sumRssKb(group.pids, rss) && rss > 0,
          "sumRssKb(group) is positive");

    const ProcessIdentity key = linuxproc::identity(self);
    CpuSampler cpuSampler;
    const CpuSample first = cpuSampler.sample(group.pids, key);
    check(first.available && first.warming, "group cpu first sample warming");

    volatile unsigned long long sink = 0;
    for (int i = 0; i < 3000000; ++i) {
        sink += static_cast<unsigned long long>(i);
    }
    ::usleep(60000);
    (void)sink;

    const CpuSample second = cpuSampler.sample(group.pids, key);
    check(second.available && !second.warming,
          "group cpu second sample computed");

    GroupCache cache;
    const ProcessGroup &a = cache.pidsFor(key);
    const ProcessGroup &b = cache.pidsFor(key);
    check(a.pids == b.pids, "group cache returns stable pids when unchanged");
    check(cache.pidsFor(key).pids.contains(self), "cached group contains self");

    std::printf("group size=%lld\n", static_cast<long long>(group.pids.size()));
#endif

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
