#pragma once

#include "DisplaySnapshot.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// M94: many machines, one window.
//
// Each Syspeek exposes a bounded summary of the machine it runs on at
// /api/v1/snapshot behind the same bearer token the /metrics endpoint uses;
// every Syspeek polls the peers listed in [fleet] peers=. Nothing leaves the
// network the user authored, there is no account and no hosted service - the
// aggregation is the same shape as Prometheus federation and netdata's
// parent/child pair, which is what the two artifacts in /tmp/opencode/m94
// ground (prometheused /federate + scrape_configs + honor_labels;
// netdata stream.conf destination + "api key" + /api/v1/info mirrored_hosts
// "include itself").
//
// Two rules are copied from those artifacts on purpose:
//   * a peer link needs BOTH an address and a token before it is used, and a
//     refusal names the missing field instead of half-connecting (netdata:
//     "STREAM [send]: cannot enable sending thread - missing required fields
//     (destination: %s, api key: %s)");
//   * the payload is versioned and a NEWER schema is refused, never
//     half-parsed (netdata had to deprecate its whole /api/v1 surface).

// Bump only when a field changes meaning. A reader refuses a peer whose schema
// is greater than this and names the fix.
constexpr int kFleetSchemaVersion = 1;

constexpr int kFleetDefaultPort = 9107;
constexpr int kFleetDefaultPollMs = 10000;
constexpr int kFleetDefaultTimeoutMs = 2000;

struct FleetPeer {
    // Configuration.
    QString name;     // as authored, or taken from the peer's own answer
    bool nameAuthored = false;  // [fleet] peers= carried name=...
    bool isSelf = false;  // the row for this machine (netdata includes itself)
    QString address;  // exactly what [fleet] peers= said
    QString url;      // normalised request target
    bool configured = true;  // false when the entry was refused
    QString configNote;      // why an entry was refused

    // Link state, measured locally.
    bool online = false;
    bool everSeen = false;
    bool refused = false;   // answered, but not with something we can use
    int httpStatus = 0;
    int latencyMs = -1;
    qint64 lastAttemptMs = -1;
    qint64 lastSeenMs = -1;
    QString error;

    // The peer's own account of itself (the netdata /api/v1/info surface).
    int schema = 0;
    QString hostname;
    QString platform;
    QString version;
    quint64 sequence = 0;
    qint64 sampledAtMs = -1;
    qint64 uptimeSec = -1;

    // What it is doing.
    QString appName;
    double appCpu = -1.0;
    unsigned long long appRssKb = 0;
    double cpuPercent = -1.0;
    double stealPercent = -1.0;
    unsigned long long memTotalKb = 0;
    unsigned long long memAvailableKb = 0;
    double load1 = -1.0;
    double diskFreePct = -1.0;
    double netRxPerSec = -1.0;
    double netTxPerSec = -1.0;
    int processCount = 0;
    int listeningCount = 0;
    int connectionCount = 0;
    int establishedCount = 0;
    double retransPercent = -1.0;
    int gpuUtilPercent = -1;
    double hottestCelsius = -1.0;
    int batteryPercent = -1;
    int openIncidents = -1;
    int flappingIncidents = -1;
};

struct FleetStatus {
    bool enabled = false;
    bool share = false;
    // Serving side: where this box answers, and whether that is reachable off
    // the machine (the metrics endpoint resolver decides - a LAN bind without
    // a token is downgraded to loopback).
    QString shareAddress = QStringLiteral("127.0.0.1");
    int sharePort = kFleetDefaultPort;
    bool tokenConfigured = false;
    QString selfName;
    QString hostname;
    QString version;
    QString platform;

    QList<FleetPeer> peers;
    QString note;  // gate text: why there are no peers, or what went wrong
    bool polling = false;
    qint64 lastPollMs = -1;
    int pollMs = kFleetDefaultPollMs;
    int timeoutMs = kFleetDefaultTimeoutMs;
};

// --- configuration -------------------------------------------------------
// Accepted peer forms: host, host:port, http://host[:port][/path],
// https://host[:port][/path], and any of those prefixed with name=.
// A bare host is a mistake people make constantly, so it is completed rather
// than rejected: http, default port, /api/v1/snapshot.
QString fleetNormalizePeerUrl(const QString &spec, QString *error = nullptr);

QList<FleetPeer> parseFleetPeers(const QStringList &specs,
                                 QString *error = nullptr);

FleetPeer fleetSelfPeer(const DisplaySnapshot &snapshot, const QString &name,
                        qint64 nowMs);

// --- payload -------------------------------------------------------------
// The bounded summary this box publishes. Deliberately NOT the whole
// DisplaySnapshot: no process list, no open files, no command lines, no
// environment, no Snapshot contents of any kind - a peer learns how busy the
// box is, not what it is doing.
QByteArray fleetSnapshotJson(const DisplaySnapshot &snapshot,
                             const QString &name, const QString &hostname,
                             const QString &version,
                             const QString &platform);

// Parses a peer's answer into `out`. Nothing is filled unless the whole
// document is understood; on failure `out` is left untouched and `error`
// says what was wrong (bad JSON, wrong schema, not a fleet answer).
bool parseFleetSnapshotJson(const QByteArray &json, FleetPeer *out,
                            QString *error = nullptr);

// --- derivation ----------------------------------------------------------
bool fleetPeerStale(const FleetPeer &peer, qint64 nowMs, int pollMs);

int fleetPeerAgeMs(const FleetPeer &peer, qint64 nowMs);

int fleetOnlineCount(const FleetStatus &status);

int fleetDownCount(const FleetStatus &status);

// Highest CPU among peers that answered; -1 when none did.
double fleetWorstCpu(const FleetStatus &status);

// Lowest available-memory percentage among peers that answered; -1 when none.
double fleetLowestMemPct(const FleetStatus &status);

// The peer a fleet view should focus on: the one that is down first, then the
// hottest, then the one closest to running out of memory.
int fleetAttentionIndex(const FleetStatus &status);

// A status whose peer list starts with this box, marked as self - the
// "list of hosts mirrored (includes itself)" shape.
FleetStatus fleetWithSelf(const FleetStatus &status, const FleetPeer &self);

// --- text ----------------------------------------------------------------
QString fleetPeerStateText(const FleetPeer &peer, qint64 nowMs, int pollMs);

// "just now", "12s", "3m", "2h" - how long ago this box last heard from it.
QString fleetPeerAgeText(const FleetPeer &peer, qint64 nowMs);

QString fleetPeerDetailText(const FleetPeer &peer, qint64 nowMs, int pollMs);

QString fleetSummaryText(const FleetStatus &status, qint64 nowMs);

QString fleetGateText(const FleetStatus &status);

QString fleetShareText(const FleetStatus &status);

QString fleetNoteText(const FleetStatus &status);

QString fleetNoPeersText();

QString fleetTokenMissingText();

// This machine's own identity: `name` when authored, else the hostname.
struct FleetSelfInfo {
    QString name;
    QString hostname;
    QString version;
    QString platform;
};

FleetSelfInfo fleetSelfInfo(const QString &name);
