#pragma once

#include "Config.h"
#include "DisplaySnapshot.h"

#include <QByteArray>
#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>

// Local Prometheus scrape endpoint (127.0.0.1 by default): serves the last
// collected snapshot in the Prometheus text exposition format at /metrics.
class MetricsServer : public QObject {
    Q_OBJECT
public:
    explicit MetricsServer(const MetricsConfig &config = MetricsConfig{},
                           QObject *parent = nullptr);

    quint16 port() const { return m_port; }

public slots:
    void start();
    void stop();
    void setSnapshot(const DisplaySnapshot &snapshot);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    MetricsConfig m_config;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    DisplaySnapshot m_snapshot;
    QByteArray m_body;
    quint16 m_port = 0;
};
