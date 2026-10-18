#include "PgClient.h"

#include <QTcpSocket>
#include <QtEndian>

#include <cstring>

PgClient::PgClient() = default;

PgClient::~PgClient() {
    close();
}

namespace {
QByteArray int32be(qint32 value) {
    QByteArray out(4, 0);
    qToBigEndian(value, out.data());
    return out;
}
qint32 readInt32(const QByteArray &data, int offset) {
    return qFromBigEndian<qint32>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}
qint16 readInt16(const QByteArray &data, int offset) {
    return qFromBigEndian<qint16>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}
} // namespace

bool PgClient::waitForBytes(int n, int timeoutMs) {
    if (m_socket->bytesAvailable() >= n) {
        return true;
    }
    if (!m_socket->waitForReadyRead(timeoutMs)) {
        while (m_socket->bytesAvailable() < n
               && m_socket->waitForReadyRead(50)) {
        }
    }
    return m_socket->bytesAvailable() >= n;
}

bool PgClient::readMessage(char &type, QByteArray &data, int timeoutMs) {
    if (!waitForBytes(5, timeoutMs)) {
        m_error = QStringLiteral("timeout reading message");
        return false;
    }
    m_socket->read(&type, 1);
    const QByteArray lenBytes = m_socket->read(4);
    const qint32 len = readInt32(lenBytes, 0);
    const int payload = len - 4;
    data.clear();
    while (data.size() < payload) {
        if (!m_socket->waitForReadyRead(timeoutMs)
            && m_socket->bytesAvailable() < payload - data.size()) {
            m_error = QStringLiteral("timeout reading payload");
            return false;
        }
        data += m_socket->read(payload - data.size());
    }
    return true;
}

bool PgClient::readUntilReady(int timeoutMs) {
    while (true) {
        char type = 0;
        QByteArray data;
        if (!readMessage(type, data, timeoutMs)) {
            return false;
        }
        if (type == 'R') {
            const qint32 code = data.size() >= 4 ? readInt32(data, 0) : -1;
            if (code != 0) {
                m_error = QStringLiteral("authentication required (code %1)")
                              .arg(code);
                return false;
            }
        } else if (type == 'E') {
            m_error = QStringLiteral("server error during startup");
            return false;
        } else if (type == 'Z') {
            return true;
        }
    }
}

bool PgClient::connect(const QString &host, quint16 port, const QString &user,
                       const QString &database, int timeoutMs) {
    close();
    m_socket = new QTcpSocket();
    m_socket->connectToHost(host, port);
    if (!m_socket->waitForConnected(timeoutMs)) {
        m_error = QStringLiteral("connect failed: %1").arg(m_socket->errorString());
        return false;
    }
    QByteArray body = int32be(196608); // protocol 3.0
    body += "user";
    body += '\0';
    body += user.toUtf8();
    body += '\0';
    body += "database";
    body += '\0';
    body += database.toUtf8();
    body += '\0';
    body += '\0';
    m_socket->write(int32be(body.size() + 4) + body);
    m_socket->flush();
    return readUntilReady(timeoutMs);
}

bool PgClient::query(const QString &sql, QList<QStringList> &rows) {
    rows.clear();
    if (m_socket == nullptr) {
        return false;
    }
    QByteArray body = sql.toUtf8();
    body += '\0';
    m_socket->write(QByteArray("Q") + int32be(body.size() + 4) + body);
    m_socket->flush();

    while (true) {
        char type = 0;
        QByteArray data;
        if (!readMessage(type, data, 5000)) {
            return false;
        }
        if (type == 'D') {
            const int columns = readInt16(data, 0);
            QStringList row;
            int offset = 2;
            for (int i = 0; i < columns; ++i) {
                const qint32 len = readInt32(data, offset);
                offset += 4;
                if (len < 0) {
                    row.append(QString());
                } else {
                    row.append(QString::fromUtf8(data.mid(offset, len)));
                    offset += len;
                }
            }
            rows.append(row);
        } else if (type == 'E') {
            m_error = QStringLiteral("query error");
            // Drain until ReadyForQuery so the session stays usable.
            while (true) {
                char drainType = 0;
                QByteArray drainData;
                if (!readMessage(drainType, drainData, 5000)) {
                    return false;
                }
                if (drainType == 'Z') {
                    return false;
                }
            }
        } else if (type == 'Z') {
            return true;
        }
    }
}

void PgClient::close() {
    if (m_socket != nullptr) {
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

bool PgClient::isConnected() const {
    return m_socket != nullptr && m_socket->state() == QAbstractSocket::ConnectedState;
}
