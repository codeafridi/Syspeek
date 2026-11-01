#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QtGlobal>

struct DockerDepth {
    double cpuPercent = -1.0;
    int cpuCores = -1;
    unsigned long long memUsedBytes = 0;
    unsigned long long memLimitBytes = 0;
    bool hasMem = false;
    unsigned long long netRxBytes = 0;
    unsigned long long netTxBytes = 0;
    bool hasNet = false;
    unsigned long long blkReadBytes = 0;
    unsigned long long blkWriteBytes = 0;
    bool hasBlk = false;
    int pids = -1;
};

struct DockerContainer {
    QString id;
    QString name;
    QString state;
    QString status;
    QString health;
    int exitCode = -1;
    bool crashed = false;
    QString image;
    DockerDepth depth;
    bool hasDepth = false;
};

enum class DockerState {
    NotInstalled,
    Starting,
    Ok,
    PermissionDenied,
    Unreachable,
    TimedOut,
    CommandFailed,
    ParseFailed,
    Stale,
};

struct DockerSnapshot {
    DockerState state = DockerState::NotInstalled;
    QList<DockerContainer> containers;
    int runningCount = 0;
    int issueCount = 0;
    bool stale = false;
    QString errorMessage;
    qint64 sampledAtMs = 0;
};

QList<DockerContainer> parseDockerPs(const QByteArray &payload, bool *ok);

// Docker Engine API payloads (/containers/json?all=1 and
// /containers/{id}/stats?stream=false) parsed into the same row model.
QList<DockerContainer> parseDockerContainersJson(const QByteArray &payload,
                                                 QString *error);

bool parseDockerStatsJson(const QByteArray &payload, DockerDepth *out);

int parseDockerExitCode(const QString &status);

int dockerRunningCount(const QList<DockerContainer> &containers);
int dockerIssueCount(const QList<DockerContainer> &containers);
bool dockerContainerIsIssue(const DockerContainer &container);

DockerState classifyDockerFailure(const QByteArray &payload);

QString dockerSummaryText(const DockerSnapshot &snapshot);
QString dockerDetailText(const DockerSnapshot &snapshot);

// M88: a container that keeps holding memory and disk while doing no work is
// the cheapest thing on the box to stop. The tracker accumulates consecutive
// low-activity ticks per container instead of judging a single sample (a
// replica that is idle between requests is not idle).
struct IdleLimits {
    double cpuPercent = 1.0;         // below this counts as idle
    double netBytesPerSec = 1024.0;  // and below this
    qint64 idleAfterMs = 900000;     // sustained for this long before reporting
};

struct IdleContainer {
    QString id;
    QString name;
    double cpuPercent = -1.0;
    double netBytesPerSec = -1.0;
    qint64 idleSeconds = 0;
};

class IdleContainerTracker {
public:
    explicit IdleContainerTracker(const IdleLimits &limits = IdleLimits{});

    void update(const QList<DockerContainer> &containers, qint64 nowMs);
    QList<IdleContainer> idle() const { return m_idle; }
    int idleCount() const { return m_idle.size(); }
    IdleLimits limits() const { return m_limits; }
    void reset();

private:
    struct State {
        qint64 lowSinceMs = 0;
        qint64 lastMs = 0;
        unsigned long long lastRx = 0;
        unsigned long long lastTx = 0;
        bool haveCounters = false;
        bool haveSample = false;   // a previous tick produced a rate
    };

    IdleLimits m_limits;
    QHash<QString, State> m_state;
    QList<IdleContainer> m_idle;
};

QString idleContainerSummaryText(const QList<IdleContainer> &idle,
                                 const IdleLimits &limits);

class DockerStateTracker {
public:
    void update(QList<DockerContainer> &containers);

private:
    QHash<QString, QString> m_previousState;
    QSet<QString> m_crashed;
};
