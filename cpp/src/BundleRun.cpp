#include "BundleRun.h"

#include "BundleStatus.h"
#include "Config.h"
#include "FocusProviderFactory.h"
#include "FocusedWindowProvider.h"
#include "MetricsExport.h"
#include "SamplerWorker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

namespace {

struct BundleArgs {
    bool present = false;
    int samples = 10;
    QString out;
    QString inspect;
};

QString valueFor(const QByteArray &arg, const char *prefix) {
    const QByteArray p(prefix);
    if (!arg.startsWith(p)) {
        return QString();
    }
    return QString::fromLocal8Bit(arg.mid(p.size()));
}

BundleArgs parseArgs(int argc, char **argv) {
    BundleArgs args;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--bundle" || arg.startsWith("--bundle=")) {
            args.present = true;
            continue;
        }
        if (arg == "--bundle") {
            continue;
        }
        const QString out = valueFor(arg, "--out=");
        if (!out.isEmpty()) {
            args.out = out;
            continue;
        }
        const QString inspect = valueFor(arg, "--inspect=");
        if (!inspect.isEmpty()) {
            args.inspect = inspect;
            continue;
        }
        const QString samples = valueFor(arg, "--samples=");
        if (!samples.isEmpty()) {
            bool ok = false;
            const int count = samples.toInt(&ok);
            if (ok && count >= 2 && count <= 3600) {
                args.samples = count;
            }
        }
    }
    return args;
}

int inspect(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "syspeek: cannot read %s\n", qUtf8Printable(path));
        return 1;
    }
    const QByteArray payload = file.readAll();
    file.close();
    QJsonObject bundle;
    QString error;
    if (!bundleDecode(payload, &bundle, &error)) {
        std::fprintf(stderr, "syspeek: %s\n", qUtf8Printable(error));
        return 1;
    }
    const QString summary = bundleSummaryText(bundle);
    const QString detail = bundleDetailText(bundle);
    std::fprintf(stdout, "%s\n\n%s\n", qUtf8Printable(summary),
                 qUtf8Printable(detail));
    return 0;
}

} // namespace

int bundleRun(int argc, char **argv) {
    const BundleArgs args = parseArgs(argc, argv);
    if (!args.inspect.isEmpty()) {
        return inspect(args.inspect);
    }

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("syspeek"));
    QCoreApplication::setOrganizationName(QStringLiteral("dev.syspeek"));

    QString configError;
    const Config config = loadConfig(defaultConfigPath(), &configError);
    const int intervalMs = qBound(200, config.sampleIntervalMs, 60000);

    std::unique_ptr<FocusedWindowProvider> provider = createFocusProvider();
    SamplerWorker worker(provider.get(), config);

    DisplaySnapshot last;
    QList<ExportRow> rows;
    rows.reserve(args.samples);
    QObject::connect(&worker, &SamplerWorker::snapshotReady,
                     [&](const DisplaySnapshot &snapshot) {
                         last = snapshot;
                         rows.append(exportRowFrom(
                             snapshot, QDateTime::currentMSecsSinceEpoch()));
                     });

    QMetaObject::invokeMethod(&worker, "start", Qt::DirectConnection);
    auto *timer = new QTimer(&app);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, &app, [&]() {
        if (rows.size() >= args.samples) {
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

    if (rows.isEmpty()) {
        std::fprintf(stderr,
                     "syspeek: nothing sampled - the bundle would be empty\n");
        return 1;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Config carries only overrides; the armed monitors are the engine
    // defaults unless a rule was edited, exactly as the HUD seeds itself.
    const QList<AlertRule> monitors =
        config.alertRules.isEmpty() ? defaultAlertRules()
                                    : config.alertRules;
    QJsonObject body = bundleBody(last, {}, rows, {}, monitors,
                                  BundleLimits{}, now);
    // Say where it came from. A bundle taken headlessly has no event stream
    // because the events are a HUD-side ring; claiming otherwise would be the
    // exact dishonesty this project keeps refusing.
    body.insert(QStringLiteral("source"), QStringLiteral("cli"));
    const QByteArray payload = bundleEncode(
        body, QString::fromLatin1(SYSPEEK_APP_VERSION));

    QString path = args.out;
    if (path.isEmpty() || path == QStringLiteral("-")) {
        if (path == QStringLiteral("-")) {
            std::fwrite(payload.constData(), 1, size_t(payload.size()), stdout);
            std::fflush(stdout);
            return 0;
        }
        QString dir =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (dir.isEmpty()) {
            dir = QDir::homePath();
        }
        QDir().mkpath(dir);
        path = dir + QLatin1Char('/') + bundleFileName(now);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "syspeek: cannot write %s\n", qUtf8Printable(path));
        return 1;
    }
    file.write(payload);
    file.close();

    std::fprintf(stdout, "bundle: %d sample(s) -> %s\n", int(rows.size()),
                 qUtf8Printable(path));
    std::fprintf(stdout,
                 "open it with: syspeek bundle --inspect=%s\n",
                 qUtf8Printable(path));
    return 0;
}
