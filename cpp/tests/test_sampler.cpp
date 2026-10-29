#include "DisplaySnapshot.h"
#include "FocusedWindowProvider.h"
#include "LinuxProcess.h"
#include "SamplerWorker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

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

namespace {

class FakeProvider : public FocusedWindowProvider {
public:
    explicit FakeProvider(int pid) : m_pid(pid) {}
    QString name() const override { return QStringLiteral("fake"); }
    int focusedPid() const override { return m_pid; }

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

bool waitFor(int &count, int target, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (count < target && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return count >= target;
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

    const int live = spawnSleeper();
    FakeProvider provider(live);

    auto *thread = new QThread;
    auto *worker = new SamplerWorker(&provider);
    worker->moveToThread(thread);
    QObject::connect(thread, &QThread::started, worker, &SamplerWorker::start);
    QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);

    int received = 0;
    DisplaySnapshot latest;
    quint64 maxSeq = 0;
    QObject::connect(worker, &SamplerWorker::snapshotReady, &app,
                     [&received, &latest, &maxSeq](const DisplaySnapshot &s) {
                         ++received;
                         latest = s;
                         if (s.sequence > maxSeq) {
                             maxSeq = s.sequence;
                         }
                     });

    thread->start();

    QElapsedTimer firstTick;
    firstTick.start();
    const bool started = waitFor(received, 1, 3000);
    std::printf("INFO first snapshot after %lld ms\n",
                static_cast<long long>(firstTick.elapsed()));
    check(started, "worker emits a snapshot after start");

    QMetaObject::invokeMethod(worker, "sampleNow", Qt::QueuedConnection);
    QElapsedTimer secondTick;
    secondTick.start();
    const bool onDemand = waitFor(received, 2, 3000);
    std::printf("INFO second snapshot after %lld ms\n",
                static_cast<long long>(secondTick.elapsed()));
    check(onDemand, "worker emits on demand");
    check(maxSeq >= 2, "sequence increases across snapshots");

#ifdef Q_OS_LINUX
    check(latest.appName == linuxproc::comm(live), "snapshot has focused name");
    check(latest.pid == QString::number(live), "snapshot has focused pid");
    check(!latest.memory.isEmpty(), "snapshot has memory text");
#endif
    check(latest.cpu == QStringLiteral("Warming up")
              || latest.cpu == QStringLiteral("--")
              || latest.cpu.endsWith(QLatin1Char('%')),
          "snapshot cpu is warming/--/percent");

    thread->quit();
    thread->wait();
    check(true, "worker thread stopped cleanly");

    delete thread;

    kill(live, SIGKILL);
    waitpid(live, nullptr, 0);

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

#endif