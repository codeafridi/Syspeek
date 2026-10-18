#include "ServicesSource.h"

#include "platform/PlatformFactory.h"

#include <QDateTime>
#include <QStandardPaths>
#include <QTimer>

namespace {

enum class QueryKind { Systemctl, Launchctl };

QueryKind queryKind() {
#if defined(Q_OS_MACOS)
    return QueryKind::Launchctl;
#else
    return QueryKind::Systemctl;
#endif
}

} // namespace

ServicesSource::ServicesSource(int intervalMs, QObject *parent)
    : QObject(parent)
    , m_intervalMs(intervalMs > 1000 ? intervalMs : 30000) {
}

void ServicesSource::start() {
    if (m_process == nullptr) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this,
                [this]() { m_stdout += m_process->readAllStandardOutput(); });
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &ServicesSource::onFinished);
        connect(m_process, &QProcess::errorOccurred, this,
                &ServicesSource::onErrorOccurred);
    }
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, &ServicesSource::onTimeout);
    }
    beginQuery();
}

void ServicesSource::stop() {
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

void ServicesSource::beginQuery() {
    if (m_stopping) {
        return;
    }

#if defined(Q_OS_MACOS)
    m_stdout.clear();
    m_running = true;
    m_process->start(QStringLiteral("/bin/launchctl"),
                     QStringList{QStringLiteral("list")});
    return;
#elif defined(Q_OS_WIN)
    const QList<ServiceRecord> records =
        systemBackend().readServices();
    QList<UnitRow> units;
    if (records.isEmpty() || !serviceRecordsToUnits(records, units)) {
        emitFailure(QStringLiteral("service control manager failed"));
        return;
    }
    onUnits(units);
    if (m_timer != nullptr) {
        m_timer->start(m_intervalMs);
    }
#else
    const QString path = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (path.isEmpty()) {
        emitFailure(QStringLiteral("systemctl not found"));
        return;
    }

    m_stdout.clear();
    m_running = true;
    m_process->start(path, {QStringLiteral("list-units"), QStringLiteral("--type=service"),
                            QStringLiteral("--all"), QStringLiteral("--no-pager"),
                            QStringLiteral("--no-legend"), QStringLiteral("--plain")});
#endif
}

void ServicesSource::onTimeout() {
    beginQuery();
}

void ServicesSource::onFinished(int exitCode, QProcess::ExitStatus status) {
    if (!m_running) {
        return;
    }
    m_running = false;
    m_stdout += m_process->readAllStandardOutput();

    if (status == QProcess::NormalExit && exitCode == 0) {
        QList<UnitRow> units;
        if (queryKind() == QueryKind::Launchctl) {
            parseLaunchctlList(m_stdout, units);
        } else {
            parseSystemctlUnits(m_stdout, units);
        }
        onUnits(units);
        if (m_timer != nullptr) {
            m_timer->start(m_intervalMs);
        }
    } else {
        emitFailure(queryKind() == QueryKind::Launchctl
                        ? QStringLiteral("launchctl failed")
                        : QStringLiteral("systemctl failed"));
    }
}

void ServicesSource::onUnits(QList<UnitRow> units) {
    ServicesSnapshot snapshot;
    snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
    snapshot.available = true;
    snapshot.units = units;
    snapshot.runningCount = servicesRunningCount(units);
    snapshot.failedCount = servicesFailedCount(units);

    if (m_havePrev) {
        const QList<ServiceChange> changes =
            diffServices(m_prevUnits, units, snapshot.sampledAtMs);
        for (const ServiceChange &change : changes) {
            m_timeline.prepend(change);
        }
        while (m_timeline.size() > 200) {
            m_timeline.removeLast();
        }
    }
    snapshot.changes = m_timeline;
    m_prevUnits = units;
    m_havePrev = true;

    emit servicesReady(snapshot);
}

void ServicesSource::onErrorOccurred(QProcess::ProcessError error) {
    if (!m_running || error != QProcess::FailedToStart) {
        return;
    }
    m_running = false;
    emitFailure(QStringLiteral("systemctl error"));
}

void ServicesSource::emitFailure(const QString &message) {
    ServicesSnapshot snapshot;
    snapshot.errorMessage = message;
    snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
    emit servicesReady(snapshot);
    if (m_timer != nullptr) {
        m_timer->start(m_intervalMs);
    }
}
