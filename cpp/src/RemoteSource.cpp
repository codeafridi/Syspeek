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

// The relay's own words when it refuses a body, on one bounded line: a 422 is
// only actionable if it says which field it did not like.
QString remoteAnswerReason(const QByteArray &body) {
    if (body.trimmed().isEmpty()) {
        return QStringLiteral("no reason given");
    }
    return QString::fromUtf8(body).simplified().left(160);
}

} // namespace

QSslConfiguration remoteSslConfiguration(const QString &caFile,
                                         QString *error) {
    if (error != nullptr) {
        error->clear();
    }
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    // Never AutoVerifyPeer: this client is a client, and saying so explicitly
    // means a future platform default cannot quietly stop verifying.
    config.setPeerVerifyMode(QSslSocket::VerifyPeer);
    if (caFile.isEmpty()) {
        return config;
    }
    const QList<QSslCertificate> extra = QSslCertificate::fromPath(
        caFile, QSsl::Pem, QSslCertificate::PatternSyntax::FixedString);
    if (extra.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("the CA file %1 holds no certificate "
                                    "(expected PEM)")
                         .arg(caFile);
        }
        return QSslConfiguration();
    }
    config.addCaCertificates(extra);
    return config;
}

RemotePostResult remoteHttpPost(const QString &url, const QString &token,
                                const QByteArray &body, int timeoutMs,
                                const QString &caFile) {
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
        QString caError;
        const QSslConfiguration sslConfig =
            remoteSslConfiguration(caFile, &caError);
        if (!caError.isEmpty()) {
            result.latencyMs = int(clock.elapsed());
            result.error = caError;
            result.classification = RemoteClass::Transport;
            return result;
        }
        secure->setSslConfiguration(sslConfig);
        // Name the certificate the relay must present: a certificate for
        // another host is not a valid answer to this address.
        secure->setPeerVerifyName(target.host());
        secure->connectToHostEncrypted(target.host(), quint16(port));
        if (!secure->waitForConnected(budget)) {
            result.latencyMs = int(clock.elapsed());
            result.error = secure->errorString();
            result.classification = RemoteClass::Transport;
            return result;
        }
        // One budget for the whole exchange, not one per step: a host that
        // blackholes the handshake must not cost twice the configured timeout.
        const int handshakeBudget = budget - int(clock.elapsed());
        if (handshakeBudget <= 0
            || !secure->waitForEncrypted(qMax(1, handshakeBudget))) {
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
    {
        QMutexLocker lock(&m_mutex);
        m_envelope = envelope;
        m_haveEnvelope = true;
    }
    // M98 write-ahead: the report is on disk before a socket is opened, so a
    // closed laptop, a crash or a relay that stays down for an hour costs
    // nothing - the bytes are already somewhere that survives the process.
    if (m_spool.isOpen() && !envelope.machine.id.isEmpty()) {
        const QString id = envelope.sample.id.isEmpty()
            ? remoteSampleId(envelope.machine.id, envelope.machine.sentAtMs)
            : envelope.sample.id;
        if (id != m_lastSpooledId) {
            const QByteArray body = remoteEnvelopeJson(envelope);
            if (m_spool.append(id, envelope.machine.sentAtMs, body)) {
                m_lastSpooledId = id;
            } else {
                logOnce(QStringLiteral("remote: a report of %1 bytes does not "
                                       "fit one spool record - it is sent but "
                                       "not buffered")
                            .arg(body.size()));
            }
            publishState();
        }
    }
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
    openSpool(options());
    publishState();
    poll();
}

void RemoteRunner::openSpool(const RemoteReportOptions &options) {
    if (m_spool.isOpen()) {
        m_spool.close();
    }
    if (options.spoolPath.isEmpty() || options.spoolBytes <= 0) {
        return;
    }
    if (!m_spool.open(options.spoolPath, options.spoolBytes)) {
        logOnce(QStringLiteral("remote: spool %1 could not be opened - an "
                               "undelivered report is dropped and counted")
                    .arg(options.spoolPath));
    }
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
        syncSpoolLocked();
        // An empty note means "describe the state as it now stands"; a caller
        // with a specific verdict ("delivered in 4ms") still carries the spool
        // line, because a delivery does not empty the spool.
        m_state.note = note.isEmpty()
            ? remoteReportNote(m_state)
            : note + remoteSpoolNoteSuffix(m_state);
        copy = m_state;
    }
    emit reportSent(copy.lastClass == RemoteClass::Ok, copy.lastClass,
                    copy.lastLatencyMs, copy.note);
    emit stateChanged(copy);
}

void RemoteRunner::publishState() {
    RemoteReportState copy;
    {
        QMutexLocker lock(&m_mutex);
        syncSpoolLocked();
        m_state.note = remoteReportNote(m_state);
        copy = m_state;
    }
    emit stateChanged(copy);
}

void RemoteRunner::syncSpoolLocked() {
    m_state.spoolOn = m_spool.isOpen();
    m_state.spooled = m_spool.isOpen() ? m_spool.count() : 0;
    m_state.spoolCapacity = m_spool.isOpen() ? m_spool.capacity() : 0;
    m_state.spoolDropped = m_spool.isOpen() ? m_spool.dropped() : 0;
    m_state.spoolBytes = m_spool.isOpen() ? m_spool.fileBytes() : 0;
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

    // M98: with the spool on, the oldest stored report is the next thing to
    // attempt - a report is written before it is sent, so nothing can be lost
    // between the tick and the delivery. A record that does not read back is a
    // corrupt slot: skip it and say so, instead of retrying bytes that will
    // never parse and blocking the queue behind them.
    RemoteSpoolRecord oldest;
    int skipped = 0;
    while (m_spool.isOpen() && m_spool.count() > 0
           && !m_spool.peekOldest(oldest)) {
        m_spool.ackOldest();
        ++skipped;
    }
    if (skipped > 0) {
        qWarning("remote: %d spool record(s) could not be read back - skipped",
                 skipped);
    }
    const bool fromSpool = m_spool.isOpen() && m_spool.count() > 0;

    QByteArray body;
    qint64 sentAtMs = -1;
    if (fromSpool) {
        body = oldest.body;
        sentAtMs = oldest.sentAtMs;
    } else {
        if (!haveEnvelope) {
            // The console owns the ring and the tick; the reporter asks for
            // bytes it has not been handed yet, which is the first second of a
            // start, not a configuration problem.
            publish(QStringLiteral("remote: waiting for the first report from "
                                   "the console"));
            schedule(options.intervalMs);
            return;
        }
        if (envelope.machine.id.isEmpty()) {
            publish(QStringLiteral("remote: no report built yet - a report "
                                   "needs a machine id (set [remote] "
                                   "machine_id=)"));
            schedule(options.intervalMs);
            return;
        }
        if (envelope.machine.sentAtMs == lastSentAtMs) {
            // Nothing new since the last accepted report: querying again would
            // only repeat the same numbers and buy a second copy on the relay.
            schedule(options.intervalMs);
            return;
        }
        body = remoteEnvelopeJson(envelope);
        sentAtMs = envelope.machine.sentAtMs;
    }

    const RemotePostResult result = remoteHttpPost(
        options.url, options.token, body, options.timeoutMs, options.caFile);

    // A body the relay refused outright (413/422/other 4xx) will be refused
    // forever: drop it with the answer on the record and move on, rather than
    // resending it and stalling every report behind it.
    const bool refused =
        fromSpool && result.classification == RemoteClass::Rejected;
    if (fromSpool
        && (result.classification == RemoteClass::Ok || refused)) {
        m_spool.ackOldest();
    }

    int backlog = 0;
    int retryMs = -1;
    bool stoppedNow = false;
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
            m_consecutiveRefused = 0;
            if (!fromSpool) {
                m_lastSentAtMs = sentAtMs;
            }
        } else if (refused) {
            m_state.failed += 1;
            m_state.refused += 1;
            m_state.consecutiveFailures = 0;
            m_state.nextRetryMs = -1;
            ++m_consecutiveRefused;
            if (m_consecutiveRefused >= kRemoteMaxConsecutiveRefused) {
                // Three refusals in a row is a configuration, not a bad report:
                // stop and keep whatever is left rather than emptying the
                // buffer into a relay that will refuse all of it.
                m_state.stopped = true;
            }
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
        backlog = m_spool.isOpen() ? m_spool.count() : 0;
        retryMs = m_state.nextRetryMs;
        stoppedNow = m_state.stopped;
    }

    // A backlog is drained quickly but not instantly: 250 ms between records
    // keeps a reopened laptop from turning an hour of buffered reports into a
    // burst the relay has to absorb in one connection.
    const int nextDelay = backlog > 0 ? kRemoteBacklogStepMs
                                      : options.intervalMs;

    if (result.classification == RemoteClass::Ok) {
        publish(QStringLiteral("remote: delivered in %1ms")
                    .arg(result.latencyMs));
        schedule(nextDelay);
        return;
    }
    if (refused) {
        logOnce(QStringLiteral("remote: the relay refused a batch with %1 - "
                               "dropped it: %2")
                    .arg(result.httpStatus)
                    .arg(remoteAnswerReason(result.body)));
        publish(QString());
        if (!stoppedNow) {
            schedule(nextDelay);
        } else {
            logOnce(QStringLiteral("remote: stopped after %1 refused payloads - "
                                   "%2 report(s) stay in the spool")
                        .arg(m_consecutiveRefused)
                        .arg(backlog));
        }
        return;
    }
    if (remoteClassIsTerminal(result.classification)) {
        // Auth/NotFound: the address or the token is wrong. Stop sending, and
        // leave the spool exactly as it is - those reports are still valid.
        logOnce(QStringLiteral("remote: stopped - %1")
                    .arg(remoteClassText(result.classification,
                                         result.httpStatus)));
        publish(QString());
        return;
    }
    publish(QString());
    schedule(retryMs > 0 ? retryMs : nextDelay);
}
