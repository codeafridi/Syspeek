#include "CloudPricing.h"

#include "CloudProbe.h"
#include "Config.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopedPointer>
#include <QStandardPaths>
#include <QStringList>

#include <cstdio>
#include <cmath>

namespace {

struct PriceArgs {
    bool present = false;
    QString instanceType;
    QString region = QStringLiteral("us-east-1");
    QString ratesPath;
};

PriceArgs parsePriceArgs(int argc, char **argv) {
    PriceArgs args;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--cloud-prices") {
            args.present = true;
            continue;
        }
        if (arg.startsWith("--cloud-prices=")) {
            args.present = true;
            args.instanceType =
                QString::fromLocal8Bit(arg.mid(int(sizeof("--cloud-prices=")) - 1));
            continue;
        }
        if (arg.startsWith("--instance-type=")) {
            args.instanceType =
                QString::fromLocal8Bit(arg.mid(int(sizeof("--instance-type=")) - 1));
            continue;
        }
        if (arg.startsWith("--region=")) {
            args.region =
                QString::fromLocal8Bit(arg.mid(int(sizeof("--region=")) - 1));
            continue;
        }
        if (arg.startsWith("--rates=")) {
            args.ratesPath =
                QString::fromLocal8Bit(arg.mid(int(sizeof("--rates=")) - 1));
            continue;
        }
    }
    return args;
}

QString formatRate(double value) {
    return QString::number(value, 'f', 4);
}

}  // namespace

QString awsPriceLocation(const QString &region) {
    static const QList<QPair<QString, QString>> known = {
        {QStringLiteral("us-east-1"), QStringLiteral("US East (N. Virginia)")},
        {QStringLiteral("us-east-2"), QStringLiteral("US East (Ohio)")},
        {QStringLiteral("us-west-1"), QStringLiteral("US West (N. California)")},
        {QStringLiteral("us-west-2"), QStringLiteral("US West (Oregon)")},
        {QStringLiteral("eu-west-1"), QStringLiteral("EU (Ireland)")},
        {QStringLiteral("eu-west-2"), QStringLiteral("EU (London)")},
        {QStringLiteral("eu-west-3"), QStringLiteral("EU (Paris)")},
        {QStringLiteral("eu-central-1"), QStringLiteral("EU (Frankfurt)")},
        {QStringLiteral("eu-north-1"), QStringLiteral("EU (Stockholm)")},
        {QStringLiteral("eu-south-1"), QStringLiteral("EU (Milan)")},
        {QStringLiteral("ap-south-1"), QStringLiteral("Asia Pacific (Mumbai)")},
        {QStringLiteral("ap-southeast-1"),
         QStringLiteral("Asia Pacific (Singapore)")},
        {QStringLiteral("ap-southeast-2"), QStringLiteral("Asia Pacific (Sydney)")},
        {QStringLiteral("ap-northeast-1"), QStringLiteral("Asia Pacific (Tokyo)")},
        {QStringLiteral("ap-northeast-2"), QStringLiteral("Asia Pacific (Seoul)")},
        {QStringLiteral("ap-northeast-3"), QStringLiteral("Asia Pacific (Osaka)")},
        {QStringLiteral("sa-east-1"), QStringLiteral("South America (Sao Paulo)")},
        {QStringLiteral("ca-central-1"), QStringLiteral("Canada (Central)")},
        {QStringLiteral("me-south-1"), QStringLiteral("Middle East (Bahrain)")},
        {QStringLiteral("af-south-1"), QStringLiteral("Africa (Cape Town)")},
    };
    const QString key = region.trimmed();
    for (const auto &entry : known) {
        if (entry.first == key) {
            return entry.second;
        }
    }
    return key;
}

QStringList awsPricingArguments(const AwsPriceQuery &query) {
    const QString location =
        query.location.isEmpty() ? awsPriceLocation(query.region) : query.location;
    QStringList args;
    args << QStringLiteral("pricing")
         << QStringLiteral("get-products")
         // The Price List Query API only exists in us-east-1 and ap-south-1;
         // every other region is one of its filters, not its endpoint.
         << QStringLiteral("--region") << QStringLiteral("us-east-1")
         << QStringLiteral("--service-code") << QStringLiteral("AmazonEC2")
         << QStringLiteral("--format-version") << QStringLiteral("aws_v1")
         << QStringLiteral("--max-results") << QStringLiteral("100");
    const QList<QPair<QString, QString>> filters = {
        {QStringLiteral("instanceType"), query.instanceType},
        {QStringLiteral("location"), location},
        {QStringLiteral("operatingSystem"), query.operatingSystem},
        {QStringLiteral("tenancy"), query.tenancy},
        {QStringLiteral("preInstalledSw"), query.preInstalledSw},
        {QStringLiteral("capacitystatus"), query.capacityStatus},
    };
    for (const auto &filter : filters) {
        if (filter.second.isEmpty()) {
            continue;
        }
        args << QStringLiteral("--filters")
             << QStringLiteral("Type=TERM_MATCH,Field=%1,Value=%2")
                    .arg(filter.first, filter.second);
    }
    return args;
}

double parseAwsPricingProducts(const QByteArray &json, QString *error) {
    const auto fail = [error](const QString &reason) {
        if (error != nullptr) {
            *error = reason;
        }
        return -1.0;
    };
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("aws pricing: response is not valid JSON"));
    }
    const QJsonArray list =
        document.object().value(QLatin1String("PriceList")).toArray();
    if (list.isEmpty()) {
        return fail(QStringLiteral("aws pricing: no products matched"));
    }
    double best = -1.0;
    int matched = 0;
    for (const QJsonValue &entry : list) {
        // PriceList members are JSON documents encoded as strings, which is the
        // exact shape the query API returns.
        QJsonDocument product;
        if (entry.isString()) {
            product = QJsonDocument::fromJson(entry.toString().toUtf8());
        } else if (entry.isObject()) {
            product = QJsonDocument::fromJson(
                QJsonDocument(entry.toObject()).toJson(QJsonDocument::Compact));
        }
        if (!product.isObject()) {
            continue;
        }
        const QJsonObject root = product.object();
        const QJsonObject attributes =
            root.value(QLatin1String("product")).toObject()
                .value(QLatin1String("attributes")).toObject();
        if (!attributes.isEmpty()) {
            if (!attributes.value(QLatin1String("capacitystatus")).toString().isEmpty()
                && attributes.value(QLatin1String("capacitystatus")).toString()
                    != QLatin1String("Used")) {
                continue;
            }
            if (!attributes.value(QLatin1String("preInstalledSw")).toString().isEmpty()
                && attributes.value(QLatin1String("preInstalledSw")).toString()
                    != QLatin1String("NA")) {
                continue;
            }
            if (attributes.value(QLatin1String("tenancy")).toString()
                == QLatin1String("Dedicated")) {
                continue;
            }
        }
        const QJsonObject onDemand =
            root.value(QLatin1String("terms")).toObject()
                .value(QLatin1String("OnDemand")).toObject();
        for (auto termIt = onDemand.begin(); termIt != onDemand.end(); ++termIt) {
            const QJsonObject dimensions =
                termIt.value().toObject()
                    .value(QLatin1String("priceDimensions")).toObject();
            for (auto dimIt = dimensions.begin(); dimIt != dimensions.end();
                 ++dimIt) {
                const QJsonObject dimension = dimIt.value().toObject();
                if (dimension.value(QLatin1String("unit")).toString()
                    != QLatin1String("Hrs")) {
                    continue;
                }
                const QJsonValue usd = dimension.value(QLatin1String("pricePerUnit"))
                                           .toObject()
                                           .value(QLatin1String("USD"));
                bool ok = false;
                const double value = usd.toString().toDouble(&ok);
                if (!ok || !std::isfinite(value) || value <= 0.0) {
                    continue;
                }
                ++matched;
                // Several dimensions can answer (shared/dedicated, used/unused
                // capacity); the cheapest on-demand hourly figure is the one a
                // running instance is billed at.
                if (best < 0.0 || value < best) {
                    best = value;
                }
            }
        }
    }
    if (matched == 0 || best < 0.0) {
        return fail(QStringLiteral(
            "aws pricing: no on-demand hourly price in the response"));
    }
    if (error != nullptr) {
        error->clear();
    }
    return best;
}

int cloudPriceRun(int argc, char **argv) {
    const PriceArgs args = parsePriceArgs(argc, argv);
    QScopedPointer<QCoreApplication> app;
    if (QCoreApplication::instance() == nullptr) {
        int fakeArgc = 1;
        char fakeName[] = "syspeek";
        char *fakeArgv[] = {fakeName, nullptr};
        app.reset(new QCoreApplication(fakeArgc, fakeArgv));
    }

    QString configError;
    const Config config = loadConfig(defaultConfigPath(), &configError);
    QString ratesPath = args.ratesPath;
    if (ratesPath.isEmpty()) {
        ratesPath = config.cloud.ratesPath;
    }
    if (ratesPath.isEmpty()) {
        ratesPath = defaultCloudRatePath();
    }

    QString instanceType = args.instanceType.trimmed();
    QString region = args.region.trimmed();
    if (instanceType.isEmpty() || region.isEmpty()) {
        CloudProbeOptions options;
        options.timeoutMs = 1500;
        const CloudProbeResult probe = probeCloudIdentity(options);
        if (probe.identity.provider == CloudProvider::None) {
            std::fprintf(stderr,
                         "syspeek: --cloud-prices needs an AWS instance type\n"
                         "  %s\n"
                         "  pass --cloud-prices=<type> --region=<region> "
                         "when IMDS is not reachable\n",
                         qUtf8Printable(probe.error.isEmpty()
                                            ? cloudUnreachableText()
                                            : probe.error));
            return 1;
        }
        if (probe.identity.provider != CloudProvider::Aws) {
            std::fprintf(stderr,
                         "syspeek: --cloud-prices imports AWS on-demand list "
                         "prices only; this host reports %s\n"
                         "  write the rate by hand instead: %s = <usd per hour>\n",
                         qUtf8Printable(cloudProviderLabel(probe.identity.provider)),
                         qUtf8Printable(cloudRateKey(probe.identity.provider,
                                                     probe.identity.instanceType)));
            return 1;
        }
        if (instanceType.isEmpty()) {
            instanceType = probe.identity.instanceType;
        }
        if (region.isEmpty()) {
            region = probe.identity.region;
        }
    }
    if (instanceType.isEmpty()) {
        std::fprintf(stderr, "syspeek: --cloud-prices needs an instance type\n");
        return 1;
    }
    if (region.isEmpty()) {
        region = QStringLiteral("us-east-1");
    }

    const QString aws = QStandardPaths::findExecutable(QStringLiteral("aws"));
    if (aws.isEmpty()) {
        std::fprintf(stderr,
                     "syspeek: the `aws` CLI is not on PATH - install it "
                     "(https://docs.aws.amazon.com/cli/) and run again, or add "
                     "the rate by hand:\n  aws/%s = <usd per hour>\n",
                     qUtf8Printable(instanceType));
        return 1;
    }

    AwsPriceQuery query;
    query.instanceType = instanceType;
    query.region = region;
    QProcess process;
    process.setProgram(aws);
    process.setArguments(awsPricingArguments(query));
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        std::fprintf(stderr, "syspeek: could not start the aws CLI (%s)\n",
                     qUtf8Printable(process.errorString()));
        return 1;
    }
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(2000);
        std::fprintf(stderr, "syspeek: aws pricing did not answer in 60s\n");
        return 1;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText =
            QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        std::fprintf(stderr, "syspeek: aws pricing failed (exit %d)\n  %s\n",
                     process.exitCode(),
                     qUtf8Printable(stderrText.isEmpty()
                                        ? QStringLiteral("no error output")
                                        : stderrText.left(400)));
        return 1;
    }

    QString parseError;
    const double rate = parseAwsPricingProducts(process.readAllStandardOutput(),
                                                &parseError);
    if (rate < 0.0) {
        std::fprintf(stderr, "syspeek: %s\n", qUtf8Printable(parseError));
        return 1;
    }

    CloudRateTable table;
    QFile existing(ratesPath);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "syspeek: cannot read %s (%s)\n",
                         qUtf8Printable(ratesPath),
                         qUtf8Printable(existing.errorString()));
            return 1;
        }
        table = parseCloudRateFile(QString::fromUtf8(existing.readAll()),
                                   ratesPath);
        existing.close();
    }

    const QString key = cloudRateKey(CloudProvider::Aws, instanceType);
    bool replaced = false;
    for (CloudRate &entry : table.rates) {
        if (entry.key.compare(key, Qt::CaseInsensitive) == 0) {
            entry.usdPerHour = rate;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        table.rates.append(CloudRate{key, rate});
    }
    table.sourcePath = ratesPath;
    table.importDate = QDate::currentDate().toString(Qt::ISODate);
    table.loaded = true;

    const QFileInfo info(ratesPath);
    if (!info.absolutePath().isEmpty()) {
        QDir().mkpath(info.absolutePath());
    }
    QFile out(ratesPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "syspeek: cannot write %s (%s)\n",
                     qUtf8Printable(ratesPath),
                     qUtf8Printable(out.errorString()));
        return 1;
    }
    out.write(cloudRateFileText(table,
                                QDateTime::currentMSecsSinceEpoch())
                  .toUtf8());
    out.close();

    std::printf("cloud prices: %s = %s/hr (%s, %s) -> %s\n",
                qUtf8Printable(key), qUtf8Printable(formatRate(rate)),
                qUtf8Printable(query.operatingSystem),
                qUtf8Printable(awsPriceLocation(query.region)),
                qUtf8Printable(ratesPath));
    return 0;
}
