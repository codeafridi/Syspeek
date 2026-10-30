#pragma once

#include "CloudSpendStatus.h"
#include "CloudStatus.h"

#include <QObject>
#include <QStringList>

class QProcess;

// Billed spend through the provider's own cost API (M85).
//
// Same contract as the inventory runner (M83): the query runs through the CLI
// the user already logged in, never stores a credential, is switchable, and is
// polled so the sampler thread never waits on it. AWS needs two calls (cost and
// usage, then the month-end forecast), so the runner is a small step machine.
struct CloudSpendOptions {
    bool enabled = true;
    int timeoutMs = 30000;
};

// The argv each provider's client gets. AWS runs `ce get-cost-and-usage` then
// `ce get-cost-forecast`; Azure runs `consumption usage list`; GCP and OCI
// have no CLI cost API and are gated before anything is spawned.
QStringList cloudSpendArguments(const CloudIdentity &identity, int step,
                                qint64 nowMs);
QString cloudSpendCommandText(const CloudIdentity &identity);
QString cloudSpendScope(const CloudIdentity &identity);
int cloudSpendStepCount(const CloudIdentity &identity);

// Synchronous one-shot used by `--dump` and the tests.
CloudSpendStatus probeCloudSpend(const CloudIdentity &identity,
                                 const CloudSpendOptions &options,
                                 qint64 nowMs = 0);

class CloudSpendRunner : public QObject {
    Q_OBJECT

public:
    explicit CloudSpendRunner(QObject *parent = nullptr);
    ~CloudSpendRunner() override;

    void setOptions(const CloudSpendOptions &options);
    CloudSpendOptions options() const;

    const CloudSpendStatus &status() const;

    // Starts the query when the cadence expires, advances through the steps,
    // and kills the process past the deadline. Never blocks the caller.
    void update(const CloudIdentity &identity, qint64 nowMs);

    void stop();

private:
    void begin(const CloudIdentity &identity, qint64 nowMs);
    void advance(qint64 nowMs);
    void collectStep(qint64 nowMs);
    void finish(qint64 nowMs);
    void fail(const QString &note, qint64 nowMs);
    void cleanup();

    CloudSpendOptions m_options;
    CloudSpendStatus m_status;
    CloudIdentity m_identity;
    QProcess *m_process = nullptr;
    bool m_running = false;
    int m_step = 0;
    qint64 m_startedAtMs = 0;
    qint64 m_lastAttemptMs = 0;
};
