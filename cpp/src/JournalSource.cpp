#include "JournalSource.h"

#if defined(Q_OS_MACOS)
#include "platform/mac/MacLogSource.h"
#endif

#if defined(Q_OS_WIN)
#include "platform/win/WinLogSource.h"
#endif

#include <QDateTime>
#include <QStandardPaths>
#include <QTimer>

JournalSource::JournalSource(int intervalMs, QObject *parent)
    : QObject(parent)
    , m_intervalMs(intervalMs > 1000 ? intervalMs : 10000) {
}

void JournalSource::start() {
    if (m_process == nullptr) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this,
                [this]() { m_stdout += m_process->readAllStandardOutput(); });
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &JournalSource::onFinished);
        connect(m_process, &QProcess::errorOccurred, this,
                &JournalSource::onErrorOccurred);
    }
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, &JournalSource::onTimeout);
    }
    beginQuery();
}

void JournalSource::stop() {
    m_stopping = true;
    if (m_timer != nullptr) {
        m_timer->stop();
    }
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(500)) {
            m_process->kill();
            m_process->waitForFinished(200);
        }
    }
}

void JournalSource::beginQuery() {
    if (m_stopping) {
        return;
    }

#if defined(Q_OS_MACOS)
    QByteArray data;
    if (fetchUnifiedLogJournalJson(data, 300, 300)) {
        m_stdout.clear();
        m_running = true;
        onFinished(0, QProcess::NormalExit);
        return;
    }
    emitFailure(QStringLiteral("unified log unavailable"));
    return;
#elif defined(Q_OS_WIN)
    QByteArray data;
    if (fetchEventLogJournalJson(data, 600, 300)) {
        m_stdout.clear();
        m_running = true;
        m_stdout += data;
        onFinished(0, QProcess::NormalExit);
        return;
    }
    emitFailure(QStringLiteral("event log unavailable"));
    return;
#else
    const QString path = QStandardPaths::findExecutable(QStringLiteral("journalctl"));
    if (path.isEmpty()) {
        emitFailure(QStringLiteral("journalctl not found"));
        return;
    }

    m_stdout.clear();
    m_running = true;
    m_process->start(path, {QStringLiteral("-o"), QStringLiteral("json"),
                            QStringLiteral("--no-pager"), QStringLiteral("-n"),
                            QStringLiteral("300")});
#endif
}

void JournalSource::onTimeout() {
    beginQuery();
}

void JournalSource::onFinished(int exitCode, QProcess::ExitStatus status) {
    if (!m_running) {
        return;
    }
    m_running = false;
    // macOS and Windows can complete this path from their native log APIs;
    // in that case no child process was started, so QProcess is not an open
    // QIODevice to read from.  Reading it emits Qt's "device not open"
    // warning on every fresh HUD launch.
    QByteArray stderrBytes;
    if (m_process != nullptr && m_process->isOpen()) {
        m_stdout += m_process->readAllStandardOutput();
        stderrBytes = m_process->readAllStandardError();
    }

    JournalSnapshot snapshot;
    snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
    if (status == QProcess::NormalExit && exitCode == 0) {
        QList<JournalEntry> entries;
        parseJournalJson(m_stdout, entries);
        snapshot.available = true;
        snapshot.entries = entries;
        snapshot.errorCount = journalErrorCount(entries);
        snapshot.errorGroups = groupJournalErrors(entries);
    } else {
        snapshot.errorMessage = journalFailureText(
            exitCode, QString::fromUtf8(stderrBytes));
    }

    emit journalReady(snapshot);
    if (m_timer != nullptr) {
        m_timer->start(m_intervalMs);
    }
}

void JournalSource::onErrorOccurred(QProcess::ProcessError error) {
    if (!m_running || error != QProcess::FailedToStart) {
        return;
    }
    m_running = false;
    emitFailure(QStringLiteral("journalctl error"));
}

void JournalSource::emitFailure(const QString &message) {
    JournalSnapshot snapshot;
    snapshot.errorMessage = message;
    snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
    emit journalReady(snapshot);
    if (m_timer != nullptr) {
        m_timer->start(m_intervalMs);
    }
}
