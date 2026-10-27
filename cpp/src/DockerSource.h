#pragma once

#include "Config.h"
#include "DockerStatus.h"

#include <QObject>
#include <QProcess>
#include <QString>

class QTimer;

class DockerSource : public QObject {
    Q_OBJECT

public:
    explicit DockerSource(const DockerConfig &config = DockerConfig{},
                          QObject *parent = nullptr);

public slots:
    void start();
    void stop();
    // M78: lifecycle verbs for one container (start|stop|restart). Runs on
    // the source thread; a refresh is scheduled immediately afterwards.
    void containerAction(const QString &id, const QString &verb);

signals:
    void dockerReady(const DockerSnapshot &snapshot);
    void containerActionFinished(const QString &verb, const QString &id,
                                 bool ok, const QString &error);

private slots:
    void onPollTimeout();
    void onStdout();
    void onStderr();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);
    void onDeadlineTimeout();
    void onKillTimeout();

private:
    void beginQuery();
    void scheduleNext(int intervalMs);
    bool queryViaEngine(QList<DockerContainer> *out, QString *error);
    void finishOk(const QList<DockerContainer> &containers);
    QString engineSocketPath() const;
    void emitSnapshot(DockerState state,
                      const QString &message,
                      const QList<DockerContainer> &containers,
                      bool success);
    int nextIntervalMs(DockerState state, bool hasIssues);
    void resetBackoff();

    QProcess *m_process = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_deadlineTimer = nullptr;
    QTimer *m_killTimer = nullptr;

    DockerConfig m_config;
    QString m_dockerPath;
    QByteArray m_stdout;
    QByteArray m_stderr;

    bool m_running = false;
    bool m_aborting = false;
    bool m_stopping = false;
    bool m_refreshPending = false;
    bool m_haveLast = false;
    DockerState m_abortState = DockerState::TimedOut;
    QString m_abortMessage;
    int m_backoffMs = 15000;
    DockerSnapshot m_lastGood;
    DockerStateTracker m_stateTracker;
};
