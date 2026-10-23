#include "MetricsServer.h"
#include "PrometheusStatus.h"

#include <QHostAddress>

namespace {

const char kContentType[] =
    "text/plain; version=0.0.4; charset=utf-8";

void deny(QTcpSocket *socket, const char *status) {
    const QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + status +
        QByteArrayLiteral(
            "\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
            "scrape /metrics\n");
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

} // namespace

MetricsServer::MetricsServer(const MetricsConfig &config, QObject *parent)
    : QObject(parent), m_config(config) {}

void MetricsServer::start() {
    if (m_config.port < 1 || m_config.port > 65535) {
        return;
    }
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this,
            &MetricsServer::onNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost,
                          static_cast<quint16>(m_config.port))) {
        qWarning() << "[metrics] listen failed on"
                   << m_config.port
                   << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return;
    }
    m_port = m_server->serverPort();
    qInfo() << "[metrics] http://127.0.0.1:" << m_port << "/metrics";
}

void MetricsServer::stop() {
    if (m_server != nullptr) {
        m_server->close();
    }
}

void MetricsServer::setSnapshot(const DisplaySnapshot &snapshot) {
    m_snapshot = snapshot;
    m_body = prometheusText(snapshot).toUtf8();
}

void MetricsServer::onNewConnection() {
    if (m_server == nullptr) {
        return;
    }
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        m_buffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this,
                &MetricsServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket,
                &QObject::deleteLater);
    }
}

void MetricsServer::onReadyRead() {
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket == nullptr || !m_buffers.contains(socket)) {
        return;
    }
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();

    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }
    const QByteArray requestLine = buffer.mid(0, buffer.indexOf("\r\n"));
    const QByteArray path = requestLine.split(' ').value(1);
    const bool head = requestLine.startsWith("HEAD");

    if (!path.startsWith("/metrics")) {
        deny(socket, "404 Not Found");
        m_buffers.remove(socket);
        return;
    }

    QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
    response += QByteArrayLiteral("Content-Type: ") + kContentType +
        "\r\nContent-Length: " + QByteArray::number(m_body.size()) +
        QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
    if (!head) {
        response += m_body;
    }
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
    m_buffers.remove(socket);
}
