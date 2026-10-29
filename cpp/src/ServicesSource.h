#pragma once

#include "ServicesStatus.h"

#include <QObject>
#include <QProcess>

class QTimer;

class ServicesSource : public QObject {
    Q_OBJECT

public:
    explicit ServicesSource(int intervalMs = 30000, QObject *parent = nullptr);

public slots:
    void start();
    void stop();
    // M78: restart one unit through the platform's own supervisor
    // (systemctl restart / launchctl kickstart -k). The next poll picks up
    // the new state; the answer is reported verbatim.
    void restartUnit(const QString &unit);

signals:
    void servicesReady(const ServicesSnapshot &snapshot);
    void unitActionFinished(const QString &verb, const QString &unit, bool ok,
                            const QString &error);

private slots:
    void onTimeout();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onErrorOccurred(QProcess::ProcessError error);

private:
    void beginQuery();
    void emitFailure(const QString &message);
    void onUnits(QList<UnitRow> units);

    QProcess *m_process = nullptr;
    QTimer *m_timer = nullptr;
    QByteArray m_stdout;
    int m_intervalMs;
    bool m_running = false;
    bool m_stopping = false;
    QList<UnitRow> m_prevUnits;
    bool m_havePrev = false;
    QList<ServiceChange> m_timeline;
};
