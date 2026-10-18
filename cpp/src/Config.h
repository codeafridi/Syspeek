#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

struct HealthConfig {
    double cpuWarnPercent = 70.0;
    double cpuHighPercent = 90.0;
    qulonglong rssWarnKb = 1024ULL * 1024ULL;
    int sustainedSamples = 3;
};

struct DockerConfig {
    int healthyMs = 20000;
    int issueMs = 8000;
    int backoffStartMs = 15000;
    int backoffCapMs = 240000;
    int absentMs = 300000;
    int timeoutMs = 3000;
    int killGraceMs = 500;
};

struct KubeConfig {
    int healthyMs = 30000;
    int issueMs = 10000;
    int backoffStartMs = 15000;
    int backoffCapMs = 240000;
    int absentMs = 300000;
    int timeoutMs = 5000;
    int killGraceMs = 500;
};

struct SyntheticConfig {
    QStringList targets;
    int intervalMs = 30000;
    int timeoutMs = 5000;
};

struct CostConfig {
    double cpuPerCoreHour = 0.031611;
    double memPerGbHour = 0.004237;
};

struct TraceConfig {
    bool enabled = true;
    int port = 4318;
    int maxSpans = 5000;
    int emitIntervalMs = 1000;
};

struct PathConfig {
    bool enabled = true;
    QStringList targets;
    int intervalMs = 60000;
    int cycles = 10;
};

struct StatsdConfig {
    bool enabled = true;
    int port = 8125;
    int emitIntervalMs = 1000;
};

struct SloConfig {
    QString name = QStringLiteral("focused-app availability");
    double objective = 0.999;
    double windowHours = 720.0;
};

struct DbmConfig {
    bool enabled = false;
    QString host = QStringLiteral("127.0.0.1");
    int port = 5432;
    QString user = QStringLiteral("postgres");
    QString database = QStringLiteral("postgres");
    int intervalMs = 5000;
};

struct TlsConfig {
    bool enabled = false;
    QStringList targets;
    int intervalMs = 300000;
};

struct FimConfig {
    bool enabled = true;
#if defined(Q_OS_MACOS)
    QStringList paths = {
        QStringLiteral("/etc/passwd"), QStringLiteral("/etc/group"),
        QStringLiteral("/etc/hosts"),  QStringLiteral("/etc/resolv.conf"),
        QStringLiteral("/etc/cron.d"), QStringLiteral("/etc/pam.d"),
        QStringLiteral("/etc/ssh"),    QStringLiteral("/Library/LaunchDaemons"),
        QStringLiteral("/usr/local/etc"), QStringLiteral("/opt/homebrew/etc")};
#elif defined(Q_OS_WIN)
    QStringList paths = {
        qEnvironmentVariable("SystemRoot")
            + QStringLiteral("\\System32\\drivers\\etc\\hosts"),
        qEnvironmentVariable("SystemRoot")
            + QStringLiteral("\\System32\\drivers\\etc\\services"),
        qEnvironmentVariable("SystemRoot")
            + QStringLiteral("\\System32\\Tasks"),
        qEnvironmentVariable("APPDATA")
            + QStringLiteral(
                "\\Microsoft\\Windows\\Start Menu\\Programs\\Startup")};
#else
    QStringList paths = {
        QStringLiteral("/etc/passwd"), QStringLiteral("/etc/group"),
        QStringLiteral("/etc/hosts"),  QStringLiteral("/etc/hostname"),
        QStringLiteral("/etc/resolv.conf"), QStringLiteral("/etc/cron.d"),
        QStringLiteral("/etc/pam.d"),  QStringLiteral("/etc/ssh")};
#endif
    int intervalMs = 30000;
    QString algorithm = QStringLiteral("sha256");
    int maxFileSizeKb = 4096;
    bool watch = true;
};

struct Config {
    HealthConfig health;
    DockerConfig docker;
    KubeConfig kube;
    SyntheticConfig synthetic;
    CostConfig cost;
    TraceConfig traces;
    PathConfig path;
    StatsdConfig dogstatsd;
    SloConfig slo;
    DbmConfig database;
    TlsConfig tls;
    FimConfig fim;
    int focusCacheTtlMs = 1500;
    int sampleIntervalMs = 1000;
};

Config defaultConfig();

Config loadConfig(const QString &path, QString *errorMessage);

QString defaultConfigPath();
