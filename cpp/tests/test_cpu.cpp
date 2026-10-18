#include "CpuSampler.h"
#include "LinuxProcess.h"
#include "ProcessIdentity.h"

#include <QCoreApplication>
#include <QHash>
#include <QList>

#if defined(Q_OS_WIN)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <cmath>
#include <cstdio>
#if defined(Q_OS_WIN)
#define GETPID() ::_getpid()
#else
#define GETPID() ::getpid()
#endif

namespace {

struct Fake {
    QHash<int, unsigned long long> ticks;
    QHash<int, unsigned long long> start;
    unsigned long long total = 0;
    int cores = 4;
    bool totalOk = true;

    void set(int pid, unsigned long long t, unsigned long long s) {
        ticks[pid] = t;
        start[pid] = s;
    }
    void remove(int pid) {
        ticks.remove(pid);
        start.remove(pid);
    }

    CpuSource source() {
        CpuSource s;
        s.procCpu = [this](int pid, unsigned long long &t, unsigned long long &st) -> bool {
            const auto it = ticks.constFind(pid);
            if (it == ticks.constEnd()) {
                return false;
            }
            t = it.value();
            st = start.value(pid);
            return true;
        };
        s.totalCpu = [this](unsigned long long &out) -> bool {
            if (!totalOk) {
                return false;
            }
            out = total;
            return true;
        };
        s.coreCount = [this]() -> int { return cores; };
        return s;
    }
};

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    int failures = 0;
    auto check = [&failures](bool cond, const char *msg) {
        std::printf("%s %s\n", cond ? "PASS" : "FAIL", msg);
        if (!cond) {
            ++failures;
        }
    };
    auto near = [](double a, double b) { return std::fabs(a - b) < 1e-6; };

    const ProcessIdentity focus{1234, 100};

    // ---- unavailable paths ----
    {
        Fake f;
        f.total = 1000;
        f.set(1234, 10, 100);
        CpuSampler s(f.source());
        const CpuSample r = s.sample({}, focus);
        check(!r.available && !r.warming, "empty pid list -> unavailable");
        const CpuSample r2 = s.sample({1234}, ProcessIdentity{});
        check(!r2.available, "invalid focus identity -> unavailable");
    }
    {
        Fake f;
        f.totalOk = false;
        f.set(1234, 10, 100);
        CpuSampler s(f.source());
        const CpuSample r = s.sample({1234}, focus);
        check(!r.available, "total cpu read failure -> unavailable");
    }
    {
        Fake f;
        f.total = 1000; // pid not present
        CpuSampler s(f.source());
        const CpuSample r = s.sample({999}, focus);
        check(!r.available, "all pids unreadable -> unavailable");
    }

    // ---- baseline / warming ----
    {
        Fake f;
        f.total = 1000;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        const CpuSample r = s.sample({1234}, focus);
        check(r.available && r.warming, "first sample -> warming baseline");
    }

    // ---- second sample computes percent ----
    {
        Fake f;
        f.total = 1000;
        f.cores = 4;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 150, 100);
        f.total = 2000;
        const CpuSample r = s.sample({1234}, focus);
        check(r.available && !r.warming && near(r.percent, 20.0),
              "second sample -> 50t/1000t*100*4 = 20%");
        check(near(r.perPidPercent.value(1234, -1.0), 20.0),
              "second sample -> per-pid percent populated");
    }

    // ---- core scaling ----
    {
        Fake f;
        f.total = 1000;
        f.cores = 8;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 150, 100);
        f.total = 2000;
        const CpuSample r = s.sample({1234}, focus);
        check(near(r.percent, 40.0), "core scaling: 8 cores -> 40%");
    }
    {
        Fake f;
        f.total = 1000;
        f.cores = 0;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 150, 100);
        f.total = 2000;
        const CpuSample r = s.sample({1234}, focus);
        check(near(r.percent, 5.0), "zero cores clamps to 1 -> 5%");
    }

    // ---- new pid appears mid-group: no reset ----
    {
        Fake f;
        f.total = 1000;
        f.cores = 4;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 150, 100);
        f.set(5678, 900, 500);
        f.total = 2000;
        const CpuSample r = s.sample({1234, 5678}, focus);
        check(r.available && !r.warming && near(r.percent, 20.0),
              "new pid appears -> no reset, comparable pid used");
    }

    // ---- pid exits mid-group ----
    {
        Fake f;
        f.total = 1000;
        f.cores = 4;
        f.set(1234, 100, 100);
        f.set(5678, 50, 500);
        CpuSampler s(f.source());
        s.sample({1234, 5678}, focus);
        f.set(1234, 150, 100);
        f.remove(5678);
        f.total = 2000;
        const CpuSample r = s.sample({1234}, focus);
        check(r.available && !r.warming && near(r.percent, 20.0),
              "pid exits -> no reset, remaining delta used");
    }

    // ---- only new entities -> warming ----
    {
        Fake f;
        f.total = 1000;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.remove(1234);
        f.set(9999, 10, 900);
        f.total = 2000;
        const CpuSample r = s.sample({9999}, focus);
        check(r.available && r.warming, "all-new pids -> warming, no bogus delta");
    }

    // ---- PID reuse (same pid, different start) ----
    {
        Fake f;
        f.total = 1000;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 150, 200);
        f.total = 2000;
        const CpuSample r = s.sample({1234}, focus);
        check(r.available && r.warming, "PID reuse -> re-seed, warming");
    }

    // ---- per-entity counter reset ----
    {
        Fake f;
        f.total = 1000;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 90, 100);
        f.total = 2000;
        const CpuSample r = s.sample({1234}, focus);
        check(r.available && r.warming, "per-pid counter reset -> re-seed, warming");
    }

    // ---- total counter reset / reboot ----
    {
        Fake f;
        f.total = 2000;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 150, 100);
        f.total = 500;
        const CpuSample r = s.sample({1234}, focus);
        check(r.available && r.warming, "total counter reset -> warming");
    }

    // ---- focus identity change ----
    {
        Fake f;
        f.total = 1000;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 150, 100);
        f.total = 2000;
        const CpuSample r = s.sample({1234}, ProcessIdentity{1234, 999});
        check(r.available && r.warming, "focus identity change -> warming");
    }

    // ---- explicit reset ----
    {
        Fake f;
        f.total = 1000;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 150, 100);
        f.total = 2000;
        s.reset();
        const CpuSample r = s.sample({1234}, focus);
        check(r.available && r.warming, "reset() -> next sample warming");
    }

    // ---- two pids deltas sum ----
    {
        Fake f;
        f.total = 1000;
        f.cores = 4;
        f.set(1, 100, 100);
        f.set(2, 200, 200);
        CpuSampler s(f.source());
        s.sample({1, 2}, focus);
        f.set(1, 150, 100);
        f.set(2, 260, 200);
        f.total = 3000;
        const CpuSample r = s.sample({1, 2}, focus);
        check(near(r.percent, 22.0), "two pids -> deltas summed (22%)");
    }

    // ---- zero delta, total advanced -> 0%, not warming ----
    {
        Fake f;
        f.total = 1000;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 100, 100);
        f.total = 2000;
        const CpuSample r = s.sample({1234}, focus);
        check(r.available && !r.warming && near(r.percent, 0.0),
              "zero delta with advancing total -> 0% (not warming)");
    }

    // ---- upper clamp ----
    {
        Fake f;
        f.total = 0;
        f.cores = 1;
        f.set(1234, 0, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        f.set(1234, 1000000000ULL, 100);
        f.total = 1;
        const CpuSample r = s.sample({1234}, focus);
        check(r.available && !r.warming && near(r.percent, 999.0),
              "extreme delta clamps to 999 * cores");
    }

    // ---- steady long run stays non-warming ----
    {
        Fake f;
        f.total = 1000;
        f.cores = 4;
        f.set(1234, 100, 100);
        CpuSampler s(f.source());
        s.sample({1234}, focus);
        bool stable = true;
        unsigned long long t = 100;
        unsigned long long tot = 1000;
        for (int i = 0; i < 1000; ++i) {
            t += 10;
            tot += 100;
            f.set(1234, t, 100);
            f.total = tot;
            const CpuSample r = s.sample({1234}, focus);
            if (!r.available || r.warming || !near(r.percent, 40.0)) {
                stable = false;
            }
        }
        check(stable, "1000 steady samples -> stable 40%, never re-warms");
    }

    // ---- real /proc integration ----
#ifdef Q_OS_LINUX
    {
        unsigned long long total = 0;
        check(linuxproc::totalCpuTicks(total) && total > 0,
              "real: totalCpuTicks positive");
        check(linuxproc::onlineCpuCount() > 0, "real: onlineCpuCount positive");

        const int self = static_cast<int>(GETPID());
        unsigned long long ticks = 0;
        unsigned long long start = 0;
        check(linuxproc::procCpu(self, ticks, start) && start != 0,
              "real: procCpu(self) reads ticks + start time");

        const ProcessIdentity id = linuxproc::identity(self);
        check(id.isValid(), "real: identity(self) valid");

        CpuSampler sampler;
        const QList<int> pids{self};
        const CpuSample first = sampler.sample(pids, id);
        check(first.available && first.warming, "real: first sample warming");

        volatile unsigned long long sink = 0;
        for (int i = 0; i < 3000000; ++i) {
            sink += static_cast<unsigned long long>(i);
        }
        ::usleep(60000);
        (void)sink;

        const CpuSample second = sampler.sample(pids, id);
        check(second.available && !second.warming, "real: second sample measured");
        check(second.percent >= 0.0 && second.percent <= 999.0 * 64,
              "real: percent within bounds");
    }
#endif

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
