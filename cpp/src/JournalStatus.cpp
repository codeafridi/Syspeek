#include "JournalStatus.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>

namespace {

QString decodeMessage(const QJsonValue &value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isArray()) {
        QByteArray bytes;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) {
            bytes.append(static_cast<char>(item.toInt()));
        }
        return QString::fromUtf8(bytes);
    }
    return QString();
}

} // namespace

bool parseJournalJson(const QByteArray &text, QList<JournalEntry> &out) {
    const QList<QByteArray> lines = text.split('\n');
    bool any = false;

    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty() || !trimmed.startsWith('{')) {
            continue;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(trimmed, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }
        const QJsonObject object = document.object();

        JournalEntry entry;
        entry.timestampUsec =
            object.value(QStringLiteral("__REALTIME_TIMESTAMP")).toString().toLongLong();
        entry.priority =
            object.value(QStringLiteral("PRIORITY")).toString().toInt();
        entry.pid = object.value(QStringLiteral("_PID")).toString().toInt();
        entry.comm = object.value(QStringLiteral("_COMM")).toString();
        if (entry.comm.isEmpty()) {
            entry.comm = object.value(QStringLiteral("SYSLOG_IDENTIFIER")).toString();
        }
        entry.message = decodeMessage(object.value(QStringLiteral("MESSAGE")));
        out.append(entry);
        any = true;
    }

    return any;
}

bool journalEntryIsError(const JournalEntry &entry) {
    return entry.priority >= 0 && entry.priority <= 3;
}

int journalErrorCount(const QList<JournalEntry> &entries) {
    int count = 0;
    for (const JournalEntry &entry : entries) {
        if (journalEntryIsError(entry)) {
            ++count;
        }
    }
    return count;
}

QString journalFailureText(int exitCode, const QString &standardError) {
    QString reason = standardError.trimmed();
    const int newline = reason.indexOf(QLatin1Char('\n'));
    if (newline > 0) {
        reason = reason.left(newline).trimmed();
    }
    if (reason.size() > 120) {
        reason = reason.left(117) + QStringLiteral("...");
    }
    if (reason.isEmpty()) {
        return QStringLiteral("journalctl failed (exit %1)").arg(exitCode);
    }
    return QStringLiteral("journalctl failed (exit %1): %2")
        .arg(exitCode)
        .arg(reason);
}

QString journalSummaryText(const JournalSnapshot &snapshot) {
    if (!snapshot.available) {
        return snapshot.errorMessage.isEmpty()
                   ? QStringLiteral("logs: --")
                   : QStringLiteral("logs: %1").arg(snapshot.errorMessage);
    }
    return QStringLiteral("journald: %1 entries | %2 errors")
        .arg(snapshot.entries.size())
        .arg(snapshot.errorCount);
}

QString journalLogsText(const JournalSnapshot &snapshot, int maxLines) {
    QStringList lines;
    const int start = qMax(0, snapshot.entries.size() - maxLines);
    for (int i = start; i < snapshot.entries.size(); ++i) {
        const JournalEntry &entry = snapshot.entries.at(i);
        const QString time =
            QDateTime::fromMSecsSinceEpoch(entry.timestampUsec / 1000)
                .toString(QStringLiteral("HH:mm:ss"));
        lines.append(QStringLiteral("%1  %2: %3")
                         .arg(time, entry.comm, entry.message));
    }
    return lines.isEmpty() ? QStringLiteral("no log entries") : lines.join('\n');
}

QString journalErrorsText(const JournalSnapshot &snapshot) {
    QStringList lines;
    for (const JournalEntry &entry : snapshot.entries) {
        if (!journalEntryIsError(entry)) {
            continue;
        }
        const QString time =
            QDateTime::fromMSecsSinceEpoch(entry.timestampUsec / 1000)
                .toString(QStringLiteral("HH:mm:ss"));
        lines.append(QStringLiteral("%1  %2: %3").arg(time, entry.comm,
                                                      entry.message));
        if (lines.size() >= 60) {
            break;
        }
    }
    return lines.isEmpty() ? QStringLiteral("no errors") : lines.join('\n');
}

QString journalFingerprint(const JournalEntry &entry) {
    QString text = entry.message.simplified();
    text.replace(QRegularExpression(QStringLiteral("0x[0-9a-fA-F]+")),
                 QStringLiteral("0x#"));
    text.replace(QRegularExpression(QStringLiteral("\"[^\"]*\"")),
                 QStringLiteral("\"...\""));
    text.replace(QRegularExpression(QStringLiteral("\\S*/\\S*")),
                 QStringLiteral("/path"));
    text.replace(QRegularExpression(QStringLiteral("\\b\\d+\\b")),
                 QStringLiteral("#"));
    if (text.isEmpty()) {
        text = QStringLiteral("(empty)");
    }
    return entry.comm + QStringLiteral("|") + text;
}

QList<ErrorGroup> groupJournalErrors(const QList<JournalEntry> &entries) {
    QHash<QString, ErrorGroup> groups;
    for (const JournalEntry &entry : entries) {
        if (!journalEntryIsError(entry)) {
            continue;
        }
        const QString fingerprint = journalFingerprint(entry);
        auto it = groups.find(fingerprint);
        if (it == groups.end()) {
            ErrorGroup group;
            group.fingerprint = fingerprint;
            group.comm = entry.comm;
            group.sample = entry.message;
            group.count = 1;
            group.lastSeenUsec = entry.timestampUsec;
            groups.insert(fingerprint, group);
        } else {
            it->count += 1;
            if (entry.timestampUsec > it->lastSeenUsec) {
                it->lastSeenUsec = entry.timestampUsec;
            }
        }
    }

    QList<ErrorGroup> out = groups.values();
    std::sort(out.begin(), out.end(),
              [](const ErrorGroup &a, const ErrorGroup &b) {
                  if (a.count != b.count) {
                      return a.count > b.count;
                  }
                  return a.comm < b.comm;
              });
    return out;
}

QString journalErrorGroupsText(const QList<ErrorGroup> &groups) {
    if (groups.isEmpty()) {
        return QStringLiteral("no errors");
    }
    QStringList lines;
    for (const ErrorGroup &group : groups) {
        lines.append(QStringLiteral("x%1  %2: %3")
                         .arg(group.count)
                         .arg(group.comm, group.sample));
        if (lines.size() >= 40) {
            lines.append(QStringLiteral("..."));
            break;
        }
    }
    return lines.join(QLatin1Char('\n'));
}
