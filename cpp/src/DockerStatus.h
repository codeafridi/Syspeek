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

class DockerStateTracker {
public:
    void update(QList<DockerContainer> &containers);

private:
    QHash<QString, QString> m_previousState;
    QSet<QString> m_crashed;
};
