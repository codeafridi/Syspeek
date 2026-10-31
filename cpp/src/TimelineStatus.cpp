#include "TimelineStatus.h"

#include <QDateTime>
#include <QtGlobal>

#include <algorithm>

namespace {

QString groupThousands(qint64 value)
{
    const QString digits = QString::number(value);
    QString out;
    out.reserve(digits.size() + digits.size() / 3);
    const int lead = digits.size() % 3;
    for (int i = 0; i < digits.size(); ++i) {
        if (i > 0 && (i - lead) % 3 == 0) {
            out.append(QLatin1Char(','));
        }
        out.append(digits.at(i));
    }
    return out;
}

QString clockText(qint64 atMs)
{
    return QDateTime::fromMSecsSinceEpoch(atMs).toString(
        QStringLiteral("HH:mm:ss"));
}

QString stampText(qint64 atMs)
{
    return QDateTime::fromMSecsSinceEpoch(atMs).toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace

QString timelineKindName(TimelineKind kind)
{
    switch (kind) {
    case TimelineKind::Alert:
        return QStringLiteral("alert");
    case TimelineKind::Incident:
        return QStringLiteral("incident");
    case TimelineKind::ProcessAction:
        return QStringLiteral("process");
    case TimelineKind::ContainerAction:
        return QStringLiteral("container");
    case TimelineKind::ServiceAction:
        return QStringLiteral("restart");
    case TimelineKind::FileChange:
        return QStringLiteral("file");
    case TimelineKind::Kubernetes:
        return QStringLiteral("kube");
    case TimelineKind::Service:
        return QStringLiteral("unit");
    case TimelineKind::Synthetic:
        return QStringLiteral("check");
    case TimelineKind::Cloud:
        return QStringLiteral("cloud");
    case TimelineKind::Other:
        break;
    }
    return QStringLiteral("event");
}

TimelineKind timelineKindFromName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key.isEmpty() || key == QLatin1String("any")) {
        return TimelineKind::Other;
    }
    if (key == QLatin1String("alert")) {
        return TimelineKind::Alert;
    }
    if (key == QLatin1String("incident")) {
        return TimelineKind::Incident;
    }
    if (key == QLatin1String("process") || key == QLatin1String("action")) {
        return TimelineKind::ProcessAction;
    }
    if (key == QLatin1String("container")) {
        return TimelineKind::ContainerAction;
    }
    if (key == QLatin1String("restart")) {
        return TimelineKind::ServiceAction;
    }
    if (key == QLatin1String("file") || key == QLatin1String("fim")) {
        return TimelineKind::FileChange;
    }
    if (key == QLatin1String("kube") || key == QLatin1String("kubernetes")) {
        return TimelineKind::Kubernetes;
    }
    if (key == QLatin1String("unit") || key == QLatin1String("service")) {
        return TimelineKind::Service;
    }
    if (key == QLatin1String("check") || key == QLatin1String("synthetic")) {
        return TimelineKind::Synthetic;
    }
    if (key == QLatin1String("cloud")) {
        return TimelineKind::Cloud;
    }
    return TimelineKind::Other;
}

QString timelineSeverityName(TimelineSeverity severity)
{
    switch (severity) {
    case TimelineSeverity::Error:
        return QStringLiteral("error");
    case TimelineSeverity::Warning:
        return QStringLiteral("warn");
    case TimelineSeverity::Ok:
        return QStringLiteral("ok");
    case TimelineSeverity::Info:
        break;
    }
    return QStringLiteral("info");
}

int timelineSeverityRank(TimelineSeverity severity)
{
    switch (severity) {
    case TimelineSeverity::Error:
        return 3;
    case TimelineSeverity::Warning:
        return 2;
    case TimelineSeverity::Info:
        return 1;
    case TimelineSeverity::Ok:
        break;
    }
    return 0;
}

TimelineSeverity timelineSeverityFromText(const QString &text)
{
    const QString key = text.trimmed().toLower();
    if (key == QLatin1String("critical") || key == QLatin1String("crit")
        || key == QLatin1String("high") || key == QLatin1String("error")
        || key == QLatin1String("down") || key == QLatin1String("failed")) {
        return TimelineSeverity::Error;
    }
    if (key == QLatin1String("warning") || key == QLatin1String("warn")) {
        return TimelineSeverity::Warning;
    }
    if (key == QLatin1String("ok") || key == QLatin1String("success")
        || key == QLatin1String("up") || key == QLatin1String("healthy")
        || key == QLatin1String("resolved")) {
        return TimelineSeverity::Ok;
    }
    return TimelineSeverity::Info;
}

void timelineAppend(QList<TimelineEvent> &ring, const TimelineEvent &event,
                    int cap)
{
    ring.append(event);
    if (cap <= 0) {
        return;
    }
    while (ring.size() > cap) {
        ring.removeFirst();
    }
}

QList<TimelineEvent> timelineNear(const QList<TimelineEvent> &events,
                                  qint64 atMs, qint64 windowMs, int limit)
{
    QList<TimelineEvent> hits;
    if (windowMs < 0) {
        return hits;
    }
    for (const TimelineEvent &event : events) {
        const qint64 delta = event.atMs > atMs ? event.atMs - atMs
                                               : atMs - event.atMs;
        if (delta <= windowMs) {
            hits.append(event);
        }
    }
    std::sort(hits.begin(), hits.end(),
              [atMs](const TimelineEvent &a, const TimelineEvent &b) {
                  const qint64 da = a.atMs > atMs ? a.atMs - atMs : atMs - a.atMs;
                  const qint64 db = b.atMs > atMs ? b.atMs - atMs : atMs - b.atMs;
                  if (da != db) {
                      return da < db;
                  }
                  const int ra = timelineSeverityRank(a.severity);
                  const int rb = timelineSeverityRank(b.severity);
                  if (ra != rb) {
                      return ra > rb;
                  }
                  return a.atMs > b.atMs;
              });
    if (limit > 0 && hits.size() > limit) {
        hits = hits.mid(0, limit);
    }
    return hits;
}

QList<TimelineEvent> timelineFilter(const QList<TimelineEvent> &events,
                                    TimelineKind kind, const QString &needle)
{
    const QString key = needle.trimmed();
    QList<TimelineEvent> out;
    for (const TimelineEvent &event : events) {
        if (kind != TimelineKind::Other && event.kind != kind) {
            continue;
        }
        if (!key.isEmpty()) {
            const QString hay = event.subject + QLatin1Char('\n') + event.title
                + QLatin1Char('\n') + event.detail + QLatin1Char('\n')
                + event.tags.join(QLatin1Char(' '));
            if (!hay.contains(key, Qt::CaseInsensitive)) {
                continue;
            }
        }
        out.append(event);
    }
    std::sort(out.begin(), out.end(),
              [](const TimelineEvent &a, const TimelineEvent &b) {
                  if (a.atMs != b.atMs) {
                      return a.atMs > b.atMs;
                  }
                  return timelineSeverityRank(a.severity)
                      > timelineSeverityRank(b.severity);
              });
    return out;
}

QStringList timelineSubjects(const QList<TimelineEvent> &events)
{
    QStringList out;
    for (const TimelineEvent &event : events) {
        if (!event.subject.isEmpty() && !out.contains(event.subject)) {
            out.append(event.subject);
        }
    }
    return out;
}

int timelineCountWith(const QList<TimelineEvent> &events,
                      TimelineSeverity severity)
{
    int count = 0;
    for (const TimelineEvent &event : events) {
        if (event.severity == severity) {
            ++count;
        }
    }
    return count;
}

QString timelineAgeText(qint64 deltaMs)
{
    if (deltaMs < 0) {
        deltaMs = 0;
    }
    const qint64 seconds = deltaMs / 1000;
    if (seconds < 1) {
        return QStringLiteral("now");
    }
    if (seconds < 60) {
        return QStringLiteral("%1s").arg(seconds);
    }
    const qint64 minutes = seconds / 60;
    if (minutes < 90) {
        return QStringLiteral("%1m").arg(minutes);
    }
    const qint64 hours = minutes / 60;
    if (hours < 36) {
        return QStringLiteral("%1h").arg(hours);
    }
    return QStringLiteral("%1d").arg(hours / 24);
}

QString timelineRowText(const TimelineEvent &event, qint64 nowMs)
{
    return QStringLiteral("%1  %2  %3  %4  %5")
        .arg(clockText(event.atMs), -8)
        .arg(timelineSeverityName(event.severity), -5)
        .arg(timelineKindName(event.kind), -9)
        .arg(event.subject.isEmpty() ? QStringLiteral("-") : event.subject, -22)
        .arg(event.title.isEmpty() ? event.detail : event.title)
        .trimmed()
        + QStringLiteral("  (+%1)").arg(timelineAgeText(nowMs - event.atMs));
}

QString timelineDetailText(const TimelineEvent &event, qint64 nowMs)
{
    QStringList lines;
    lines.append(QStringLiteral("%1  %2  %3")
                     .arg(stampText(event.atMs),
                          timelineKindName(event.kind),
                          timelineSeverityName(event.severity)));
    lines.append(QStringLiteral("subject  %1")
                     .arg(event.subject.isEmpty() ? QStringLiteral("-")
                                                  : event.subject));
    if (!event.title.isEmpty()) {
        lines.append(QStringLiteral("event    %1").arg(event.title));
    }
    lines.append(QStringLiteral("age      %1")
                     .arg(timelineAgeText(nowMs - event.atMs)));
    if (!event.tags.isEmpty()) {
        lines.append(QStringLiteral("tags     %1")
                         .arg(event.tags.join(QStringLiteral(", "))));
    }
    if (!event.detail.isEmpty()) {
        lines.append(QStringLiteral("detail   %1").arg(event.detail));
    }
    return lines.join(QLatin1Char('\n'));
}

QString timelineSummaryText(const QList<TimelineEvent> &events, qint64 nowMs,
                            int capacity, bool enabled)
{
    if (!enabled) {
        return QStringLiteral("timeline: disabled in [timeline] - no events "
                              "are recorded");
    }
    if (events.isEmpty()) {
        return QStringLiteral("timeline: waiting for the first event - alerts, "
                              "actions, file changes and cluster changes land "
                              "here");
    }
    const int errors = timelineCountWith(events, TimelineSeverity::Error);
    const int warnings = timelineCountWith(events, TimelineSeverity::Warning);
    const int subjects = int(timelineSubjects(events).size());
    QString head = QStringLiteral(
        "%1 events - %2 errors, %3 warnings, %4 subjects")
                       .arg(groupThousands(qint64(events.size())))
                       .arg(errors)
                       .arg(warnings)
                       .arg(subjects);
    const TimelineEvent &oldest = events.first();
    const TimelineEvent &newest = events.last();
    QString tail = QStringLiteral("ring %1 - newest %2 (%3 ago), oldest %4")
                       .arg(groupThousands(qint64(capacity)))
                       .arg(clockText(newest.atMs))
                       .arg(timelineAgeText(nowMs - newest.atMs))
                       .arg(clockText(oldest.atMs));
    return head + QStringLiteral("  -  ") + tail;
}

QString timelineWindowText(qint64 atMs, qint64 windowMs, int count)
{
    const qint64 seconds = windowMs / 1000;
    if (count <= 0) {
        return QStringLiteral("%1 +-%2s: no events in this window")
            .arg(clockText(atMs))
            .arg(seconds);
    }
    return QStringLiteral("%1 +-%2s: %3 event%4 in this window")
        .arg(clockText(atMs))
        .arg(seconds)
        .arg(count)
        .arg(count == 1 ? QString() : QStringLiteral("s"));
}

QString timelineNoEventText(bool enabled)
{
    if (!enabled) {
        return QStringLiteral("timeline: disabled in [timeline] - the stream "
                              "stays empty");
    }
    return QStringLiteral("nothing has happened yet - a breach, an action, a "
                          "file change or a cluster change will appear here");
}
