#include "ProbeRun.h"

#include "AppLog.h"
#include "FocusProviderFactory.h"
#include "FocusedWindowProvider.h"
#include "SamplerWorker.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

#include <cstdio>

namespace {
constexpr int kDefaultTicks = 8;

int probeTickCount(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (!arg.startsWith("--probe")) {
            continue;
        }
        const int eq = arg.indexOf('=');
        if (eq > 0) {
            bool ok = false;
            const int value = arg.mid(eq + 1).toInt(&ok);
            if (ok && value > 0) {
                return value;
            }
            return kDefaultTicks;
        }
        // Space form: --probe 4 (next argv is the count if numeric).
        if (i + 1 < argc) {
            bool ok = false;
            const int value = QByteArray(argv[i + 1]).toInt(&ok);
            if (ok && value > 0) {
                return value;
            }
        }
        return kDefaultTicks;
    }
    return 0;
}
} // namespace

int probeRun(int argc, char **argv) {
    const int ticks = probeTickCount(argc, argv);
    if (ticks <= 0) {
        return 0;
    }

    qputenv("QT_LOGGING_RULES", QByteArrayLiteral("qt.*=false"));

    QCoreApplication app(argc, argv);

    const Config rawConfig;
    // The internal auto-timer would interleave with the probe's explicit
    // sampleNow ticks; make the probe chain the only driver.
    // Make the probe's explicit sampleNow chain the only driver; the
    // internal auto-timer would interleave and blur tick attribution.
    Config config{rawConfig};
    config.sampleIntervalMs = 120000;
    auto provider = createFocusProvider(config);

    SamplerWorker worker(provider.get(), config);

    int received = 0;
    QObject::connect(&worker, &SamplerWorker::snapshotReady, [&](const DisplaySnapshot &snapshot) {
        const int hostOk = snapshot.hostAvailable ? 1 : 0;
        bool focusOk = !snapshot.appName.isEmpty() && snapshot.appName != QStringLiteral("--");
        ++received;
        std::printf("probe tick=%d seq=%llu provider=%s status=%s app=%s pid=%s cpu=%s "
                    "mem=%s host=%d hostCpu=%.1f hostMemTotalKb=%llu\n",
                    received,
                    static_cast<unsigned long long>(snapshot.sequence),
                    provider ? provider->name().toUtf8().constData() : "none",
                    snapshot.status.toUtf8().constData(),
                    focusOk ? snapshot.appName.toUtf8().constData() : "-",
                    focusOk ? snapshot.pid.toUtf8().constData() : "-",
                    snapshot.cpu.toUtf8().constData(),
                    snapshot.memory.toUtf8().constData(),
                    hostOk,
                    snapshot.hostCpuPercent,
                    static_cast<unsigned long long>(snapshot.hostMemTotalKb));
        std::fflush(stdout);
    });

    QMetaObject::invokeMethod(&worker, "start", Qt::DirectConnection);

    // start() seeds the first sample synchronously; schedule the rest.
    auto *timer = new QTimer(&app);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, &app, [&]() {
        if (received > ticks) {
            // Defensive: never loop forever if a merge regressed the
            // one-sample-per-tick contract.
            std::printf("probe summary requested=%d received=%d (overrun)\n",
                        ticks, received);
            std::fflush(stdout);
            app.quit();
            return;
        }
        if (received >= ticks) {
            QMetaObject::invokeMethod(&worker, "stop", Qt::DirectConnection);
            std::printf("probe summary requested=%d received=%d\n",
                        ticks, received);
            std::fflush(stdout);
            app.quit();
            return;
        }
        QMetaObject::invokeMethod(&worker, "sampleNow", Qt::DirectConnection);
        timer->start(900);
    });
    timer->start(250);

    app.exec();
    provider.reset();
    return 0;
}
