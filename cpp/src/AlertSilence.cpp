#include "AlertSilence.h"

#include <QDateTime>
#include <QRegularExpression>

namespace {

QString clockOf(qint64 atMs) {
    return QDateTime::fromMSecsSinceEpoch(atMs).toString(
        QStringLiteral("HH:mm"));
}

QString wildcardToPattern(const QString &matcher) {
    QString pattern;
    pattern.reserve(matcher.size() * 2);
    for (const QChar ch : matcher) {
        if (ch == QLatin1Char('*')) {
            pattern += QStringLiteral(".*");
        } else if (ch == QLatin1Char('?')) {
            pattern += QLatin1Char('.');
        } else {
            pattern += QRegularExpression::escape(QString(ch));
        }
    }
    return pattern;
}

}  // namespace

QString silenceStateAt(const SilenceRule &rule, qint64 nowMs) {
    if (rule.endsAtMs > 0 && nowMs >= rule.endsAtMs) {
        return QStringLiteral("expired");
    }
    if (rule.startsAtMs > nowMs) {
        return QStringLiteral("pending");
    }
    return QStringLiteral("active");
}

bool silenceLive(const SilenceRule &rule, qint64 nowMs) {
    return silenceStateAt(rule, nowMs) == QStringLiteral("active");
}

bool silenceMatches(const SilenceRule &rule, const QString &monitorId) {
    const QString matcher = rule.matcher.trimmed();
    if (matcher.isEmpty() || monitorId.isEmpty()) {
        return false;
    }
    if (!matcher.contains(QLatin1Char('*')) && !matcher.contains(QLatin1Char('?'))
        && !matcher.contains(QLatin1Char('['))) {
        return matcher.compare(monitorId, Qt::CaseInsensitive) == 0;
    }
    const QRegularExpression re(
        QStringLiteral("^%1$").arg(wildcardToPattern(matcher)),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(monitorId).hasMatch();
}

const SilenceRule *activeSilenceFor(const QList<SilenceRule> &rules,
                                    const QString &monitorId, qint64 nowMs) {
    const SilenceRule *best = nullptr;
    for (const SilenceRule &rule : rules) {
        if (!silenceMatches(rule, monitorId) || !silenceLive(rule, nowMs)) {
            continue;
        }
        if (best == nullptr || rule.endsAtMs > best->endsAtMs) {
            best = &rule;
        }
    }
    return best;
}

QStringList silencedMonitorIds(const QList<SilenceRule> &rules,
                               const QStringList &monitorIds, qint64 nowMs) {
    QStringList silenced;
    for (const QString &id : monitorIds) {
        if (activeSilenceFor(rules, id, nowMs) != nullptr
            && !silenced.contains(id)) {
            silenced.append(id);
        }
    }
    return silenced;
}

QStringList silencedIdsFor(const QList<SilenceRule> &rules,
                           const QList<QString> &ids, qint64 nowMs) {
    QStringList silenced;
    for (const QString &id : ids) {
        if (activeSilenceFor(rules, id, nowMs) != nullptr
            && !silenced.contains(id)) {
            silenced.append(id);
        }
    }
    return silenced;
}

qint64 parseSilenceDurationMs(const QString &text) {
    const QString value = text.trimmed().toLower();
    if (value.isEmpty()) {
        return -1;
    }
    bool plainOk = false;
    const double plainSeconds = value.toDouble(&plainOk);
    if (plainOk) {
        if (plainSeconds < 0.0) {
            return -1;
        }
        return qint64(plainSeconds * 1000.0);
    }
    qint64 totalMs = 0;
    int index = 0;
    bool sawUnit = false;
    while (index < value.size()) {
        int numberStart = index;
        while (index < value.size()
               && (value.at(index).isDigit() || value.at(index) == QLatin1Char('.'))) {
            ++index;
        }
        if (index == numberStart) {
            return -1;
        }
        bool numberOk = false;
        const double amount = value.mid(numberStart, index - numberStart).toDouble(&numberOk);
        if (!numberOk) {
            return -1;
        }
        QString unit;
        while (index < value.size() && value.at(index).isLetter()) {
            unit += value.at(index);
            ++index;
        }
        qint64 scale = 0;
        if (unit == QStringLiteral("ms")) {
            scale = 1;
        } else if (unit == QStringLiteral("s")) {
            scale = 1000;
        } else if (unit == QStringLiteral("m")) {
            scale = 60 * 1000;
        } else if (unit == QStringLiteral("h")) {
            scale = 60 * 60 * 1000;
        } else if (unit == QStringLiteral("d")) {
            scale = 24 * 60 * 60 * 1000;
        } else {
            return -1;
        }
        totalMs += qint64(amount * double(scale));
        sawUnit = true;
    }
    return sawUnit ? totalMs : -1;
}

QString formatSilenceRemaining(qint64 ms) {
    if (ms <= 0) {
        return QStringLiteral("expired");
    }
    const qint64 seconds = ms / 1000;
    if (seconds < 90) {
        return QStringLiteral("%1s left").arg(qMax<qint64>(1, seconds));
    }
    if (seconds < 5400) {
        return QStringLiteral("%1m left").arg(seconds / 60);
    }
    if (seconds < 172800) {
        return QStringLiteral("%1h %2m left").arg(seconds / 3600).arg(
            (seconds % 3600) / 60);
    }
    return QStringLiteral("%1d left").arg(seconds / 86400);
}

QString silenceSummaryText(const QList<SilenceRule> &rules, qint64 nowMs) {
    if (rules.isEmpty()) {
        return QStringLiteral(
            "silences: none - a silence mutes one monitor for a while without "
            "disabling it");
    }
    int active = 0;
    int pending = 0;
    int expired = 0;
    for (const SilenceRule &rule : rules) {
        const QString state = silenceStateAt(rule, nowMs);
        if (state == QStringLiteral("active")) {
            ++active;
        } else if (state == QStringLiteral("pending")) {
            ++pending;
        } else {
            ++expired;
        }
    }
    return QStringLiteral("silences: %1 active, %2 pending, %3 expired")
        .arg(active)
        .arg(pending)
        .arg(expired);
}

QString silenceRowText(const SilenceRule &rule, qint64 nowMs) {
    const QString state = silenceStateAt(rule, nowMs);
    QString when;
    if (state == QStringLiteral("active")) {
        when = formatSilenceRemaining(rule.endsAtMs - nowMs);
    } else if (state == QStringLiteral("pending")) {
        when = QStringLiteral("starts %1").arg(clockOf(rule.startsAtMs));
    } else {
        when = QStringLiteral("ended %1").arg(clockOf(rule.endsAtMs));
    }
    const QString comment =
        rule.comment.isEmpty() ? QStringLiteral("--") : rule.comment;
    const QString author =
        rule.author.isEmpty() ? QStringLiteral("--") : rule.author;
    return QStringLiteral("%1  %2  %3  by %4  %5")
        .arg(rule.matcher, state, when, author, comment);
}

QString silencedNoteText(const QStringList &ids, const QList<SilenceRule> &rules,
                         qint64 nowMs) {
    if (ids.isEmpty()) {
        return QString();
    }
    QStringList parts;
    for (const QString &id : ids) {
        const SilenceRule *rule = activeSilenceFor(rules, id, nowMs);
        if (rule == nullptr) {
            continue;
        }
        parts.append(QStringLiteral("%1 (%2)")
                         .arg(id, formatSilenceRemaining(rule->endsAtMs - nowMs)));
    }
    if (parts.isEmpty()) {
        return QString();
    }
    return QStringLiteral("silenced: %1").arg(parts.join(QStringLiteral(", ")));
}
