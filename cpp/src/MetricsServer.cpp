#include "MetricsServer.h"
#include "PrometheusStatus.h"

#include <QHostAddress>
#include <QHostInfo>

namespace {

const char kContentType[] =
    "text/plain; version=0.0.4; charset=utf-8";
const char kJsonType[] = "application/json";

void respond(QTcpSocket *socket, const char *status, const char *contentType,
             const QByteArray &body, bool head) {
    QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + status + "\r\n";
    response += QByteArrayLiteral("Content-Type: ") + contentType + "\r\n";
    response += QByteArrayLiteral("Content-Length: ")
        + QByteArray::number(body.size())
        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
    if (!head) {
        response += body;
    }
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void deny(QTcpSocket *socket, const char *status) {
    respond(socket, status, "text/plain",
            QByteArrayLiteral("scrape /metrics or GET /api/v1/snapshot\n"),
            false);
}

void denyUnauthorized(QTcpSocket *socket) {
    const QByteArray body =
        QByteArrayLiteral("401 unauthorized - send Authorization: Bearer <token>\n");
    const QByteArray response = QByteArrayLiteral("HTTP/1.1 401 Unauthorized\r\n")
        + QByteArrayLiteral("WWW-Authenticate: Bearer realm=\"syspeek\"\r\n")
        + QByteArrayLiteral("Content-Type: text/plain\r\nContent-Length: ")
        + QByteArray::number(body.size())
        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

} // namespace

MetricsServer::MetricsServer(const MetricsConfig &config, QObject *parent)
    : QObject(parent),
      m_config(config),
      m_endpoint(resolveMetricsEndpoint(config)),
      m_token(config.token.trimmed().toUtf8()),
      m_metricsEnabled(config.enabled) {}

void MetricsServer::setFleetInfo(const FleetSelfInfo &info, bool share) {
    m_fleetInfo = info;
    m_fleetShare = share;
    rebuildFleetBody();
}

void MetricsServer::rebuildFleetBody() {
    m_fleetBody = fleetSnapshotJson(m_snapshot, m_fleetInfo.name,
                                    m_fleetInfo.hostname, m_fleetInfo.version,
                                    m_fleetInfo.platform);
}

void MetricsServer::start() {
    if (m_endpoint.port < 0 || m_endpoint.port > 65535) {
        qWarning().noquote() << QStringLiteral(
            "[metrics] invalid port %1 - endpoint stays closed")
            .arg(m_endpoint.port);
        return;
    }
    if (m_endpoint.bindDowngraded) {
        qWarning().noquote()
            << QStringLiteral("[metrics] ") + m_endpoint.note;
    }
    QHostAddress address(m_endpoint.bindAddress);
    if (address.isNull()) {
        const QHostInfo info =
            QHostInfo::fromName(m_endpoint.bindAddress);
        if (!info.addresses().isEmpty()) {
            address = info.addresses().first();
        }
    }
    if (address.isNull()) {
        qWarning().noquote() << QStringLiteral(
            "[metrics] cannot resolve bind address %1 - using 127.0.0.1")
            .arg(m_endpoint.bindAddress);
        address = QHostAddress(QHostAddress::LocalHost);
        m_endpoint.bindAddress = QStringLiteral("127.0.0.1");
    }
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this,
            &MetricsServer::onNewConnection);
    if (!m_server->listen(address,
                          static_cast<quint16>(m_endpoint.port))) {
        qWarning() << "[metrics] listen failed on"
                   << m_endpoint.bindAddress
                   << m_endpoint.port
                   << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return;
    }
    m_port = m_server->serverPort();
    MetricsEndpoint live = m_endpoint;
    live.port = m_port;
    qInfo().noquote() << QStringLiteral("[metrics] listening on %1")
                             .arg(metricsEndpointText(live));
}

void MetricsServer::stop() {
    if (m_server != nullptr) {
        m_server->close();
    }
}

void MetricsServer::setFleetStatus(const FleetStatus &fleet) {
    m_fleetStatus = fleet;
    m_fleetReported = true;
    m_body = prometheusText(m_snapshot).toUtf8() + prometheusFleetText(fleet).toUtf8();
}

void MetricsServer::setSnapshot(const DisplaySnapshot &snapshot) {
    // The sampler knows nothing about alerts; keep the console-pushed state
    // across a snapshot replace so a scrape never flaps to zero.
    const AlertExposure alerts = m_snapshot.alerts;
    const int idle = m_snapshot.idleContainers;
    m_snapshot = snapshot;
    m_snapshot.alerts = alerts;
    m_snapshot.idleContainers = idle;
    m_body = prometheusText(m_snapshot).toUtf8();
    if (m_fleetReported) {
        m_body += prometheusFleetText(m_fleetStatus).toUtf8();
    }
    rebuildFleetBody();
}

void MetricsServer::setIdleContainers(int count) {
    m_snapshot.idleContainers = count;
    m_body = prometheusText(m_snapshot).toUtf8();
    if (m_fleetReported) {
        m_body += prometheusFleetText(m_fleetStatus).toUtf8();
    }
    rebuildFleetBody();
}

void MetricsServer::setAlertState(const AlertExposure &alerts) {
    m_snapshot.alerts = alerts;
    m_snapshot.alerts.pushed = true;
    m_body = prometheusText(m_snapshot).toUtf8();
    if (m_fleetReported) {
        m_body += prometheusFleetText(m_fleetStatus).toUtf8();
    }
    rebuildFleetBody();
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

    if (!metricsRequestAuthorized(buffer.left(headerEnd), m_token)) {
        denyUnauthorized(socket);
        m_buffers.remove(socket);
        return;
    }

    if (path.startsWith("/api/v1/snapshot")) {
        if (!m_fleetShare) {
            respond(socket, "404 Not Found", "text/plain",
                    QByteArrayLiteral(
                        "snapshot sharing is off ([fleet] share=false)\n"),
                    head);
        } else {
            respond(socket, "200 OK", kJsonType, m_fleetBody, head);
        }
        m_buffers.remove(socket);
        return;
    }

    if (!path.startsWith("/metrics")) {
        deny(socket, "404 Not Found");
        m_buffers.remove(socket);
        return;
    }

    if (!m_metricsEnabled) {
        respond(socket, "404 Not Found", "text/plain",
                QByteArrayLiteral(
                    "the scrape endpoint is off ([metrics] enabled=false)\n"),
                head);
        m_buffers.remove(socket);
        return;
    }

    respond(socket, "200 OK", kContentType, m_body, head);
    m_buffers.remove(socket);
}
