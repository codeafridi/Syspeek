#include "DockerSource.h"
#include "DockerEngineClient.h"

#include <QDateTime>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace {
constexpr qint64 kMaxOutputBytes = 1LL << 20;
} // namespace

DockerSource::DockerSource(const DockerConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config) {
    m_backoffMs = m_config.backoffStartMs;
}

void DockerSource::start() {
    m_dockerPath = QStandardPaths::findExecutable(QStringLiteral("docker"));

    if (m_process == nullptr) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this,
                &DockerSource::onStdout);
        connect(m_process, &QProcess::readyReadStandardError, this,
                &DockerSource::onStderr);
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &DockerSource::onFinished);
        connect(m_process, &QProcess::errorOccurred, this,
                &DockerSource::onErrorOccurred);
    }

    if (m_pollTimer == nullptr) {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setSingleShot(true);
        connect(m_pollTimer, &QTimer::timeout, this, &DockerSource::onPollTimeout);
    }
    if (m_deadlineTimer == nullptr) {
        m_deadlineTimer = new QTimer(this);
        m_deadlineTimer->setSingleShot(true);
        connect(m_deadlineTimer, &QTimer::timeout, this,
                &DockerSource::onDeadlineTimeout);
    }
    if (m_killTimer == nullptr) {
        m_killTimer = new QTimer(this);
        m_killTimer->setSingleShot(true);
        connect(m_killTimer, &QTimer::timeout, this, &DockerSource::onKillTimeout);
    }

    resetBackoff();
    beginQuery();
}

void DockerSource::stop() {
    m_stopping = true;

    if (m_pollTimer != nullptr) {
        m_pollTimer->stop();
    }
    if (m_deadlineTimer != nullptr) {
        m_deadlineTimer->stop();
    }
    if (m_killTimer != nullptr) {
        m_killTimer->stop();
    }

    if (m_process != nullptr && m_running) {
        m_process->terminate();
        if (!m_process->waitForFinished(m_config.killGraceMs)) {
            m_process->kill();
            m_process->waitForFinished(200);
        }
    }

    m_running = false;
    m_aborting = false;
}

void DockerSource::beginQuery() {
    if (m_stopping) {
        return;
    }

    if (m_running) {
        m_refreshPending = true;
        return;
    }

    QList<DockerContainer> engineRows;
    QString engineError;
    if (queryViaEngine(&engineRows, &engineError)) {
        finishOk(engineRows);
        return;
    }

    if (m_dockerPath.isEmpty()) {
        DockerSnapshot snapshot;
        snapshot.state = DockerState::NotInstalled;
        snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
        emit dockerReady(snapshot);
        scheduleNext(nextIntervalMs(DockerState::NotInstalled, false));
        return;    }

    m_stdout.clear();
    m_stderr.clear();
    m_running = true;
    m_aborting = false;

    m_process->start(m_dockerPath,
                     {QStringLiteral("ps"), QStringLiteral("--all"),
                      QStringLiteral("--format"),
                      QStringLiteral("{{.ID}}\t{{.Names}}\t{{.State}}\t{{.Status}}")});
    m_deadlineTimer->start(m_config.timeoutMs);
}

void DockerSource::scheduleNext(int intervalMs) {
    if (m_stopping) {
        return;
    }
    if (m_refreshPending) {
        m_refreshPending = false;
        beginQuery();
        return;
    }
    m_pollTimer->start(intervalMs);
}

void DockerSource::onPollTimeout() {
    beginQuery();
}

void DockerSource::onStdout() {
    m_stdout += m_process->readAllStandardOutput();
    if (m_stdout.size() > kMaxOutputBytes) {
        m_aborting = true;
        m_abortState = DockerState::CommandFailed;
        m_abortMessage = QStringLiteral("docker output exceeded limit");
        m_process->terminate();
        m_killTimer->start(m_config.killGraceMs);
    }
}

void DockerSource::onStderr() {
    m_stderr += m_process->readAllStandardError();
    if (m_stderr.size() > kMaxOutputBytes) {
        m_aborting = true;
        m_abortState = DockerState::CommandFailed;
        m_abortMessage = QStringLiteral("docker error output exceeded limit");
        m_process->terminate();
        m_killTimer->start(m_config.killGraceMs);
    }
}

void DockerSource::onDeadlineTimeout() {
    if (!m_running) {
        return;
    }

    m_aborting = true;
    m_abortState = DockerState::TimedOut;
    m_abortMessage = QStringLiteral("docker command timed out");
    m_process->terminate();
    m_killTimer->start(m_config.killGraceMs);
}

void DockerSource::onKillTimeout() {
    if (m_running && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
}

void DockerSource::onErrorOccurred(QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart) {
        return;
    }

    m_deadlineTimer->stop();
    m_running = false;
    m_aborting = false;
    emitSnapshot(DockerState::CommandFailed,
                 QStringLiteral("could not start docker"), {}, false);
    scheduleNext(nextIntervalMs(DockerState::CommandFailed, false));
}

void DockerSource::onFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_deadlineTimer->stop();
    m_killTimer->stop();

    const bool wasAborting = m_aborting;
    m_running = false;
    m_aborting = false;

    if (m_stopping) {
        return;
    }

    if (wasAborting) {
        emitSnapshot(m_abortState, m_abortMessage, {}, false);
        scheduleNext(nextIntervalMs(m_abortState, false));
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const DockerState state = classifyDockerFailure(m_stderr);
        emitSnapshot(state, QString::fromUtf8(m_stderr).trimmed(), {}, false);
        scheduleNext(nextIntervalMs(state, false));
        return;
    }

    bool ok = false;
    const QList<DockerContainer> containers = parseDockerPs(m_stdout, &ok);
    if (!ok) {
        emitSnapshot(DockerState::ParseFailed,
                     QStringLiteral("unexpected docker ps output"), {}, false);
        scheduleNext(nextIntervalMs(DockerState::ParseFailed, false));
        return;
    }

    const int issues = dockerIssueCount(containers);
    emitSnapshot(DockerState::Ok, QString(), containers, true);
    scheduleNext(nextIntervalMs(DockerState::Ok, issues > 0));
}

void DockerSource::finishOk(const QList<DockerContainer> &containers) {
    const int issues = dockerIssueCount(containers);
    emitSnapshot(DockerState::Ok, QString(), containers, true);
    scheduleNext(nextIntervalMs(DockerState::Ok, issues > 0));
}

QString DockerSource::engineSocketPath() const {
    if (!m_config.socketPath.isEmpty()) {
        return m_config.socketPath;
    }
#if defined(Q_OS_WIN)
    return QStringLiteral("//./pipe/docker_engine");
#else
    const QByteArray dockerHost = qgetenv("DOCKER_HOST");
    if (!dockerHost.isEmpty()) {
        const QString host = QString::fromUtf8(dockerHost);
        if (host.startsWith(QLatin1String("unix://"))) {
            return host.mid(7);
        }
        return QString();
    }
    return QStringLiteral("/var/run/docker.sock");
#endif
}

bool DockerSource::queryViaEngine(QList<DockerContainer> *out,
                                  QString *error) {
    const QString socketPath = engineSocketPath();
    if (socketPath.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("non-local docker host");
        }
        return false;
    }

    DockerEngineClient client(socketPath, m_config.timeoutMs);
    QByteArray body;
    QString localError;
    if (!client.get(QStringLiteral("/containers/json?all=1"), &body,
                    &localError)) {
        if (error != nullptr) {
            *error = localError;
        }
        return false;
    }

    QList<DockerContainer> containers =
        parseDockerContainersJson(body, &localError);
    if (!localError.isEmpty()) {
        if (error != nullptr) {
            *error = localError;
        }
        return false;
    }

    QStringList runningIds;
    for (const DockerContainer &container : containers) {
        if (container.state.compare(QStringLiteral("running"),
                                    Qt::CaseInsensitive) == 0) {
            runningIds.append(container.id);
        }
    }
    const QHash<QString, QByteArray> stats =
        client.statsMulti(runningIds, qint64(m_config.timeoutMs) * 2,
                          &localError);
    for (DockerContainer &container : containers) {
        const QByteArray raw = stats.value(container.id);
        if (!raw.isEmpty() && parseDockerStatsJson(raw, &container.depth)) {
            container.hasDepth = true;
        }
    }

    if (out != nullptr) {
        *out = containers;
    }
    return true;
}

void DockerSource::emitSnapshot(DockerState state,
                                const QString &message,
                                const QList<DockerContainer> &containers,
                                bool success) {
    if (success) {
        DockerSnapshot snapshot;
        snapshot.state = DockerState::Ok;
        snapshot.containers = containers;
        m_stateTracker.update(snapshot.containers);
        snapshot.runningCount = dockerRunningCount(snapshot.containers);
        snapshot.issueCount = dockerIssueCount(snapshot.containers);
        snapshot.stale = false;
        snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
        m_lastGood = snapshot;
        m_haveLast = true;
        resetBackoff();
        emit dockerReady(snapshot);
        return;
    }

    if (m_haveLast) {
        DockerSnapshot snapshot = m_lastGood;
        snapshot.state = DockerState::Stale;
        snapshot.stale = true;
        snapshot.errorMessage = message;
        snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
        emit dockerReady(snapshot);
        return;
    }

    DockerSnapshot snapshot;
    snapshot.state = state;
    snapshot.errorMessage = message;
    snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
    emit dockerReady(snapshot);
}

int DockerSource::nextIntervalMs(DockerState state, bool hasIssues) {
    switch (state) {
    case DockerState::Ok:
        resetBackoff();
        return hasIssues ? m_config.issueMs : m_config.healthyMs;
    case DockerState::NotInstalled:
    case DockerState::PermissionDenied:
        return m_config.absentMs;
    case DockerState::Unreachable:
    case DockerState::TimedOut:
    case DockerState::CommandFailed:
    case DockerState::ParseFailed:
    case DockerState::Stale:
    case DockerState::Starting:
    default: {
        const int current = m_backoffMs;
        m_backoffMs = std::min(m_backoffMs * 2, m_config.backoffCapMs);
        return current;
    }
    }
}

void DockerSource::resetBackoff() {
    m_backoffMs = m_config.backoffStartMs;
}

void DockerSource::containerAction(const QString &id, const QString &verb) {
    if (id.isEmpty()) {
        return;
    }
    if (verb != QStringLiteral("start") && verb != QStringLiteral("stop")
        && verb != QStringLiteral("restart")) {
        emit containerActionFinished(verb, id, false,
                                     QStringLiteral("unknown verb"));
        return;
    }

    QString error;
    bool ok = false;
    // dockerd answers a lifecycle verb only once the container has moved
    // state, and its own graceful stop grace is 10 s (docker stop -t). A
    // list/stats-sized timeout would report a slow-but-correct restart as a
    // failure, so the verb gets its own budget.
    const int lifecycleMs = qMax(m_config.timeoutMs, 30000);
    const QString socketPath = engineSocketPath();
    if (!socketPath.isEmpty()) {
        DockerEngineClient client(socketPath, lifecycleMs);
        ok = client.post(QStringLiteral("/containers/%1/%2").arg(id, verb),
                         nullptr, &error, lifecycleMs);
    }
    if (!ok) {
        const QString cli = m_dockerPath;
        if (cli.isEmpty()) {
            if (error.isEmpty()) {
                error = QStringLiteral("docker not found");
            }
        } else {
            QProcess process;
            process.start(cli, {verb, id});
            if (!process.waitForFinished(lifecycleMs)) {
                process.kill();
                process.waitForFinished(200);
                error = QStringLiteral("docker %1 timed out").arg(verb);
            } else if (process.exitStatus() != QProcess::NormalExit
                       || process.exitCode() != 0) {
                error = QString::fromLocal8Bit(
                            process.readAllStandardError())
                            .trimmed()
                            .left(160);
                if (error.isEmpty()) {
                    error = QStringLiteral("docker %1 failed").arg(verb);
                }
            } else {
                ok = true;
            }
        }
    }

    emit containerActionFinished(verb, id, ok, error);

    if (m_pollTimer != nullptr) {
        m_pollTimer->stop();
    }
    QTimer::singleShot(0, this, &DockerSource::onPollTimeout);
}
