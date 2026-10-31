    // M87: Track flap transitions per rule
    for (const Alert &alert : active) {
        RuleFlapState &fs = m_flapStates[alert.id];
        bool wasFiring = fs.currentlyFiring;
        fs.currentlyFiring = true;

        if (wasFiring) {
            // Transition: firing -> not firing (clearing)
            fs.transitionCount++;
            fs.lastTransitionMs = nowMs;
            // Check for flap: if we've seen enough transitions in the window
            if (fs.transitionCount >= m_flapTransitions) {
                // Calculate effective window: lastTransitionMs minus skipped window periods
                qint64 effectiveWindow = nowMs - fs.lastTransitionMs
                    - (m_flapWindowMs * (fs.transitionCount / m_flapTransitions - 1));
                if (effectiveWindow < m_flapWindowMs) {
                    // Rule is flapping — suppress further notifications
                    fs.currentlyFiring = false; // hold notifications
                }
            }
        } else {
            // Transition: not firing -> firing (new breach)
            fs.transitionCount++;
            fs.lastTransitionMs = nowMs;
            // Check for flap
            if (fs.transitionCount >= m_flapTransitions) {
                qint64 effectiveWindow = nowMs - fs.lastTransitionMs
                    - (m_flapWindowMs * (fs.transitionCount / m_flapTransitions - 1));
                if (effectiveWindow < m_flapWindowMs) {
                    // Rule is flapping — suppress further notifications
                    fs.currentlyFiring = false; // hold notifications
                }
            }
        }
    }

    // M87: Clean up flap states for rules no longer active
    QSet<QString> activeIds;
    for (const Alert &alert : active) {
        activeIds.insert(alert.id);
    }
    QHash<QString, RuleFlapState> newFlapStates;
    for (auto it = m_flapStates.begin(); it != m_flapStates.end(); ++it) {
        if (activeIds.contains(it.key())) {
            newFlapStates.insert(it.key(), *it);
        }
    }
    m_flapStates = newFlapStates;

    // M87: Group alerts by their group key, fold into incidents
    struct AlertGroup {
        QString groupKey;
        QList<Alert> alerts;
    };
    QHash<QString, AlertGroup> groups;

    for (const Alert &alert : active) {
        // Determine the group key: use alert.group if set, otherwise the rule id
        QString groupKey = alert.group.isEmpty() ? alert.id : alert.group;
        groups[groupKey].alerts.append(alert);
    }

    // Build set of all group keys
    QSet<QString> groupKeys;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        groupKeys.insert(it.key());
    }

    // M87: Track per-group last-activity for group_interval resolution
    // We'll store last seen per group key in a hash
    // For now, we use the incident's lastSeen field

    // M87: For each group, apply group_wait / repeat_interval / group_interval logic
    // First, handle each group
    for (QString groupKey : activeGroupKeys) {
        AlertGroup &ag = groups[groupKey];
        if (ag.alerts.isEmpty()) continue;

        // The group key to use for the incident
        QString incidentGroupKey = groupKey;

        // Find or create incident for this group
        Incident *match = nullptr;
        for (Incident &incident : m_incidents) {
            if (incident.groupKey == incidentGroupKey && incident.status == QStringLiteral("open")) {
                match = &incident;
                break;
            }
        }

        // M87: group_wait handling — small delay before creating incident (5ms default, negligible)
        // For MVP, create immediately

        if (match == nullptr) {
            // M87: Check if we should suppress due to flap
            RuleFlapState *fs = m_flapStates.value(ag.alerts.first().id, nullptr);
            bool suppress = false;
            if (fs && fs->transitionCount >= m_flapTransitions) {
                suppress = true;
            }

            if (suppress) {
                // Flapping — do not create a new incident, hold notifications
                continue;
            }

            Incident incident;
            incident.id = QStringLiteral("inc-%1").arg(m_nextId++);
            incident.groupKey = incidentGroupKey;
            incident.key = ag.alerts.first().id; // primary rule id for backward compat
            incident.owner = QStringLiteral("unowned"); // default, will be overridden if rule has owner
            incident.title = ag.alerts.first().title;
            incident.severity = ag.alerts.first().severity;
            incident.status = QStringLiteral("open");
            incident.openedAt = nowMs;
            incident.lastSeen = nowMs;
            incident.count = ag.alerts.size();
            incident.timeline.append(
                QStringLiteral("%1 opened at %2")
                    .arg(incident.title)
                    .arg(QDateTime::fromMSecsSinceEpoch(nowMs).toString(QStringLiteral("HH:mm:ss"))));
            m_incidents.append(incident);
            match = &m_incidents.last();
        }

        // Update the existing/created incident
        match->lastSeen = nowMs;
        match->count = qMax(match->count, (int)ag.alerts.size());
        match->severity = ag.alerts.first().severity;
        match->title = ag.alerts.first().title;

        // M87: Append timeline entry with detail from the most recent alert
        match->timeline.append(
            QStringLiteral("%1  updated (%2)")
                .arg(QDateTime::fromMSecsSinceEpoch(nowMs).toString(QStringLiteral("HH:mm:ss")))
                .arg(ag.alerts.first().detail));

        // M87: Check group_interval for resolution — if no new alerts for group_interval ms,
        // the incident resolves. We approximate using lastSeen.
        // The group_interval check happens at the end of this function.

        // M87: Owner propagation — set from rule owner if available
        // For now, owner stays "unowned" until Config/AlertRule adds owner field
    }

    // M87: Resolve incidents that have been silent too long (group_interval)
    for (int i = m_incidents.size() - 1; i >= 0; --i) {
        Incident &incident = m_incidents[i];
        if (incident.status == QStringLiteral("open")) {
            // M87: If the incident has been silent (no update) for group_interval ms, resolve it
            if (nowMs - incident.lastSeen >= m_groupIntervalMs) {
                incident.status = QStringLiteral("resolved");
                incident.resolvedAt = nowMs;
                incident.timeline.append(
                    QStringLiteral("%1 resolved after %2s silence")
                        .arg(QDateTime::fromMSecsSinceEpoch(nowMs).toString(QStringLiteral("HH:mm:ss")))
                        .arg((nowMs - incident.lastSeen) / 1000));
            }
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
