#include "ServicesStatus.h"

#include <QDateTime>
#include <QHash>
#include <QSet>

#include <algorithm>

bool parseSystemctlUnits(const QByteArray &text, QList<UnitRow> &out) {
    const QList<QByteArray> lines = text.split('\n');
    bool any = false;

    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        QList<QByteArray> fields = trimmed.simplified().split(' ');
        // systemctl marks failed units with a leading U+25CF bullet.
        if (!fields.isEmpty()
            && fields.first() == QByteArrayLiteral("\xE2\x97\x8F")) {
            fields.removeFirst();
        }
        if (fields.size() < 4) {
            continue;
        }

        UnitRow row;
        row.name = QString::fromUtf8(fields.at(0));
        row.load = QString::fromUtf8(fields.at(1));
        row.active = QString::fromUtf8(fields.at(2));
        row.sub = QString::fromUtf8(fields.at(3));
        row.description = QString::fromUtf8(fields.mid(4).join(' '));
        if (!row.name.contains(QLatin1Char('.'))) {
            continue; // skip summary/footer lines such as "19 loaded units listed."
        }
        out.append(row);
        any = true;
    }

    return any;
}

bool parseLaunchctlList(const QByteArray &text, QList<UnitRow> &out) {
    const QList<QByteArray> lines = text.split('\n');
    bool any = false;

    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        QList<QByteArray> fields = trimmed.split('\t');
        if (fields.size() < 2) {
            fields = trimmed.simplified().split(' ');
        }
        if (fields.isEmpty()
            || fields.first() == QByteArrayLiteral("PID")) {
            continue;
        }
        if (fields.size() < 2) {
            continue;
        }

        UnitRow row;
        row.name = QString::fromUtf8(fields.size() > 2
                                         ? fields.at(2)
                                         : fields.at(1));
        if (row.name.isEmpty()) {
            continue;
        }
        row.load = QStringLiteral("loaded");
        if (fields.size() > 2) {
            const QString pid = QString::fromUtf8(fields.at(0));
            const QString status = QString::fromUtf8(fields.at(1));
            if (pid != QStringLiteral("-")) {
                row.active = QStringLiteral("active");
                row.sub = QStringLiteral("running");
            } else if (status.toInt() != 0) {
                row.active = QStringLiteral("failed");
                row.sub = QStringLiteral("failed");
            } else {
                row.active = QStringLiteral("inactive");
                row.sub = QStringLiteral("dead");
            }
        } else {
            row.active = QStringLiteral("inactive");
            row.sub = QStringLiteral("dead");
        }
        out.append(row);
        any = true;
    }
    return any;
}

bool serviceRecordsToUnits(const QList<ServiceRecord> &records, QList<UnitRow> &out) {
    bool any = false;

    QList<ServiceRecord> sorted = records;
    std::sort(sorted.begin(), sorted.end(),
              [](const ServiceRecord &a, const ServiceRecord &b) {
                  return a.name < b.name;
              });

    for (const ServiceRecord &rec : sorted) {
        if (rec.name.isEmpty()) {
            continue;
        }

        UnitRow row;
        row.name = rec.name;
        row.description = rec.displayName;
        row.load = QStringLiteral("loaded");

        // dwCurrentState values are the winsvc.h SERVICE_* constants.
        switch (rec.state) {
        case 4: // SERVICE_RUNNING
            row.active = QStringLiteral("active");
            row.sub = QStringLiteral("running");
            break;
        case 1: // SERVICE_STOPPED
            // ERROR_SERVICE_NOT_ACTIVE (1062) / ERROR_SERVICE_NEVER_STARTED
            // (1077) mark services that simply have not run, not failures
            // (winerror.h 937/952); dwWin32ExitCode != 0 otherwise is a
            // genuine failure exit.
            if (rec.exitCode != 0 && rec.exitCode != 1062
                && rec.exitCode != 1077) {
                row.active = QStringLiteral("failed");
                row.sub = QStringLiteral("failed");
            } else {
                row.active = QStringLiteral("inactive");
                row.sub = QStringLiteral("dead");
            }
            break;
        case 2: // SERVICE_START_PENDING
        case 5: // SERVICE_CONTINUE_PENDING
        case 6: // SERVICE_PAUSE_PENDING
            row.active = QStringLiteral("activating");
            row.sub = QStringLiteral("start-pending");
            break;
        case 3: // SERVICE_STOP_PENDING
            row.active = QStringLiteral("deactivating");
            row.sub = QStringLiteral("stop-pending");
            break;
        case 7: // SERVICE_PAUSED
            row.active = QStringLiteral("active");
            row.sub = QStringLiteral("paused");
            break;
        default:
            row.active = QStringLiteral("unknown");
            row.sub = QStringLiteral("unknown");
            break;
        }

        out.append(row);
        any = true;
    }
    return any;
}

int servicesFailedCount(const QList<UnitRow> &units) {
    int count = 0;
    for (const UnitRow &row : units) {
        if (row.active == QStringLiteral("failed")
            || row.sub == QStringLiteral("failed")) {
            ++count;
        }
    }
    return count;
}

int servicesRunningCount(const QList<UnitRow> &units) {
    int count = 0;
    for (const UnitRow &row : units) {
        if (row.active == QStringLiteral("active")
            && row.sub == QStringLiteral("running")) {
            ++count;
        }
    }
    return count;
}

QString servicesSummaryText(const ServicesSnapshot &snapshot) {
    if (!snapshot.available) {
        return snapshot.errorMessage.isEmpty()
                   ? QStringLiteral("services: --")
                   : QStringLiteral("services: %1").arg(snapshot.errorMessage);
    }
    return QStringLiteral("services: %1 running | %2 failed")
        .arg(snapshot.runningCount)
        .arg(snapshot.failedCount);
}

QString servicesDetailText(const ServicesSnapshot &snapshot) {
    QStringList lines;
    for (const UnitRow &row : snapshot.units) {
        if (row.active == QStringLiteral("failed")
            || row.sub == QStringLiteral("failed")) {
            lines.append(QStringLiteral("%1 [%2/%3]").arg(row.name, row.active,
                                                          row.sub));
        }
    }
    return lines.isEmpty() ? QStringLiteral("no issues") : lines.join('\n');
}

QList<ServiceChange> diffServices(const QList<UnitRow> &prev,
                                  const QList<UnitRow> &cur, qint64 nowMs) {
    QList<ServiceChange> changes;

    QHash<QString, UnitRow> prevMap;
    for (const UnitRow &unit : prev) {
        prevMap.insert(unit.name, unit);
    }

    QSet<QString> curNames;
    for (const UnitRow &unit : cur) {
        curNames.insert(unit.name);
        const auto it = prevMap.constFind(unit.name);
        if (it == prevMap.constEnd()) {
            changes.append({nowMs, unit.name,
                            QStringLiteral("appeared (%1/%2)")
                                .arg(unit.active, unit.sub)});
            continue;
        }
        if (it->active != unit.active || it->sub != unit.sub) {
            changes.append({nowMs, unit.name,
                            QStringLiteral("%1/%2 -> %3/%4")
                                .arg(it->active, it->sub, unit.active, unit.sub)});
        }
    }

    for (const UnitRow &unit : prev) {
        if (!curNames.contains(unit.name)) {
            changes.append({nowMs, unit.name, QStringLiteral("disappeared")});
        }
    }

    return changes;
}

QString servicesChangesText(const QList<ServiceChange> &changes) {
    if (changes.isEmpty()) {
        return QStringLiteral("no recent changes");
    }
    QStringList lines;
    for (const ServiceChange &change : changes) {
        const QString time =
            QDateTime::fromMSecsSinceEpoch(change.atMs)
                .toString(QStringLiteral("HH:mm:ss"));
        lines.append(QStringLiteral("%1  %2: %3")
                         .arg(time, change.unit, change.summary));
        if (lines.size() >= 40) {
            lines.append(QStringLiteral("..."));
            break;
        }
    }
    return lines.join(QLatin1Char('\n'));
}
