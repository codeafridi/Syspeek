#pragma once

#include "RemoteStatus.h"
#include "RemoteSpool.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>

class QTimer;

// M97: one report leaves the machine, on a thread of its own.
//
// Same socket discipline as fleetHttpGet (M94): a plain QTcpSocket/QSslSocket
// with a hard budget, no QNetworkAccessManager, so the sampler keeps no event
// loop dependency and `--dump` can run it synchronously without a QApplication.
// What is different is the shape of the answer: a POST to a relay has seven
// distinguishable outcomes and only three of them are worth retrying.

struct RemotePostResult {
    RemoteClass classification = RemoteClass::None;
    int httpStatus = 0;
    int latencyMs = -1;
    qint64 retryAfterMs = -1;
    QByteArray body;
    QString error;
};

RemotePostResult remoteHttpPost(const QString &url, const QString &token,
                                const QByteArray &body, int timeoutMs);

struct RemoteReportOptions {
    bool enabled = false;
    QString url; // the ingest endpoint, already normalized
    QString token;
    int intervalMs = 15000;
    int timeoutMs = 3000;
    // M98: where undelivered reports wait. An empty path or a budget that
    // cannot hold one record turns the spool off, and the note says so instead
    // of pretending a dropped report was delivered.
    QString spoolPath;
    qint64 spoolBytes = 0;
};

// The runner posts whatever envelope was handed to it last, once. The envelope
// (and its sample id) is built by the caller so a retry of the same bytes cannot
// mint a new id.
class RemoteRunner : public QObject {
    Q_OBJECT
public:
    explicit RemoteRunner(QObject *parent = nullptr);
    ~RemoteRunner() override;

    void setOptions(const RemoteReportOptions &options);
    RemoteReportOptions options() const;

    void setEnvelope(const RemoteEnvelope &envelope);
    RemoteReportState state() const;

public slots:
    void start();
    void stop();
    void pollNow();

signals:
    void reportSent(bool ok, RemoteClass classification, int latencyMs,
                    const QString &note);
    // The spool changes on its own (a report is written before the attempt), so
    // the console reads this rather than only the verdict line.
    void stateChanged(const RemoteReportState &state);

private:
    void poll();
    void schedule(int delayMs);
    void publish(const QString &note);
    void publishState();
    void openSpool(const RemoteReportOptions &options);

    mutable QMutex m_mutex;
    RemoteReportOptions m_options;
    RemoteEnvelope m_envelope;
    bool m_haveEnvelope = false;
    RemoteReportState m_state;
    qint64 m_lastSentAtMs = -1;
    QTimer *m_timer = nullptr;
    // Touched only on the runner's own thread.
    RemoteSpool m_spool;
    QString m_lastSpooledId;

    void syncSpoolLocked();
};
