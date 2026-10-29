#pragma once

#include "Config.h"

#include <QByteArray>
#include <QString>

struct MetricsEndpoint {
    QString bindAddress = QStringLiteral("127.0.0.1");
    int port = 9107;
    bool authRequired = false;
    bool bindDowngraded = false;
    QString note;
};

bool metricsAddressIsLoopback(const QString &address);

MetricsEndpoint resolveMetricsEndpoint(const MetricsConfig &config);

bool metricsTokenMatches(const QByteArray &presented, const QByteArray &token);

bool metricsRequestAuthorized(const QByteArray &requestHead,
                              const QByteArray &token);

QByteArray metricsAuthorizationValue(const QString &token);

QString metricsEndpointText(const MetricsEndpoint &endpoint);
