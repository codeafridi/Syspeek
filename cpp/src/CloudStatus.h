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

struct CloudFleetInstance {
    QString instanceId;
    QString name;
    QString instanceType;
    QString state;
    QString rawState;
    QString region;
    QString zone;
    QString group;
    QString privateIp;
    QString publicIp;
    QStringList tags;
    qint64 launchedAtMs = -1;
    qint64 ageSec = -1;
    bool spot = false;
    bool isSelf = false;
    double ratePerHour = -1.0;
    double monthlyUsd = -1.0;
};

struct CloudFleetStatus {
    bool attempted = false;
    bool available = false;
    CloudProvider provider = CloudProvider::None;
    QString cliName;
    QString source;
    QString scope;
    QString note;
    qint64 fetchedAtMs = 0;
    QList<CloudFleetInstance> rows;
    int running = 0;
    int unpriced = 0;
    // Rows the CLI listed beyond the render cap; never silently dropped.
    int dropped = 0;
    double monthlyUsd = -1.0;
    int selfIndex = -1;
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
    // M83: the account inventory, and the instance age it resolves for this
    // box. `accruedLaunchUsd` is the cost since the provider's launch time
    // (an upper bound: it assumes the instance ran continuously) and stays
    // -1 until the fleet answers with a launch time for this instance.
    CloudFleetStatus fleet;
    qint64 instanceAgeSec = -1;
    qint64 launchedAtMs = -1;
    double accruedLaunchUsd = -1.0;
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

// --- fleet inventory (M83) ------------------------------------------------
// The account's own instances, listed by the provider's CLI instead of the
// link-local metadata service, so the page can answer "how old is this box"
// and "what else is in the account". Syspeek never reads or stores
// credentials: the CLI uses the profile the user already configured.
//
// Grounding (each provider's own shipped client, kept /tmp/opencode/m83):
//   AWS - botocore 1.43.120 ec2/2016-11-15 service model
//         (/tmp/opencode/m82/rea/ec2-model.json):
//         `InstanceId locationName:"instanceId"`, `InstanceType`,
//         `LaunchTime locationName:"launchTime"` ("The time that the
//         instance was last launched"), `Placement locationName:"placement"`,
//         `State locationName:"instanceState"`, `Tags locationName:"tagSet"`,
//         `PrivateIpAddress`, `PublicIpAddress`,
//         `SpotInstanceRequestId`, `Reservations` -> `Instances`.
//         aws-cli 2.34.53 own help: "--query (string) A JMESPath query to
//         use in filtering the response data", "If you do not specify
//         instance IDs or filters, the output includes information for all
//         instances".
//   Azure - azure-cli-vm 2.2.23 _params.py:
//         "c.argument('show_details', action='store_true',
//          options_list=['--show-details', '-d'], help='show public ip
//          address, FQDN, and power states. command will run slow')"
//         custom.py list_vm/get_vm_details: "setattr(result, 'power_state',
//         ','.join([s.display_status for s in result.instance_view.statuses
//         if s.code.startswith('PowerState/')]))" + "del result.instance_view";
//         azure-mgmt-compute 5.0.0 v2019_03_01 virtual_machine.py attribute
//         map: `hardware_profile` -> properties.hardwareProfile (vmSize),
//         `provisioning_state`, `vm_id` -> properties.vmId, `zones`.
//   GCP - google-cloud-compute 1.56.0 compute_v1/types/compute.py Instance:
//         "creation_timestamp (str): Output only. [Output Only] Creation
//          timestamp inRFC3339 text format.",
//         "status (str): ... One of the following values: PROVISIONING,
//          STAGING,RUNNING, STOPPING, SUSPENDING,SUSPENDED, REPAIRING, and
//          TERMINATED.",
//         "machine_type (str): Full or partial URL of the machine type ...
//          format:zones/zone/machineTypes/machine-type",
//         "zone (str): Output only. [Output Only] URL of the zone ...",
//         `network_interfaces`, `labels`, `last_start_timestamp`.
//         google-cloud-cli calliope/base.py FORMAT_FLAG "--format": "Sets the
//         format for printing command output resources."; surface/compute/
//         instances/list.py: '"""List Compute Engine virtual machine
//         instances."""' and "displays all Compute Engine instances in a
//         project".
//   OCI - oci-cli 3.95.1 compute_cli.py `def list_instances(ctx, from_json,
//         all_pages, page_size, compartment_id, ...)` with
//         "@cli_util.option('--all', all_pages, is_flag=True, help=Fetches
//         all pages of results)", the probe lifecycle states
//         ["MOVING","PROVISIONING","RUNNING","STARTING","STOPPING","STOPPED",
//         "CREATING_IMAGE","TERMINATING","TERMINATED"] and
//         "@click.option('--output', ... help='The output format. [Default
//         is json]')"; oci-python-sdk core/models/instance.py attribute map
//         display_name/lifecycle_state/time_created/shape/region/
//         availability_domain/compartment_id/freeform_tags.
CloudFleetStatus parseAwsDescribeInstances(const QByteArray &body);
CloudFleetStatus parseAzureVmList(const QByteArray &body);
CloudFleetStatus parseGcpInstanceList(const QByteArray &body);
CloudFleetStatus parseOciInstanceList(const QByteArray &body);
CloudFleetStatus parseCloudFleetJson(CloudProvider provider,
                                     const QByteArray &body);

QString fleetState(CloudProvider provider, const QString &rawState);
qint64 fleetAgeSeconds(qint64 launchedAtMs, qint64 nowMs);
void fleetResolveAges(CloudFleetStatus &fleet, qint64 nowMs);
void fleetApplyRates(CloudFleetStatus &fleet, const CloudRateTable &table,
                     double hoursPerMonth);
int fleetMarkSelf(CloudFleetStatus &fleet, const CloudIdentity &identity);

QString fleetSummaryText(const CloudFleetStatus &fleet);
QString fleetTableNote();
QString fleetDisabledText();
QString fleetNoProviderText();
QString fleetCliMissingText(const QString &cli);
QString fleetTimeoutText(const QString &cli, int seconds);
QString fleetEmptyText(const CloudFleetStatus &fleet);
QString fleetFailureText(const QString &cli, int exitCode,
                         const QString &firstErrorLine);
QString fleetCredentialsText(CloudProvider provider);
QString cloudFleetCredentialHint(CloudProvider provider);
bool cloudFleetNeedsCredentials(CloudProvider provider,
                                const QString &stderrText);

// --- text ----------------------------------------------------------------
QString cloudUnreachableText();
// The first sampler tick deliberately skips the IMDS sweep so the initial
// sample never waits on a link-local socket; the card says so for that tick.
QString cloudProbingText();
QString cloudSummaryText(const CloudSnapshot &snapshot);
QString cloudDetailText(const CloudSnapshot &snapshot);
QString cloudCostText(const CloudSnapshot &snapshot);
QString cloudAppText(const CloudSnapshot &snapshot);
QString cloudShareNote();
QString cloudSettingsLine(bool enabled, const QString &ratesPath,
                         const CloudRateTable &table, qint64 nowMs);
QString cloudUptimeText(qint64 seconds);
