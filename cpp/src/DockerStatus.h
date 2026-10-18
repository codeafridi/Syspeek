#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QtGlobal>

struct DockerContainer {
    QString id;
    QString name;
    QString state;
    QString status;
    QString health;
    int exitCode = -1;
    bool crashed = false;
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
