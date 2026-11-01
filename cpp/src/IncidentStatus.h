#pragma once

#include "AlertEngine.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// One correlated incident: every alert whose rule folds into the same group
// key lands on the same row, the row carries who owns it (PagerDuty-style
// assignment), and it records how many alerts it absorbed.
//
// Grounding (shipped artifacts, kept /tmp/opencode/m8791):
//   Alertmanager 0.34.1 linux-amd64 (`am.strings`): the grouping/notification
//     model this mirrors - `GroupByStr3yaml:"group_by,omitempty"`,
//     `GroupWait7yaml:"group_wait,omitempty"`,
//     `GroupInterval?yaml:"group_interval,omitempty"`,
//     `RepeatIntervalAyaml:"repeat_interval,omitempty"`, and the notify loop
//     reason `NOTIFY_REASON_REPEAT_INTERVAL_ELAPSED`.
//   PagerDuty api-schema `reference/REST/openapiv3.json` (ownership model):
//     `Assignment` = `{"at": date-time, "assignee": User|UserReference}` with
//     `"required": ["at", "assignee"]`;
//     `Incident.assignments` - "List of all assignments for this incident.
//     This list will be empty if the `Incident.status` is `resolved`";
//     `Incident.status` enum `["triggered", "acknowledged", "resolved"]`;
//     `Incident.alert_counts` (AlertCount) - the alerts folded into the
//     incident; `last_status_change_at`.
struct Incident {
    QString id;
    QString groupKey;      // rule.group when set, else the rule id
    QStringList rules;     // rule ids folded onto this row
    QString title;         // title of the most severe folded alert
    QString severity;      // most severe of the folded alerts
    QString status;        // open / resolved
    QString owner;         // assignment; empty renders "unowned"
    qint64 openedAt = 0;
    qint64 lastSeen = 0;
    qint64 resolvedAt = 0;
    qint64 lastNotifiedAt = 0;  // repeat_interval bookkeeping (0 = never)
    bool notifyPending = true;  // still inside group_wait for the first notify
    qint64 groupWaitMs = 5000;        // Alertmanager group_wait of the fold
    qint64 repeatIntervalMs = 300000; // Alertmanager repeat_interval of the fold
    int count = 0;              // alerts folded onto this row
    bool flapping = false;      // a folded rule is past the flap threshold
    QStringList timeline;
};

// Correlates firing alerts into incidents the way Alertmanager groups them:
// fold by group key, hold the first notification for group_wait, re-notify a
// still-firing group only every repeat_interval, and mark (then suppress the
// outbound path of) rules that fire/clear faster than a human can page on.
class IncidentTracker {
public:
    explicit IncidentTracker(qint64 resolveAfterMs = 60000,
                             int keepResolved = 30,
                             int flapTransitions = 6,
                             qint64 flapWindowMs = 600000);

    void update(const QList<Alert> &active, const QList<AlertRule> &rules,
                qint64 nowMs);
    QList<Incident> incidents() const;

    // Open incidents whose group_wait / repeat_interval has elapsed. Marks
    // them notified, so each call advances the ledger exactly once.
    QList<Incident> dueNotifications(qint64 nowMs);

    // Rules past the flap threshold right now - their outbound notifications
    // are held while the incident row keeps showing the breach.
    QSet<QString> flappingRules() const { return m_flapping; }

    void reset();

private:
    struct RuleFlapState {
        QList<qint64> transitions;  // bounded, pruned to the window
        bool firing = false;
        bool haveState = false;
    };

    static QString foldKey(const QString &ruleId, const QString &group);
    void trackFlapping(const QList<Alert> &active, const QList<AlertRule> &rules,
                       qint64 nowMs);
    void pruneResolved(qint64 nowMs);

    QList<Incident> m_incidents;
    QHash<QString, RuleFlapState> m_flapStates;
    QSet<QString> m_flapping;
    qint64 m_resolveAfterMs;
    int m_keepResolved;
    int m_flapTransitions;
    qint64 m_flapWindowMs;
    int m_nextId = 1;
};

QString incidentsSummaryText(const QList<Incident> &incidents);
QString incidentsDetailText(const QList<Incident> &incidents);
// "flapping: N rules changed state M times in Ks - outbound notifications held"
QString alertsFlappingText(const QSet<QString> &flapping, int transitions,
                           qint64 windowMs);
