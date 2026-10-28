#pragma once

#include "CloudStatus.h"

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QStringList>

// `aws pricing get-products` invocation + response parsing.
//
// Grounding: botocore 1.43.120 `data/pricing/2017-10-15/service-2.json`
// (/tmp/opencode/m82/pricing-model.json) pins GetProducts with
//   ServiceCode (required), Filters, FormatVersion ("aws_v1"),
//   MaxResults, NextToken -> FormatVersion / PriceList / NextToken.
// The price document shape comes from the shipped OpenCost image
// (/tmp/opencode/m81/opencost.strings):
//   `PriceDimensions json:"priceDimensions"`, `PricePerUnit json:"pricePerUnit"`,
//   `OperatingSystem json:"operatingSystem"`, `capacitystatus`,
//   `preInstalledSw`, `instanceFamily`, "pricing.us-east-1.amazonaws.com".
struct AwsPriceQuery {
    QString instanceType;
    QString region = QStringLiteral("us-east-1");
    QString location;
    QString operatingSystem = QStringLiteral("Linux");
    QString tenancy = QStringLiteral("Shared");
    QString preInstalledSw = QStringLiteral("NA");
    QString capacityStatus = QStringLiteral("Used");
};

// Price List Query API location names ("US East (N. Virginia)") are not the
// region code, so the well-known ones are mapped explicitly; an unknown region
// falls back to the literal code, which the API also accepts for newer
// regions.
QString awsPriceLocation(const QString &region);

QStringList awsPricingArguments(const AwsPriceQuery &query);

// Hourly on-demand USD for the SKU that matches the shared/used/Linux/no-SW
// price dimensions; -1 with a reason in `error` when the payload carries no
// usable price.
double parseAwsPricingProducts(const QByteArray &json, QString *error);

// `syspeek --cloud-prices[=<instanceType>]` — opt-in, user-initiated import.
// Never runs on a tick: it shells out to the AWS CLI the user already
// installed, so Syspeek never reads or stores credentials.
int cloudPriceRun(int argc, char **argv);
