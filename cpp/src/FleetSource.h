#pragma once

#include "FleetStatus.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

// M94: poll the peers this box was told about.
//
// One blocking GET per peer over a plain socket, on a thread of its own, so a
// peer that blackholes the connection costs this machine nothing in its own
// sampling tick. The same code runs synchronously from `--dump`.
//
// Grounding (kept /tmp/opencode/m94): Prometheus 3.7.3 ships every knob this
// needs as a separate field - `yaml:"scrape_configs,omitempty"` (the peer
// list), `yaml:"job_name"` (a per-peer name), `yaml:"scheme,omitempty"`,
// `yaml:"scrape_interval,omitempty"` (the cadence) and
// `yaml:"scrape_timeout,omitempty"` (the per-poll budget) - and netdata's
// stream.conf refuses a parent link that has an address but no key
// ("missing required fields (destination: %s, api key: %s)") instead of
// half-connecting.

struct FleetFetchResult {
    bool ok = false;
    int httpStatus = 0;
    int latencyMs = -1;
    QByteArray body;
    QString error;
};

FleetFetchResult fleetHttpGet(const QString &url, const QByteArray &token,
                              int timeoutMs);

struct FleetPollOptions {
    bool enabled = false;
    bool share = false;
    QString shareAddress = QStringLiteral("127.0.0.1");
    int sharePort = kFleetDefaultPort;
    QString token;
    QString selfName;
    QString hostname;
    QString version;
    QString platform;
    QStringList peers;
    int pollMs = kFleetDefaultPollMs;
    int timeoutMs = kFleetDefaultTimeoutMs;
};

// One synchronous pass over every configured peer. Never call this from the
// sampler tick - it blocks for up to timeoutMs per peer on purpose.
FleetStatus probeFleet(const FleetPollOptions &options, qint64 nowMs = 0);

class FleetRunner : public QObject {
    Q_OBJECT
public:
    explicit FleetRunner(QObject *parent = nullptr);
    ~FleetRunner() override;

    void setOptions(const FleetPollOptions &options);
    FleetPollOptions options() const;

public slots:
    void start();
    void stop();
    void pollNow();

signals:
    void fleetUpdated(const FleetStatus &status);

private:
    void poll();
    FleetStatus buildStatus(bool polling) const;

    mutable QMutex m_mutex;
    FleetPollOptions m_options;
    QStringList m_peerSpec;
    QList<FleetPeer> m_peers;
    QTimer *m_timer = nullptr;
    qint64 m_lastPollMs = -1;
};
