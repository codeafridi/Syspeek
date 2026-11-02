#pragma once

#include "AlertEngine.h"
#include "DisplaySnapshot.h"
#include "IncidentStatus.h"
#include "MetricsExport.h"
#include "TimelineStatus.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

// A Syspeek bundle is one self-contained file that carries enough of a
// machine's state for somebody else to understand what happened, without
// installing anything and without a server.
//
// It exists because the product's best asset - "why did this spike" - otherwise
// dies with the window it was rendered in. Everything here is local: the file is
// written where the user asks and nothing is transmitted.
//
// Integrity, not authenticity: the bundle carries a SHA-256 digest of its own
// body so a truncated or edited file is refused on open. It does NOT prove who
// wrote it - there is no key and no PKI here, and pretending otherwise would be
// worse than saying so.
struct BundleLimits {
    int maxSamples = 900;   // ~15 min at 1 s
    int maxEvents = 400;
    int maxProcesses = 15;
};

// The bundle body, without the digest. Split out so the digest can be computed
// over exactly what gets written.
QJsonObject bundleBody(const DisplaySnapshot &snapshot,
                       const QList<TimelineEvent> &events,
                       const QList<ExportRow> &history,
                       const QList<Incident> &incidents,
                       const QList<AlertRule> &monitors,
                       const BundleLimits &limits = BundleLimits{},
                       qint64 nowMs = 0);

QString bundleDigest(const QJsonObject &body);

// Wraps the body with its digest plus the envelope (schema, tool, generated at).
QByteArray bundleEncode(const QJsonObject &body, const QString &toolVersion);

// Parses and verifies. On failure `error` says why, in a sentence a user could
// act on ("the digest does not match - the file was edited or truncated").
bool bundleDecode(const QByteArray &json, QJsonObject *bundle, QString *error);

// One-screen read-out for `syspeek bundle --inspect` and for the recipient who
// has no Syspeek installed.
QString bundleSummaryText(const QJsonObject &bundle);
// Every event and the samples, as plain lines - what a human reads first.
QString bundleDetailText(const QJsonObject &bundle);

QString bundleFileName(qint64 nowMs);
