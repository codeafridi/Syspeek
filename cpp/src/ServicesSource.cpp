#include "ServicesSource.h"

#include "platform/PlatformFactory.h"

#include <QDateTime>
#include <QStandardPaths>
#include <QTimer>

#if defined(Q_OS_MACOS)
#include <unistd.h>
#endif

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

void ServicesSource::restartUnit(const QString &unit) {
    const QString target = unit.trimmed();
    if (target.isEmpty()) {
        emit unitActionFinished(QStringLiteral("restart"), unit, false,
                                QStringLiteral("no unit selected"));
        return;
    }

    QString error;
    bool ok = false;

#if defined(Q_OS_MACOS)
    const QString launchctl =
        QStandardPaths::findExecutable(QStringLiteral("launchctl"));
    if (launchctl.isEmpty()) {
        error = QStringLiteral("launchctl not found");
    } else {
        QProcess process;
        process.start(launchctl,
                      {QStringLiteral("kickstart"), QStringLiteral("-k"),
                       QStringLiteral("gui/%1/%2").arg(::getuid()).arg(target)});
        if (!process.waitForFinished(15000)) {
            process.kill();
            process.waitForFinished(200);
            error = QStringLiteral("launchctl kickstart timed out");
        } else if (process.exitStatus() != QProcess::NormalExit
                   || process.exitCode() != 0) {
            error = QString::fromLocal8Bit(process.readAllStandardError())
                        .trimmed()
                        .left(160);
            if (error.isEmpty()) {
                error = QStringLiteral("launchctl kickstart failed");
            }
        } else {
            ok = true;
        }
    }
#elif defined(Q_OS_WIN)
    ok = systemBackend().restartService(target, &error);
#else
    const QString systemctl =
        QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (systemctl.isEmpty()) {
        error = QStringLiteral("systemctl not found");
    } else {
        QProcess process;
        process.start(systemctl, {QStringLiteral("restart"), target});
        if (!process.waitForFinished(15000)) {
            process.kill();
            process.waitForFinished(200);
            error = QStringLiteral("systemctl restart timed out");
        } else if (process.exitStatus() != QProcess::NormalExit
                   || process.exitCode() != 0) {
            error = QString::fromLocal8Bit(process.readAllStandardError())
                        .trimmed()
                        .left(160);
            if (error.isEmpty()) {
                error = QStringLiteral("systemctl restart failed");
            }
        } else {
            ok = true;
        }
    }
#endif

    emit unitActionFinished(QStringLiteral("restart"), target, ok, error);
    if (m_timer != nullptr) {
        m_timer->stop();
    }
    QTimer::singleShot(0, this, &ServicesSource::onTimeout);
}
