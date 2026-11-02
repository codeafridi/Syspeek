#pragma once

#include "Config.h"
#include "DisplaySnapshot.h"
#include "FleetStatus.h"
#include "MetricsEndpoint.h"

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
    MetricsEndpoint endpoint() const { return m_endpoint; }

    // M94: the same server carries this machine's bounded fleet summary at
    // /api/v1/snapshot, behind the same bearer token. Sharing is its own
    // switch ([fleet] share) because a user may want the peek page without
    // exposing anything to peers.
    void setFleetInfo(const FleetSelfInfo &info, bool share);

public slots:
    void start();
    void stop();
    void setSnapshot(const DisplaySnapshot &snapshot);
    // M87: correlation state lives in the HUD, not the sampler - the console
    // pushes it here so a scrape sees the same incidents the page shows.
    void setAlertState(const AlertExposure &alerts);
    void setIdleContainers(int count);
    // M94: the peer list the console polls, so a scrape sees fleet gauges too.
    void setFleetStatus(const FleetStatus &fleet);

private:
    void rebuildFleetBody();

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    MetricsConfig m_config;
    MetricsEndpoint m_endpoint;
    QByteArray m_token;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    DisplaySnapshot m_snapshot;
    QByteArray m_body;
    bool m_metricsEnabled = true;
    bool m_fleetShare = false;
    FleetSelfInfo m_fleetInfo;
    QByteArray m_fleetBody;
    FleetStatus m_fleetStatus;
    bool m_fleetReported = false;
    quint16 m_port = 0;
};
