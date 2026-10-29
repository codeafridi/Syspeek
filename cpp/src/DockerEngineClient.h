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
    bool get(const QString &path, QByteArray *body, QString *error,
             int timeoutMs = -1);

    // Single blocking POST with an empty body (engine lifecycle verbs:
    // /containers/{id}/start|stop|restart answer 204, or 304 when already
    // in the requested state). GET-only status handling stays strict (200).
    // A lifecycle verb must outlast the engine's own graceful-stop grace
    // (dockerd kills the container after 10 s by default), so callers pass a
    // larger timeoutMs than the list/stats budget.
    bool post(const QString &path, QByteArray *body, QString *error,
              int timeoutMs = -1);

    // Concurrent stats over one body per container id, round-robin on the
    // calling thread so the wall time is the slowest request (~1-2 s of
    // daemon-side sampling), not the sum. Bodies that never complete inside
    // budgetMs are simply absent from the map (honest missing depth).
    QHash<QString, QByteArray>
    statsMulti(const QStringList &ids, qint64 budgetMs, QString *error);

    const QString &lastError() const { return m_lastError; }

private:
    bool perform(const QByteArray &method, const QString &path,
                 QByteArray *body, QString *error, int timeoutMs);
    QString m_socketPath;
    int m_timeoutMs;
    QString m_lastError;
};
