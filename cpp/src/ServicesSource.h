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

signals:
    void servicesReady(const ServicesSnapshot &snapshot);

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
