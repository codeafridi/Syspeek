#include "DockerEngineClient.h"

#include <QDateTime>
#include <QLocalSocket>
#include <QTimer>

namespace {

QByteArray requestBytes(const QByteArray &method, const QString &path) {
    return QStringLiteral(
               "%1 %2 HTTP/1.1\r\n"
               "Host: docker\r\n"
               "User-Agent: syspeek/0.8\r\n"
               "Accept: application/json\r\n"
               "Connection: close\r\n"
               "\r\n")
        .arg(QString::fromLatin1(method), path)
        .toUtf8();
}

int headerKick(const QByteArray &frame, QByteArray *headerOut) {
    const int sep = frame.indexOf("\r\n\r\n");
    if (sep < 0) {
        return -1;
    }
    if (headerOut != nullptr) {
        *headerOut = frame.left(sep);
    }
    return sep + 4;
}

int statusFromHeader(const QByteArray &header) {
    const int sp1 = header.indexOf(' ');
    if (sp1 < 0) {
        return 0;
    }
    const int sp2 = header.indexOf(' ', sp1 + 1);
    if (sp2 < 0) {
        return 0;
    }
    return header.mid(sp1 + 1, sp2 - sp1 - 1).toInt();
}

int contentLengthFromHeader(const QByteArray &header, bool *ok) {
    for (const QByteArray &line : header.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
            if (ok != nullptr) {
                *ok = true;
            }
            return trimmed.mid(15).trimmed().toInt();
        }
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return 0;
}

bool chunkedFromHeader(const QByteArray &header) {
    for (const QByteArray &line : header.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("transfer-encoding:")
            && trimmed.toLower().contains("chunked")) {
            return true;
        }
    }
    return false;
}

// RFC 7230 chunked decoding: one-shot over the accumulated frame tail.
QByteArray dechunk(const QByteArray &raw) {
    QByteArray out;
    int offset = 0;
    while (true) {
        const int lineEnd = raw.indexOf("\r\n", offset);
        if (lineEnd < 0) {
            break;
        }
        const QByteArray sizeField =
            raw.mid(offset, lineEnd - offset)
                .split(';').value(0).trimmed();
        bool ok = false;
        const int size = sizeField.toInt(&ok, 16);
        if (!ok || size < 0) {
            break;
        }
        if (size == 0) {
            break;
        }
        const int bodyStart = lineEnd + 2;
        if (raw.size() < bodyStart + size) {
            break;
        }
        out += raw.mid(bodyStart, size);
        offset = bodyStart + size + 2;
    }
    return out;
}

} // namespace

DockerEngineClient::DockerEngineClient(const QString &socketPath, int timeoutMs)
    : m_socketPath(socketPath), m_timeoutMs(timeoutMs) {}

bool DockerEngineClient::get(const QString &path, QByteArray *body,
                             QString *error, int timeoutMs) {
    return perform("GET", path, body, error, timeoutMs);
}

bool DockerEngineClient::post(const QString &path, QByteArray *body,
                              QString *error, int timeoutMs) {
    return perform("POST", path, body, error, timeoutMs);
}

bool DockerEngineClient::perform(const QByteArray &method, const QString &path,
                                 QByteArray *body, QString *error,
                                 int timeoutMs) {
    if (timeoutMs <= 0) {
        timeoutMs = m_timeoutMs;
    }
    m_lastError.clear();
    if (body != nullptr) {
        body->clear();
    }
    if (error != nullptr) {
        error->clear();
    }

    QLocalSocket socket;
    socket.connectToServer(m_socketPath);
    if (!socket.waitForConnected(timeoutMs)) {
        m_lastError = socket.errorString();
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    socket.write(requestBytes(method, path));
    if (!socket.waitForBytesWritten(timeoutMs)) {
        m_lastError = QStringLiteral("write to engine did not drain");
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    QByteArray frame;
    while (frame.indexOf("\r\n\r\n") < 0) {
        if (!socket.waitForReadyRead(timeoutMs)) {
            m_lastError = QStringLiteral("engine header never completed: %1")
                              .arg(socket.errorString());
            if (error != nullptr) {
                *error = m_lastError;
            }
            return false;
        }
        frame += socket.readAll();
    }

    QByteArray header;
    const int bodyStart = headerKick(frame, &header);
    const int status = statusFromHeader(header);
    const bool ok = (method == "GET") ? status == 200
                                      : ((status >= 200 && status < 300)
                                         || status == 304);
    if (!ok) {
        m_lastError = QStringLiteral("engine status %1 for %2 %3")
                          .arg(status)
                          .arg(QString::fromLatin1(method), path);
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    const bool chunked = chunkedFromHeader(header);
    bool hasLength = false;
    const int contentLength = contentLengthFromHeader(header, &hasLength);
    QByteArray payload = frame.mid(bodyStart);
    while (true) {
        if (chunked) {
            if (payload.endsWith(QByteArrayLiteral("\r\n0\r\n\r\n"))) {
                break;
            }
        } else if (!hasLength || payload.size() >= contentLength) {
            break;
        }
        if (!socket.waitForReadyRead(timeoutMs)) {
            break;
        }
        payload += socket.readAll();
    }

    socket.disconnectFromServer();
    if (body != nullptr) {
        *body = chunked ? dechunk(payload)
                        : (hasLength ? payload.left(contentLength) : payload);
    }
    return true;
}

QHash<QString, QByteArray>
DockerEngineClient::statsMulti(const QStringList &ids, qint64 budgetMs,
                               QString *error) {
    m_lastError.clear();
    if (error != nullptr) {
        error->clear();
    }

    QHash<QString, QByteArray> bodies;
    if (ids.isEmpty()) {
        return bodies;
    }

    struct Session {
        QLocalSocket socket;
        QString id;
        QByteArray frame;
        QByteArray header;
        int bodyStart = -1;
        bool headerDone = false;
        bool chunked = false;
        bool hasLength = false;
        int contentLength = 0;
        QByteArray payload;
        bool complete = false;
    };

    QList<Session *> sessions;
    for (const QString &id : ids) {
        Session *s = new Session{};
        sessions.append(s);
        s->id = id;
        s->socket.connectToServer(m_socketPath);
        if (s->socket.waitForConnected(m_timeoutMs)) {
            s->socket.write(requestBytes(
                "GET",
                QStringLiteral("/containers/%1/stats?stream=false").arg(id)));
            s->socket.waitForBytesWritten(m_timeoutMs);
        } else {
            s->complete = true;
        }
    }

    const qint64 startedMs = QDateTime::currentMSecsSinceEpoch();
    while (QDateTime::currentMSecsSinceEpoch() - startedMs < budgetMs) {
        bool anyOpen = false;
        for (Session *s : sessions) {
            if (s->complete) {
                continue;
            }
            anyOpen = true;
            const qint64 remaining =
                budgetMs - (QDateTime::currentMSecsSinceEpoch() - startedMs);
            s->socket.waitForReadyRead(int(qMin<qint64>(remaining, 200)));
            s->frame += s->socket.readAll();

            if (!s->headerDone) {
                const int sep = s->frame.indexOf("\r\n\r\n");
                if (sep < 0) {
                    continue;
                }
                s->header = s->frame.left(sep);
                s->bodyStart = sep + 4;
                s->headerDone = true;
                const int status = statusFromHeader(s->header);
                if (status != 200) {
                    s->complete = true;
                    continue;
                }
                s->chunked = chunkedFromHeader(s->header);
                s->contentLength = contentLengthFromHeader(s->header,
                                                           &s->hasLength);
            }

            s->payload = s->frame.mid(s->bodyStart);
            if (s->chunked) {
                if (s->payload.endsWith(QByteArrayLiteral("\r\n0\r\n\r\n"))) {
                    s->complete = true;
                    bodies.insert(s->id, dechunk(s->payload));
                }
            } else if (s->hasLength
                       && s->payload.size() >= s->contentLength) {
                s->complete = true;
                bodies.insert(s->id, s->payload.left(s->contentLength));
            }
        }
        if (!anyOpen) {
            break;
        }
    }

    for (Session *s : sessions) {
        delete s;
    }

    if (bodies.isEmpty() && error != nullptr) {
        *error = QStringLiteral("no engine stats completed within budget");
    }
    return bodies;
}
