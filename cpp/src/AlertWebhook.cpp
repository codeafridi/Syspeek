#include "AlertWebhook.h"

#include <QDateTime>
#include <QTimeZone>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTime>
#include <QUrl>

namespace {

constexpr int kMaxEventsPerPost = 20;

QString isoTime(qint64 ms) {
    if (ms <= 0) {
        return QStringLiteral("0001-01-01T00:00:00Z");
    }
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC)
        .toString(Qt::ISODate);
}

QString slackColor(const WebhookEvent &event) {
    if (event.status == QLatin1String("resolved")) {
        return QStringLiteral("36a64f");
    }
    const QString severity = event.severity.toLower();
    if (severity == QLatin1String("critical")
        || severity == QLatin1String("high")) {
        return QStringLiteral("d1685f");
    }
    if (severity == QLatin1String("warning")
        || severity == QLatin1String("warn")) {
        return QStringLiteral("d9a04a");
    }
    return QStringLiteral("7f6ff2");
}

WebhookEvent eventFor(const Alert &alert, const QString &status,
                      qint64 startsAtMs, qint64 endsAtMs) {
    WebhookEvent event;
    event.id = alert.id;
    event.title = alert.title;
    event.detail = alert.detail;
    event.severity = alert.severity;
    event.status = status;
    event.startsAtMs = startsAtMs > 0 ? startsAtMs : alert.atMs;
    event.endsAtMs = endsAtMs;
    return event;
}

} // namespace

QByteArray alertmanagerWebhookPayload(const QList<WebhookEvent> &events) {
    QJsonArray alerts;
    for (const WebhookEvent &event : events) {
        QJsonObject labels{
            {QStringLiteral("alertname"), event.id},
            {QStringLiteral("severity"), event.severity},
            {QStringLiteral("source"), QStringLiteral("syspeek")}};
        QJsonObject annotations{
            {QStringLiteral("summary"), event.title},
            {QStringLiteral("description"), event.detail}};
        QJsonObject alert{
            {QStringLiteral("status"), event.status},
            {QStringLiteral("labels"), labels},
            {QStringLiteral("annotations"), annotations},
            {QStringLiteral("startsAt"), isoTime(event.startsAtMs)},
            {QStringLiteral("endsAt"), isoTime(event.endsAtMs)},
            {QStringLiteral("generatorURL"), QString()},
            {QStringLiteral("fingerprint"), event.id}};
        alerts.append(alert);
    }

    QJsonObject message{
        {QStringLiteral("version"), QStringLiteral("4")},
        {QStringLiteral("groupKey"), QStringLiteral("syspeek")},
        {QStringLiteral("truncatedAlerts"), 0},
        {QStringLiteral("receiver"), QStringLiteral("syspeek")},
        {QStringLiteral("status"),
         events.isEmpty() ? QString()
                          : events.first().status},
        {QStringLiteral("alerts"), alerts},
        {QStringLiteral("groupLabels"), QJsonObject{}},
        {QStringLiteral("commonLabels"),
         QJsonObject{{QStringLiteral("source"), QStringLiteral("syspeek")}}},
        {QStringLiteral("commonAnnotations"), QJsonObject{}},
        {QStringLiteral("externalURL"), QString()}};
    return QJsonDocument(message).toJson(QJsonDocument::Compact);
}

QByteArray slackWebhookPayload(const QList<WebhookEvent> &events) {
    const bool resolved = !events.isEmpty()
                          && events.first().status
                                 == QLatin1String("resolved");
    const QString text =
        resolved
            ? QStringLiteral(":white_check_mark: Syspeek resolved %1 alert(s)")
                  .arg(events.size())
            : QStringLiteral(":rotating_light: Syspeek firing %1 alert(s)")
                  .arg(events.size());

    QJsonArray attachments;
    for (const WebhookEvent &event : events) {
        QJsonArray fields{
            QJsonObject{{QStringLiteral("title"), QStringLiteral("severity")},
                        {QStringLiteral("value"), event.severity},
                        {QStringLiteral("short"), true}},
            QJsonObject{{QStringLiteral("title"), QStringLiteral("status")},
                        {QStringLiteral("value"), event.status},
                        {QStringLiteral("short"), true}}};
        QJsonObject attachment{
            {QStringLiteral("fallback"),
             QStringLiteral("%1 [%2]").arg(event.title, event.status)},
            {QStringLiteral("color"), slackColor(event)},
            {QStringLiteral("title"), event.title},
            {QStringLiteral("text"), event.detail},
            {QStringLiteral("fields"), fields},
            {QStringLiteral("mrkdwn_in"),
             QJsonArray{QStringLiteral("text")}}};
        attachments.append(attachment);
    }

    QJsonObject message{
        {QStringLiteral("text"), text},
        {QStringLiteral("attachments"), attachments}};
    return QJsonDocument(message).toJson(QJsonDocument::Compact);
}

QString webhookConfigLine(const WebhookConfig &config) {
    if (!config.enabled) {
        return QStringLiteral("off ([webhook] enabled=false)");
    }
    if (config.url.isEmpty()) {
        return QStringLiteral("no url ([webhook] url= missing)");
    }
    return QStringLiteral("%1 -> %2").arg(config.format, config.url);
}

AlertWebhook::AlertWebhook(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this)) {
}

void AlertWebhook::setConfig(const WebhookConfig &config) {
    m_config = config;
}

const WebhookConfig &AlertWebhook::config() const {
    return m_config;
}

bool AlertWebhook::ready() const {
    return m_config.enabled && !m_config.url.isEmpty();
}

void AlertWebhook::processEvents(const QList<Alert> &fired,
                                 const QList<Alert> &active, qint64 nowMs) {
    QSet<QString> activeIds;
    activeIds.reserve(active.size());
    for (const Alert &alert : active) {
        activeIds.insert(alert.id);
    }

    QList<WebhookEvent> firing;
    QSet<QString> firedIds;
    for (const Alert &alert : fired) {
        firedIds.insert(alert.id);
        const auto known = m_active.constFind(alert.id);
        const qint64 startsAt = known != m_active.constEnd()
                                    ? known->startsAtMs
                                    : alert.atMs;
        const WebhookEvent event =
            eventFor(alert, QStringLiteral("firing"), startsAt, 0);
        m_active.insert(alert.id, event);
        if (ready()) {
            firing.append(event);
        }
    }

    for (const Alert &alert : active) {
        if (!m_active.contains(alert.id) && !firedIds.contains(alert.id)) {
            m_active.insert(alert.id,
                            eventFor(alert, QStringLiteral("firing"),
                                     alert.atMs, 0));
        }
    }

    QList<WebhookEvent> resolved;
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (!activeIds.contains(it.key())) {
            WebhookEvent event = it.value();
            event.status = QStringLiteral("resolved");
            event.endsAtMs = nowMs;
            resolved.append(event);
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }

    if (!ready()) {
        return;
    }
    if (!firing.isEmpty()) {
        post(firing);
    }
    if (!resolved.isEmpty()) {
        post(resolved);
    }
}

void AlertWebhook::post(const QList<WebhookEvent> &events) {
    if (events.size() > kMaxEventsPerPost) {
        post(events.mid(0, kMaxEventsPerPost));
        post(events.mid(kMaxEventsPerPost));
        return;
    }

    const QByteArray payload =
        m_config.format == QLatin1String("slack")
            ? slackWebhookPayload(events)
            : alertmanagerWebhookPayload(events);

    QNetworkRequest request{QUrl(m_config.url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setTransferTimeout(m_config.timeoutMs);
    QNetworkReply *reply = m_network->post(request, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, events]() {
        reply->deleteLater();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool transportOk = reply->error() == QNetworkReply::NoError;
        bool ok = transportOk && status >= 200 && status < 300;
        QString error;
        if (!ok) {
            if (status > 0) {
                error = QStringLiteral("HTTP %1").arg(status);
            } else {
                error = reply->errorString();
            }
        }
        m_lastOk = ok;
        if (ok) {
            ++m_sent;
            m_lastResult =
                QStringLiteral("ok %1 (%2 %3)")
                    .arg(QTime::currentTime().toString(QStringLiteral("HH:mm")),
                         QString::number(events.size()),
                         events.first().status);
            qInfo().noquote() << QStringLiteral("[webhook] delivered")
                              << events.size() << events.first().status;
        } else {
            ++m_failed;
            m_lastResult = QStringLiteral("error %1").arg(error);
            qWarning().noquote() << QStringLiteral("[webhook] delivery failed:")
                                 << error;
        }
        emit deliveryFinished(ok, error);
    });
}

QString AlertWebhook::statusLine() const {
    QString line = webhookConfigLine(m_config);
    if (!ready()) {
        return line;
    }
    line += QStringLiteral(" - %1 sent, %2 failed").arg(m_sent).arg(m_failed);
    if (!m_lastResult.isEmpty()) {
        line += QStringLiteral("; last ") + m_lastResult;
    }
    return line;
}
