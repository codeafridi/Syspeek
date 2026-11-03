#pragma once

#include "Config.h"
#include "DisplaySnapshot.h"
#include "TimelineStatus.h"

#include <QByteArray>
#include <QList>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>

// [remote] bounds. They live here, not in Config.cpp, so the loader and the
// --dump self-test read the same numbers and cannot drift.
constexpr int kRemoteIntervalMinMs = 5000;
constexpr int kRemoteIntervalMaxMs = 300000;
constexpr int kRemoteTimeoutMinMs = 500;
constexpr int kRemoteTimeoutMaxMs = 30000;
constexpr int kRemoteSpoolMinMb = 0;
constexpr int kRemoteSpoolMaxMb = 256;

// The wire contract: one integer added to every payload. A relay that reads a
// NEWER schema refuses the envelope instead of half-parsing it, so an older
// client can never corrupt a newer store.
constexpr int kRemoteSchemaVersion = 1;
constexpr int kRemoteMaxEvents = 50;

void remoteValidate(RemoteOptions &options);

// Bare host -> https. Deliberately the opposite of fleetNormalizePeerUrl(), which
// assumes http because a fleet peer is a laptop on your own LAN. The relay is a
// hosted endpoint, so an entry with no scheme must not fall back to cleartext;
// a self-hosted relay says http:// explicitly.
QString remoteNormalizeUrl(const QString &spec, QString *error = nullptr);

QString remoteIngestUrl(const QString &base, QString *error = nullptr);

bool remoteConfigUsable(const RemoteOptions &options);

QString remoteSpoolPath();

QString remoteConfigNote(const RemoteOptions &options);

// The Settings GENERAL value column: no "remote: " prefix, and it names the
// spool budget, because the Settings page is where that switch is read.
QString remoteSettingsLine(const RemoteOptions &options);

// ---------------------------------------------------------------------------
// M96: the envelope. Everything here is pure: it turns a DisplaySnapshot into
// the exact bytes a report would carry, so the privacy guarantee is a test
// rather than a promise. No socket exists in this milestone.
// ---------------------------------------------------------------------------

struct RemoteMachine {
    QString id;       // M100's stable machine id; empty until it is derived
    QString name;     // [remote] name= overrides the id on the relay
    QString version;  // Syspeek version
    QString platform;
    qint64 sentAtMs = 0;
};

// Counts that live outside DisplaySnapshot today (the console polls containers
// and Kubernetes on their own sources). -1 means "this build has no reading",
// which is emitted as null rather than as a zero.
struct RemoteCounts {
    int containersRunning = -1;
    int containersIssues = -1;
    int kubeNodes = -1;
    int kubePods = -1;
};

struct RemoteSample {
    QString id; // machineId + "-" + sentAtMs, fixed at build time
    bool appPresent = false;
    QString appName;
    int appPid = -1;
    double appCpu = -1.0;
    bool hostReady = false;
    double hostCpu = -1.0;
    double hostSteal = -1.0;
    double hostLoad1 = -1.0;
    qint64 memTotalKb = -1;
    qint64 memAvailableKb = -1;
    qint64 diskTotalBytes = -1;
    qint64 diskFreeBytes = -1;
    double netRxPerSec = -1.0;
    double netTxPerSec = -1.0;
    int connections = -1;
    int listening = -1;
    int established = -1;
    int incidentsOpen = -1;
    int incidentsFlapping = -1;
    int incidentsUnowned = -1;
    int containersRunning = -1;
    int containersIssues = -1;
    int kubeNodes = -1;
    int kubePods = -1;
    qint64 uptimeSec = -1;
};

struct RemoteEvent {
    QString id;
    qint64 atMs = 0;
    QString kind; // TimelineStatus vocabulary: alert/incident/process/…
    QString severity;
    QString subject;
    QString title;
    QStringList tags;
};

struct RemoteEnvelope {
    int schema = kRemoteSchemaVersion;
    RemoteMachine machine;
    RemoteSample sample;
    QList<RemoteEvent> events;
};

QString remoteSampleId(const QString &machineId, qint64 sentAtMs);

RemoteMachine remoteMachine(const RemoteOptions &options, const QString &version,
                            const QString &platform, qint64 sentAtMs);

RemoteSample remoteBuildSample(const DisplaySnapshot &snapshot, qint64 uptimeSec,
                               const RemoteCounts &counts = RemoteCounts{});

// M86 ring -> wire events. Oldest dropped first, capped at kRemoteMaxEvents,
// order preserved, and file-integrity events never leave the machine: a path is
// exactly what the paid tier promises never to carry.
QList<RemoteEvent> remoteBuildEvents(const QList<TimelineEvent> &ring,
                                     qint64 sinceMs, qint64 nowMs,
                                     const QString &machineId);

RemoteEnvelope remoteBuildEnvelope(const RemoteMachine &machine,
                                   const DisplaySnapshot &snapshot,
                                   const QList<TimelineEvent> &ring,
                                   qint64 sinceMs, const RemoteCounts &counts,
                                   qint64 uptimeSec);

QJsonObject remoteEnvelopeObject(const RemoteEnvelope &envelope);

QByteArray remoteEnvelopeJson(const RemoteEnvelope &envelope);

// Refuses a newer schema, a missing machine id, a missing/non-object sample and
// a non-array event list, each with its own reason.
bool parseRemoteEnvelopeJson(const QByteArray &json, RemoteEnvelope *out,
                             QString *error);

// Walks every key at any depth and returns the first forbidden one, or an empty
// string when the document is clean.
QString remotePrivacyOffenders(const QJsonObject &root);

// ---------------------------------------------------------------------------
// M97: transport classification. Every answer the relay can give ends in
// exactly one state, so "it did not work" is never the whole story.
// ---------------------------------------------------------------------------

enum class RemoteClass {
    None,      // no attempt yet
    Ok,        // 2xx
    Auth,      // 401/403 - the token is wrong; retrying cannot fix it
    NotFound,  // 404 - no ingest endpoint at that address
    Rejected,  // 413/422/other 4xx - the body itself was refused
    Throttled, // 429
    Server,    // 5xx
    Transport  // connect refused, timeout, TLS failure
};

RemoteClass remoteClassify(int httpStatus);

QString remoteClassName(RemoteClass value);

// One line a person can read, including the status code when there was one.
QString remoteClassText(RemoteClass value, int httpStatus);

// Auth, NotFound and Rejected are answers about the *configuration* or the
// *body*, so resending the same request is waste. Server, Throttled and
// Transport are transient by nature and are retried on the ladder.
bool remoteClassIsTerminal(RemoteClass value);

// 2s, 4s, 8s ... capped at the reporting interval: a relay that is down for an
// hour costs 8 retries an hour rather than 240.
int remoteBackoffMs(int consecutiveFailures, int intervalMs);

// Retry-After in seconds (the shape every 429 carries); -1 when absent or not a
// number. Dates are not honoured - this client always speaks to one relay.
qint64 remoteRetryAfterMs(const QByteArray &headerBlock);

struct RemoteReportState {
    RemoteClass lastClass = RemoteClass::None;
    int lastStatus = 0;
    int lastLatencyMs = -1;
    qint64 lastAttemptMs = -1;
    qint64 lastOkMs = -1;
    int sent = 0;
    int failed = 0;
    int consecutiveFailures = 0;
    int nextRetryMs = -1;
    bool stopped = false; // a terminal answer; nothing more is sent
    // M98: the offline spool. A report is written here before the attempt and
    // released only when the relay answered, so these numbers are the state of
    // what has not been delivered - not a queue length that hides a loss.
    bool spoolOn = false;
    int spooled = 0;
    int spoolCapacity = 0;
    qint64 spoolDropped = 0;
    qint64 spoolBytes = 0;
    qint64 refused = 0; // bodies the relay refused outright, dropped with reason
    QString note;
};

QString remoteReportNote(const RemoteReportState &state);

// M98: ", 3 spooled, 1 dropped" - the suffix both a verdict line and a
// "delivered in 4ms" line carry, because a delivery does not empty the spool.
QString remoteSpoolNoteSuffix(const RemoteReportState &state);

// The Settings row's value: the verdict without its "remote: " prefix (the row
// label already says that), including whatever the spool still holds.
QString remoteRowText(const RemoteReportState &state);

// ---------------------------------------------------------------------------
// M99: the disclosure card. Everything the console says about the paid tier
// comes from these pure builders, so a line the card claims is a line a test
// can assert - the M85 lesson (a disclosure card needs an assertion per line).
// ---------------------------------------------------------------------------

// The keys a report may never carry. The card's "never sent" line is rendered
// from this list, so the promise the user reads and the scan the tests run are
// the same list.
QStringList remoteForbiddenKeys();
QString remoteNeverSentLine();

// One group per row of the scope matrix: what is in a report, and what is not.
struct RemoteFieldGroup {
    QString name;
    bool sent = false;
};

QList<RemoteFieldGroup> remoteFieldGroups();

struct RemoteInfo {
    bool enabled = false;
    QString ingestUrl;
    QString tokenLine;
    QString spoolLine;
    QString lastLine;
    QStringList scopeLines;
    QString neverSentLine;
    QString rowText; // the Settings GENERAL value (M98)
};

RemoteInfo remoteInfoFor(const RemoteOptions &options,
                         const RemoteReportState &state, qint64 nowMs);

QString remoteReportCardText(const RemoteInfo &info);

// Queued across the reporter thread, so both the enum and the envelope need a
// metatype. Declared here rather than in each caller so main.cpp and the tests
// cannot disagree about it.
Q_DECLARE_METATYPE(RemoteClass)
Q_DECLARE_METATYPE(RemoteEnvelope)
Q_DECLARE_METATYPE(RemoteReportState)

