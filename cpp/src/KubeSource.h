#pragma once

#include "Config.h"
#include "KubeStatus.h"

#include <QObject>
#include <QProcess>
#include <QString>

class QTimer;

class KubeSource : public QObject {
    Q_OBJECT

public:
    explicit KubeSource(const KubeConfig &config = KubeConfig{},
                        const CostConfig &costConfig = CostConfig{},
                        QObject *parent = nullptr);

public slots:
    void start();
    void stop();

signals:
    void kubeReady(const KubeSnapshot &snapshot);

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
    void startPhase(int phase, const QStringList &arguments);
    void advancePhase();
    void finishMetrics();
    void scheduleNext(int intervalMs);
    void emitSnapshot(KubeState state, const QString &message,
                      const KubeResources &resources, bool success);
    int nextIntervalMs(KubeState state, bool hasIssues);
    void resetBackoff();

    QProcess *m_process = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_deadlineTimer = nullptr;
    QTimer *m_killTimer = nullptr;

    KubeConfig m_config;
    CostConfig m_costConfig;
    QString m_kubectlPath;
    QByteArray m_stdout;
    QByteArray m_stderr;

    bool m_running = false;
    bool m_aborting = false;
    bool m_stopping = false;
    bool m_refreshPending = false;
    bool m_haveLast = false;
    KubeState m_abortState = KubeState::TimedOut;
    QString m_abortMessage;
    int m_backoffMs = 15000;
    KubeSnapshot m_lastGood;
    KubeResources m_prevResources;
    bool m_havePrev = false;
    QList<KubeChange> m_timeline;

    int m_phase = 0;
    KubeResources m_pendingResources;
    bool m_havePending = false;
    bool m_metricsOk = false;
    QString m_metricsNote;
    QByteArray m_metricsPodsRaw;
    QByteArray m_metricsNodesRaw;
};
