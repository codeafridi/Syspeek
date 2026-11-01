#include "IncidentStatus.h"

#include <QDateTime>

#include <algorithm>

namespace {

int severityRank(const QString &severity) {
    if (severity.compare(QStringLiteral("critical"), Qt::CaseInsensitive) == 0
        || severity.compare(QStringLiteral("high"), Qt::CaseInsensitive) == 0) {
        return 2;
    }
    if (severity.compare(QStringLiteral("warning"), Qt::CaseInsensitive) == 0
        || severity.compare(QStringLiteral("warn"), Qt::CaseInsensitive) == 0) {
        return 1;
    }
    return 0;
}

QString stamp(qint64 ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss"));
}

// The effective timings of one rule: its own group_wait/repeat_interval when
// configured, otherwise the tracker default.
qint64 ruleGroupWaitMs(const AlertRule &rule) {
    return rule.groupWaitMs > 0 ? rule.groupWaitMs : 5000;
}

qint64 ruleRepeatIntervalMs(const AlertRule &rule) {
    return rule.repeatIntervalMs > 0 ? rule.repeatIntervalMs : 300000;
}

void incidentCadence(Incident &incident, const AlertRule &rule) {
    incident.groupWaitMs = qMax(incident.groupWaitMs, ruleGroupWaitMs(rule));
    incident.repeatIntervalMs =
        qMax(incident.repeatIntervalMs, ruleRepeatIntervalMs(rule));
}

} // namespace

IncidentTracker::IncidentTracker(qint64 resolveAfterMs, int keepResolved,
                                 int flapTransitions, qint64 flapWindowMs)
    : m_resolveAfterMs(resolveAfterMs)
    , m_keepResolved(keepResolved)
    , m_flapTransitions(qMax(2, flapTransitions))
    , m_flapWindowMs(qMax(1000, flapWindowMs)) {
}

QString IncidentTracker::foldKey(const QString &ruleId, const QString &group) {
    const QString trimmed = group.trimmed();
    return trimmed.isEmpty() ? ruleId : trimmed;
}

void IncidentTracker::trackFlapping(const QList<Alert> &active,
                                    const QList<AlertRule> &rules,
                                    qint64 nowMs) {
    QSet<QString> firingIds;
    for (const Alert &alert : active) {
        firingIds.insert(alert.id);
    }

    QSet<QString> seen;
    for (const AlertRule &rule : rules) {
        seen.insert(rule.id);
        const bool firing = firingIds.contains(rule.id);
        RuleFlapState &state = m_flapStates[rule.id];
        if (state.haveState && state.firing != firing) {
            state.transitions.append(nowMs);
            state.firing = firing;
        } else if (!state.haveState) {
            state.firing = firing;
            state.haveState = true;
            if (firing) {
                // A rule that is firing on the first observation has just
                // changed state; record it so a rule that flips before the
                // tracker has ever seen it still counts.
                state.transitions.append(nowMs);
            }
        }
        while (!state.transitions.isEmpty()
               && nowMs - state.transitions.first() > m_flapWindowMs) {
            state.transitions.removeFirst();
        }
        while (state.transitions.size() > 2 * m_flapTransitions) {
            state.transitions.removeFirst();
        }
    }

    m_flapping.clear();
    for (auto it = m_flapStates.begin(); it != m_flapStates.end();) {
        if (!seen.contains(it.key())) {
            it = m_flapStates.erase(it);
            continue;
        }
        if (it.value().transitions.size() >= m_flapTransitions) {
            m_flapping.insert(it.key());
        }
        ++it;
    }
}

void IncidentTracker::update(const QList<Alert> &active,
                             const QList<AlertRule> &rules, qint64 nowMs) {
    trackFlapping(active, rules, nowMs);

    QHash<QString, AlertRule> byId;
    byId.reserve(rules.size());
    for (const AlertRule &rule : rules) {
        byId.insert(rule.id, rule);
    }

    for (const Alert &alert : active) {
        const AlertRule rule = byId.value(alert.id);
        const QString key = foldKey(alert.id, rule.group);

        Incident *match = nullptr;
        for (Incident &incident : m_incidents) {
            if (incident.status == QStringLiteral("open")
                && incident.groupKey == key) {
                match = &incident;
                break;
            }
        }

        if (match == nullptr) {
            Incident incident;
            incident.id = QStringLiteral("inc-%1").arg(m_nextId++);
            incident.groupKey = key;
            incident.status = QStringLiteral("open");
            incident.openedAt = nowMs;
            incident.lastSeen = nowMs;
            incident.notifyPending = true;
            incident.lastNotifiedAt = 0;
            incident.groupWaitMs = ruleGroupWaitMs(rule);
            incident.repeatIntervalMs = ruleRepeatIntervalMs(rule);
            m_incidents.append(incident);
            match = &m_incidents.last();
        } else {
            // The slowest rule in the fold sets the cadence: a group never
            // notifies faster than its most patient member allows.
            incidentCadence(*match, rule);
        }

        if (!match->rules.contains(alert.id)) {
            match->rules.append(alert.id);
        }
        match->lastSeen = nowMs;
        ++match->count;
        if (severityRank(alert.severity) > severityRank(match->severity)) {
            match->severity = alert.severity;
            match->title = alert.title;
        } else if (match->title.isEmpty()) {
            match->title = alert.title;
        }
        // Ownership is re-read every tick, so assigning an owner while the
        // incident is already open takes effect immediately (a PagerDuty
        // assignment is not frozen at trigger time).
        if (!rule.owner.isEmpty()) {
            match->owner = rule.owner;
        }
        match->flapping = m_flapping.contains(alert.id);
        match->timeline.append(
            QStringLiteral("%1  updated (%2)").arg(stamp(nowMs), alert.detail));
    }

    for (Incident &incident : m_incidents) {
        if (incident.status != QStringLiteral("open")) {
            continue;
        }
        bool anyFlapping = false;
        for (const QString &ruleId : incident.rules) {
            if (m_flapping.contains(ruleId)) {
                anyFlapping = true;
                break;
            }
        }
        incident.flapping = anyFlapping;

        if (nowMs - incident.lastSeen >= m_resolveAfterMs) {
            incident.status = QStringLiteral("resolved");
            incident.resolvedAt = nowMs;
            incident.notifyPending = false;
            incident.timeline.append(
                QStringLiteral("%1  resolved after %2s")
                    .arg(stamp(nowMs))
                    .arg((nowMs - incident.lastSeen) / 1000));
        }
    }

    pruneResolved(nowMs);
}

QList<Incident> IncidentTracker::dueNotifications(qint64 nowMs) {
    QList<Incident> due;
    for (Incident &incident : m_incidents) {
        if (incident.status != QStringLiteral("open")) {
            continue;
        }
        // group_wait holds the first notification for a brand new group;
        // repeat_interval gates every later one (NOTIFY_REASON_REPEAT_INTERVAL_
        // ELAPSED in Alertmanager's notify loop).
        if (incident.notifyPending) {
            if (nowMs - incident.openedAt < incident.groupWaitMs) {
                continue;
            }
            incident.notifyPending = false;
        } else if (incident.lastNotifiedAt > 0
                   && nowMs - incident.lastNotifiedAt
                          < incident.repeatIntervalMs) {
            continue;
        }
        incident.lastNotifiedAt = nowMs;
        incident.timeline.append(
            QStringLiteral("%1  notified").arg(stamp(nowMs)));
        due.append(incident);
    }
    return due;
}

void IncidentTracker::pruneResolved(qint64 nowMs) {
    Q_UNUSED(nowMs);
    int resolved = 0;
    for (int i = m_incidents.size() - 1; i >= 0; --i) {
        if (m_incidents.at(i).status == QStringLiteral("resolved")) {
            ++resolved;
            if (resolved > m_keepResolved) {
                m_incidents.removeAt(i);
            }
        }
    }
}

QList<Incident> IncidentTracker::incidents() const {
    QList<Incident> open;
    QList<Incident> resolved;
    for (const Incident &incident : m_incidents) {
        if (incident.status == QStringLiteral("open")) {
            open.append(incident);
        } else {
            resolved.append(incident);
        }
    }
    std::sort(open.begin(), open.end(),
              [](const Incident &a, const Incident &b) {
                  return a.openedAt > b.openedAt;
              });
    std::sort(resolved.begin(), resolved.end(),
              [](const Incident &a, const Incident &b) {
                  return a.resolvedAt > b.resolvedAt;
              });
    open.append(resolved);
    return open;
}

void IncidentTracker::reset() {
    m_incidents.clear();
    m_flapStates.clear();
    m_flapping.clear();
    m_nextId = 1;
}

QString incidentsSummaryText(const QList<Incident> &incidents) {
    int open = 0;
    int resolved = 0;
    int flapping = 0;
    for (const Incident &incident : incidents) {
        if (incident.status != QStringLiteral("open")) {
            ++resolved;
            continue;
        }
        ++open;
        if (incident.flapping) {
            ++flapping;
        }
    }
    QString text = QStringLiteral("%1 open  %2 resolved").arg(open).arg(resolved);
    if (flapping > 0) {
        text += QStringLiteral("  %1 flapping").arg(flapping);
    }
    return text;
}

QString incidentsDetailText(const QList<Incident> &incidents) {
    if (incidents.isEmpty()) {
        return QStringLiteral("no incidents");
    }
    QString text;
    for (const Incident &incident : incidents) {
        const QString status =
            incident.status == QStringLiteral("open")
                ? (incident.flapping ? QStringLiteral("FLAPPING")
                                     : QStringLiteral("OPEN    "))
                : QStringLiteral("resolved");
        text += status + QStringLiteral("  ")
            + incident.severity.toUpper().leftJustified(8, ' ')
            + incident.title + QStringLiteral("  x%1").arg(incident.count)
            + QStringLiteral("  owner %1")
                  .arg(incident.owner.isEmpty() ? QStringLiteral("unowned")
                                                : incident.owner);
        if (incident.rules.size() > 1) {
            text += QStringLiteral("  group %1 (%2 rules)")
                        .arg(incident.groupKey)
                        .arg(incident.rules.size());
        }
        text += QLatin1Char('\n');
    }
    return text.trimmed();
}

QString alertsFlappingText(const QSet<QString> &flapping, int transitions,
                           qint64 windowMs) {
    if (flapping.isEmpty()) {
        return {};
    }
    QStringList names = flapping.values();
    names.sort(Qt::CaseInsensitive);
    return QStringLiteral(
               "flapping: %1 rule(s) changed state %2 times in %3s - %4")
        .arg(names.size())
        .arg(transitions)
        .arg(windowMs / 1000)
        .arg(names.join(QStringLiteral(", ")));
}
