#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// One append-only event stream for the whole console: the answer to "what
// changed at this minute" that the four per-page change lists could never
// give, because each of them was text-only and scoped to its own page.
//
// The record shape is grounded on two shipped paid-SaaS artifacts:
//   Grafana 12.3.0 `annotation.Item` (xorm columns dashboard_id, panel_id,
//   alert_id, new_state, login; JSON epoch/epochEnd/panelId/alertId/tags/
//   timeEnd) and its shipped "Annotations list" panel options (limit, tags,
//   showTime, showUser, onlyInTimeRange, navigateBefore/After).
//   Datadog Agent 7.84.6 `datadog.agentpayload.EventsPayload.Event`
//   (json alert_type, event_type, aggregation_key, source_type_name,
//   msg_title, created timestamp).
// So: a wall-clock instant, a severity, an aggregation key that groups
// related events, a title, a body, and tags.
enum class TimelineKind {
    Alert,
    Incident,
    ProcessAction,
    ContainerAction,
    ServiceAction,
    FileChange,
    Kubernetes,
    Service,
    Synthetic,
    Cloud,
    Other
};

// Datadog's alert_type vocabulary (error / warning / info / success) minus
// the marketing tone: error, warning, info, ok.
enum class TimelineSeverity { Ok, Info, Warning, Error };

struct TimelineEvent {
    qint64 atMs = 0;
    TimelineKind kind = TimelineKind::Other;
    TimelineSeverity severity = TimelineSeverity::Info;
    QString subject; // aggregation key: "app:cursor", "unit:nginx", "pod:web-1"
    QString title;   // msg_title
    QString detail;  // msg_text
    QStringList tags;
};

QString timelineKindName(TimelineKind kind);
TimelineKind timelineKindFromName(const QString &name);
QString timelineSeverityName(TimelineSeverity severity);
int timelineSeverityRank(TimelineSeverity severity);
// Maps an AlertEngine severity ("critical"/"high"/"warning"/"warn") or a
// check state ("down") onto the four-level scale.
TimelineSeverity timelineSeverityFromText(const QString &text);

// Bounded ring. `cap` <= 0 keeps every event (tests), which is never what
// the HUD passes.
void timelineAppend(QList<TimelineEvent> &ring, const TimelineEvent &event,
                    int cap);

// "Why did this spike": the events nearest `atMs`, nearest first, only
// inside +-windowMs. Empty when nothing happened nearby, which the page
// states rather than drawing a blank box.
QList<TimelineEvent> timelineNear(const QList<TimelineEvent> &events,
                                  qint64 atMs, qint64 windowMs, int limit);

// Newest first, filtered by kind (Other = any kind) and by a case-insensitive
// needle over subject/title/detail/tags.
QList<TimelineEvent> timelineFilter(const QList<TimelineEvent> &events,
                                    TimelineKind kind, const QString &needle);

QStringList timelineSubjects(const QList<TimelineEvent> &events);
int timelineCountWith(const QList<TimelineEvent> &events,
                      TimelineSeverity severity);

QString timelineAgeText(qint64 deltaMs);
QString timelineRowText(const TimelineEvent &event, qint64 nowMs);
QString timelineDetailText(const TimelineEvent &event, qint64 nowMs);
QString timelineSummaryText(const QList<TimelineEvent> &events, qint64 nowMs,
                            int capacity, bool enabled);
QString timelineWindowText(qint64 atMs, qint64 windowMs, int count);
QString timelineNoEventText(bool enabled);
