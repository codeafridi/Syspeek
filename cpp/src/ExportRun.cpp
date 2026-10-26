#include "ExportRun.h"

#include "Config.h"
#include "FocusProviderFactory.h"
#include "FocusedWindowProvider.h"
#include "MetricsExport.h"
#include "SamplerWorker.h"
#include "platform/PlatformFactory.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QMetaObject>
#include <QTimer>

#include <cstdio>

namespace {

constexpr int kMaxSamples = 86400;

struct RecordArgs {
    bool present = false;
    int samples = 0;
    bool byDuration = false;
    int durationSeconds = 0;
    QString format = QStringLiteral("csv");
    QString out;
};

QString valueFor(const QByteArray &arg, const char *prefix) {
    const QByteArray p(prefix);
    if (!arg.startsWith(p)) {
        return QString();
    }
    return QString::fromLocal8Bit(arg.mid(p.size()));
}

RecordArgs parseRecordArgs(int argc, char **argv) {
    RecordArgs args;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--record" || arg.startsWith("--record=")) {
            args.present = true;
            QString value = valueFor(arg, "--record=");
            if (value.isEmpty() && i + 1 < argc) {
                const QByteArray next(argv[i + 1]);
                if (!next.startsWith("--")) {
                    value = QString::fromLocal8Bit(next);
                }
            }
            int unitSeconds = 0;
            const QChar suffix = value.isEmpty() ? QChar()
                                                 : value.at(value.size() - 1);
            if (suffix.isLetter()) {
                const QChar lower = suffix.toLower();
                if (lower == QLatin1Char('s')) {
                    unitSeconds = 1;
                } else if (lower == QLatin1Char('m')) {
                    unitSeconds = 60;
                } else if (lower == QLatin1Char('h')) {
                    unitSeconds = 3600;
                }
                if (unitSeconds > 0) {
                    value.chop(1);
                }
            }
            bool ok = false;
            const int count = value.toInt(&ok);
            if (ok && count > 0) {
                if (unitSeconds > 0) {
                    args.byDuration = true;
                    args.durationSeconds = count * unitSeconds;
                } else {
                    args.samples = count;
                }
            }
            continue;
        }
        const QString format = valueFor(arg, "--format=");
        if (!format.isEmpty()) {
            args.format = normalizedExportFormat(format);
            continue;
        }
        const QString out = valueFor(arg, "--out=");
        if (!out.isEmpty()) {
            args.out = out;
        }
    }
    return args;
}

} // namespace

int exportRun(int argc, char **argv) {
    const RecordArgs args = parseRecordArgs(argc, argv);
    if (!args.present) {
        return 0;
    }

    qputenv("QT_LOGGING_RULES", QByteArrayLiteral("qt.*=false"));

    QCoreApplication app(argc, argv);

    QString ignoredError;
    const Config config = loadConfig(defaultConfigPath(), &ignoredError);
    Config samplerConfig = config;
    const int intervalMs = config.sampleIntervalMs > 0
                               ? config.sampleIntervalMs
                               : 1000;
    samplerConfig.sampleIntervalMs = 120000;

    int ticks = args.samples;
    if (args.byDuration) {
        ticks = (args.durationSeconds * 1000 + intervalMs - 1) / intervalMs;
        if (ticks < 1) {
            ticks = 1;
        }
    }
    if (ticks > kMaxSamples) {
        ticks = kMaxSamples;
    }

    auto provider = createFocusProvider(config);
    SamplerWorker worker(provider.get(), samplerConfig);

    QList<ExportRow> rows;
    rows.reserve(ticks);
    QObject::connect(&worker, &SamplerWorker::snapshotReady,
                     [&](const DisplaySnapshot &snapshot) {
                         rows.append(exportRowFrom(
                             snapshot, QDateTime::currentMSecsSinceEpoch()));
                     });

    QMetaObject::invokeMethod(&worker, "start", Qt::DirectConnection);

    auto *timer = new QTimer(&app);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, &app, [&]() {
        if (rows.size() >= ticks || rows.size() > ticks) {
            QMetaObject::invokeMethod(&worker, "stop", Qt::DirectConnection);
            app.quit();
            return;
        }
        QMetaObject::invokeMethod(&worker, "sampleNow", Qt::DirectConnection);
        timer->start(intervalMs);
    });
    timer->start(intervalMs);

    app.exec();
    provider.reset();

    ExportMeta meta;
    meta.hostname = QSysInfo::machineHostName();
    meta.platform = platformName();
    meta.version = QString::fromLatin1(SYSPEEK_APP_VERSION);
    meta.intervalMs = intervalMs;

    const QString body = exportRows(rows, meta, args.format);

    if (args.out.isEmpty() || args.out == QStringLiteral("-")) {
        std::fwrite(body.toUtf8().constData(), 1, size_t(body.toUtf8().size()),
                    stdout);
        std::fflush(stdout);
        std::printf("export summary format=%s samples=%d\n",
                    args.format.toUtf8().constData(), int(rows.size()));
        std::fflush(stdout);
        return rows.isEmpty() ? 1 : 0;
    }

    QFile file(args.out);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::printf("export error: could not write %s\n",
                    args.out.toUtf8().constData());
        std::fflush(stdout);
        return 1;
    }
    file.write(body.toUtf8());
    file.close();
    std::printf("export ok: format=%s samples=%d bytes=%lld -> %s\n",
                args.format.toUtf8().constData(), int(rows.size()),
                static_cast<long long>(body.toUtf8().size()),
                args.out.toUtf8().constData());
    std::fflush(stdout);
    return rows.isEmpty() ? 1 : 0;
}
