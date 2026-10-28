#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QtGlobal>

// Cloud instance identity + accrued-cost model. Everything here is pure so
// the same code serves the HUD, `--dump` and the tests.
//
// Grounding (Datadog agent 7.84.6, the agent's cloud-provider metadata
// client; strings kept at /tmp/opencode/m81/ddagent.strings):
//   "X-aws-ec2-metadata-token"
//   "X-aws-ec2-metadata-token-ttl-seconds"
//   "http://169.254.169.254/latest/dynamic/instance-identity/document"
//   `PrivateIP json:"privateIp"`, `AccountID json:"accountId"`,
//   "AvailabilityZone", `instanceType`, `instanceId`, `imageId`,
//   `BillingProducts json:"billingProducts"`,
//   `MarketplaceProductCodes json:"marketplaceProductCodes"`,
//   "http://169.254.169.254/metadata/instance/compute?api-version=2021-12-13&format=json"
//   "Metadata-Flavor", "169.254.169.254/computeMetadata/v1",
//   "169.254.169.254/instance/machine-type", "instance/zone",
//   "expected 'projects/PROJECT_NUM/zones/ZONE/machineTypes/MACHINE_TYPE'",
//   "Bearer Oracle", "http://169.254.0.23", "opc/v2/instance/id",
//   "opc/v2/instance/shape", "Oracle '%s' returned empty id"
//
// Price-document vocabulary (OpenCost, kept /tmp/opencode/m81/opencost.strings):
//   `PriceDimensions json:"priceDimensions"`, `PricePerUnit json:"pricePerUnit"`,
//   `OperatingSystem json:"operatingSystem"`, `capacitystatus`,
//   `preInstalledSw`, `instanceFamily`, "pricing.us-east-1.amazonaws.com".
enum class CloudProvider {
    None,
    Aws,
    Azure,
    Gcp,
    Oci,
};

QString cloudProviderName(CloudProvider provider);
QString cloudProviderLabel(CloudProvider provider);

struct CloudIdentity {
    CloudProvider provider = CloudProvider::None;
    QString instanceId;
    QString instanceName;
    QString instanceType;
    QString region;
    QString zone;
    QString accountId;
    QString privateIp;
    QString imageId;
    QString architecture;
    QString operatingSystem;
    bool spot = false;
    bool marketplace = false;
    QString note;
};

struct CloudRate {
    QString key;
    double usdPerHour = 0.0;
};

struct CloudRateTable {
    QList<CloudRate> rates;
    QString importDate;
    QString sourcePath;
    bool loaded = false;
};

struct CloudProcessSample {
    QString name;
    int pid = 0;
    double cpuPercent = 0.0;
};

struct CloudConsumer {
    QString name;
    int pid = 0;
    double cpuPercent = 0.0;
    double usdPerHour = 0.0;
};

struct CloudSnapshot {
    bool available = false;
    bool warming = false;
    QString note;
    CloudIdentity identity;
    int vcpus = -1;
    double ratePerHour = -1.0;
    QString rateKey;
    QString rateSource;
    QString rateImportDate;
    int rateStaleDays = -1;
    qint64 uptimeSec = 0;
    double accruedUsd = -1.0;
    double projectedMonthlyUsd = -1.0;
    QString appName;
    int appPid = 0;
    double appCpuPercent = -1.0;
    double appCostPerHour = -1.0;
    QList<CloudConsumer> consumers;
    qint64 sampledAtMs = 0;
};

struct CloudCostMath {
    double accruedUsd = -1.0;
    double projectedMonthlyUsd = -1.0;
};

struct GcpMetadataFields {
    QByteArray machineType;
    QByteArray zone;
    QByteArray id;
    QByteArray name;
    QByteArray project;
};

// --- IMDS document parsers ------------------------------------------------
CloudIdentity parseAwsIdentityDocument(const QByteArray &body);
CloudIdentity parseAzureCompute(const QByteArray &body);
CloudIdentity parseGcpInstance(const GcpMetadataFields &fields);
CloudIdentity parseOciInstance(const QByteArray &body);

QString cloudMachineTypeTail(const QString &path);
QString cloudRegionFromZone(const QString &zone);

// --- rate table ----------------------------------------------------------
// "<provider>/<instanceType> = <usd per hour>", '#' starts a comment line,
// "# imported YYYY-MM-DD" records when the prices were fetched.
CloudRateTable parseCloudRateFile(const QString &text,
                                  const QString &sourcePath = QString());
double cloudRateFor(const CloudRateTable &table, CloudProvider provider,
                    const QString &instanceType);
QString cloudRateKey(CloudProvider provider, const QString &instanceType);
QString cloudRatesTemplateText();

// Age in whole days of the import stamp; -1 when there is no usable stamp.
int cloudRateStaleDays(const QString &importDate, qint64 nowMs);
QString cloudRateFileText(const CloudRateTable &table, qint64 nowMs);

// --- cost maths ----------------------------------------------------------
CloudCostMath computeCloudCost(double ratePerHour, qint64 uptimeSec);
double cloudProcessCostPerHour(double ratePerHour, double cpuPercent,
                               int vcpus);
QList<CloudConsumer> cloudTopConsumers(const QList<CloudProcessSample> &rows,
                                       double ratePerHour, int vcpus,
                                       int limit);
double cloudConsumerTotal(const QList<CloudConsumer> &consumers);

// --- text ----------------------------------------------------------------
QString cloudUnreachableText();
QString cloudSummaryText(const CloudSnapshot &snapshot);
QString cloudDetailText(const CloudSnapshot &snapshot);
QString cloudCostText(const CloudSnapshot &snapshot);
QString cloudAppText(const CloudSnapshot &snapshot);
QString cloudShareNote();
QString cloudSettingsLine(bool enabled, const QString &ratesPath,
                         const CloudRateTable &table, qint64 nowMs);
QString cloudUptimeText(qint64 seconds);
