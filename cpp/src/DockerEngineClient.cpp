#include "DockerEngineClient.h"

#include <QLocalSocket>
#include <QTimer>

namespace {

QByteArray requestBytes(const QString &path) {
    return QStringLiteral(
               "GET %1 HTTP/1.1\r\n"
               "Host: docker\r\n"
               "User-Agent: syspeek/0.8\r\n"
               "Accept: application/json\r\n"
               "Connection: close\r\n"
               "\r\n")
        .arg(path)
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
                             QString *error) {
    m_lastError.clear();
    if (body != nullptr) {
        body->clear();
    }
    if (error != nullptr) {
        error->clear();
    }

    QLocalSocket socket;
    socket.connectToServer(m_socketPath);
    if (!socket.waitForConnected(m_timeoutMs)) {
        m_lastError = socket.errorString();
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    socket.write(requestBytes(path));
    if (!socket.waitForBytesWritten(m_timeoutMs)) {
        m_lastError = QStringLiteral("write to engine did not drain");
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    QByteArray frame;
    while (frame.indexOf("\r\n\r\n") < 0) {
        if (!socket.waitForReadyRead(m_timeoutMs)) {
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
    if (status != 200) {
        m_lastError = QStringLiteral("engine status %1 for %2").arg(status).arg(
            path);
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
        if (!socket.waitForReadyRead(m_timeoutMs)) {
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
