#include "TlsSource.h"

#include <QDateTime>
#include <QSslCipher>
#include <QSslCertificate>
#include <QSslSocket>
#include <QTimer>

namespace {

// Reporting only: legacy protocols stay mapped so a server that actually
// negotiates TLS 1.0/1.1 is named instead of being filed under "TLS". Qt
// deprecates the enum constants, not the ability to read them.
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED

QString protocolName(QSsl::SslProtocol protocol) {
    switch (protocol) {
    case QSsl::TlsV1_3:
        return QStringLiteral("TLSv1.3");
    case QSsl::TlsV1_2:
        return QStringLiteral("TLSv1.2");
    case QSsl::TlsV1_1:
        return QStringLiteral("TLSv1.1");
    case QSsl::TlsV1_0:
        return QStringLiteral("TLSv1.0");
    default:
        return QStringLiteral("TLS");
    }
}

QT_WARNING_POP

} // namespace

TlsSource::TlsSource(const TlsConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config) {
}

void TlsSource::start() {
    if (!m_config.enabled || m_config.targets.isEmpty()) {
        emit tlsReady(TlsSnapshot{});
        return;
    }
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setInterval(m_config.intervalMs);
        connect(m_timer, &QTimer::timeout, this, &TlsSource::onTick);
    }
    m_timer->start();
    onTick();
}

void TlsSource::stop() {
    if (m_timer != nullptr) {
        m_timer->stop();
    }
}

void TlsSource::onTick() {
    TlsSnapshot snapshot;
    snapshot.available = true;
    const QDateTime now = QDateTime::currentDateTimeUtc();

    for (const QString &target : m_config.targets) {
        QString host = target;
        int port = 443;
        const int colon = target.lastIndexOf(QLatin1Char(':'));
        if (colon > 0) {
            host = target.left(colon);
            port = target.mid(colon + 1).toInt();
            if (port <= 0) {
                port = 443;
            }
        }
        TlsResult result;
        result.host = host;
        result.port = port;

        QSslSocket socket;
        socket.setPeerVerifyMode(QSslSocket::VerifyNone);
        socket.connectToHostEncrypted(host, static_cast<quint16>(port));
        if (!socket.waitForEncrypted(4000)) {
            result.error = QStringLiteral("connect/TLS failed");
            snapshot.results.append(result);
            continue;
        }
        const QSslCertificate cert = socket.peerCertificate();
        if (cert.isNull()) {
            result.error = QStringLiteral("no certificate");
        } else {
            result.ok = true;
            result.subject =
                cert.subjectInfo(QSslCertificate::CommonName).join(QLatin1Char(','));
            result.issuer =
                cert.issuerInfo(QSslCertificate::CommonName).join(QLatin1Char(','));
            result.notBefore = cert.effectiveDate();
            result.notAfter = cert.expiryDate();
            result.daysLeft = daysUntil(result.notAfter, now);
            result.protocol = protocolName(socket.sessionProtocol());
            result.cipher = socket.sessionCipher().name();
        }
        socket.disconnectFromHost();
        snapshot.results.append(result);
    }

    snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
    emit tlsReady(snapshot);
}
