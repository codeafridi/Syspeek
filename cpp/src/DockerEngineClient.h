#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

// Minimal HTTP client for the Docker Engine API over its local listen point
// (unix socket on Linux/macOS, named pipe on Windows) — same endpoints the
// shipped docker CLI issues (/containers/json, /containers/{id}/stats).
class DockerEngineClient {
public:
    explicit DockerEngineClient(const QString &socketPath, int timeoutMs);

    // Single blocking GET; body without headers on success.
    bool get(const QString &path, QByteArray *body, QString *error);

    // Concurrent stats over one body per container id, round-robin on the
    // calling thread so the wall time is the slowest request (~1-2 s of
    // daemon-side sampling), not the sum. Bodies that never complete inside
    // budgetMs are simply absent from the map (honest missing depth).
    QHash<QString, QByteArray>
    statsMulti(const QStringList &ids, qint64 budgetMs, QString *error);

    const QString &lastError() const { return m_lastError; }

private:
    QString m_socketPath;
    int m_timeoutMs;
    QString m_lastError;
};
