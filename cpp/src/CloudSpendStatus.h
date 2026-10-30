#pragma once

#include "CloudProvider.h"

#include <QByteArray>
#include <QList>
#include <QString>

// Billed spend from the provider's own cost API (M85). The Cloud page already
// estimates what this instance costs from a rate file; this reads what the
// provider actually billed the account this month, and what it expects to bill
// by month end.
//
// Syspeek still never holds credentials: the query runs through the CLI the
// user already logged in, exactly like the inventory (M83).
//
// Grounding (shipped artifacts, kept /tmp/opencode/m85/rea):
//   botocore 1.43.120 `data/ce/2017-10-25/service-2.json`:
//     metadata `serviceFullName: "AWS Cost Explorer Service"`,
//     `apiVersion: "2017-10-25"`, `targetPrefix: "AWSInsightsIndexService"`;
//     GetCostAndUsage required `["TimePeriod","Granularity","Metrics"]`,
//     GetCostAndUsageResponse members `ResultsByTime`/`GroupDefinitions`/
//     `DimensionValueAttributes`/`NextPageToken`;
//     ResultByTime members `TimePeriod`/`Total`/`Groups`/`Estimated`;
//     Group members `Keys`/`Metrics`; MetricValue members `Amount`/`Unit`;
//     DateInterval members `Start`/`End` ("The start date is inclusive...
//     The end date is exclusive");
//     GetCostAndUsageResponse `ResultsByTime` documentation "The time period
//     that's covered by the results in the response";
//     GetCostForecast required `["TimePeriod","Metric","Granularity"]`,
//     response members `Total`/`ForecastResultsByTime`, ForecastResult
//     members `MeanValue`/`PredictionIntervalLowerBound`/
//     `PredictionIntervalUpperBound`/`TimePeriod`;
//     Granularity enum `["DAILY","MONTHLY","HOURLY"]`,
//     Metric enum `["BLENDED_COST","UNBLENDED_COST",...]`,
//     GroupDefinitionType enum `["DIMENSION","TAG","COST_CATEGORY",
//     "PRODUCT_ATTRIBUTE"]`.
//   aws-cli 2.34.53 (this box, `aws ce get-cost-and-usage help`):
//     "Retrieves cost and usage metrics for your account. You can specify which
//      cost and usage-related metric that you want the request to return...
//      You can also filter and group your data by various dimensions, such as
//      SERVICE or AZ, in a specific time range."
//     SYNOPSIS: `--time-period <value> --granularity <value> [--filter <value>]
//     --metrics <value> [--group-by <value>]`;
//     `aws ce get-cost-forecast help` SYNOPSIS: `--time-period <value>
//     --metric <value> --granularity <value> [... --prediction-interval-level
//     <value>]`.
//   azure-cli 2.91.0 `command_modules/consumption/aaz/latest/consumption/
//     usage/_list.py`: `@register_command("consumption usage list",
//     is_preview=True)` "List the details of Azure resource consumption,
//     either as an invoice or within a billing period."; args `-t/--top`
//     "Maximum number of items to return. Value range: 1-1000", `--filter`
//     "... by properties/usageEnd (Utc time), properties/usageStart (Utc
//     time), properties/resourceGroup, properties/instanceName or
//     properties/instanceId"; usage detail serialized names `pretaxCost`,
//     `usageStart`, `usageEnd`, `product`, `instanceName`, `isEstimated`,
//     `meterDetails.meterCategory`.
//
// Why the AWS metric is UNBLENDED_COST: it is the rate the account is actually
// charged per account, where BLENDED_COST averages the organization's rates —
// the number a single-account user can reconcile against their invoice.

struct CloudSpendService {
    QString name;
    double usd = -1.0;
};

struct CloudSpendDay {
    QString date;
    double usd = -1.0;
};

struct CloudSpendStatus {
    bool attempted = false;
    bool available = false;
    CloudProvider provider = CloudProvider::None;
    QString cliName;
    QString scope;
    QString source;
    QString note;
    qint64 fetchedAtMs = 0;
    // The period actually queried, as the provider reports it: the start of
    // the current month and the exclusive end (today or tomorrow).
    QString periodStart;
    QString periodEnd;
    double monthToDateUsd = -1.0;
    // Month end projection: month to date + the forecast for the rest of the
    // month. -1 when the provider did not answer a forecast.
    double forecastUsd = -1.0;
    double forecastRemainingUsd = -1.0;
    bool estimated = false;
    // The provider answered a capped page: the total is then a floor.
    bool truncated = false;
    QList<CloudSpendService> services;
    QList<CloudSpendDay> days;
};

// --- parsers (pure) ------------------------------------------------------
CloudSpendStatus parseAwsCostAndUsage(const QByteArray &json, QString *error);
CloudSpendStatus parseAwsCostForecast(const QByteArray &json, QString *error);
CloudSpendStatus parseAzureUsageList(const QByteArray &json, QString *error);

// Folds a forecast response into a cost-and-usage result: the projection is
// month-to-date plus the forecast for the remaining days.
void spendApplyForecast(CloudSpendStatus *spend, double remainingUsd);
void spendSortServices(CloudSpendStatus *spend, int cap = 12);

// --- text ----------------------------------------------------------------
QString cloudSpendAmountText(double usd);
QString cloudSpendSummaryText(const CloudSpendStatus &spend);
QString cloudSpendDetailText(const CloudSpendStatus &spend);
QString cloudSpendServicesText(const CloudSpendStatus &spend);
QString cloudSpendNote();          // what the numbers are, and their lag
QString cloudSpendGateText(CloudProvider provider);
QString cloudSpendFailureText(const QString &cli, int exitCode,
                              const QString &firstLine);
QString cloudSpendTimeoutText(const QString &cli, int seconds);
QString cloudSpendDisabledText();
QString cloudSpendNoProviderText();
QString cloudSpendCliMissingText(const QString &cli);

// The month-to-date window the query uses, as CE/Azure want it:
// start = first day of the current month, end = tomorrow (CE's end is
// exclusive, so tomorrow includes today).
QString cloudSpendPeriodStart(qint64 nowMs);
QString cloudSpendPeriodEnd(qint64 nowMs);
QString cloudSpendForecastEnd(qint64 nowMs);
