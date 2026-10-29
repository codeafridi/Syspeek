#include "QtCompat.h"
#include "CloudStatus.h"

#include "CostModel.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace {

QString trimmedValue(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toString().trimmed();
}

QString orDash(const QString &value) {
    return value.isEmpty() ? QStringLiteral("--") : value;
}

QString firstLine(const QByteArray &body) {
    const QString text = QString::fromUtf8(body).trimmed();
    return text.section(QLatin1Char('\n'), 0, 0).trimmed();
}

}  // namespace

QString cloudProviderName(CloudProvider provider) {
    switch (provider) {
    case CloudProvider::Aws:
        return QStringLiteral("aws");
    case CloudProvider::Azure:
        return QStringLiteral("azure");
    case CloudProvider::Gcp:
        return QStringLiteral("gcp");
    case CloudProvider::Oci:
        return QStringLiteral("oci");
    case CloudProvider::None:
        break;
    }
    return QStringLiteral("-");
}

QString cloudProviderLabel(CloudProvider provider) {
    switch (provider) {
    case CloudProvider::Aws:
        return QStringLiteral("AWS");
    case CloudProvider::Azure:
        return QStringLiteral("Azure");
    case CloudProvider::Gcp:
        return QStringLiteral("Google Cloud");
    case CloudProvider::Oci:
        return QStringLiteral("OCI");
    case CloudProvider::None:
        break;
    }
    return QStringLiteral("--");
}

// --- IMDS parsers ---------------------------------------------------------

CloudIdentity parseAwsIdentityDocument(const QByteArray &body) {
    CloudIdentity identity;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        identity.note = QStringLiteral(
            "aws: instance identity document is not valid JSON");
        return identity;
    }
    const QJsonObject object = document.object();
    identity.instanceId = trimmedValue(object, "instanceId");
    if (identity.instanceId.isEmpty()) {
        identity.note = QStringLiteral(
            "aws: identity document has no instanceId");
        return identity;
    }
    identity.provider = CloudProvider::Aws;
    identity.instanceType = trimmedValue(object, "instanceType");
    identity.region = trimmedValue(object, "region");
    identity.zone = trimmedValue(object, "availabilityZone");
    identity.accountId = trimmedValue(object, "accountId");
    identity.privateIp = trimmedValue(object, "privateIp");
    identity.imageId = trimmedValue(object, "imageId");
    identity.architecture = trimmedValue(object, "architecture");
    identity.operatingSystem = QStringLiteral("linux");
    identity.marketplace =
        !object.value(QLatin1String("marketplaceProductCodes")).toArray().isEmpty();
    // A Spot instance reports a billing product code; the identity document
    // carries no price, so the rate file decides the dollars either way.
    const QJsonArray billing =
        object.value(QLatin1String("billingProducts")).toArray();
    for (const QJsonValue &value : billing) {
        const QString code = value.toString().toLower();
        if (code.contains(QLatin1String("spot"))) {
            identity.spot = true;
        }
    }
    return identity;
}

CloudIdentity parseAzureCompute(const QByteArray &body) {
    CloudIdentity identity;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        identity.note = QStringLiteral("azure: compute metadata is not valid JSON");
        return identity;
    }
    // The documented API-version responses nest under "compute"; older
    // api-versions return the same members at the top level.
    QJsonObject object = document.object();
    if (object.value(QLatin1String("compute")).isObject()) {
        object = object.value(QLatin1String("compute")).toObject();
    }
    identity.instanceId = trimmedValue(object, "vmId");
    if (identity.instanceId.isEmpty()) {
        identity.note = QStringLiteral("azure: compute metadata has no vmId");
        return identity;
    }
    identity.provider = CloudProvider::Azure;
    identity.instanceName = trimmedValue(object, "name");
    identity.instanceType = trimmedValue(object, "vmSize");
    identity.region = trimmedValue(object, "location");
    identity.zone = trimmedValue(object, "zone");
    identity.accountId = trimmedValue(object, "subscriptionId");
    identity.imageId = trimmedValue(object, "imageReference");
    identity.operatingSystem = trimmedValue(object, "osType").toLower();
    if (!identity.zone.isEmpty()) {
        identity.zone =
            QStringLiteral("%1-%2").arg(identity.region, identity.zone);
    }
    if (identity.zone == identity.region) {
        identity.zone.clear();
    }
    return identity;
}

QString cloudMachineTypeTail(const QString &path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    return trimmed.section(QLatin1Char('/'), -1);
}

QString cloudRegionFromZone(const QString &zone) {
    const QString trimmed = zone.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    const QString tail = trimmed.section(QLatin1Char('/'), -1);
    // "us-central1-a" -> "us-central1": the zone letter is its own dash group.
    const int dash = tail.lastIndexOf(QLatin1Char('-'));
    if (dash > 0 && tail.size() - dash - 1 == 1) {
        return tail.left(dash);
    }
    // An AWS availability zone is the region with a letter appended
    // ("eu-west-1a"), and a local zone appends another dash group
    // ("us-west-2-lax-1a"), so the region is the leading
    // <area>-<place>-<number> prefix rather than "everything but the letter".
    static const QRegularExpression awsRegion(
        QStringLiteral("^([a-z]{2}-[a-z]+-\\d+)"));
    const QRegularExpressionMatch match = awsRegion.match(tail);
    if (match.hasMatch()) {
        return match.captured(1);
    }
    return tail;
}

CloudIdentity parseGcpInstance(const GcpMetadataFields &fields) {
    CloudIdentity identity;
    const QString id = firstLine(fields.id);
    const QString machine = cloudMachineTypeTail(QString::fromUtf8(fields.machineType));
    const QString zonePath = QString::fromUtf8(fields.zone).trimmed();
    if (id.isEmpty() && machine.isEmpty()) {
        identity.note = QStringLiteral("gcp: metadata has no instance id");
        return identity;
    }
    identity.provider = CloudProvider::Gcp;
    identity.instanceId = id;
    identity.instanceName = firstLine(fields.name);
    identity.instanceType = machine;
    identity.zone = zonePath.section(QLatin1Char('/'), -1);
    identity.region = cloudRegionFromZone(zonePath);
    identity.accountId = firstLine(fields.project);
    identity.operatingSystem = QStringLiteral("linux");
    // The metadata service returns projects/<num>/zones/<zone>/machineTypes/<t>;
    // anything else means the endpoint answered with something we do not
    // understand, which the agent's own parser rejects the same way.
    const QString raw = QString::fromUtf8(fields.machineType).trimmed();
    if (!raw.isEmpty() && !raw.contains(QLatin1String("/machineTypes/"))) {
        identity.note = QStringLiteral(
            "gcp: machine-type is not "
            "projects/PROJECT_NUM/zones/ZONE/machineTypes/MACHINE_TYPE");
    }
    return identity;
}

CloudIdentity parseOciInstance(const QByteArray &body) {
    CloudIdentity identity;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        identity.note = QStringLiteral("oci: instance metadata is not valid JSON");
        return identity;
    }
    const QJsonObject object = document.object();
    identity.instanceId = trimmedValue(object, "id");
    if (identity.instanceId.isEmpty()) {
        identity.note = QStringLiteral("oci: Oracle returned empty id");
        return identity;
    }
    identity.provider = CloudProvider::Oci;
    identity.instanceName = trimmedValue(object, "displayName");
    identity.instanceType = trimmedValue(object, "shape");
    identity.region = trimmedValue(object, "canonicalRegionName");
    if (identity.region.isEmpty()) {
        identity.region = trimmedValue(object, "region");
    }
    identity.zone = trimmedValue(object, "availabilityDomain");
    identity.accountId = trimmedValue(object, "compartmentId");
    identity.imageId = trimmedValue(object, "image");
    identity.operatingSystem = QStringLiteral("linux");
    return identity;
}

// --- rate table -----------------------------------------------------------

QString cloudRateKey(CloudProvider provider, const QString &instanceType) {
    const QString type = instanceType.trimmed();
    if (provider == CloudProvider::None || type.isEmpty()) {
        return QString();
    }
    return cloudProviderName(provider) + QLatin1Char('/') + type;
}

CloudRateTable parseCloudRateFile(const QString &text, const QString &sourcePath) {
    CloudRateTable table;
    table.sourcePath = sourcePath;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('#'))) {
            const QString comment = line.mid(1).trimmed();
            if (comment.startsWith(QLatin1String("imported"))) {
                const QString stamp = comment.mid(8).trimmed();
                if (!stamp.isEmpty()) {
                    table.importDate = stamp;
                }
            }
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            continue;
        }
        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        // Allow a trailing comment after the number.
        const int hash = value.indexOf(QLatin1Char('#'));
        if (hash >= 0) {
            value = value.left(hash).trimmed();
        }
        bool ok = false;
        const double rate = value.toDouble(&ok);
        if (!ok || !std::isfinite(rate) || rate < 0.0 || key.isEmpty()) {
            continue;
        }
        bool replaced = false;
        for (CloudRate &existing : table.rates) {
            if (existing.key.compare(key, Qt::CaseInsensitive) == 0) {
                existing.usdPerHour = rate;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            table.rates.append(CloudRate{key, rate});
        }
    }
    table.loaded = true;
    return table;
}

double cloudRateFor(const CloudRateTable &table, CloudProvider provider,
                    const QString &instanceType) {
    const QString key = cloudRateKey(provider, instanceType);
    if (key.isEmpty()) {
        return -1.0;
    }
    for (const CloudRate &rate : table.rates) {
        if (rate.key.compare(key, Qt::CaseInsensitive) == 0) {
            return rate.usdPerHour;
        }
    }
    // The instance type alone is unambiguous enough when exactly one provider
    // lists it, which keeps a hand-written file usable without prefixes.
    for (const CloudRate &rate : table.rates) {
        if (rate.key.compare(instanceType, Qt::CaseInsensitive) == 0) {
            return rate.usdPerHour;
        }
    }
    return -1.0;
}

QString cloudRatesTemplateText() {
    return QStringLiteral(
        "# Syspeek cloud rate table.\n"
        "# <provider>/<instanceType> = <usd per hour>, e.g.\n"
        "#   aws/c7g.2xlarge = 0.2920\n"
        "#   azure/Standard_D2s_v3 = 0.0960\n"
        "#   gcp/n2-standard-4 = 0.1901\n"
        "#   oci/VM.Standard.E4.Flex = 0.0450\n"
        "#\n"
        "# Prices are never bundled or invented: fill this in from your own\n"
        "# bill, or run `syspeek --cloud-prices` to import the current\n"
        "# on-demand list price for this instance.\n");
}

int cloudRateStaleDays(const QString &importDate, qint64 nowMs) {
    const QDate date = QDate::fromString(importDate.trimmed(), Qt::ISODate);
    if (!date.isValid()) {
        return -1;
    }
    const QDate today = nowMs > 0
        ? QDateTime::fromMSecsSinceEpoch(nowMs).date()
        : QDate::currentDate();
    const qint64 days = date.daysTo(today);
    if (days < 0) {
        return 0;
    }
    return int(std::min<qint64>(days, 100000));
}

QString cloudRateFileText(const CloudRateTable &table, qint64 nowMs) {
    QStringList lines;
    lines.append(QStringLiteral("# imported %1")
                     .arg(table.importDate.isEmpty()
                              ? QDate::currentDate().toString(Qt::ISODate)
                              : table.importDate));
    for (const CloudRate &rate : table.rates) {
        lines.append(QStringLiteral("%1 = %2")
                         .arg(rate.key)
                         .arg(rate.usdPerHour, 0, 'f', 4));
    }
    const int stale = cloudRateStaleDays(table.importDate, nowMs);
    if (stale > 30) {
        lines.append(QStringLiteral("# imported prices are %1 days old - "
                                    "re-import or edit for current rates")
                         .arg(stale));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// --- cost maths -----------------------------------------------------------

CloudCostMath computeCloudCost(double ratePerHour, qint64 uptimeSec) {
    CloudCostMath math;
    if (ratePerHour < 0.0 || uptimeSec < 0) {
        return math;
    }
    math.accruedUsd = ratePerHour * double(uptimeSec) / 3600.0;
    math.projectedMonthlyUsd = ratePerHour * kCostHoursPerMonth;
    return math;
}

double cloudProcessCostPerHour(double ratePerHour, double cpuPercent,
                               int vcpus) {
    if (ratePerHour < 0.0 || vcpus <= 0 || cpuPercent <= 0.0) {
        return -1.0;
    }
    const double share = ratePerHour * (cpuPercent / 100.0) / double(vcpus);
    return std::isfinite(share) ? share : -1.0;
}

QList<CloudConsumer> cloudTopConsumers(const QList<CloudProcessSample> &rows,
                                       double ratePerHour, int vcpus,
                                       int limit) {
    QList<CloudConsumer> out;
    if (ratePerHour < 0.0 || vcpus <= 0) {
        return out;
    }
    for (const CloudProcessSample &row : rows) {
        if (row.pid <= 0 || row.cpuPercent <= 0.0) {
            continue;
        }
        CloudConsumer consumer;
        consumer.name = row.name.isEmpty() ? QString::number(row.pid)
                                           : row.name;
        consumer.pid = row.pid;
        consumer.cpuPercent = row.cpuPercent;
        consumer.usdPerHour =
            cloudProcessCostPerHour(ratePerHour, row.cpuPercent, vcpus);
        if (consumer.usdPerHour < 0.0) {
            continue;
        }
        out.append(consumer);
    }
    std::sort(out.begin(), out.end(),
              [](const CloudConsumer &a, const CloudConsumer &b) {
                  if (qFuzzyCompare(a.usdPerHour, b.usdPerHour)) {
                      return a.cpuPercent > b.cpuPercent;
                  }
                  return a.usdPerHour > b.usdPerHour;
              });
    if (limit > 0 && out.size() > limit) {
        out = out.mid(0, limit);
    }
    return out;
}

double cloudConsumerTotal(const QList<CloudConsumer> &consumers) {
    double total = 0.0;
    for (const CloudConsumer &consumer : consumers) {
        total += consumer.usdPerHour;
    }
    return total;
}

// --- text -----------------------------------------------------------------

QString cloudUptimeText(qint64 seconds) {
    if (seconds < 0) {
        return QStringLiteral("--");
    }
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (hours >= 48) {
        return QStringLiteral("%1d %2h").arg(hours / 24).arg(hours % 24);
    }
    return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
}

QString cloudUnreachableText() {
    return QStringLiteral(
        "cloud: no instance metadata service answered - this host is not on "
        "AWS/Azure/GCP/OCI, or IMDS is blocked (firewall, or IMDSv2-only with "
        "token retrieval refused)");
}

QString cloudProbingText() {
    return QStringLiteral(
        "cloud: reading the instance metadata service - the probe runs on the "
        "next tick so the first sample never waits on a link-local socket");
}

QString cloudSummaryText(const CloudSnapshot &snapshot) {
    if (!snapshot.available) {
        return snapshot.note.isEmpty() ? cloudUnreachableText() : snapshot.note;
    }
    QStringList parts;
    parts.append(cloudProviderLabel(snapshot.identity.provider));
    if (!snapshot.identity.region.isEmpty()) {
        parts.append(snapshot.identity.region);
    }
    if (!snapshot.identity.instanceType.isEmpty()) {
        parts.append(snapshot.identity.instanceType);
    }
    QString text = parts.join(QLatin1Char(' '));
    if (!snapshot.identity.instanceId.isEmpty()) {
        text += QStringLiteral(" - %1").arg(snapshot.identity.instanceId);
    }
    if (snapshot.vcpus > 0) {
        text += QStringLiteral(" - %1 vCPU").arg(snapshot.vcpus);
    }
    if (snapshot.identity.spot) {
        text += QStringLiteral(" - spot");
    }
    return text;
}

QString cloudDetailText(const CloudSnapshot &snapshot) {
    if (!snapshot.available) {
        return cloudSummaryText(snapshot);
    }
    const CloudIdentity &id = snapshot.identity;
    QStringList lines;
    lines.append(QStringLiteral("provider   %1").arg(cloudProviderLabel(id.provider)));
    lines.append(QStringLiteral("instance   %1").arg(orDash(id.instanceId)));
    if (!id.instanceName.isEmpty()) {
        lines.append(QStringLiteral("name       %1").arg(id.instanceName));
    }
    QString type = orDash(id.instanceType);
    if (snapshot.vcpus > 0) {
        type += QStringLiteral("  (%1 vCPU)").arg(snapshot.vcpus);
    }
    lines.append(QStringLiteral("type       %1").arg(type));
    QString where = orDash(id.region);
    if (!id.zone.isEmpty() && id.zone != id.region) {
        where += QStringLiteral("  %1").arg(id.zone);
    }
    lines.append(QStringLiteral("region     %1").arg(where));
    if (!id.accountId.isEmpty()) {
        lines.append(QStringLiteral("account    %1").arg(id.accountId));
    }
    lines.append(QStringLiteral("private ip %1").arg(orDash(id.privateIp)));
    if (!id.imageId.isEmpty() || !id.architecture.isEmpty()) {
        QString image = id.imageId.isEmpty() ? QStringLiteral("--") : id.imageId;
        if (!id.architecture.isEmpty()) {
            image += QStringLiteral("  (%1)").arg(id.architecture);
        }
        lines.append(QStringLiteral("image      %1").arg(image));
    }
    if (snapshot.instanceAgeSec >= 0) {
        QString age = QStringLiteral("age        %1").arg(
            cloudUptimeText(snapshot.instanceAgeSec));
        if (snapshot.launchedAtMs > 0) {
            age += QStringLiteral("  launched %1").arg(
                qtcompat::fromMSecsSinceEpochUtc(snapshot.launchedAtMs)
                    .toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'")));
        }
        lines.append(age);
    } else {
        lines.append(QStringLiteral(
            "age        --  (the provider reported no launch time for this "
            "instance)"));
    }
    if (id.marketplace) {
        lines.append(QStringLiteral("marketplace yes - rate file decides the price"));
    }
    return lines.join(QLatin1Char('\n'));
}

QString cloudCostText(const CloudSnapshot &snapshot) {
    if (!snapshot.available) {
        return cloudSummaryText(snapshot);
    }
    if (snapshot.ratePerHour < 0.0) {
        const QString key = cloudRateKey(snapshot.identity.provider,
                                         snapshot.identity.instanceType);
        return QStringLiteral(
            "rate: no price for %1 - add `%1 = <usd per hour>` to %2")
            .arg(key.isEmpty() ? QStringLiteral("this instance") : key,
                 snapshot.rateSource.isEmpty()
                     ? QStringLiteral("the cloud rate file")
                     : snapshot.rateSource);
    }
    QString line = QStringLiteral("rate       %1/hr  %2")
                       .arg(formatCostMoney(snapshot.ratePerHour),
                            snapshot.rateKey.isEmpty()
                                ? QStringLiteral("-")
                                : snapshot.rateKey);
    if (snapshot.rateStaleDays > 30) {
        line += QStringLiteral("  (imported %1, %2 days old)")
                    .arg(snapshot.rateImportDate)
                    .arg(snapshot.rateStaleDays);
    } else if (!snapshot.rateImportDate.isEmpty()) {
        line += QStringLiteral("  (imported %1)").arg(snapshot.rateImportDate);
    }
    line += QLatin1Char('\n');
    if (snapshot.launchedAtMs > 0 && snapshot.instanceAgeSec >= 0) {
        line += QStringLiteral("launched   %1  age %2")
                    .arg(qtcompat::fromMSecsSinceEpochUtc(
                                 snapshot.launchedAtMs)
                             .toString(QStringLiteral("yyyy-MM-dd")),
                         cloudUptimeText(snapshot.instanceAgeSec));
        line += QLatin1Char('\n');
        if (snapshot.accruedLaunchUsd >= 0.0) {
            // Upper bound: the provider counts from launch, so an instance
            // that was stopped for a week is overcharged here. The honest
            // "this boot" figure stays right below it.
            line += QStringLiteral("accrued    %1  since launch (upper bound)")
                        .arg(formatCostMoney(snapshot.accruedLaunchUsd));
            line += QLatin1Char('\n');
        }
    }
    line += QStringLiteral("accrued    %1  this boot (%2)")
                .arg(formatCostMoney(snapshot.accruedUsd),
                     cloudUptimeText(snapshot.uptimeSec));
    line += QLatin1Char('\n');
    line += QStringLiteral("projected  %1/month  (%2 h)")
                .arg(formatCostMoney(snapshot.projectedMonthlyUsd))
                .arg(kCostHoursPerMonth, 0, 'f', 0);
    return line;
}

QString cloudAppText(const CloudSnapshot &snapshot) {
    if (!snapshot.available) {
        return cloudSummaryText(snapshot);
    }
    if (snapshot.ratePerHour < 0.0) {
        return QStringLiteral(
            "app        no rate - the per-process share needs a price");
    }
    if (snapshot.appCpuPercent < 0.0 || snapshot.vcpus <= 0) {
        return QStringLiteral(
            "app        no focused-process cpu sample yet");
    }
    QStringList lines;
    lines.append(QStringLiteral("app        %1  (pid %2)")
                     .arg(snapshot.appName.isEmpty() ? QStringLiteral("--")
                                                    : snapshot.appName,
                          snapshot.appPid > 0
                              ? QString::number(snapshot.appPid)
                              : QStringLiteral("--")));
    lines.append(QStringLiteral("cpu        %1%  of %2 vCPU")
                     .arg(snapshot.appCpuPercent, 0, 'f', 1)
                     .arg(snapshot.vcpus));
    lines.append(QStringLiteral("share      %1/hr  %2/month at 730 h")
                     .arg(formatCostMoney(snapshot.appCostPerHour),
                          formatCostMoney(snapshot.appCostPerHour
                                          * kCostHoursPerMonth)));
    return lines.join(QLatin1Char('\n'));
}

QString cloudShareNote() {
    return QStringLiteral(
        "share = instance $/hr x app cpu% / vCPU; cpu% is per-core, so a "
        "fully busy 8 vCPU box spends the whole rate");
}

QString cloudSettingsLine(bool enabled, const QString &ratesPath,
                          const CloudRateTable &table, qint64 nowMs) {
    if (!enabled) {
        return QStringLiteral("disabled ([cloud] enabled=false)");
    }
    const QString path =
        ratesPath.isEmpty() ? QStringLiteral("no rate file") : ratesPath;
    QString line = QStringLiteral("imds 169.254.169.254 (aws/azure/gcp), "
                                 "169.254.0.23 (oci) - read only");
    if (table.rates.isEmpty()) {
        line += QStringLiteral("\nnone yet at %1 - run `syspeek "
                               "--cloud-prices` or add a rate").arg(path);
        return line;
    }
    line += QStringLiteral("\nprices %1 at %2").arg(table.rates.size()).arg(path);
    const int stale = cloudRateStaleDays(table.importDate, nowMs);
    if (!table.importDate.isEmpty()) {
        line += QStringLiteral(" - imported %1").arg(table.importDate);
        if (stale > 30) {
            line += QStringLiteral(" (%1 days old)").arg(stale);
        }
    }
    return line;
}

QString cloudFootprintText(const CloudFootprint &footprint) {
    if (!footprint.enabled) {
        return QStringLiteral(
            "disabled ([cloud] enabled=false) - no instance metadata request "
            "is ever issued and no provider CLI is ever spawned");
    }
    QStringList lines;
    lines.append(QStringLiteral(
        "metadata   169.254.169.254 (aws/azure/gcp) + 169.254.0.23 (oci), "
        "link-local only, read only"));
    lines.append(QStringLiteral(
        "cadence    every 5 min while an instance is identified, every 15 min "
        "otherwise"));
    QString budget =
        QStringLiteral("budget     %1 ms for the whole sweep").arg(footprint.timeoutMs);
    if (footprint.lastSweepMs >= 0) {
        budget += QStringLiteral(", last %1 ms").arg(footprint.lastSweepMs);
        if (footprint.lastRequests > 0) {
            budget += QStringLiteral(" / %1 request(s)").arg(footprint.lastRequests);
        }
    } else if (footprint.probed) {
        budget += QStringLiteral(", last sweep not timed");
    } else {
        budget += QStringLiteral(", the first sweep starts on the second tick");
    }
    lines.append(budget);
    if (footprint.inventory) {
        lines.append(
            QStringLiteral("inventory  %1 every 10 min, %2 ms budget - the "
                           "CLI's own login, no credential is read")
                .arg(footprint.cliName.isEmpty() ? QStringLiteral("provider cli")
                                                 : footprint.cliName)
                .arg(footprint.inventoryTimeoutMs));
    } else {
        lines.append(QStringLiteral(
            "inventory  off ([cloud] inventory=false) - no provider CLI is "
            "spawned"));
    }
    lines.append(QStringLiteral(
        "prices     on demand only: `syspeek --cloud-prices` asks the AWS "
        "Price List API with your aws(1) credentials, never from a tick"));
    lines.append(QStringLiteral("rates      %1").arg(
        footprint.ratesPath.isEmpty() ? QStringLiteral("no rate file")
                                      : footprint.ratesPath));
    lines.append(QStringLiteral(
        "writes     none - no credential is read or stored, no instance is "
        "ever started, stopped or terminated"));
    return lines.join(QLatin1Char('\n'));
}
