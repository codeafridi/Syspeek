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
    // [k8s] context: the kubeconfig context every kubectl call targets. Empty
    // means kubectl's own current-context.
    QString context;
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

// M86: the event stream. `events` is the ring size, `windowSeconds` the
// radius of the "why did this spike" view a chart tick opens, and `markers`
// whether charts draw the ticks at all.
struct TimelineConfig {
    bool enabled = true;
    int events = 2000;
    int windowSeconds = 90;
    bool markers = true;
};

struct MetricsConfig {
    bool enabled = true;
    int port = 9107;
    QString bindAddress = QStringLiteral("127.0.0.1");
    QString token;
};

struct FleetConfig {
    bool enabled = true;
    bool share = true;
    QString name;
    QStringList peers;
    QString token;
    int pollMs = 10000;
    int timeoutMs = 2000;
};

struct RemoteOptions {
    // [remote] enabled: off by default. Turning it on is the whole upgrade -
    // there is no licence check, and the local console never depends on it.
    bool enabled = false;
    // [remote] url: the relay base. A bare host means https (hosted relay); a
    // self-hosted relay states http:// explicitly.
    QString url;
    // [remote] token: the per-machine ingest token from the relay. Empty means
    // the relay will refuse the report, which is why the note says so.
    QString token;
    // [remote] name: what the relay calls this machine. Empty means the
    // machine id is shown instead.
    QString name;
    // [remote] machine_id: derived once by M100 and kept, never regenerated on
    // a restart.
    QString machineId;
    int intervalMs = 15000;
    int timeoutMs = 3000;
    // [remote] ca_file: a private CA for a self-hosted relay. Verification is
    // never turned off; naming the CA is the way to trust a certificate the
    // system store does not know.
    QString caFile;
    // [remote] spool_mb: bounded offline buffer. 0 disables the spool (a
    // dropped report stays dropped, and the count is stated).
    int spoolMb = 8;
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
    // [overlay] opacity: how solid the COLLAPSED strip is, 40..100. The
    // expanded console is always opaque - it is a data page, and a see-through
    // table is a readability bug, not a style.
    int opacity = 100;
    // [overlay] wallpaper: a curated backdrop id (none | graphite | slate |
    // moss | ember | dusk). Not a file path on purpose - a photograph cannot
    // promise that white 9 px text stays readable over it, and the attempt at
    // allowing one ended with the PID and CPU numbers camouflaged.
    QString wallpaper = QStringLiteral("none");
};

struct CloudConfig {
    // [cloud] enabled: the only network read Crowsnest makes on its own is the
    // link-local instance metadata service (169.254.169.254 / 169.254.0.23),
    // which is never routed off the host. Turn it off on a box where even that
    // is unwanted.
    bool enabled = true;
    // [cloud] rates: the price table to read. Empty means
    // defaultCloudRatePath().
    QString ratesPath;
    // [cloud] timeout_ms: the budget for the whole metadata sweep (GCP, OCI,
    // Azure, AWS together). A host with no IMDS blackholes the link-local
    // address instead of refusing it, so one budget per request would stall a
    // sampler tick for seconds; IMDS answers in single digits of milliseconds.
    int timeoutMs = 450;
    // [cloud] inventory: list the account through the provider's CLI so the
    // page can show the instance age and the fleet. Off means no CLI is ever
    // spawned (the metadata read still works).
    bool inventory = true;
    // [cloud] inventory_timeout_ms: one listing budget. The CLI call is a
    // network round trip to the provider API, not link-local.
    int inventoryTimeoutMs = 25000;
    // [cloud] spend: read the account's BILLED month-to-date cost from the
    // provider's cost API (AWS Cost Explorer, Azure consumption). Cost Explorer
    // requests are themselves billable and the data lags, so this is a
    // separate switch from the link-local read and the inventory listing.
    bool spend = true;
    // [cloud] spend_timeout_ms: budget per cost query.
    int spendTimeoutMs = 30000;
    // [cloud] terraform_state: the file `terraform show -json` wrote (or a raw
    // .tfstate). Empty means IaC drift is off and nothing is read.
    QString terraformStatePath;
    // [cloud] identity=<provider>/<region>[/<account>]: used ONLY when the
    // instance metadata service answers nothing (IMDS blocked by network
    // policy, or a local emulator). A real IMDS answer always wins.
    QString identityOverride;
};

struct ThemeConfig {
    // [theme] accent: the one colour the user may change. Anything that is not
    // a #rrggbb literal keeps the built-in classic blue.
    QString accent = QStringLiteral("#4a86c4");
};

struct Config {
    HealthConfig health;
    QList<AlertRule> alertRules;
    ThemeConfig theme;
    DockerConfig docker;
    KubeConfig kube;
    SyntheticConfig synthetic;
    CostConfig cost;
    TraceConfig traces;
    PathConfig path;
    StatsdConfig dogstatsd;
    MetricsConfig metrics;
    FleetConfig fleet;
    RemoteOptions remote;
    HistoryConfig history;
    TimelineConfig timeline;
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
void saveThemeAccent(const QString &path, const QString &accent);

QString defaultConfigPath();

QString defaultCloudRatePath();
