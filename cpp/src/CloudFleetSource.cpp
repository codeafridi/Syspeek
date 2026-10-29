#include "CloudFleetSource.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace {

// The account does not change often; a 10 min listing keeps the $/MO figure
// honest without paying for a CLI call on every tick. Instance age is
// recomputed from the cached launch time each tick, so the AGE tile stays live.
constexpr qint64 kCloudFleetCadenceMs = 600000;
constexpr int kCloudFleetRowCap = 200;

constexpr int kCloudFleetStartMs = 5000;
constexpr int kCloudFleetKillGraceMs = 500;

QString cloudFirstLine(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    return trimmed.section(QLatin1Char('\n'), 0, 0).trimmed();
}

QString cloudFleetExecutable(const QString &cli) {
    QString path = QStandardPaths::findExecutable(cli);
#if defined(Q_OS_WIN)
    if (path.isEmpty()) {
        const char *const extensions[] = {"cmd", "bat", "exe"};
        for (const char *extension : extensions) {
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

QString cloudQuoteArgument(const QString &argument) {
    if (!argument.isEmpty() && !argument.contains(QLatin1Char(' ')) &&
        !argument.contains(QLatin1Char('\t')) &&
        !argument.contains(QLatin1Char('"'))) {
        return argument;
    }
    QString escaped = argument;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

} // namespace

QString cloudFleetCli(CloudProvider provider) {
    switch (provider) {
    case CloudProvider::Aws:
        return QStringLiteral("aws");
    case CloudProvider::Azure:
        return QStringLiteral("az");
    case CloudProvider::Gcp:
        return QStringLiteral("gcloud");
    case CloudProvider::Oci:
        return QStringLiteral("oci");
    case CloudProvider::None:
        break;
    }
    return QString();
}

QStringList cloudFleetArguments(const CloudIdentity &identity) {
    switch (identity.provider) {
    case CloudProvider::Aws: {
        QStringList args;
        args << QStringLiteral("ec2") << QStringLiteral("describe-instances")
             << QStringLiteral("--output") << QStringLiteral("json");
        // The EC2 API is regional and an instance-profile host carries no
        // region in ~/.aws/config, so the region the metadata service just
        // reported is passed explicitly.
        if (!identity.region.isEmpty()) {
            args << QStringLiteral("--region") << identity.region;
        }
        // Projecting to the instances keeps the payload to the fields the
        // table renders; the projected members keep the AWS key names, so the
        // parser handles this and the full document the same way.
        args << QStringLiteral("--query")
             << QStringLiteral("Reservations[].Instances[]");
        return args;
    }
    case CloudProvider::Azure: {
        QStringList args;
        args << QStringLiteral("vm") << QStringLiteral("list")
             << QStringLiteral("-d") << QStringLiteral("-o")
             << QStringLiteral("json");
        if (!identity.accountId.isEmpty()) {
            args << QStringLiteral("--subscription") << identity.accountId;
        }
        return args;
    }
    case CloudProvider::Gcp: {
        QStringList args;
        args << QStringLiteral("compute") << QStringLiteral("instances")
             << QStringLiteral("list") << QStringLiteral("--format=json");
        if (!identity.accountId.isEmpty()) {
            args << QStringLiteral("--project") << identity.accountId;
        }
        return args;
    }
    case CloudProvider::Oci: {
        QStringList args;
        args << QStringLiteral("compute") << QStringLiteral("instance")
             << QStringLiteral("list");
        if (!identity.accountId.isEmpty()) {
            args << QStringLiteral("--compartment-id") << identity.accountId;
        }
        if (!identity.region.isEmpty()) {
            args << QStringLiteral("--region") << identity.region;
        }
        args << QStringLiteral("--all") << QStringLiteral("--output")
             << QStringLiteral("json");
        return args;
    }
    case CloudProvider::None:
        break;
    }
    return QStringList();
}

QString cloudFleetScope(const CloudIdentity &identity) {
    switch (identity.provider) {
    case CloudProvider::Aws:
        return identity.region.isEmpty()
                   ? QStringLiteral("this region (the CLI default)")
                   : identity.region;
    case CloudProvider::Azure:
        return identity.accountId.isEmpty()
                   ? QStringLiteral("the CLI's default subscription")
                   : identity.accountId;
    case CloudProvider::Gcp:
        return identity.accountId.isEmpty()
                   ? QStringLiteral("the CLI's default project")
                   : identity.accountId;
    case CloudProvider::Oci:
        return identity.accountId.isEmpty()
                   ? QStringLiteral("the CLI's default compartment")
                   : identity.accountId;
    case CloudProvider::None:
        break;
    }
    return QString();
}

QString cloudFleetCommandText(const CloudIdentity &identity) {
    const QString cli = cloudFleetCli(identity.provider);
    if (cli.isEmpty()) {
        return QString();
    }
    return cli + QLatin1Char(' ') +
           cloudFleetArguments(identity).join(QLatin1Char(' '));
}

QString cloudWindowsCmdLine(const QString &program,
                            const QStringList &arguments) {
    QString text = QStringLiteral("/s /c \"\"%1\"").arg(program);
    for (const QString &argument : arguments) {
        text += QLatin1Char(' ');
        text += cloudQuoteArgument(argument);
    }
    text += QLatin1Char('"');
    return text;
}

namespace {

CloudFleetStatus cloudFleetFailure(const CloudIdentity &identity,
                                   const CloudFleetOptions &options,
                                   const QString &note, qint64 nowMs) {
    Q_UNUSED(options);
    CloudFleetStatus status;
    status.attempted = true;
    status.provider = identity.provider;
    status.cliName = cloudFleetCli(identity.provider);
    status.scope = cloudFleetScope(identity);
    status.source = cloudFleetCommandText(identity);
    status.note = note;
    status.fetchedAtMs = nowMs;
    return status;
}

// Shared by the blocking and the polled path: the process is started the same
// way, so a Windows .cmd shim and a POSIX script both work.
void cloudStartProcess(QProcess *process, const QString &cliName,
                       const QString &cliPath,
                       const QStringList &arguments) {
    process->setProcessChannelMode(QProcess::SeparateChannels);
    const QString suffix = QFileInfo(cliPath).suffix().toLower();
    if (suffix == QLatin1String("cmd") || suffix == QLatin1String("bat")) {
#if defined(Q_OS_WIN)
        // A .cmd shim is a batch file, not an image CreateProcess can start,
        // so it runs through the command processor with the line assembled for
        // it (no quoting layer left to Qt).
        process->setProgram(QStringLiteral("cmd.exe"));
        process->setNativeArguments(
            cloudWindowsCmdLine(cliPath, arguments));
#else
        process->setProgram(cliPath);
        process->setArguments(arguments);
#endif
    } else {
        process->setProgram(cliPath);
        process->setArguments(arguments);
    }
    Q_UNUSED(cliName);
    process->start();
}

CloudFleetStatus cloudFleetCollect(const CloudIdentity &identity,
                                   CloudFleetStatus status,
                                   const QByteArray &stdoutText,
                                   const QByteArray &stderrText, int exitCode,
                                   bool normalExit, qint64 nowMs) {
    if (!normalExit || exitCode != 0) {
        const QString errors = QString::fromLocal8Bit(stderrText);
        const QString first = cloudFirstLine(errors);
        status.note =
            cloudFleetNeedsCredentials(identity.provider, errors)
                ? fleetCredentialsText(identity.provider)
                : fleetFailureText(status.cliName, exitCode, first);
        status.fetchedAtMs = nowMs;
        return status;
    }
    if (stdoutText.trimmed().isEmpty()) {
        status.note = fleetFailureText(status.cliName, 0,
                                       QStringLiteral("no output"));
        status.fetchedAtMs = nowMs;
        return status;
    }
    QJsonParseError parseError{};
    QJsonDocument::fromJson(stdoutText, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        status.note = fleetFailureText(
            status.cliName, 0,
            QStringLiteral("output is not JSON (%1)")
                .arg(parseError.errorString()));
        status.fetchedAtMs = nowMs;
        return status;
    }

    CloudFleetStatus parsed =
        parseCloudFleetJson(identity.provider, stdoutText);
    parsed.attempted = true;
    parsed.provider = identity.provider;
    parsed.cliName = status.cliName;
    parsed.scope = status.scope;
    parsed.source = status.source;
    parsed.fetchedAtMs = nowMs;
    if (parsed.rows.size() > kCloudFleetRowCap) {
        parsed.dropped = parsed.rows.size() - kCloudFleetRowCap;
        parsed.rows = parsed.rows.mid(0, kCloudFleetRowCap);
        parsed.available = true;
    }
    if (parsed.rows.isEmpty()) {
        parsed.note = fleetEmptyText(parsed);
        parsed.available = false;
        parsed.monthlyUsd = -1.0;
    }
    return parsed;
}

} // namespace

CloudFleetStatus probeCloudFleet(const CloudIdentity &identity,
                                 const CloudFleetOptions &options,
                                 qint64 nowMs) {
    if (nowMs <= 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (!options.enabled) {
        CloudFleetStatus status;
        status.note = fleetDisabledText();
        return status;
    }
    if (identity.provider == CloudProvider::None) {
        CloudFleetStatus status;
        status.note = fleetNoProviderText();
        return status;
    }
    const QString cli = cloudFleetCli(identity.provider);
    const QString cliPath = cloudFleetExecutable(cli);
    if (cliPath.isEmpty()) {
        return cloudFleetFailure(identity, options,
                                 fleetCliMissingText(cli), nowMs);
    }

    QProcess process;
    cloudStartProcess(&process, cli, cliPath, cloudFleetArguments(identity));
    if (!process.waitForStarted(kCloudFleetStartMs)) {
        return cloudFleetFailure(
            identity, options,
            QStringLiteral("fleet: could not start %1 (%2)")
                .arg(cli, process.errorString()),
            nowMs);
    }
    const int budget = qBound(2000, options.timeoutMs, 120000);
    if (!process.waitForFinished(budget)) {
        process.kill();
        process.waitForFinished(kCloudFleetKillGraceMs);
        return cloudFleetFailure(
            identity, options,
            fleetTimeoutText(cli, qMax(1, options.timeoutMs / 1000)),
            nowMs);
    }

    CloudFleetStatus status;
    status.attempted = true;
    status.provider = identity.provider;
    status.cliName = cli;
    status.scope = cloudFleetScope(identity);
    status.source = cloudFleetCommandText(identity);
    status =
        cloudFleetCollect(identity, status, process.readAllStandardOutput(),
                          process.readAllStandardError(), process.exitCode(),
                          process.exitStatus() == QProcess::NormalExit,
                          nowMs);
    fleetResolveAges(status, nowMs);
    return status;
}

CloudFleetRunner::CloudFleetRunner(QObject *parent)
    : QObject(parent) {
}

CloudFleetRunner::~CloudFleetRunner() {
    stop();
}

void CloudFleetRunner::setOptions(const CloudFleetOptions &options) {
    m_options = options;
}

CloudFleetOptions CloudFleetRunner::options() const {
    return m_options;
}

const CloudFleetStatus &CloudFleetRunner::status() const {
    return m_status;
}

void CloudFleetRunner::stop() {
    if (m_process != nullptr) {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(kCloudFleetKillGraceMs)) {
                m_process->kill();
                m_process->waitForFinished(kCloudFleetKillGraceMs);
            }
        }
        cleanup();
    }
    m_running = false;
    m_startedAtMs = 0;
}

void CloudFleetRunner::cleanup() {
    if (m_process != nullptr) {
        // The runner polls the process instead of listening to it, so no
        // signal handler can be on the stack here and a direct delete is both
        // safe and independent of an event loop (--dump, --probe and tests
        // drive the sampler without one).
        delete m_process;
        m_process = nullptr;
    }
    m_running = false;
    m_startedAtMs = 0;
}

void CloudFleetRunner::begin(const CloudIdentity &identity, qint64 nowMs) {
    cleanup();
    m_identity = identity;
    m_lastAttemptMs = nowMs;
    m_status = CloudFleetStatus{};
    m_status.attempted = true;
    m_status.provider = identity.provider;
    m_status.cliName = cloudFleetCli(identity.provider);
    m_status.scope = cloudFleetScope(identity);
    m_status.source = cloudFleetCommandText(identity);
    m_status.note =
        QStringLiteral("fleet: listing %1 (%2) ...")
            .arg(m_status.cliName,
                 m_status.scope.isEmpty() ? QStringLiteral("account")
                                          : m_status.scope);

    const QString cliPath = cloudFleetExecutable(m_status.cliName);
    if (cliPath.isEmpty()) {
        fail(fleetCliMissingText(m_status.cliName), nowMs);
        return;
    }

    auto *process = new QProcess(this);
    cloudStartProcess(process, m_status.cliName, cliPath,
                      cloudFleetArguments(identity));
    m_process = process;
    if (!process->waitForStarted(kCloudFleetStartMs)) {
        const QString error = process->errorString();
        cleanup();
        fail(QStringLiteral("fleet: could not start %1 (%2)")
                 .arg(m_status.cliName, error),
             nowMs);
        return;
    }
    m_running = true;
    m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
}

void CloudFleetRunner::finish(qint64 nowMs) {
    if (m_process == nullptr) {
        m_running = false;
        return;
    }
    const QByteArray out = m_process->readAllStandardOutput();
    const QByteArray err = m_process->readAllStandardError();
    const int exitCode = m_process->exitCode();
    const bool normalExit = m_process->exitStatus() == QProcess::NormalExit;
    cleanup();
    m_status = cloudFleetCollect(m_identity, m_status, out, err, exitCode,
                                 normalExit, nowMs);
    fleetResolveAges(m_status, nowMs);
}

void CloudFleetRunner::fail(const QString &note, qint64 nowMs) {
    m_status.note = note;
    m_status.fetchedAtMs = nowMs;
    m_status.rows.clear();
    m_status.available = false;
    m_status.running = 0;
    m_status.unpriced = 0;
    m_status.dropped = 0;
    m_status.monthlyUsd = -1.0;
    m_status.selfIndex = -1;
}

void CloudFleetRunner::update(const CloudIdentity &identity, qint64 nowMs) {
    if (nowMs <= 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (!m_options.enabled) {
        stop();
        m_status = CloudFleetStatus{};
        m_status.note = fleetDisabledText();
        return;
    }
    if (identity.provider == CloudProvider::None) {
        stop();
        m_status = CloudFleetStatus{};
        m_status.note = fleetNoProviderText();
        return;
    }
    if (identity.provider != m_identity.provider ||
        identity.instanceId != m_identity.instanceId) {
        stop();
        m_status = CloudFleetStatus{};
        m_lastAttemptMs = 0;
    }
    if (m_running) {
        if (m_startedAtMs > 0 &&
            nowMs - m_startedAtMs > qint64(m_options.timeoutMs)) {
            const QString cli = m_status.cliName;
            stop();
            fail(fleetTimeoutText(cli, qMax(1, m_options.timeoutMs / 1000)),
                 nowMs);
            return;
        }
        if (m_process != nullptr && m_process->state() == QProcess::NotRunning) {
            finish(nowMs);
        }
        return;
    }
    if (m_lastAttemptMs > 0 && nowMs - m_lastAttemptMs < kCloudFleetCadenceMs) {
        return;
    }
    begin(identity, nowMs);
}
