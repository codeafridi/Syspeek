#include "platform/PlatformFactory.h"
#include "platform/PortCatalog.h"
#include "platform/null/NullBackend.h"
#include "ProcessActions.h"

#include "DumpReport.h"

#if defined(Q_OS_LINUX)
#include "platform/linux/LinuxBackend.h"
#endif

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#if defined(Q_OS_WIN)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <cstdio>
#include <memory>
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

    NullBackend null;
    check(null.platformName() == QStringLiteral("null"), "null: platform name");

    const PlatformCapabilities nullCaps = null.capabilities();
    check(!nullCaps.cgroups && !nullCaps.systemServices && !nullCaps.journal
              && !nullCaps.fim && !nullCaps.dpkgSbom && !nullCaps.tcpCounters
              && !nullCaps.psi && !nullCaps.namespaces,
          "null: no capabilities");

    check(null.allPids().isEmpty(), "null: no pids");
    check(null.exists(1) == false, "null: exists() false");
    check(null.comm(1).isEmpty(), "null: comm() empty");
    check(null.exePath(1).isEmpty(), "null: exePath() empty");
    check(null.parentPid(1) == -1, "null: parentPid() -1");
    check(!null.identity(1).isValid(), "null: identity() invalid");
    check(null.onlineCpuCount() == 0, "null: onlineCpuCount() 0");

    unsigned long long ticks = 0;
    unsigned long long start = 0;
    check(!null.procCpu(1, ticks, start), "null: procCpu() false");
    check(!null.totalCpuTicks(ticks), "null: totalCpuTicks() false");

    linuxproc::HostCpuTimes cpuTimes;
    check(!null.hostCpuTimes(cpuTimes), "null: hostCpuTimes() false");
    linuxproc::HostMem mem;
    check(!null.hostMem(mem), "null: hostMem() false");
    linuxproc::NetTotals net;
    check(!null.hostNet(net), "null: hostNet() false");
    double l1 = 0, l5 = 0, l15 = 0;
    check(!null.hostLoad(l1, l5, l15), "null: hostLoad() false");
    unsigned long long totalKb = 0, freeKb = 0;
    check(!null.diskUsage(QStringLiteral("/"), totalKb, freeKb),
          "null: diskUsage() false");

    linuxproc::MemInfo info;
    linuxproc::ProcDetail detail;
    check(!null.procSample(1, info, detail), "null: procSample() false");
    check(!null.procMem(1, info), "null: procMem() false");
    check(null.rssKb(1, ticks) == false, "null: rssKb() false");
    check(null.countFds(1) == -1, "null: countFds() -1");
    check(null.fdTargets(1).isEmpty(), "null: fdTargets() empty");
    check(null.socketInodes(1).isEmpty(), "null: socketInodes() empty");
    QList<QString> libs;
    check(!null.sharedLibraries(1, libs), "null: sharedLibraries() false");
    QList<QString> agents;
    null.processInstrumentation(1, agents);
    check(agents.isEmpty(), "null: processInstrumentation() empty");
    check(null.processEnv(1).isEmpty(), "null: processEnv() empty");
    check(null.processCmdline(1).isEmpty(), "null: processCmdline() empty");

    linuxproc::CgroupInfo cgroup;
    check(!null.cgroupSample(1, cgroup), "null: cgroupSample() false");

    check(null.readNetSockets().isEmpty(), "null: readNetSockets() empty");
    QList<linuxproc::InterfaceStats> interfaces;
    check(!null.hostInterfaces(interfaces), "null: hostInterfaces() false");

    unsigned long long rssSum = 0;
    check(!null.sumRssKb(QList<int>{1, 2}, rssSum), "null: sumRssKb() false");

    const std::unique_ptr<SystemBackend> backend = createSystemBackend();
    check(backend != nullptr, "factory: createSystemBackend() non-null");
    check(!backend->platformName().isEmpty(), "factory: platform name set");
#if defined(Q_OS_LINUX)
    check(backend->platformName() == QStringLiteral("linux"),
          "factory: linux backend");
#elif defined(Q_OS_MACOS)
    check(backend->platformName() == QStringLiteral("macos"),
          "factory: macos backend");
#elif defined(Q_OS_WIN)
    check(backend->platformName() == QStringLiteral("windows"),
          "factory: windows backend");
#else
    check(backend->platformName() == QStringLiteral("null"),
          "factory: null backend");
#endif
    check(&systemBackend() == &systemBackend(), "factory: singleton stable");
    check(systemBackend().platformName() == backend->platformName(),
          "factory: singleton matches factory kind");

    check(portcatalog::serviceForPort(5432) == QStringLiteral("postgres"),
          "portcatalog: 5432 postgres");
    check(portcatalog::portIsDatabase(6379), "portcatalog: 6379 database");
    check(!portcatalog::portIsDatabase(22), "portcatalog: 22 not database");
    check(portcatalog::endpointExposure(QStringLiteral("0.0.0.0"))
              == QStringLiteral("public"),
          "portcatalog: wildcard exposure");
    check(portcatalog::endpointExposure(QStringLiteral("127.0.0.1"))
              == QStringLiteral("local"),
          "portcatalog: loopback exposure");

#if defined(Q_OS_LINUX)
    LinuxBackend linux;
    check(linux.platformName() == QStringLiteral("linux"), "linux: platform name");
    const PlatformCapabilities linuxCaps = linux.capabilities();
    check(linuxCaps.cgroups && linuxCaps.journal && linuxCaps.tcpCounters
              && linuxCaps.psi,
          "linux: capabilities on");

    const int self = static_cast<int>(GETPID());
    check(linux.exists(self), "linux: exists(self)");
    check(!linux.comm(self).isEmpty(), "linux: comm(self)");
    check(!linux.allPids().isEmpty(), "linux: allPids() non-empty");
    check(linux.onlineCpuCount() > 0, "linux: onlineCpuCount() > 0");
    check(linux.totalCpuTicks(ticks) && ticks > 0,
          "linux: totalCpuTicks() positive");
    check(linux.procCpu(self, ticks, start) && start != 0,
          "linux: procCpu(self)");
    check(linux.identity(self).isValid(), "linux: identity(self) valid");
    check(linux.countFds(self) >= 0, "linux: countFds(self)");
    check(!linux.readNetSockets().isEmpty(), "linux: readNetSockets() non-empty");
    linuxproc::HostCpuTimes hostCpu;
    check(linux.hostCpuTimes(hostCpu) && hostCpu.total > 0,
          "linux: hostCpuTimes() positive");
    linuxproc::HostMem hostMem;
    check(linux.hostMem(hostMem) && hostMem.totalKb > 0,
          "linux: hostMem() positive");
    double hl1 = 0, hl5 = 0, hl15 = 0;
    check(linux.hostLoad(hl1, hl5, hl15), "linux: hostLoad()");
    unsigned long long diskTotal = 0, diskFree = 0;
    check(linux.diskUsage(QStringLiteral("/"), diskTotal, diskFree)
              && diskTotal > 0,
          "linux: diskUsage(/) positive");
    unsigned long long groupRss = 0;
    check(linux.sumRssKb(QList<int>{self}, groupRss) && groupRss > 0,
          "linux: sumRssKb(self) positive");
    QList<DiskCounters> diskCounters;
    check(linux.readDiskCounters(diskCounters) && !diskCounters.isEmpty(),
          "linux: readDiskCounters() non-empty");
#endif

    const QString report = dumpReportJson(5);
    const QJsonDocument doc = QJsonDocument::fromJson(report.toUtf8());
    check(doc.isObject(), "dump: valid JSON object");
    const QJsonObject root = doc.object();
    check(root.value(QStringLiteral("platform")).toString()
              == backend->platformName(),
          "dump: platform matches factory");
    check(root.value(QStringLiteral("processCount")).toInt() > 0,
          "dump: processCount > 0");
#if defined(Q_OS_LINUX)
    check(root.value(QStringLiteral("host"))
                  .toObject()
                  .value(QStringLiteral("memTotalKb"))
                  .toDouble()
              > 0.0,
          "dump: host mem positive");
    check(!root.value(QStringLiteral("processes")).toArray().isEmpty(),
          "dump: processes non-empty");
#endif

    // ---- M78 verbs through the real backend ----
    {
        const PlatformCapabilities caps = backend->capabilities();
#if !defined(Q_OS_WIN)
        check(caps.processSignals && caps.processRenice,
              "actions: the backend advertises signals + renice");
#endif
#if defined(Q_OS_LINUX)
        check(caps.processIonice && caps.processOomScore && caps.processPause,
              "actions: linux advertises ioprio, oom_score_adj and pause");
#elif defined(Q_OS_MACOS)
        check(!caps.processIonice && !caps.processOomScore && caps.processPause,
              "actions: macos claims no ioprio/oom, does claim pause");
#else
        check(!caps.processIonice && !caps.processOomScore
                  && !caps.processPause,
              "actions: windows claims terminate only");
#endif

        ProcessAction bad;
        bad.pid = -1;
        bad.kind = ProcessActionKind::Signal;
        bad.signal = ProcessSignal::Term;
        QString badError;
        check(!backend->applyProcessAction(bad, &badError)
                  && !badError.isEmpty(),
              "actions: an invalid pid is refused with a reason");

        NullBackend nullBackend;
        ProcessAction nullAction;
        nullAction.pid = GETPID();
        nullAction.kind = ProcessActionKind::Signal;
        nullAction.signal = ProcessSignal::Term;
        QString nullError;
        check(!nullBackend.applyProcessAction(nullAction, &nullError)
                  && nullError.contains(QStringLiteral("not supported")),
              "actions: the null backend reports the gate text");

#if !defined(Q_OS_WIN)
        QProcess child;
        child.start(QStringLiteral("sleep"), {QStringLiteral("30")});
        const bool spawned = child.waitForStarted(3000);
        check(spawned, "actions: spawned a child to act on");
        if (spawned) {
            const int childPid = int(child.processId());
            QString error;

            ProcessAction renice;
            renice.pid = childPid;
            renice.kind = ProcessActionKind::Renice;
            renice.niceValue = 5;
            renice.relative = true;
            check(backend->applyProcessAction(renice, &error),
                  "actions: renice +5 is applied");

            ProcessAction pause;
            pause.pid = childPid;
            pause.kind = ProcessActionKind::Signal;
            pause.signal = ProcessSignal::Stop;
            check(backend->applyProcessAction(pause, &error),
                  "actions: SIGSTOP is applied");

            ProcessAction resume = pause;
            resume.signal = ProcessSignal::Cont;
            check(backend->applyProcessAction(resume, &error),
                  "actions: SIGCONT is applied");

            ProcessAction term;
            term.pid = childPid;
            term.kind = ProcessActionKind::Signal;
            term.signal = ProcessSignal::Term;
            check(backend->applyProcessAction(term, &error),
                  "actions: SIGTERM is applied");
            child.waitForFinished(3000);
            check(child.state() == QProcess::NotRunning,
                  "actions: the child exits on SIGTERM");
        }
        if (child.state() != QProcess::NotRunning) {
            child.kill();
            child.waitForFinished(2000);
        }
#endif
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
