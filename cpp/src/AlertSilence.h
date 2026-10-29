#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Alert silences: a temporary, authored suppression of one monitor without
// disabling the monitor itself (the rule keeps evaluating and the page keeps
// showing the breach, but no webhook delivery or incident is raised).
//
// Model grounded on the shipped Alertmanager 0.34.1 linux-amd64 release
// (/tmp/opencode/m69/alertmanager-0.34.1.linux-amd64):
//   amtool silence add --help
//     -a, --author="..."      Username for CreatedBy field
//     -d, --duration="1h"     Duration of silence
//         --start=START       Set when the silence should start. RFC3339
//         --end=END           Set when the silence should end (overwrites
//                             duration). RFC3339
//     -c, --comment=COMMENT   A comment to help describe the silence
//     amtool silence expire   expire an alertmanager silence
//     amtool silence query|import|update
//   binary strings: "*silence.SilenceState", "silencesActive",
//     "silencesPending", "silencesExpired", `json:"matchers"`,
//     `json:"startsAt"`, `json:"endsAt"`, `json:"createdBy"`,
//     `json:"comment"`, `json:"silencedBy"`, and the state words
//     "active" / "pending" / "expired".
//   amtool's matcher syntax: "amtool silence add alertname=foo node=bar" and
//     "As well as direct equality, regex matching is also supported. The '=~'
//     syntax (similar to Prometheus) is used to represent a regex match."
//
// Syspeek keeps the three-state model (pending before startsAt, active inside
// the window, expired after endsAt) and absolute millisecond bounds so a
// silence survives a HUD restart instead of silently restarting its clock.
struct SilenceRule {
    QString matcher;   // monitor id, or a wildcard/regex pattern (see below)
    QString comment;
    QString author;
    qint64 startsAtMs = 0;
    qint64 endsAtMs = 0;
};

QString silenceStateAt(const SilenceRule &rule, qint64 nowMs);
bool silenceLive(const SilenceRule &rule, qint64 nowMs);

// Matcher semantics: a plain id matches itself; a matcher containing '*', '?'
// or '[' is compiled as an anchored pattern where '*' means "any run of
// characters" and '?' exactly one (amtool's regex matcher, made forgiving).
bool silenceMatches(const SilenceRule &rule, const QString &monitorId);

// Live silence covering the monitor, newest expiry wins (a later, longer
// silence must not be masked by an earlier, shorter one).
const SilenceRule *activeSilenceFor(const QList<SilenceRule> &rules,
                                     const QString &monitorId, qint64 nowMs);

QStringList silencedMonitorIds(const QList<SilenceRule> &rules,
                               const QStringList &monitorIds, qint64 nowMs);
QStringList silencedIdsFor(const QList<SilenceRule> &rules,
                           const QList<QString> &ids, qint64 nowMs);

// Go duration as amtool accepts it: "45s", "30m", "2h", "1h30m"; a bare
// number is seconds. Returns -1 when the text is not a duration.
qint64 parseSilenceDurationMs(const QString &text);
QString formatSilenceRemaining(qint64 ms);

QString silenceSummaryText(const QList<SilenceRule> &rules, qint64 nowMs);
QString silenceRowText(const SilenceRule &rule, qint64 nowMs);
QString silencedNoteText(const QStringList &ids, const QList<SilenceRule> &rules,
                         qint64 nowMs);
