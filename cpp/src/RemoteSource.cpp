#include "RemoteSource.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace {

// One line per terminal answer. The card (M99) shows the same text, so this is
// the log a user can grep for rather than a second vocabulary.
void logOnce(const QString &note) {
    qWarning("%s", note.toUtf8().constData());
}

} // namespace

RemotePostResult remoteHttpPost(const QString &url, const QString &token,
                                const QByteArray &body, int timeoutMs) {
    RemotePostResult result;
    const QUrl target(url);
    if (!target.isValid() || target.host().isEmpty()) {
        result.error = QStringLiteral("not a usable URL: %1").arg(url);
        result.classification = RemoteClass::Transport;
        return result;
    }
    const int budget = qBound(100, timeoutMs, 30000);
    const bool tls =
        target.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive)
        == 0;
    const int defaultPort = tls ? 443 : 80;
    const int port = target.port(defaultPort);

    QTcpSocket plain;
    // Same M95 lesson as the fleet fetch: constructing a QSslSocket touches the
    // TLS backend, which is not available in the QCoreApplication-free --dump
    // path. Only an https relay needs it.
    std::unique_ptr<QSslSocket> secure;
    QTcpSocket *socket = &plain;
    if (tls) {
        secure = std::make_unique<QSslSocket>();
        socket = secure.get();
    }

    QString hostHeader = target.host();
    if (port != defaultPort) {
        hostHeader += QLatin1Char(':') + QString::number(port);
    }

    QByteArray request = QByteArrayLiteral("POST ") + target.path().toUtf8()
        + QByteArrayLiteral(" HTTP/1.1\r\nHost: ") + hostHeader.toUtf8()
        + QByteArrayLiteral("\r\nAccept: application/json\r\n")
        + QByteArrayLiteral("Content-Type: application/json\r\n")
        + QByteArrayLiteral("Content-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n");
    if (!token.isEmpty()) {
        request += QByteArrayLiteral("Authorization: Bearer ") + token.toUtf8()
            + QByteArrayLiteral("\r\n");
    }
    request += QByteArrayLiteral("Connection: close\r\n\r\n") + body;

    QElapsedTimer clock;
    clock.start();

    if (tls) {
        secure->connectToHostEncrypted(target.host(), quint16(port));
        if (!secure->waitForConnected(budget)) {
            result.latencyMs = int(clock.elapsed());
            result.error = secure->errorString();
            result.classification = RemoteClass::Transport;
            return result;
        }
        if (!secure->waitForEncrypted(budget)) {
            result.latencyMs = int(clock.elapsed());
            result.error = QStringLiteral("TLS handshake failed: %1")
                               .arg(secure->errorString());
            result.classification = RemoteClass::Transport;
            secure->abort();
            return result;
        }
    } else {
        plain.connectToHost(target.host(), quint16(port));
        if (!plain.waitForConnected(budget)) {
            result.latencyMs = int(clock.elapsed());
            result.error = plain.errorString();
            result.classification = RemoteClass::Transport;
            return result;
        }
    }

    socket->write(request);
    socket->flush();

    QByteArray raw;
    // A relay answers with a declared body and closes, but a proxy in front of
    // it may hold the connection open, so "the body is complete" ends the read
    // just as much as EOF does. Waiting for a close that never comes would
    // turn a delivered report into a timeout.
    int headEnd = -1;
    int expectedBody = -1;
    while (true) {
        if (socket->bytesAvailable() > 0) {
            raw += socket->readAll();
        }
        if (headEnd < 0) {
            headEnd = raw.indexOf("\r\n\r\n");
            if (headEnd >= 0) {
                const QList<QByteArray> headLines =
                    raw.left(headEnd).split('\n');
                for (const QByteArray &line : headLines) {
                    const QByteArray lower = line.trimmed().toLower();
                    if (!lower.startsWith("content-length:")) {
                        continue;
                    }
                    bool okLength = false;
                    const int length = lower.mid(15).trimmed().toInt(&okLength);
                    if (okLength && length >= 0) {
                        expectedBody = length;
                    }
                }
            }
        }
        if (expectedBody >= 0 && headEnd >= 0
            && raw.size() >= headEnd + 4 + expectedBody) {
            break;
        }
        if (socket->state() == QAbstractSocket::UnconnectedState) {
            raw += socket->readAll();
            break;
        }
        const int remaining = budget - int(clock.elapsed());
        if (remaining <= 0) {
            result.error = QStringLiteral("no answer within %1ms").arg(budget);
            socket->abort();
            break;
        }
        socket->waitForReadyRead(qMin(remaining, 200));
    }
    result.latencyMs = int(clock.elapsed());
    if (!result.error.isEmpty()) {
        result.classification = RemoteClass::Transport;
        return result;
    }

    const int headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        result.error = QStringLiteral("no HTTP response");
        result.classification = RemoteClass::Transport;
        return result;
    }
    const QByteArray head = raw.left(headerEnd);
    const QList<QByteArray> lines = head.split('\n');
    const QList<QByteArray> statusParts = lines.value(0).trimmed().split(' ');
    bool okStatus = false;
    const int status = statusParts.value(1).toInt(&okStatus);
    if (!okStatus) {
        result.error = QStringLiteral("malformed status line");
        result.classification = RemoteClass::Transport;
        return result;
    }

    result.httpStatus = status;
    result.classification = remoteClassify(status);
    result.retryAfterMs = remoteRetryAfterMs(head);

    QByteArray responseBody = raw.mid(headerEnd + 4);
    for (const QByteArray &line : lines) {
        const QByteArray lower = line.trimmed().toLower();
        if (!lower.startsWith("content-length:")) {
            continue;
        }
        bool okLength = false;
        const int length = lower.mid(15).trimmed().toInt(&okLength);
        if (okLength && length >= 0 && responseBody.size() > length) {
            responseBody.truncate(length);
        }
    }
    result.body = responseBody;
    if (result.classification != RemoteClass::Ok) {
        result.error = remoteClassText(result.classification, status);
    }
    return result;
}

RemoteRunner::RemoteRunner(QObject *parent)
    : QObject(parent) {
}

RemoteRunner::~RemoteRunner() {
    stop();
}

void RemoteRunner::setOptions(const RemoteReportOptions &options) {
    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        changed = m_options.enabled != options.enabled
            || m_options.url != options.url || m_options.token != options.token
            || m_options.intervalMs != options.intervalMs
            || m_options.timeoutMs != options.timeoutMs;
        m_options = options;
        if (changed) {
            // A new address or key is a new question: the old answer does not
            // apply, so a terminal stop is lifted.
            m_state.stopped = false;
            m_state.consecutiveFailures = 0;
            m_state.nextRetryMs = -1;
        }
    }
    if (changed && m_timer != nullptr) {
        QMetaObject::invokeMethod(this, "pollNow", Qt::QueuedConnection);
    }
}

RemoteReportOptions RemoteRunner::options() const {
    QMutexLocker lock(&m_mutex);
    return m_options;
}

void RemoteRunner::setEnvelope(const RemoteEnvelope &envelope) {
    QMutexLocker lock(&m_mutex);
    m_envelope = envelope;
    m_haveEnvelope = true;
}

RemoteReportState RemoteRunner::state() const {
    QMutexLocker lock(&m_mutex);
    return m_state;
}

void RemoteRunner::start() {
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::CoarseTimer);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, &RemoteRunner::poll);
    }
    poll();
}

void RemoteRunner::stop() {
    if (m_timer != nullptr) {
        m_timer->stop();
    }
}

void RemoteRunner::pollNow() {
    poll();
}

void RemoteRunner::schedule(int delayMs) {
    if (m_timer == nullptr) {
        return;
    }
    m_timer->setInterval(qMax(200, delayMs));
    m_timer->start();
}

void RemoteRunner::publish(const QString &note) {
    RemoteReportState copy;
    {
        QMutexLocker lock(&m_mutex);
        m_state.note = note;
        copy = m_state;
    }
    emit reportSent(copy.lastClass == RemoteClass::Ok, copy.lastClass,
                    copy.lastLatencyMs, copy.note);
}

void RemoteRunner::poll() {
    RemoteReportOptions options;
    RemoteEnvelope envelope;
    bool haveEnvelope = false;
    RemoteReportState before;
    qint64 lastSentAtMs = -1;
    {
        QMutexLocker lock(&m_mutex);
        options = m_options;
        envelope = m_envelope;
        haveEnvelope = m_haveEnvelope;
        before = m_state;
        lastSentAtMs = m_lastSentAtMs;
    }
    if (m_timer != nullptr) {
        m_timer->stop();
    }

    if (!options.enabled) {
        publish(QStringLiteral("remote: off - nothing leaves this machine"));
        return;
    }
    if (options.url.isEmpty()) {
        publish(QStringLiteral("remote: no ingest address - set [remote] url="));
        return;
    }
    if (before.stopped) {
        return;
    }
    if (!haveEnvelope) {
        // The console owns the ring and the tick; the reporter asks for bytes it
        // has not been handed yet, which is the first second of a start, not a
        // configuration problem.
        publish(QStringLiteral("remote: waiting for the first report from the "
                               "console"));
        schedule(options.intervalMs);
        return;
    }
    if (envelope.machine.id.isEmpty()) {
        publish(QStringLiteral("remote: no report built yet - a report needs "
                               "a machine id (set [remote] machine_id=)"));
        schedule(options.intervalMs);
        return;
    }
    if (envelope.machine.sentAtMs == lastSentAtMs) {
        // Nothing new since the last accepted report: querying again would only
        // repeat the same numbers and buy a second copy on the relay.
        schedule(options.intervalMs);
        return;
    }

    const QByteArray body = remoteEnvelopeJson(envelope);
    const RemotePostResult result = remoteHttpPost(
        options.url, options.token, body, options.timeoutMs);

    RemoteReportState state;
    {
        QMutexLocker lock(&m_mutex);
        m_state.lastClass = result.classification;
        m_state.lastStatus = result.httpStatus;
        m_state.lastLatencyMs = result.latencyMs;
        m_state.lastAttemptMs = QDateTime::currentMSecsSinceEpoch();
        if (result.classification == RemoteClass::Ok) {
            m_state.sent += 1;
            m_state.lastOkMs = m_state.lastAttemptMs;
            m_state.consecutiveFailures = 0;
            m_state.nextRetryMs = -1;
            m_lastSentAtMs = envelope.machine.sentAtMs;
        } else {
            m_state.failed += 1;
            m_state.consecutiveFailures += 1;
            if (remoteClassIsTerminal(result.classification)) {
                m_state.stopped = true;
                m_state.nextRetryMs = -1;
            } else {
                const int ladder = remoteBackoffMs(m_state.consecutiveFailures,
                                                   options.intervalMs);
                m_state.nextRetryMs =
                    result.classification == RemoteClass::Throttled
                        && result.retryAfterMs > 0
                    ? int(qMin<qint64>(result.retryAfterMs, options.intervalMs))
                    : ladder;
            }
        }
        state = m_state;
    }

    if (result.classification == RemoteClass::Ok) {
        publish(QStringLiteral("remote: delivered in %1ms")
                    .arg(result.latencyMs));
        schedule(options.intervalMs);
        return;
    }
    if (remoteClassIsTerminal(result.classification)) {
        logOnce(QStringLiteral("remote: stopped - %1")
                    .arg(remoteClassText(result.classification,
                                         result.httpStatus)));
        publish(remoteReportNote(state));
        return;
    }
    publish(remoteReportNote(state));
    schedule(state.nextRetryMs > 0 ? state.nextRetryMs : options.intervalMs);
}
