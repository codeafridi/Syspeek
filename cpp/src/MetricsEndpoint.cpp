#include "MetricsEndpoint.h"

#include <QHostAddress>
#include <QList>
#include <QStringList>

bool metricsAddressIsLoopback(const QString &address) {
    QString value = address.trimmed();
    if (value.isEmpty()) {
        return true;
    }
    if (value.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (value == QLatin1String("*")) {
        return false;
    }
    const QHostAddress parsed(value);
    if (parsed.isNull()) {
        return false;
    }
    return parsed.isLoopback();
}

MetricsEndpoint resolveMetricsEndpoint(const MetricsConfig &config) {
    MetricsEndpoint endpoint;
    QString bind = config.bindAddress.trimmed();
    if (bind.isEmpty()) {
        bind = QStringLiteral("127.0.0.1");
    }
    if (bind.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) {
        bind = QStringLiteral("127.0.0.1");
    }
    endpoint.bindAddress = bind;
    endpoint.port = config.port;

    const QByteArray token = config.token.trimmed().toUtf8();
    endpoint.authRequired = !token.isEmpty();

    if (!metricsAddressIsLoopback(bind)) {
        if (!endpoint.authRequired) {
            endpoint.bindDowngraded = true;
            endpoint.note = QStringLiteral(
                "refusing to expose %1 without a token - using 127.0.0.1")
                                .arg(bind);
            endpoint.bindAddress = QStringLiteral("127.0.0.1");
        } else {
            endpoint.note = QStringLiteral("bearer token required for %1")
                                .arg(bind);
        }
    } else if (endpoint.authRequired) {
        endpoint.note = QStringLiteral("bearer token required");
    }
    return endpoint;
}

bool metricsTokenMatches(const QByteArray &presented, const QByteArray &token) {
    if (token.isEmpty() || presented.isEmpty()) {
        return false;
    }
    int diff = presented.size() ^ token.size();
    const int width = qMax(presented.size(), token.size());
    for (int i = 0; i < width; ++i) {
        const char left = i < presented.size() ? presented.at(i) : '\0';
        const char right = i < token.size() ? token.at(i) : '\0';
        diff |= (left ^ right);
    }
    return diff == 0;
}

bool metricsRequestAuthorized(const QByteArray &requestHead,
                              const QByteArray &token) {
    if (token.isEmpty()) {
        return true;
    }
    const QList<QByteArray> lines = requestHead.split('\n');
    for (const QByteArray &rawLine : lines) {
        const QByteArray line = rawLine.trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        const QByteArray name = line.left(colon).trimmed().toLower();
        if (name != QByteArrayLiteral("authorization")) {
            continue;
        }
        const QByteArray value = line.mid(colon + 1).trimmed();
        const int space = value.indexOf(' ');
        if (space <= 0) {
            return false;
        }
        const QByteArray scheme = value.left(space).trimmed().toLower();
        if (scheme != QByteArrayLiteral("bearer")) {
            return false;
        }
        return metricsTokenMatches(value.mid(space + 1).trimmed(), token);
    }
    return false;
}

QByteArray metricsAuthorizationValue(const QString &token) {
    return QByteArrayLiteral("Bearer ") + token.trimmed().toUtf8();
}

QString metricsEndpointText(const MetricsEndpoint &endpoint) {
    QString host = endpoint.bindAddress;
    if (host.contains(QLatin1Char(':'))
        && !host.startsWith(QLatin1Char('['))) {
        host = QStringLiteral("[%1]").arg(host);
    }
    QString text = QStringLiteral("http://%1:%2/metrics")
                       .arg(host)
                       .arg(endpoint.port);
    text += endpoint.authRequired
        ? QStringLiteral(" (bearer token required)")
        : QStringLiteral(" (no auth, loopback only)");
    if (!endpoint.note.isEmpty() && endpoint.bindDowngraded) {
        text += QStringLiteral(" - ") + endpoint.note;
    }
    return text;
}
