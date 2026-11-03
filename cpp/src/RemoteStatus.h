#pragma once

#include "Config.h"
#include "DisplaySnapshot.h"
#include "TimelineStatus.h"

#include <QByteArray>
#include <QList>
#include <QJsonObject>
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
