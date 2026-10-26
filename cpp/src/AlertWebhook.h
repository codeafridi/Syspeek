#pragma once

#include "AlertEngine.h"
#include "Config.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Alert delivery to a user-configured HTTP endpoint. Off by default: Syspeek
// never sends data anywhere unless the user configures a URL.
//
// Payload shapes are grounded on the shipped Prometheus Alertmanager 0.34.1
// binary and its notifier sources:
//   generic -> Alertmanager webhook v4 message
//     {version:"4", groupKey, truncatedAlerts, receiver, status, alerts:[
//      {status, labels, annotations, startsAt, endsAt, generatorURL,
//       fingerprint}], groupLabels, commonLabels, commonAnnotations,
//      externalURL}
//     (notify/webhook/webhook.go Message + template/template.go Data tags)
//   slack   -> Slack incoming-webhook message {text, attachments:[
//      {fallback, color, title, fields[], mrkdwn_in}]}
//     (notify/slack/slack.go SlackMessage/SlackAttachment/SlackField)
struct WebhookEvent {
    QString id;
    QString title;
    QString detail;
    QString severity;
    QString status; // "firing" | "resolved"
    qint64 startsAtMs = 0;
    qint64 endsAtMs = 0;
};

QByteArray alertmanagerWebhookPayload(const QList<WebhookEvent> &events);
QByteArray slackWebhookPayload(const QList<WebhookEvent> &events);
QString webhookConfigLine(const WebhookConfig &config);

class AlertWebhook : public QObject {
    Q_OBJECT

public:
    explicit AlertWebhook(QObject *parent = nullptr);

    void setConfig(const WebhookConfig &config);
    const WebhookConfig &config() const;

    // Feed the engine's fired transitions plus the currently-active alerts
    // each tick; ids that vanish produce one resolved event each.
    void processEvents(const QList<Alert> &fired, const QList<Alert> &active,
                       qint64 nowMs);

    QString statusLine() const;

signals:
    void deliveryFinished(bool ok, const QString &error);

private:
    void post(const QList<WebhookEvent> &events);
    bool ready() const;

    WebhookConfig m_config;
    QNetworkAccessManager *m_network = nullptr;
    QHash<QString, WebhookEvent> m_active;
    QString m_lastResult;
    bool m_lastOk = true;
    int m_sent = 0;
    int m_failed = 0;
};
