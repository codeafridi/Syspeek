#include "FocusSampler.h"
#include "FocusedWindowProvider.h"
#include "GnomePidFileProvider.h"
#include "LinuxProcess.h"
#include "ProcessIdentity.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryFile>

#include <signal.h>
#if !defined(Q_OS_WIN)
#include <sys/wait.h>
#endif
#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

#include <cstdio>
#if defined(Q_OS_WIN)
int main() {
    std::printf("skip: POSIX-only test\n");
    return 0;
}
#else
#if defined(Q_OS_WIN)
#define GETPID() ::_getpid()
#else
#define GETPID() ::getpid()
#endif

#ifdef Q_OS_LINUX

namespace {

class FakeProvider : public FocusedWindowProvider {
public:
    explicit FakeProvider(int pid) : m_pid(pid) {}

    QString name() const override { return QStringLiteral("fake"); }
    int focusedPid() const override { return m_pid; }
    void setPid(int pid) { m_pid = pid; }

private:
    int m_pid;
};

int spawnSleeper() {
    const pid_t pid = fork();
    if (pid == 0) {
        for (;;) {
            pause();
        }
    }
    return static_cast<int>(pid);
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    int failures = 0;
    auto check = [&failures](bool condition, const char *message) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition) {
            ++failures;
        }
    };

    const int self = GETPID();
    const int deadPid = 1 << 30;

    check(linuxproc::exists(self), "exists(self) is true");
    check(!linuxproc::exists(deadPid), "exists(bogus pid) is false");
    check(!linuxproc::exists(-1), "exists(-1) is false");
    check(!linuxproc::comm(self).isEmpty(), "comm(self) is not empty");
    check(linuxproc::comm(deadPid).isEmpty(), "comm(bogus pid) is empty");
    check(linuxproc::startTime(self) > 0, "startTime(self) > 0");
    check(linuxproc::identity(self).isValid(), "identity(self) is valid");
    check(!linuxproc::identity(deadPid).isValid(), "identity(bogus pid) invalid");

    QTemporaryFile tmp;
    QString pidFilePath;
    if (tmp.open()) {
        tmp.write(QByteArray::number(self));
        tmp.write("\n");
        tmp.flush();
        pidFilePath = tmp.fileName();
    }

    GnomePidFileProvider fileProvider(pidFilePath);
    check(fileProvider.focusedPid() == self, "pid-file provider resolves live pid");

    GnomePidFileProvider missingProvider(
        QStringLiteral("/nonexistent/syspeek-active-pid"));
    check(missingProvider.focusedPid() == -1, "missing pid file -> -1");

    GnomePidFileProvider emptyPathProvider{QString()};
    check(emptyPathProvider.focusedPid() == -1, "empty path -> -1");

    const int live = spawnSleeper();
    check(live > 0 && linuxproc::exists(live), "spawned a live external process");

    FakeProvider liveProvider(live);
    FocusSampler liveSampler(&liveProvider);
    const FocusSample liveSample = liveSampler.sample();
    check(liveSample.available, "sampler available for live pid");
    check(liveSample.identity.pid == live, "sampler reports correct pid");
    check(liveSample.appName == linuxproc::comm(live), "sampler reports comm name");
    check(liveSample.providerName == QStringLiteral("fake"),
          "sampler reports provider name");

    FakeProvider selfProvider(self);
    FocusSampler selfSampler(&selfProvider);
    check(!selfSampler.sample().available,
          "sampler ignores Syspeek's own pid (no prior app)");

    FakeProvider fallbackProvider(live);
    FocusSampler fallbackSampler(&fallbackProvider);
    check(fallbackSampler.sample().identity.pid == live,
          "fallback baseline is the live app");
    fallbackProvider.setPid(self);
    const FocusSample afterSelf = fallbackSampler.sample();
    check(afterSelf.available && afterSelf.identity.pid == live,
          "overlay focus keeps the last real app");

    FakeProvider deadProvider(deadPid);
    FocusSampler deadSampler(&deadProvider);
    check(!deadSampler.sample().available, "sampler unavailable for dead pid");

    FakeProvider noneProvider(-1);
    FocusSampler noneSampler(&noneProvider);
    check(!noneSampler.sample().available, "sampler unavailable for -1");

    FocusSampler nullSampler(nullptr);
    check(!nullSampler.sample().available, "sampler unavailable without provider");

    kill(live, SIGKILL);
    waitpid(live, nullptr, 0);

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

#else

int main(int argc, char *argv[]) {
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    std::printf("focus tests skipped (linux-only)\n");
    return 0;
}

#endif

#endif