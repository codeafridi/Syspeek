#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QtGlobal>

#include "DeprecationStatus.h"

struct KubePod {
    QString ns;
    QString name;
    QString phase;
    QString reason;
    QString lastReason;
    QString nodeName;
    int restarts = 0;
    double cpuRequestCores = 0.0;
    double memRequestBytes = 0.0;
    double cpuLimitCores = 0.0;
    double memLimitBytes = 0.0;
    int containersWithoutCpuRequest = 0;
    int containersWithoutMemRequest = 0;
    double cpuMillis = -1.0;
    long long memBytes = -1;
};

// Per-pod right-sizing (M89): OpenCost computes usage/request efficiency,
// recommends a request at usage x buffer_multiplier (its default 1.2 for 20%
// headroom), and prices the headroom the declared request carries above that
// recommendation. The maths lives in CostModel (computePodRightSizing); the
// row is declared here because a KubeSnapshot carries it.
struct PodRightSizing {
    QString ns;
    QString pod;
    double cpuUsageCores = 0.0;
    double cpuRequestCores = 0.0;
    double memUsageBytes = 0.0;
    double memRequestBytes = 0.0;
    double cpuRatio = 0.0;          // usage/request (0 when no request)
    double memRatio = 0.0;
    double suggestedCpuCores = 0.0; // usage x buffer
    double suggestedMemBytes = 0.0;
    // Headroom above the recommendation, priced at this cluster's rates.
    // 0 when the pod is under-provisioned (usage above its request) - the
    // recommendation is then a raise, and it saves nothing.
    double monthlySavings = 0.0;
    bool hasCpu = false;       // both a request and a usage sample
    bool hasMem = false;
    bool underCpu = false;     // usage is already above the request
    bool underMem = false;
};

// Multi-context support (M90): the kubeconfig is the authority on which
// clusters a user has, so Crowsnest reads it rather than inventing a list.
//
// Grounding (shipped kubectl, strings kept /tmp/opencode/m8791/m90/
// kubectl.strings): the kubeconfig JSON schema kubectl itself emits for
// `kubectl config view -o json` - `"contexts"`, `"current-context"`,
// `"clusters"`, `"users"`, and per-context `cluster` / `user` / `namespace`.
struct KubeContext {
    QString name;
    QString cluster;
    QString user;
    QString namespace_;
    bool current = false;
};

struct KubeDeployment {
    QString ns;
    QString name;
    int desired = 0;
    int available = 0;
    int unavailable = 0;
    int generation = 0;
};

struct KubeDiagnosis {
    QString ns;
    QString pod;
    QString severity;
    QString title;
    QString detail;
};

struct KubeChange {
    qint64 atMs = 0;
    QString ns;
    QString object;
    QString kind;
    QString summary;
};

struct KubeEvent {
    QString ns;
    QString involvedName;
    QString type;
    QString reason;
    QString message;
    qint64 lastTimestampMs = 0;
    int count = 0;
};

enum class KubeState {
    NotConfigured,
    Unreachable,
    Ok,
    TimedOut,
    CommandFailed,
    ParseFailed,
    Stale,
};

struct KubeNode {
    QString name;
    bool ready = false;
    QString roles;
    QString kubeletVersion;
    QString runtime;
    QString osImage;
    QString providerId;
    QString instanceType;
    QString zone;
    QString region;
    double cpuCapacityCores = 0.0;
    double cpuAllocatableCores = 0.0;
    unsigned long long memCapacityBytes = 0;
    unsigned long long memAllocatableBytes = 0;
    int podsCapacity = 0;
    int podsAllocatable = 0;
    int podsRunning = 0;
    double cpuMillis = -1.0;
    long long memBytes = -1;
};

struct KubeMetricRow {
    QString key;
    double cpuMillis = -1.0;
    long long memBytes = -1;
};

struct KubeResources {
    QList<KubePod> pods;
    QList<KubeDeployment> deployments;
    QList<KubeEvent> events;
    QList<KubeNode> nodes;
    QList<ObjectRef> objects;
};

struct KubeSnapshot {
    KubeState state = KubeState::NotConfigured;
    QList<KubePod> pods;
    QList<KubeDeployment> deployments;
    QList<KubeEvent> events;
    QList<KubeNode> nodes;
    QList<KubeDiagnosis> diagnoses;
    QList<KubeChange> timeline;
    QList<DeprecationFinding> deprecations;
    QList<KubeContext> contexts;
    QString currentContext;   // as the kubeconfig marks it
    QString selectedContext;  // what Crowsnest is actually querying
    int totalPods = 0;
    int issueCount = 0;
    int eventCount = 0;
    double costMonthly = 0.0;
    int costMissingRequests = 0;
    int costPods = 0;
    QList<QPair<QString, double>> costByNamespace;
    QList<QPair<QString, double>> costTopWorkloads;
    // M89: per-pod right-sizing (OpenCost buffer model), computed by KubeSource
    // once the metrics sample and the rates are both known.
    QList<PodRightSizing> costRightSizing;
    int costRightSizingPods = 0;
    double costRightSizingSavings = 0.0;
    // OpenCost-style pricing detail (M81): the rates actually applied, the
    // cpu/ram split of the estimate, and usage/request efficiency over the
    // pods that report both (metrics.k8s.io) - -1 when unmeasurable.
    double costCpuPerCoreHour = 0.0;
    double costMemPerGbHour = 0.0;
    double costCpuMonthly = 0.0;
    double costMemMonthly = 0.0;
    double costCpuEfficiency = -1.0;
    double costMemEfficiency = -1.0;
    double costWastedMonthly = 0.0;
    int costEfficiencyPods = 0;
    bool metricsOk = false;
    QString metricsNote;
    double podsCpuMillis = 0.0;
    long long podsMemBytes = 0;
    int podsWithUsage = 0;
    int nodesWithUsage = 0;
    bool stale = false;
    QString errorMessage;
    qint64 sampledAtMs = 0;
};

KubeResources parseKubectlResources(const QByteArray &json, bool *ok);

QList<KubeMetricRow> parseKubePodMetricsJson(const QByteArray &json, bool *ok);
QList<KubeMetricRow> parseKubeNodeMetricsJson(const QByteArray &json,
                                              bool *ok);
void kubeMergeMetrics(QList<KubePod> *pods, QList<KubeNode> *nodes,
                      const QList<KubeMetricRow> &podRows,
                      const QList<KubeMetricRow> &nodeRows);

double kubeParseCpuCores(const QString &quantity);
double kubeParseMemoryBytes(const QString &quantity);

QList<KubeDiagnosis> kubeDiagnose(const QList<KubePod> &pods,
                                  const QList<KubeEvent> &events);
QList<KubeChange> kubeDiff(const KubeResources &prev, const KubeResources &cur,
                           qint64 nowMs);

bool kubePodIsIssue(const KubePod &pod);
bool kubeDeploymentIsIssue(const KubeDeployment &deployment);
bool kubeEventIsWarning(const KubeEvent &event);
int kubeIssueCount(const QList<KubePod> &pods,
                   const QList<KubeDeployment> &deployments);
int kubeWarningCount(const QList<KubeEvent> &events);

KubeState classifyKubeFailure(const QByteArray &payload);

QString kubeSummaryText(const KubeSnapshot &snapshot);
QString kubeDetailText(const KubeSnapshot &snapshot);
QString kubeEventsText(const KubeSnapshot &snapshot);
QString kubePodsText(const KubeSnapshot &snapshot);
QString kubeDeploymentsText(const KubeSnapshot &snapshot);
QString kubeDiagnosisText(const QList<KubeDiagnosis> &diagnoses);
QString kubeTimelineText(const QList<KubeChange> &changes);
QString kubeNodesText(const QList<KubeNode> &nodes);

// Right-sizing recommendation per pod
// Parses `kubectl config view -o json`. A malformed document is refused with
// a reason; a document with no contexts is a valid (empty) kubeconfig.
QList<KubeContext> kubeParseContexts(const QByteArray &json, QString *error);

// The name marked current, or the first one when kubectl marked none.
QString kubeCurrentContextName(const QList<KubeContext> &contexts);

// "contexts: dev (current), prod, staging - SELECTED prod"
QString kubeContextsText(const QList<KubeContext> &contexts,
                         const QString &selected);

QString kubeSummaryText(const KubeSnapshot &snapshot);
QString kubeDetailText(const KubeSnapshot &snapshot);
QString kubeEventsText(const KubeSnapshot &snapshot);
QString kubePodsText(const KubeSnapshot &snapshot);
QString kubeDeploymentsText(const KubeSnapshot &snapshot);
QString kubeDiagnosisText(const QList<KubeDiagnosis> &diagnoses);
QString kubeTimelineText(const QList<KubeChange> &changes);
QString kubeNodesText(const QList<KubeNode> &nodes);
