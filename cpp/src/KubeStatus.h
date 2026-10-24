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
    int totalPods = 0;
    int issueCount = 0;
    int eventCount = 0;
    double costMonthly = 0.0;
    int costMissingRequests = 0;
    int costPods = 0;
    QList<QPair<QString, double>> costByNamespace;
    QList<QPair<QString, double>> costTopWorkloads;
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
