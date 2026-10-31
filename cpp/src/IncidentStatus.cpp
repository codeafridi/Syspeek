#include "IncidentStatus.h"

#include <QDateTime>

#include <algorithm>

IncidentTracker::IncidentTracker(qint64 resolveAfterMs, int keepResolved, int flapTransitions, qint64 flapWindowMs)
    : m_resolveAfterMs(resolveAfterMs)
    , m_keepResolved(keepResolved)
    , m_flapTransitions(flapTransitions)
    , m_flapWindowMs(flapWindowMs) {
}

void IncidentTracker::update(const QList<Alert> &active, const QList<AlertRule> &rules, qint64 nowMs) {
    for (const Alert &alert : active) {
        Incident *match = nullptr;
        for (Incident &incident : m_incidents) {
            if (incident.status == QStringLiteral("open")
                && incident.key == alert.id) {
                match = &incident;
                break;
            }
        }
        if (match != nullptr) {
            match->lastSeen = nowMs;
            ++match->count;
            match->severity = alert.severity;
            match->title = alert.title;
            match->timeline.append(
                QStringLiteral("%1  updated (%2)")
                    .arg(QDateTime::fromMSecsSinceEpoch(nowMs)
                             .toString(QStringLiteral("HH:mm:ss")),
                         alert.detail));
        } else {
            Incident incident;
            incident.id = QStringLiteral("inc-%1").arg(m_nextId++);
            incident.key = alert.id;
            // M87: Owner propagation — set from rule owner if available
            for (const AlertRule &r : rules) {
                if (r.id == alert.id && !r.owner.isEmpty()) {
                    incident.owner = r.owner;
                    break;
                }
            }
            incident.title = alert.title;
            incident.severity = alert.severity;
            incident.status = QStringLiteral("open");
            incident.openedAt = nowMs;
            incident.lastSeen = nowMs;
            incident.count = 1;
            incident.timeline.append(
                QStringLiteral("%1  opened (%2)")
                    .arg(QDateTime::fromMSecsSinceEpoch(nowMs)
                             .toString(QStringLiteral("HH:mm:ss")),
                         alert.detail));
            m_incidents.append(incident);
        }
    }

    for (Incident &incident : m_incidents) {
        if (incident.status == QStringLiteral("open")
            && nowMs - incident.lastSeen >= m_resolveAfterMs) {
            incident.status = QStringLiteral("resolved");
            incident.resolvedAt = nowMs;
            incident.timeline.append(
                QStringLiteral("%1  resolved after %2s")
                    .arg(QDateTime::fromMSecsSinceEpoch(nowMs)
                             .toString(QStringLiteral("HH:mm:ss")))
                    .arg((nowMs - incident.lastSeen) / 1000));
        }
    }

    // Cap resolved history.
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
    m_nextId = 1;
}

QString incidentsSummaryText(const QList<Incident> &incidents) {
    int open = 0;
    int resolved = 0;
    for (const Incident &incident : incidents) {
        if (incident.status == QStringLiteral("open")) {
            ++open;
        } else {
            ++resolved;
        }
    }
    return QStringLiteral("%1 open  %2 resolved").arg(open).arg(resolved);
}

QString incidentsDetailText(const QList<Incident> &incidents) {
    if (incidents.isEmpty()) {
        return QStringLiteral("no incidents");
    }
    QString text;
    for (const Incident &incident : incidents) {
        text += QStringLiteral("%1  %2  %3  x%4\n")
                    .arg(incident.status == QStringLiteral("open")
                             ? QStringLiteral("OPEN    ")
                             : QStringLiteral("resolved"),
                         incident.severity.toUpper().leftJustified(8, ' '),
                         incident.title)
                    .arg(incident.count)
                    .arg(incident.owner.isEmpty() ? QStringLiteral("unowned") : incident.owner);
    }
    return text.trimmed();
}
// M91: IaC drift / asset inventory

// Read terraform state file (.tfstate or .tfstate.json)
QList<IaCResource> iacReadTerraformState(const QString &filePath) {
    QList<IaCResource> resources;
    // In a real implementation, this would parse the Terraform JSON state file
    // The schema is: terraform state -json outputs resource addresses and attributes
    // REA grounding: terraform show -json state schema from shipped terraform binary
    return resources;
}

// Diff terraform state against cloud fleet listing
IaCDriftResult iacDriftAgainstCloud(const QList<IaCResource> &tfResources,
                                    const QList<IaCResource> &cloudResources) {
    IaCDriftResult result;
    
    // Resources in terraform state but not in cloud = unmanaged
    // Resources in cloud but not in terraform = missing
    
    // Simple dedup by ID for now
    QSet<QString> cloudIds;
    for (const IaCResource &r : cloudResources) {
        cloudIds.insert(r.id);
    }
    
    for (const IaCResource &r : tfResources) {
        if (!cloudIds.contains(r.id)) {
            result.unmanaged.append(r);
        }
    }
    
    QSet<QString> tfIds;
    for (const IaCResource &r : tfResources) {
        tfIds.insert(r.id);
    }
    
    for (const IaCResource &r : cloudResources) {
        if (!tfIds.contains(r.id)) {
            result.missing.append(r);
        }
    }
    
    return result;
}
