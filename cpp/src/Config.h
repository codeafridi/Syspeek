#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include "AlertEngine.h"
#include "AlertSilence.h"

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
    QString socketPath;
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

struct HistoryConfig {
    bool enabled = true;
    int samples = 3600;
};

struct MetricsConfig {
    bool enabled = true;
    int port = 9107;
    QString bindAddress = QStringLiteral("127.0.0.1");
    QString token;
};

struct WebhookConfig {
    bool enabled = false;
    QString url;
    QString format = QStringLiteral("generic");
    int timeoutMs = 5000;
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

struct SilencesConfig {
    // [silences]: <matcher>=<duration> for a rolling silence authored in the
    // config, or <matcher>_until=<epoch ms> + <matcher>_comment=... for one
    // the HUD authored (absolute bounds survive a restart).
    QList<SilenceRule> rules;
};

struct ActionsConfig {
    // [actions] enabled: the master switch for every M78 verb (process
    // signals, renice/ionice/oom_score_adj, unit restart, container
    // lifecycle). Off means the console reports the refusal, nothing runs.
    bool enabled = true;
};

struct OverlayConfig {
    // [overlay] click_through: the HUD passes every click to the window
    // below and keeps only its CLICK control live, so the mode can be
    // turned back off without a terminal.
    bool clickThrough = false;
    // [overlay] sticky: ask the X11 window manager to keep the HUD on every
    // desktop, so switching workspaces never loses it.
    bool sticky = true;
    // [overlay] x/y/width/height: the last position and size the user gave
    // the HUD, restored on the next launch when a screen still holds it.
    int x = -1;
    int y = -1;
    int width = -1;
    int height = -1;
};

struct CloudConfig {
    // [cloud] enabled: the only network read Syspeek makes on its own is the
    // link-local instance metadata service (169.254.169.254 / 169.254.0.23),
    // which is never routed off the host. Turn it off on a box where even that
    // is unwanted.
    bool enabled = true;
    // [cloud] rates: the price table to read. Empty means
    // defaultCloudRatePath().
    QString ratesPath;
    // [cloud] timeout_ms: one metadata request budget. IMDS answers in single
    // digits of milliseconds; a slow answer means it is not there.
    int timeoutMs = 450;
    // [cloud] inventory: list the account through the provider's CLI so the
    // page can show the instance age and the fleet. Off means no CLI is ever
    // spawned (the metadata read still works).
    bool inventory = true;
    // [cloud] inventory_timeout_ms: one listing budget. The CLI call is a
    // network round trip to the provider API, not link-local.
    int inventoryTimeoutMs = 25000;
};

struct Config {
    HealthConfig health;
    QList<AlertRule> alertRules;
    DockerConfig docker;
    KubeConfig kube;
    SyntheticConfig synthetic;
    CostConfig cost;
    TraceConfig traces;
    PathConfig path;
    StatsdConfig dogstatsd;
    MetricsConfig metrics;
    HistoryConfig history;
    SilencesConfig silences;
    ActionsConfig actions;
    OverlayConfig overlay;
    CloudConfig cloud;
    WebhookConfig webhook;
    SloConfig slo;
    DbmConfig database;
    TlsConfig tls;
    FimConfig fim;
    int focusCacheTtlMs = 1500;
    int sampleIntervalMs = 1000;
};

Config defaultConfig();

Config loadConfig(const QString &path, QString *errorMessage);

void saveAlertOverrides(const QString &path,
                        const QList<AlertRule> &rules);

void saveSilences(const QString &path, const QList<SilenceRule> &rules);

void saveOverlayConfig(const QString &path, const OverlayConfig &overlay);

QString defaultConfigPath();

QString defaultCloudRatePath();
