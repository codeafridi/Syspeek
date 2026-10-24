#pragma once

#include <QByteArray>
#include <QString>

// Minimal HTTP client for the Docker Engine API over its local listen point
// (unix socket on Linux/macOS, named pipe on Windows) — same endpoints the
// shipped docker CLI issues (/containers/json, /containers/{id}/stats).
class DockerEngineClient {
public:
    explicit DockerEngineClient(const QString &socketPath, int timeoutMs);

    // Single blocking GET; body without headers on success.
    bool get(const QString &path, QByteArray *body, QString *error);

    const QString &lastError() const { return m_lastError; }

private:
    QString m_socketPath;
    int m_timeoutMs;
    QString m_lastError;
};
