#pragma once

#include "AlertEngine.h"

#include <QList>
#include <QString>
#include <QStringList>

struct Incident {
    QString id;
    QString key;      // grouping key (alert rule id) — deprecated: use m_groupKey instead
    QString groupKey; // M87: grouping key (group_by) — alerts sharing this fold into one incident
    QString title;
    QString severity;
    QString status;   // open / resolved
    qint64 openedAt = 0;
    qint64 lastSeen = 0;
    qint64 resolvedAt = 0;
    int count = 0;
    QStringList timeline;
    // M87:
    QString owner;    // incident owner; "unowned" if empty, displayed on the incident row
    bool flapping = false; // M87: set when transition count exceeds threshold in window
};

// Alertmanager-style grouping: alerts sharing a group_by key are folded into a
// single incident; the incident resolves after group_interval of silence.
// Flap tracking: if a rule transitions fires/clears more than m_flapTransitions
// within m_flapWindowMs, the rule is marked flapping and notifications are held.
class IncidentTracker {
public:
    explicit IncidentTracker(qint64 resolveAfterMs = 60000,
                             int keepResolved = 30,
                             int flapTransitions = 6,
                             qint64 flapWindowMs = 600000);

    void update(const QList<Alert> &active, const QList<AlertRule> &rules, qint64 nowMs);
    QList<Incident> incidents() const;
    void reset();

private:
    // M87: group_wait_ms after first alert before incident is created
    qint64 m_groupWaitMs = 5000;
    // M87: repeat_interval_ms between repeated notifications for the same incident
    qint64 m_repeatIntervalMs = 300000;
    // M87: group_interval_ms of silence before incident resolves
    qint64 m_groupIntervalMs = 3600000;
    // M87: flap detection: if flapTransitions transitions occur within flapWindowMs
    int m_flapTransitions;
    qint64 m_flapWindowMs;

    // M87: per-rule transition tracking for flap detection
    struct RuleFlapState {
        qint64 lastTransitionMs = 0;
        int transitionCount = 0; // fires+clears count
        bool currentlyFiring = false;
    };
    QHash<QString, RuleFlapState> m_flapStates;

    QList<Incident> m_incidents;
    qint64 m_resolveAfterMs;
    int m_keepResolved;
    int m_nextId = 1;
};

QString incidentsSummaryText(const QList<Incident> &incidents);
QString incidentsDetailText(const QList<Incident> &incidents);

// M91: IaC drift / asset inventory
// Reads terraform state -json and diffs against cloud fleet listing

// Terraform resource type
struct IaCResource {
    QString type;        // e.g. "aws_instance", "aws_s3_bucket"
    QString id;          // resource ID
    QString name;        // resource name
    QString state;       // "managed", "unmanaged", "missing"
    QString details;     // extra details
};

// Read terraform state file
QList<IaCResource> iacReadTerraformState(const QString &filePath);

// Diff terraform state against cloud fleet
// Returns: unmanaged (in state only), missing (in cloud only), plus unattached resources
struct IaCDriftResult {
    QList<IaCResource> unmanaged;   // in terraform state only
    QList<IaCResource> missing;     // in cloud only
    QList<IaCResource> unattached;  // e.g. volumes without instances
};
IaCDriftResult iacDrift againstCloud(const QList<IaCResource> &tfResources,
                                     const QList<IaCResource> &cloudResources);
