#include "CloudSpendSource.h"

#include "CloudFleetSource.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

namespace {

// Billing data moves slowly (Cost Explorer lags by up to a day) and every CE
// request is billable, so the account is asked about four times a day.
constexpr qint64 kCloudSpendCadenceMs = 6 * 60 * 60 * 1000;
constexpr int kCloudSpendStartMs = 5000;
constexpr int kCloudSpendKillGraceMs = 500;

QString firstLine(const QString &text) {
    const QString trimmed = text.trimmed();
    return trimmed.isEmpty() ? QString()
                             : trimmed.section(QLatin1Char('\n'), 0, 0).trimmed();
}

QString cliExecutable(const QString &cli) {
    QString path = QStandardPaths::findExecutable(cli);
#if defined(Q_OS_WIN)
    if (path.isEmpty()) {
        for (const char *extension : {"cmd", "bat", "exe"}) {
            path = QStandardPaths::findExecutable(
                cli + QLatin1Char('.') + QLatin1String(extension));
            if (!path.isEmpty()) {
                break;
            }
        }
    }
#endif
    return path;
}

void startProcess(QProcess *process, const QString &cliPath,
                  const QStringList &arguments) {
    process->setProcessChannelMode(QProcess::SeparateChannels);
    const QString suffix = QFileInfo(cliPath).suffix().toLower();
    if (suffix == QLatin1String("cmd") || suffix == QLatin1String("bat")) {
#if defined(Q_OS_WIN)
        process->setProgram(QStringLiteral("cmd.exe"));
        // `cmd /s /c ""<cli>" <args>"` — the same shape the inventory runner
        // uses, so a Windows .cmd shim starts the same way.
        QString line = QStringLiteral("/s /c \"\"%1\"").arg(cliPath);
        for (const QString &argument : arguments) {
            line += QLatin1Char(' ');
            if (argument.contains(QLatin1Char(' '))) {
                line += QLatin1Char('"') + argument + QLatin1Char('"');
            } else {
                line += argument;
            }
        }
        line += QLatin1Char('"');
        process->setNativeArguments(line);
#else
        process->setProgram(cliPath);
        process->setArguments(arguments);
#endif
    } else {
        process->setProgram(cliPath);
        process->setArguments(arguments);
    }
    process->start();
}

CloudSpendStatus failure(const CloudIdentity &identity,
                         const QString &note, qint64 nowMs) {
    CloudSpendStatus status;
    status.attempted = true;
    status.provider = identity.provider;
    status.cliName = cloudFleetCli(identity.provider);
    status.scope = cloudSpendScope(identity);
    status.source = cloudSpendCommandText(identity);
    status.note = note;
    status.fetchedAtMs = nowMs;
    return status;
}

} // namespace

int cloudSpendStepCount(const CloudIdentity &identity) {
    return identity.provider == CloudProvider::Aws ? 2 : 1;
}

QStringList cloudSpendArguments(const CloudIdentity &identity, int step,
                                qint64 nowMs) {
    const QString start = cloudSpendPeriodStart(nowMs);
    const QString end = cloudSpendPeriodEnd(nowMs);
    switch (identity.provider) {
    case CloudProvider::Aws: {
        if (step <= 0) {
            QStringList args;
            args << QStringLiteral("ce") << QStringLiteral("get-cost-and-usage")
                 << QStringLiteral("--time-period")
                 << QStringLiteral("Start=%1,End=%2").arg(start, end)
                 << QStringLiteral("--granularity") << QStringLiteral("DAILY")
                 << QStringLiteral("--metrics") << QStringLiteral("UnblendedCost")
                 << QStringLiteral("--group-by")
                 << QStringLiteral("Type=DIMENSION,Key=SERVICE")
                 << QStringLiteral("--output") << QStringLiteral("json");
            return args;
        }
        QStringList args;
        args << QStringLiteral("ce") << QStringLiteral("get-cost-forecast")
             << QStringLiteral("--time-period")
             << QStringLiteral("Start=%1,End=%2")
                    .arg(cloudSpendPeriodEnd(nowMs).isEmpty()
                             ? QStringLiteral("--")
                             : cloudSpendPeriodEnd(nowMs),
                         cloudSpendForecastEnd(nowMs))
             << QStringLiteral("--metric") << QStringLiteral("UNBLENDED_COST")
             << QStringLiteral("--granularity") << QStringLiteral("MONTHLY")
             << QStringLiteral("--output") << QStringLiteral("json");
        return args;
    }
    case CloudProvider::Azure: {
        QStringList args;
        args << QStringLiteral("consumption") << QStringLiteral("usage")
             << QStringLiteral("list") << QStringLiteral("--top")
             << QStringLiteral("1000") << QStringLiteral("--filter")
             << QStringLiteral("properties/usageStart ge '%1'").arg(start);
        if (!identity.accountId.isEmpty()) {
            args << QStringLiteral("--subscription") << identity.accountId;
        }
        args << QStringLiteral("-o") << QStringLiteral("json");
        return args;
    }
    case CloudProvider::Gcp:
    case CloudProvider::Oci:
    case CloudProvider::None:
        break;
    }
    return QStringList();
}

QString cloudSpendCommandText(const CloudIdentity &identity) {
    const QString cli = cloudFleetCli(identity.provider);
    if (cli.isEmpty()) {
        return QString();
    }
    const QStringList args = cloudSpendArguments(
        identity, 0, QDateTime::currentMSecsSinceEpoch());
    if (args.isEmpty()) {
        return QString();
    }
    return cli + QLatin1Char(' ') + args.join(QLatin1Char(' '));
}

QString cloudSpendScope(const CloudIdentity &identity) {
    switch (identity.provider) {
    case CloudProvider::Aws:
        return identity.accountId.isEmpty()
                   ? QStringLiteral("the CLI's account")
                   : QStringLiteral("account %1").arg(identity.accountId);
    case CloudProvider::Azure:
        return identity.accountId.isEmpty()
                   ? QStringLiteral("the CLI's default subscription")
                   : identity.accountId;
    case CloudProvider::Gcp:
    case CloudProvider::Oci:
    case CloudProvider::None:
        break;
    }
    return QString();
}

CloudSpendStatus probeCloudSpend(const CloudIdentity &identity,
                                 const CloudSpendOptions &options,
                                 qint64 nowMs) {
    if (nowMs <= 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (!options.enabled) {
        CloudSpendStatus status;
        status.note = cloudSpendDisabledText();
        return status;
    }
    if (identity.provider == CloudProvider::None) {
        CloudSpendStatus status;
        status.note = cloudSpendNoProviderText();
        return status;
    }
    if (identity.provider == CloudProvider::Gcp
        || identity.provider == CloudProvider::Oci) {
        CloudSpendStatus status;
        // attempted stays false: nothing was spawned, so nothing was billed.
        status.provider = identity.provider;
        status.note = cloudSpendGateText(identity.provider);
        return status;
    }

    const QString cli = cloudFleetCli(identity.provider);
    const QString cliPath = cliExecutable(cli);
    if (cliPath.isEmpty()) {
        return failure(identity, cloudSpendCliMissingText(cli), nowMs);
    }

    CloudSpendStatus cost;
    bool haveCost = false;
    double forecastRemaining = -1.0;
    QString forecastError;

    const int steps = cloudSpendStepCount(identity);
    for (int step = 0; step < steps; ++step) {
        QProcess process;
        startProcess(&process, cliPath,
                     cloudSpendArguments(identity, step, nowMs));
        if (!process.waitForStarted(kCloudSpendStartMs)) {
            return failure(identity,
                           QStringLiteral("billed spend: could not start %1 (%2)")
                               .arg(cli, process.errorString()),
                           nowMs);
        }
        const int budget = qBound(5000, options.timeoutMs, 180000);
        if (!process.waitForFinished(budget)) {
            process.kill();
            process.waitForFinished(kCloudSpendKillGraceMs);
            CloudSpendStatus status = failure(
                identity,
                cloudSpendTimeoutText(cli, qMax(1, options.timeoutMs / 1000)),
                nowMs);
            if (haveCost) {
                status = cost;
                status.note =
                    cloudSpendTimeoutText(cli, qMax(1, options.timeoutMs / 1000));
            }
            return status;
        }
        const QByteArray out = process.readAllStandardOutput();
        const QString errors =
            QString::fromLocal8Bit(process.readAllStandardError());
        const bool normalExit = process.exitStatus() == QProcess::NormalExit;
        if (!normalExit || process.exitCode() != 0) {
            CloudSpendStatus status = failure(
                identity,
                cloudFleetNeedsCredentials(identity.provider, errors)
                    ? fleetCredentialsText(identity.provider)
                    : cloudSpendFailureText(cli, process.exitCode(),
                                            firstLine(errors)),
                nowMs);
            if (haveCost) {
                status = cost;
                status.note = cloudSpendFailureText(cli, process.exitCode(),
                                                    firstLine(errors));
            }
            return status;
        }
        if (step == 0) {
            QString error;
            cost = identity.provider == CloudProvider::Aws
                       ? parseAwsCostAndUsage(out, &error)
                       : parseAzureUsageList(out, &error);
            if (!cost.available) {
                return failure(identity,
                               QStringLiteral("billed spend: %1").arg(error),
                               nowMs);
            }
            haveCost = true;
        } else {
            QString error;
            const CloudSpendStatus forecast = parseAwsCostForecast(out, &error);
            if (forecast.available) {
                forecastRemaining = forecast.forecastRemainingUsd;
            } else {
                forecastError = error;
            }
        }
    }

    cost.attempted = true;
    cost.provider = identity.provider;
    cost.cliName = cli;
    cost.scope = cloudSpendScope(identity);
    cost.source = cloudSpendCommandText(identity);
    cost.fetchedAtMs = nowMs;
    if (forecastRemaining >= 0.0) {
        spendApplyForecast(&cost, forecastRemaining);
    } else if (!forecastError.isEmpty()) {
        cost.note = QStringLiteral("billed spend: %1").arg(forecastError);
    }
    return cost;
}

CloudSpendRunner::CloudSpendRunner(QObject *parent)
    : QObject(parent) {
}

CloudSpendRunner::~CloudSpendRunner() {
    stop();
}

void CloudSpendRunner::setOptions(const CloudSpendOptions &options) {
    m_options = options;
}

CloudSpendOptions CloudSpendRunner::options() const {
    return m_options;
}

const CloudSpendStatus &CloudSpendRunner::status() const {
    return m_status;
}

void CloudSpendRunner::cleanup() {
    if (m_process != nullptr) {
        // Polled, not signal-driven: no handler can be on the stack here, so a
        // direct delete is safe without an event loop (--dump, tests).
        delete m_process;
        m_process = nullptr;
    }
    m_running = false;
    m_startedAtMs = 0;
}

void CloudSpendRunner::stop() {
    if (m_process != nullptr
        && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(kCloudSpendKillGraceMs)) {
            m_process->kill();
            m_process->waitForFinished(kCloudSpendKillGraceMs);
        }
    }
    cleanup();
    m_step = 0;
}

void CloudSpendRunner::begin(const CloudIdentity &identity, qint64 nowMs) {
    cleanup();
    m_identity = identity;
    m_lastAttemptMs = nowMs;
    m_step = 0;
    m_status = CloudSpendStatus{};
    m_status.attempted = true;
    m_status.provider = identity.provider;
    m_status.cliName = cloudFleetCli(identity.provider);
    m_status.scope = cloudSpendScope(identity);
    m_status.source = cloudSpendCommandText(identity);
    m_status.note = QStringLiteral("billed spend: asking %1 for the month to date ...")
                        .arg(m_status.cliName);

    const QString cliPath = cliExecutable(m_status.cliName);
    if (cliPath.isEmpty()) {
        fail(cloudSpendCliMissingText(m_status.cliName), nowMs);
        return;
    }
    auto *process = new QProcess(this);
    startProcess(process, cliPath, cloudSpendArguments(identity, m_step, nowMs));
    m_process = process;
    if (!process->waitForStarted(kCloudSpendStartMs)) {
        const QString error = process->errorString();
        cleanup();
        fail(QStringLiteral("billed spend: could not start %1 (%2)")
                 .arg(m_status.cliName, error),
             nowMs);
        return;
    }
    m_running = true;
    m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
}

void CloudSpendRunner::advance(qint64 nowMs) {
    m_step += 1;
    if (m_step >= cloudSpendStepCount(m_identity)) {
        finish(nowMs);
        return;
    }
    const QString cliPath = cliExecutable(m_status.cliName);
    auto *process = new QProcess(this);
    startProcess(process, cliPath, cloudSpendArguments(m_identity, m_step, nowMs));
    m_process = process;
    if (!process->waitForStarted(kCloudSpendStartMs)) {
        const QString error = process->errorString();
        cleanup();
        fail(QStringLiteral("billed spend: could not start %1 (%2)")
                 .arg(m_status.cliName, error),
             nowMs);
        return;
    }
    m_running = true;
    m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
}

void CloudSpendRunner::collectStep(qint64 nowMs) {
    const QByteArray out = m_process->readAllStandardOutput();
    const QString errors =
        QString::fromLocal8Bit(m_process->readAllStandardError());
    const int exitCode = m_process->exitCode();
    const bool normalExit = m_process->exitStatus() == QProcess::NormalExit;
    cleanup();

    if (!normalExit || exitCode != 0) {
        fail(cloudFleetNeedsCredentials(m_identity.provider, errors)
                 ? fleetCredentialsText(m_identity.provider)
                 : cloudSpendFailureText(m_status.cliName, exitCode,
                                         firstLine(errors)),
             nowMs);
        return;
    }
    if (m_step == 0) {
        QString error;
        CloudSpendStatus parsed =
            m_identity.provider == CloudProvider::Aws
                ? parseAwsCostAndUsage(out, &error)
                : parseAzureUsageList(out, &error);
        if (!parsed.available) {
            fail(QStringLiteral("billed spend: %1").arg(error), nowMs);
            return;
        }
        const QString note = m_status.note;
        m_status = parsed;
        m_status.attempted = true;
        m_status.provider = m_identity.provider;
        m_status.cliName = cloudFleetCli(m_identity.provider);
        m_status.scope = cloudSpendScope(m_identity);
        m_status.source = cloudSpendCommandText(m_identity);
        m_status.note = note;
        advance(nowMs);
        return;
    }
    QString error;
    const CloudSpendStatus forecast = parseAwsCostForecast(out, &error);
    if (forecast.available) {
        spendApplyForecast(&m_status, forecast.forecastRemainingUsd);
    } else {
        m_status.note = QStringLiteral("billed spend: %1").arg(error);
    }
    finish(nowMs);
}

void CloudSpendRunner::finish(qint64 nowMs) {
    m_status.fetchedAtMs = nowMs;
    if (m_status.available && m_status.note.startsWith(
            QStringLiteral("billed spend: asking"))) {
        m_status.note.clear();
    }
    m_step = 0;
}

void CloudSpendRunner::fail(const QString &note, qint64 nowMs) {
    m_status.note = note;
    m_status.fetchedAtMs = nowMs;
    m_status.available = false;
    m_status.services.clear();
    m_status.days.clear();
    m_status.monthToDateUsd = -1.0;
    m_status.forecastUsd = -1.0;
    m_status.forecastRemainingUsd = -1.0;
    m_step = 0;
}

void CloudSpendRunner::update(const CloudIdentity &identity, qint64 nowMs) {
    if (nowMs <= 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (!m_options.enabled) {
        stop();
        m_status = CloudSpendStatus{};
        m_status.note = cloudSpendDisabledText();
        return;
    }
    if (identity.provider == CloudProvider::None) {
        stop();
        m_status = CloudSpendStatus{};
        m_status.note = cloudSpendNoProviderText();
        return;
    }
    if (identity.provider == CloudProvider::Gcp
        || identity.provider == CloudProvider::Oci) {
        stop();
        m_status = CloudSpendStatus{};
        m_status.provider = identity.provider;
        m_status.note = cloudSpendGateText(identity.provider);
        return;
    }
    if (identity.provider != m_identity.provider
        || identity.instanceId != m_identity.instanceId) {
        stop();
        m_status = CloudSpendStatus{};
        m_lastAttemptMs = 0;
    }
    if (m_running) {
        if (m_startedAtMs > 0
            && nowMs - m_startedAtMs > qint64(m_options.timeoutMs)) {
            stop();
            fail(cloudSpendTimeoutText(m_status.cliName,
                                       qMax(1, m_options.timeoutMs / 1000)),
                 nowMs);
            return;
        }
        if (m_process != nullptr
            && m_process->state() == QProcess::NotRunning) {
            collectStep(nowMs);
        }
        return;
    }
    if (m_lastAttemptMs > 0 && nowMs - m_lastAttemptMs < kCloudSpendCadenceMs) {
        return;
    }
    begin(identity, nowMs);
}
