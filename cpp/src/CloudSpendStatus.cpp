#include "CloudSpendStatus.h"

#include "CloudStatus.h"
#include "QtCompat.h"

#include "CostModel.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>

namespace {

constexpr const char *kUnblended = "UnblendedCost";

double jsonAmount(const QJsonObject &metrics, const char *metric) {
    const QJsonObject node = metrics.value(QLatin1String(metric)).toObject();
    const QJsonValue amount = node.value(QStringLiteral("Amount"));
    if (amount.isString()) {
        bool ok = false;
        const double value = amount.toString().toDouble(&ok);
        return ok ? value : -1.0;
    }
    return amount.isDouble() ? amount.toDouble() : -1.0;
}

void addService(CloudSpendStatus *spend, const QString &name, double usd) {
    for (CloudSpendService &row : spend->services) {
        if (row.name.compare(name, Qt::CaseInsensitive) == 0) {
            row.usd += usd;
            return;
        }
    }
    spend->services.append(CloudSpendService{name, usd});
}

} // namespace

CloudSpendStatus parseAwsCostAndUsage(const QByteArray &json, QString *error) {
    CloudSpendStatus spend;
    spend.provider = CloudProvider::Aws;
    spend.cliName = QStringLiteral("aws");
    spend.attempted = true;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("cost payload is not JSON (%1)")
                         .arg(parseError.errorString());
        }
        return spend;
    }
    const QJsonObject root = doc.object();
    const QJsonArray results =
        root.value(QStringLiteral("ResultsByTime")).toArray();
    if (results.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "cost payload carries no ResultsByTime (the account may have "
                "no usage this month)");
        }
        return spend;
    }

    double total = 0.0;
    for (const QJsonValue &entry : results) {
        const QJsonObject row = entry.toObject();
        const QJsonObject period =
            row.value(QStringLiteral("TimePeriod")).toObject();
        const QString start = period.value(QStringLiteral("Start")).toString();
        double dayTotal = 0.0;
        const QJsonArray groups = row.value(QStringLiteral("Groups")).toArray();
        if (groups.isEmpty()) {
            const double amount = jsonAmount(
                row.value(QStringLiteral("Total")).toObject(), kUnblended);
            if (amount >= 0.0) {
                dayTotal += amount;
                addService(&spend, QStringLiteral("(unclassified)"), amount);
            }
        }
        for (const QJsonValue &groupValue : groups) {
            const QJsonObject group = groupValue.toObject();
            const QString name =
                group.value(QStringLiteral("Keys")).toArray().isEmpty()
                    ? QStringLiteral("(unclassified)")
                    : group.value(QStringLiteral("Keys"))
                          .toArray()
                          .first()
                          .toString();
            const double amount = jsonAmount(
                group.value(QStringLiteral("Metrics")).toObject(), kUnblended);
            if (amount < 0.0) {
                continue;
            }
            dayTotal += amount;
            addService(&spend, name, amount);
        }
        if (row.value(QStringLiteral("Estimated")).toBool()) {
            spend.estimated = true;
        }
        if (spend.periodStart.isEmpty() && !start.isEmpty()) {
            spend.periodStart = start;
        }
        if (!start.isEmpty()) {
            spend.periodEnd = period.value(QStringLiteral("End")).toString();
            spend.days.append(CloudSpendDay{start, dayTotal});
        }
        total += dayTotal;
    }

    spend.monthToDateUsd = total;
    spend.available = true;
    spendSortServices(&spend);
    return spend;
}

CloudSpendStatus parseAwsCostForecast(const QByteArray &json, QString *error) {
    CloudSpendStatus spend;
    spend.provider = CloudProvider::Aws;
    spend.cliName = QStringLiteral("aws");

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("forecast payload is not JSON (%1)")
                         .arg(parseError.errorString());
        }
        return spend;
    }
    const QJsonObject total =
        doc.object().value(QStringLiteral("Total")).toObject();
    const QJsonValue amount = total.value(QStringLiteral("Amount"));
    double value = -1.0;
    if (amount.isString()) {
        bool ok = false;
        const double parsed = amount.toString().toDouble(&ok);
        value = ok ? parsed : -1.0;
    } else if (amount.isDouble()) {
        value = amount.toDouble();
    }
    if (value < 0.0) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "forecast payload carries no Total.Amount");
        }
        return spend;
    }
    spend.forecastRemainingUsd = value;
    spend.available = true;
    return spend;
}

CloudSpendStatus parseAzureUsageList(const QByteArray &json, QString *error) {
    CloudSpendStatus spend;
    spend.provider = CloudProvider::Azure;
    spend.cliName = QStringLiteral("az");
    spend.attempted = true;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("usage payload is not JSON (%1)")
                         .arg(parseError.errorString());
        }
        return spend;
    }
    // `az consumption usage list -o json` prints the array; the ARM wrapper
    // (`{"value": [...]}`) is accepted too, since some CLI versions page it.
    QJsonArray rows = doc.isArray() ? doc.array()
                                    : doc.object()
                                          .value(QStringLiteral("value"))
                                          .toArray();
    if (rows.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "usage payload carries no rows (no usage this month, or the "
                "subscription has no cost data)");
        }
        return spend;
    }

    spend.truncated = rows.size() >= 1000;
    double total = 0.0;
    for (const QJsonValue &entry : rows) {
        const QJsonObject row = entry.toObject();
        const QJsonObject properties =
            row.value(QStringLiteral("properties")).toObject();
        const QJsonValue cost =
            properties.value(QStringLiteral("pretaxCost"));
        double amount = -1.0;
        if (cost.isDouble()) {
            amount = cost.toDouble();
        } else if (cost.isString()) {
            bool ok = false;
            const double parsed = cost.toString().toDouble(&ok);
            amount = ok ? parsed : -1.0;
        }
        if (amount < 0.0) {
            continue;
        }
        const QString name =
            properties.value(QStringLiteral("product")).toString().isEmpty()
                ? QStringLiteral("(unclassified)")
                : properties.value(QStringLiteral("product")).toString();
        addService(&spend, name, amount);
        const QString start =
            properties.value(QStringLiteral("usageStart")).toString();
        const QString day = start.left(10);
        bool merged = false;
        for (CloudSpendDay &existing : spend.days) {
            if (existing.date == day) {
                existing.usd += amount;
                merged = true;
                break;
            }
        }
        if (!merged && !day.isEmpty()) {
            spend.days.append(CloudSpendDay{day, amount});
        }
        if (properties.value(QStringLiteral("isEstimated")).toBool()) {
            spend.estimated = true;
        }
        if (spend.periodStart.isEmpty() && !start.isEmpty()) {
            spend.periodStart = day;
        }
        total += amount;
    }

    std::sort(spend.days.begin(), spend.days.end(),
              [](const CloudSpendDay &a, const CloudSpendDay &b) {
                  return a.date < b.date;
              });
    spend.monthToDateUsd = total;
    spend.available = true;
    spendSortServices(&spend);
    return spend;
}

void spendApplyForecast(CloudSpendStatus *spend, double remainingUsd) {
    if (spend == nullptr) {
        return;
    }
    spend->forecastRemainingUsd = remainingUsd;
    if (spend->monthToDateUsd >= 0.0 && remainingUsd >= 0.0) {
        spend->forecastUsd = spend->monthToDateUsd + remainingUsd;
    }
}

void spendSortServices(CloudSpendStatus *spend, int cap) {
    if (spend == nullptr) {
        return;
    }
    std::sort(spend->services.begin(), spend->services.end(),
              [](const CloudSpendService &a, const CloudSpendService &b) {
                  return a.usd > b.usd;
              });
    if (cap > 0 && spend->services.size() > cap) {
        const int dropped = spend->services.size() - cap;
        spend->services = spend->services.mid(0, cap);
        spend->services.append(
            CloudSpendService{QStringLiteral("(%1 more services)").arg(dropped),
                              -1.0});
    }
}

QString cloudSpendAmountText(double usd) {
    if (usd < 0.0) {
        return QStringLiteral("--");
    }
    if (usd > 0.0 && usd < 0.01) {
        return QStringLiteral("<$0.01");
    }
    return formatCostMoney(usd);
}

QString cloudSpendSummaryText(const CloudSpendStatus &spend) {
    if (!spend.available) {
        return spend.note.isEmpty() ? cloudSpendGateText(spend.provider)
                                    : spend.note;
    }
    QString line =
        QStringLiteral("%1  month to date %2  month end %3")
            .arg(cloudProviderLabel(spend.provider),
                 cloudSpendAmountText(spend.monthToDateUsd),
                 spend.forecastUsd >= 0.0
                     ? cloudSpendAmountText(spend.forecastUsd)
                     : QStringLiteral("--"));
    if (spend.forecastUsd >= 0.0 && spend.forecastRemainingUsd >= 0.0) {
        line += QStringLiteral(" (+%1 forecast)")
                    .arg(cloudSpendAmountText(spend.forecastRemainingUsd));
    }
    if (spend.estimated) {
        line += QStringLiteral("  (provider marks part of this as estimated)");
    }
    if (spend.truncated) {
        line += QStringLiteral("  (first 1000 usage rows - the total is a floor)");
    }
    if (!spend.scope.isEmpty()) {
        line += QStringLiteral("\nscope %1").arg(spend.scope);
    }
    if (!spend.source.isEmpty()) {
        line += QStringLiteral("\nsource %1").arg(spend.source);
    }
    return line;
}

QString cloudSpendDetailText(const CloudSpendStatus &spend) {
    if (!spend.available) {
        return spend.note;
    }
    QStringList lines;
    lines.append(QStringLiteral("period     %1 .. %2")
                     .arg(spend.periodStart.isEmpty()
                              ? QStringLiteral("--")
                              : spend.periodStart,
                          spend.periodEnd.isEmpty()
                              ? QStringLiteral("--")
                              : spend.periodEnd));
    lines.append(QStringLiteral("billed     %1  (%2 metric)")
                     .arg(cloudSpendAmountText(spend.monthToDateUsd),
                          spend.provider == CloudProvider::Azure
                              ? QStringLiteral("pretax cost")
                              : QStringLiteral("unblended cost")));
    lines.append(
        QStringLiteral("rest       %1  forecast for the remaining days")
            .arg(spend.forecastRemainingUsd >= 0.0
                     ? cloudSpendAmountText(spend.forecastRemainingUsd)
                     : QStringLiteral("--")));
    lines.append(QStringLiteral("month end  %1  billed + forecast")
                     .arg(spend.forecastUsd >= 0.0
                              ? cloudSpendAmountText(spend.forecastUsd)
                              : QStringLiteral("--")));
    lines.append(QStringLiteral("days       %1 day(s) with cost data")
                     .arg(spend.days.size()));
    return lines.join(QLatin1Char('\n'));
}

QString cloudSpendServicesText(const CloudSpendStatus &spend) {
    if (!spend.available) {
        return spend.note;
    }
    if (spend.services.isEmpty()) {
        return QStringLiteral("no service detail in this payload");
    }
    QStringList lines;
    for (const CloudSpendService &service : spend.services) {
        if (service.usd < 0.0) {
            lines.append(service.name);
            continue;
        }
        const double share =
            spend.monthToDateUsd > 0.0 ? service.usd / spend.monthToDateUsd * 100.0
                                       : -1.0;
        lines.append(QStringLiteral("%1  %2%3")
                         .arg(service.name,
                              cloudSpendAmountText(service.usd),
                              share >= 0.0
                                  ? QStringLiteral("  (%1%)").arg(share, 0, 'f', 1)
                                  : QString()));
    }
    return lines.join(QLatin1Char('\n'));
}

QString cloudSpendNote() {
    return QStringLiteral(
        "billed = what the provider charged this month, not this instance's "
        "estimate; cost data lags by up to a day, and Cost Explorer requests "
        "are themselves billable, so the query runs every 6 h and "
        "[cloud] spend=false turns it off");
}

QString cloudSpendGateText(CloudProvider provider) {
    switch (provider) {
    case CloudProvider::Gcp:
        return QStringLiteral(
            "billed spend: gcloud has no cost API - Google bills through a "
            "BigQuery billing export, so this surface stays off on GCP");
    case CloudProvider::Oci:
        return QStringLiteral(
            "billed spend: the OCI usage API is not wired yet - the estimate "
            "above comes from the rate file");
    case CloudProvider::Aws:
    case CloudProvider::Azure:
        return QStringLiteral(
            "billed spend: no answer yet - the first query runs on the next "
            "cadence");
    case CloudProvider::None:
        break;
    }
    return cloudSpendNoProviderText();
}

QString cloudSpendFailureText(const QString &cli, int exitCode,
                              const QString &firstLine) {
    QString line = QStringLiteral("billed spend: %1 exited %2").arg(cli).arg(exitCode);
    if (!firstLine.trimmed().isEmpty()) {
        line += QStringLiteral(" - %1").arg(firstLine.trimmed().left(160));
    }
    return line;
}

QString cloudSpendTimeoutText(const QString &cli, int seconds) {
    return QStringLiteral(
               "billed spend: %1 did not answer within %2 s - killed")
        .arg(cli)
        .arg(qMax(1, seconds));
}

QString cloudSpendDisabledText() {
    return QStringLiteral(
        "billed spend: off ([cloud] spend=false) - no cost query is issued");
}

QString cloudSpendNoProviderText() {
    return QStringLiteral(
        "billed spend: no cloud instance identified - the provider's cost API "
        "is never called");
}

QString cloudSpendCliMissingText(const QString &cli) {
    return QStringLiteral(
               "billed spend: %1 is not on PATH - install it to read the "
               "account's billed cost")
        .arg(cli);
}

// Billing windows are UTC on every provider (Cost Explorer's TimePeriod is a
// UTC date, Azure's usageStart is UTC), so the window is computed in UTC and
// not from the local calendar day.
QString cloudSpendPeriodStart(qint64 nowMs) {
    const QDate today = qtcompat::fromMSecsSinceEpochUtc(nowMs).date();
    return QDate(today.year(), today.month(), 1).toString(Qt::ISODate);
}

QString cloudSpendPeriodEnd(qint64 nowMs) {
    // CE's end is exclusive and Azure's window is inclusive of the day, so
    // asking for tomorrow includes today on both.
    return qtcompat::fromMSecsSinceEpochUtc(nowMs)
        .date()
        .addDays(1)
        .toString(Qt::ISODate);
}

QString cloudSpendForecastEnd(qint64 nowMs) {
    const QDate today = qtcompat::fromMSecsSinceEpochUtc(nowMs).date();
    return QDate(today.year(), today.month(), 1)
        .addMonths(1)
        .toString(Qt::ISODate);
}
