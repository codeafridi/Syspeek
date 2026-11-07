#include "Config.h"

#include "RemoteStatus.h"
#include "WallpaperArt.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QtGlobal>

#include <algorithm>

namespace {

// #rrggbb only. Config is a data module and does not link the theme, so the
// check lives here; Theme.h validates the same shape when the UI sets one.
bool looksLikeAccent(const QString &hex) {
    if (hex.size() != 7 || hex.at(0) != QLatin1Char('#')) {
        return false;
    }
    for (int i = 1; i < 7; ++i) {
        const QChar ch = hex.at(i);
        if (!ch.isDigit() && !(ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
            && !(ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))) {
            return false;
        }
    }
    return true;
}

} // namespace

namespace {

double clampDouble(double value, double low, double high) {
    return std::clamp(value, low, high);
}

int clampInt(int value, int low, int high) {
    return std::clamp(value, low, high);
}

// QSettings splits a comma-separated INI value into a QStringList on read, so
// reading `key = a, b` through toString() yields nothing at all. Accept both
// shapes for every whitespace/comma separated target list.
QStringList readList(QSettings &settings, const QString &key) {
    const QVariant raw = settings.value(key);
    if (raw.typeId() == QMetaType::QStringList) {
        return raw.toStringList();
    }
    return raw.toString().split(
        QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
}

void validate(Config &config) {
    config.health.cpuWarnPercent =
        clampDouble(config.health.cpuWarnPercent, 0.0, 100.0);
    config.health.cpuHighPercent =
        clampDouble(config.health.cpuHighPercent, 0.0, 100.0);
    if (config.health.cpuHighPercent < config.health.cpuWarnPercent) {
        config.health.cpuHighPercent = config.health.cpuWarnPercent;
    }

    config.health.sustainedSamples =
        clampInt(config.health.sustainedSamples, 1, 100);
    config.health.rssWarnKb = std::min<qulonglong>(
        config.health.rssWarnKb, 1024ULL * 1024ULL * 1024ULL * 1024ULL);

    config.docker.healthyMs = clampInt(config.docker.healthyMs, 1000, 3600000);
    config.docker.issueMs = clampInt(config.docker.issueMs, 1000, 3600000);
    config.docker.backoffStartMs =
        clampInt(config.docker.backoffStartMs, 1000, 3600000);
    config.docker.backoffCapMs =
        clampInt(config.docker.backoffCapMs, 1000, 3600000);
    if (config.docker.backoffCapMs < config.docker.backoffStartMs) {
        config.docker.backoffCapMs = config.docker.backoffStartMs;
    }
    config.docker.absentMs = clampInt(config.docker.absentMs, 1000, 3600000);
    config.docker.timeoutMs = clampInt(config.docker.timeoutMs, 100, 600000);
    config.docker.killGraceMs =
        clampInt(config.docker.killGraceMs, 50, 60000);

    config.focusCacheTtlMs = clampInt(config.focusCacheTtlMs, 0, 600000);
    config.sampleIntervalMs = clampInt(config.sampleIntervalMs, 100, 600000);

    config.kube.healthyMs = clampInt(config.kube.healthyMs, 1000, 3600000);
    config.kube.issueMs = clampInt(config.kube.issueMs, 1000, 3600000);
    config.kube.backoffStartMs =
        clampInt(config.kube.backoffStartMs, 1000, 3600000);
    config.kube.backoffCapMs =
        clampInt(config.kube.backoffCapMs, 1000, 3600000);
    if (config.kube.backoffCapMs < config.kube.backoffStartMs) {
        config.kube.backoffCapMs = config.kube.backoffStartMs;
    }
    config.kube.absentMs = clampInt(config.kube.absentMs, 1000, 3600000);
    config.kube.timeoutMs = clampInt(config.kube.timeoutMs, 100, 600000);
    config.kube.killGraceMs = clampInt(config.kube.killGraceMs, 50, 60000);

    config.synthetic.intervalMs =
        clampInt(config.synthetic.intervalMs, 5000, 3600000);
    config.synthetic.timeoutMs =
        clampInt(config.synthetic.timeoutMs, 500, 60000);
    if (config.synthetic.targets.size() > 32) {
        config.synthetic.targets = config.synthetic.targets.mid(0, 32);
    }

    config.cost.cpuPerCoreHour =
        clampDouble(config.cost.cpuPerCoreHour, 0.0, 1000.0);
    config.cost.memPerGbHour =
        clampDouble(config.cost.memPerGbHour, 0.0, 1000.0);

    config.traces.port = clampInt(config.traces.port, 1, 65535);
    config.traces.maxSpans = clampInt(config.traces.maxSpans, 100, 100000);
    config.traces.emitIntervalMs =
        clampInt(config.traces.emitIntervalMs, 200, 60000);

    config.path.intervalMs = clampInt(config.path.intervalMs, 10000, 3600000);
    config.path.cycles = clampInt(config.path.cycles, 1, 100);
    if (config.path.targets.size() > 16) {
        config.path.targets = config.path.targets.mid(0, 16);
    }

    config.dogstatsd.port = clampInt(config.dogstatsd.port, 1, 65535);
    config.dogstatsd.emitIntervalMs =
        clampInt(config.dogstatsd.emitIntervalMs, 200, 60000);

    config.slo.objective = clampDouble(config.slo.objective, 0.5, 0.999999);
    config.slo.windowHours = clampDouble(config.slo.windowHours, 1.0, 8760.0);

    config.database.port = clampInt(config.database.port, 1, 65535);
    config.database.intervalMs =
        clampInt(config.database.intervalMs, 1000, 300000);

    config.tls.intervalMs = clampInt(config.tls.intervalMs, 10000, 86400000);
    if (config.tls.targets.size() > 32) {
        config.tls.targets = config.tls.targets.mid(0, 32);
    }

    config.fim.intervalMs = clampInt(config.fim.intervalMs, 1000, 86400000);
    config.fim.maxFileSizeKb =
        clampInt(config.fim.maxFileSizeKb, 0, 1024 * 1024);
    if (config.fim.paths.size() > 64) {
        config.fim.paths = config.fim.paths.mid(0, 64);
    }

    config.cloud.timeoutMs = clampInt(config.cloud.timeoutMs, 120, 5000);

    config.fleet.pollMs = clampInt(config.fleet.pollMs, 2000, 600000);
    config.fleet.timeoutMs = clampInt(config.fleet.timeoutMs, 200, 30000);
    if (config.fleet.peers.size() > 64) {
        config.fleet.peers = config.fleet.peers.mid(0, 64);
    }

    remoteValidate(config.remote);
}

} // namespace

Config defaultConfig() {
    return Config{};
}

Config loadConfig(const QString &path, QString *errorMessage) {
    Config config;

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return config;
    }

    QSettings settings(path, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("could not parse config: %1").arg(path);
        }
        return config;
    }

    settings.beginGroup(QStringLiteral("health"));
    {
        bool ok = false;
        const double warn =
            settings.value(QStringLiteral("cpu_warn_percent")).toString().toDouble(&ok);
        if (ok) {
            config.health.cpuWarnPercent = warn;
        }
        ok = false;
        const double high =
            settings.value(QStringLiteral("cpu_high_percent")).toString().toDouble(&ok);
        if (ok) {
            config.health.cpuHighPercent = high;
        }
        const int rss =
            settings.value(QStringLiteral("rss_warn_kb")).toInt();
        if (rss > 0) {
            config.health.rssWarnKb = static_cast<qulonglong>(rss);
        }
        const int sustain =
            settings.value(QStringLiteral("sustained_samples")).toInt();
        if (sustain > 0) {
            config.health.sustainedSamples = sustain;
        }
    }
    settings.endGroup();

    // [alerts] = per-rule monitor overrides (M66). Missing keys keep the
    // engine defaults; entries are keyed per rule id.
    settings.beginGroup(QStringLiteral("alerts"));
    {
        QList<AlertRule> rules = defaultAlertRules();
        for (AlertRule &rule : rules) {
            const QString thresholdKey =
                rule.id + QStringLiteral("_threshold");
            const QString cyclesKey = rule.id + QStringLiteral("_cycles");
            bool ok = false;
            const double threshold = settings.value(thresholdKey)
                                         .toString()
                                         .toDouble(&ok);
            if (ok && threshold >= 0.0 && threshold <= 1e9) {
                rule.threshold = threshold;
            }
            const int cycles = settings.value(cyclesKey).toInt(&ok);
            if (ok && cycles >= 1 && cycles <= 100) {
                rule.sustained = cycles;
            }
            // M87: correlation keys. A group folds every rule sharing it onto
            // one incident; an owner is the PagerDuty-style assignment shown on
            // the row. The cadence keys are Alertmanager's group_wait /
            // repeat_interval, in seconds, clamped to a sane band.
            const QString groupKey = rule.id + QStringLiteral("_group");
            if (settings.contains(groupKey)) {
                const QString group =
                    settings.value(groupKey).toString().trimmed();
                if (group.size() <= 64) {
                    // Present-but-empty is a real choice: fold alone.
                    rule.group = group;
                }
            }
            const QString ownerKey = rule.id + QStringLiteral("_owner");
            if (settings.contains(ownerKey)) {
                const QString owner =
                    settings.value(ownerKey).toString().trimmed();
                if (owner.size() <= 64) {
                    rule.owner = owner;
                }
            }
            const double waitS =
                settings.value(rule.id + QStringLiteral("_group_wait_s"))
                    .toString()
                    .toDouble(&ok);
            if (ok && waitS > 0.0 && waitS <= 86400.0) {
                rule.groupWaitMs = static_cast<qint64>(waitS * 1000.0);
            }
            const double repeatS =
                settings.value(rule.id + QStringLiteral("_repeat_s"))
                    .toString()
                    .toDouble(&ok);
            // repeat_interval shorter than group_wait is rejected the same way
            // Alertmanager rejects it ("repeat_interval is less than
            // group_interval").
            if (ok && repeatS > 0.0 && repeatS <= 86400.0
                && repeatS * 1000.0 >= static_cast<double>(rule.groupWaitMs)) {
                rule.repeatIntervalMs =
                    static_cast<qint64>(repeatS * 1000.0);
            }
        }
        config.alertRules = rules;
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("docker"));
    {
        const int healthy = settings.value(QStringLiteral("healthy_ms")).toInt();
        if (healthy > 0) {
            config.docker.healthyMs = healthy;
        }
        const int issue = settings.value(QStringLiteral("issue_ms")).toInt();
        if (issue > 0) {
            config.docker.issueMs = issue;
        }
        const int backoff =
            settings.value(QStringLiteral("backoff_start_ms")).toInt();
        if (backoff > 0) {
            config.docker.backoffStartMs = backoff;
        }
        const int cap = settings.value(QStringLiteral("backoff_cap_ms")).toInt();
        if (cap > 0) {
            config.docker.backoffCapMs = cap;
        }
        const int absent = settings.value(QStringLiteral("absent_ms")).toInt();
        if (absent > 0) {
            config.docker.absentMs = absent;
        }
        const int timeout = settings.value(QStringLiteral("timeout_ms")).toInt();
        if (timeout > 0) {
            config.docker.timeoutMs = timeout;
        }
        const QString socket = settings.value(QStringLiteral("socket")).toString();
        if (!socket.isEmpty()) {
            config.docker.socketPath = socket;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("kubernetes"));
    {
        const int healthy = settings.value(QStringLiteral("healthy_ms")).toInt();
        if (healthy > 0) {
            config.kube.healthyMs = healthy;
        }
        const int issue = settings.value(QStringLiteral("issue_ms")).toInt();
        if (issue > 0) {
            config.kube.issueMs = issue;
        }
        const int backoff =
            settings.value(QStringLiteral("backoff_start_ms")).toInt();
        if (backoff > 0) {
            config.kube.backoffStartMs = backoff;
        }
        const int cap = settings.value(QStringLiteral("backoff_cap_ms")).toInt();
        if (cap > 0) {
            config.kube.backoffCapMs = cap;
        }
        const int absent = settings.value(QStringLiteral("absent_ms")).toInt();
        if (absent > 0) {
            config.kube.absentMs = absent;
        }
        const int timeout = settings.value(QStringLiteral("timeout_ms")).toInt();
        if (timeout > 0) {
            config.kube.timeoutMs = timeout;
        }
        config.kube.context =
            settings.value(QStringLiteral("context")).toString().trimmed();
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("theme"));
    {
        const QString accent =
            settings.value(QStringLiteral("accent")).toString().trimmed();
        if (looksLikeAccent(accent)) {
            config.theme.accent = accent.toLower();
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("overlay"));
    {
        const int opacity = settings.value(QStringLiteral("opacity")).toInt();
        if (opacity > 0) {
            config.overlay.opacity = qBound(40, opacity, 100);
        }
        // An id, validated against the curated set; an older config that holds
        // a file path (or anything else) falls back to none instead of trying
        // to load it.
        const QString wallpaper =
            settings.value(QStringLiteral("wallpaper"))
                .toString()
                .trimmed()
                .toLower();
        if (wallpaperPresetExists(wallpaper)) {
            config.overlay.wallpaper = wallpaper;
        }
        const int ttl =
            settings.value(QStringLiteral("focus_cache_ttl_ms")).toInt();
        if (ttl >= 0 && settings.contains(QStringLiteral("focus_cache_ttl_ms"))) {
            config.focusCacheTtlMs = ttl;
        }
        const int sample =
            settings.value(QStringLiteral("sample_interval_ms")).toInt();
        if (sample > 0) {
            config.sampleIntervalMs = sample;
        }
        if (settings.contains(QStringLiteral("click_through"))) {
            config.overlay.clickThrough =
                settings.value(QStringLiteral("click_through")).toBool();
        }
        if (settings.contains(QStringLiteral("sticky"))) {
            config.overlay.sticky =
                settings.value(QStringLiteral("sticky")).toBool();
        }
        bool ok = false;
        const int x = settings.value(QStringLiteral("x")).toString().toInt(&ok);
        if (ok) {
            config.overlay.x = x;
        }
        ok = false;
        const int y = settings.value(QStringLiteral("y")).toString().toInt(&ok);
        if (ok) {
            config.overlay.y = y;
        }
        ok = false;
        const int width =
            settings.value(QStringLiteral("width")).toString().toInt(&ok);
        if (ok) {
            config.overlay.width = width;
        }
        ok = false;
        const int height =
            settings.value(QStringLiteral("height")).toString().toInt(&ok);
        if (ok) {
            config.overlay.height = height;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("cloud"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.cloud.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        const QString rates =
            settings.value(QStringLiteral("rates")).toString().trimmed();
        if (!rates.isEmpty()) {
            config.cloud.ratesPath = rates;
        }
        const int timeout = settings.value(QStringLiteral("timeout_ms")).toInt();
        if (timeout > 0) {
            config.cloud.timeoutMs = timeout;
        }
        if (settings.contains(QStringLiteral("inventory"))) {
            config.cloud.inventory =
                settings.value(QStringLiteral("inventory")).toBool();
        }
        const int inventoryTimeout =
            settings.value(QStringLiteral("inventory_timeout_ms")).toInt();
        if (inventoryTimeout > 0) {
            config.cloud.inventoryTimeoutMs =
                qBound(2000, inventoryTimeout, 120000);
        }
        if (settings.contains(QStringLiteral("spend"))) {
            config.cloud.spend =
                settings.value(QStringLiteral("spend")).toBool();
        }
        const int spendTimeout =
            settings.value(QStringLiteral("spend_timeout_ms")).toInt();
        if (spendTimeout > 0) {
            config.cloud.spendTimeoutMs =
                qBound(5000, spendTimeout, 180000);
        }
        config.cloud.terraformStatePath =
            settings.value(QStringLiteral("terraform_state"))
                .toString()
                .trimmed();
        config.cloud.identityOverride =
            settings.value(QStringLiteral("identity")).toString().trimmed();
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("synthetic"));
    {
        config.synthetic.targets =
            readList(settings, QStringLiteral("targets"));
        const int interval =
            settings.value(QStringLiteral("interval_ms")).toInt();
        if (interval > 0) {
            config.synthetic.intervalMs = interval;
        }
        const int timeout = settings.value(QStringLiteral("timeout_ms")).toInt();
        if (timeout > 0) {
            config.synthetic.timeoutMs = timeout;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("cost"));
    {
        const double cpu =
            settings.value(QStringLiteral("cpu_per_core_hour")).toDouble();
        if (cpu > 0.0) {
            config.cost.cpuPerCoreHour = cpu;
        }
        const double mem =
            settings.value(QStringLiteral("mem_per_gb_hour")).toDouble();
        if (mem > 0.0) {
            config.cost.memPerGbHour = mem;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("traces"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.traces.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        const int port = settings.value(QStringLiteral("port")).toInt();
        if (port > 0) {
            config.traces.port = port;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("path"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.path.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        config.path.targets = readList(settings, QStringLiteral("targets"));
        const int interval = settings.value(QStringLiteral("interval_ms")).toInt();
        if (interval > 0) {
            config.path.intervalMs = interval;
        }
        const int cycles = settings.value(QStringLiteral("cycles")).toInt();
        if (cycles > 0) {
            config.path.cycles = cycles;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("dogstatsd"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.dogstatsd.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        const int port = settings.value(QStringLiteral("port")).toInt();
        if (port > 0) {
            config.dogstatsd.port = port;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("history"));
    {
        bool ok = false;
        const bool enabled =
            settings.value(QStringLiteral("enabled"), true).toBool();
        config.history.enabled = enabled;
        ok = false;
        const int samples =
            settings.value(QStringLiteral("samples")).toString().toInt(&ok);
        if (ok) {
            config.history.samples = clampInt(samples, 60, 100000);
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("timeline"));
    {
        config.timeline.enabled =
            settings.value(QStringLiteral("enabled"), true).toBool();
        config.timeline.markers =
            settings.value(QStringLiteral("markers"), true).toBool();
        bool ok = false;
        const int events =
            settings.value(QStringLiteral("events")).toString().toInt(&ok);
        if (ok) {
            config.timeline.events = clampInt(events, 100, 20000);
        }
        ok = false;
        const int window =
            settings.value(QStringLiteral("window_s")).toString().toInt(&ok);
        if (ok) {
            config.timeline.windowSeconds = clampInt(window, 10, 3600);
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("silences"));
    for (const QString &key : settings.childKeys()) {
        if (key.endsWith(QStringLiteral("_comment"))
            || key.endsWith(QStringLiteral("_author"))) {
            continue;
        }
        QString matcher = key;
        qint64 endsAtMs = 0;
        qint64 startsAtMs = 0;
        if (key.endsWith(QStringLiteral("_until"))) {
            matcher = key.left(key.size() - 6);
            bool ok = false;
            endsAtMs = settings.value(key).toString().toLongLong(&ok);
            if (!ok || endsAtMs <= 0) {
                continue;
            }
        } else {
            const qint64 duration =
                parseSilenceDurationMs(settings.value(key).toString());
            if (duration < 0) {
                continue;
            }
            startsAtMs = QDateTime::currentMSecsSinceEpoch();
            endsAtMs = startsAtMs + duration;
        }
        SilenceRule rule;
        rule.matcher = matcher;
        rule.startsAtMs = startsAtMs;
        rule.endsAtMs = endsAtMs;
        rule.comment =
            settings.value(matcher + QStringLiteral("_comment")).toString();
        rule.author =
            settings.value(matcher + QStringLiteral("_author")).toString();
        config.silences.rules.append(rule);
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("metrics"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.metrics.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        const int port = settings.value(QStringLiteral("port")).toInt();
        if (port > 0) {
            config.metrics.port = clampInt(port, 1, 65535);
        }
        const QString bind =
            settings.value(QStringLiteral("bind")).toString().trimmed();
        if (!bind.isEmpty()) {
            config.metrics.bindAddress = bind;
        }
        config.metrics.token =
            settings.value(QStringLiteral("token")).toString().trimmed();
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("fleet"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.fleet.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        if (settings.contains(QStringLiteral("share"))) {
            config.fleet.share =
                settings.value(QStringLiteral("share")).toBool();
        }
        config.fleet.name =
            settings.value(QStringLiteral("name")).toString().trimmed();
        config.fleet.peers = readList(settings, QStringLiteral("peers"));
        config.fleet.token =
            settings.value(QStringLiteral("token")).toString().trimmed();
        const int poll = settings.value(QStringLiteral("poll_ms")).toInt();
        if (poll > 0) {
            config.fleet.pollMs = poll;
        }
        const int timeout =
            settings.value(QStringLiteral("timeout_ms")).toInt();
        if (timeout > 0) {
            config.fleet.timeoutMs = timeout;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("remote"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.remote.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        config.remote.url =
            settings.value(QStringLiteral("url")).toString().trimmed();
        config.remote.token =
            settings.value(QStringLiteral("token")).toString().trimmed();
        config.remote.name =
            settings.value(QStringLiteral("name")).toString().trimmed();
        config.remote.machineId =
            settings.value(QStringLiteral("machine_id")).toString().trimmed();
        config.remote.caFile =
            settings.value(QStringLiteral("ca_file")).toString().trimmed();
        const int interval =
            settings.value(QStringLiteral("interval_ms")).toInt();
        if (interval > 0) {
            config.remote.intervalMs = interval;
        }
        const int remoteTimeout =
            settings.value(QStringLiteral("timeout_ms")).toInt();
        if (remoteTimeout > 0) {
            config.remote.timeoutMs = remoteTimeout;
        }
        // spool_mb=0 means "no spool", so an explicit zero must survive; only a
        // missing key keeps the default.
        if (settings.contains(QStringLiteral("spool_mb"))) {
            config.remote.spoolMb =
                settings.value(QStringLiteral("spool_mb")).toInt();
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("actions"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.actions.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("webhook"));    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.webhook.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        const QString url =
            settings.value(QStringLiteral("url")).toString().trimmed();
        if (!url.isEmpty()) {
            config.webhook.url = url;
        }
        const QString format =
            settings.value(QStringLiteral("format")).toString().trimmed();
        if (format == QLatin1String("generic")
            || format == QLatin1String("slack")) {
            config.webhook.format = format;
        }
        const int timeout = settings.value(QStringLiteral("timeout_ms")).toInt();
        if (timeout > 0) {
            config.webhook.timeoutMs = clampInt(timeout, 500, 60000);
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("slo"));
    {
        const QString name = settings.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
            config.slo.name = name;
        }
        const double objective =
            settings.value(QStringLiteral("objective")).toDouble();
        if (objective > 0.0) {
            config.slo.objective = objective;
        }
        const double window = settings.value(QStringLiteral("window_hours")).toDouble();
        if (window > 0.0) {
            config.slo.windowHours = window;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("database"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.database.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        const QString host = settings.value(QStringLiteral("host")).toString();
        if (!host.isEmpty()) {
            config.database.host = host;
        }
        const int port = settings.value(QStringLiteral("port")).toInt();
        if (port > 0) {
            config.database.port = port;
        }
        const QString user = settings.value(QStringLiteral("user")).toString();
        if (!user.isEmpty()) {
            config.database.user = user;
        }
        const QString name = settings.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
            config.database.database = name;
        }
        const int interval = settings.value(QStringLiteral("interval_ms")).toInt();
        if (interval > 0) {
            config.database.intervalMs = interval;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("tls"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.tls.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        const QString targets =
            settings.value(QStringLiteral("targets")).toString();
        config.tls.targets =
            targets.split(QRegularExpression(QStringLiteral("[\\s,]+")),
                          Qt::SkipEmptyParts);
        const int interval = settings.value(QStringLiteral("interval_ms")).toInt();
        if (interval > 0) {
            config.tls.intervalMs = interval;
        }
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("fim"));
    {
        if (settings.contains(QStringLiteral("enabled"))) {
            config.fim.enabled =
                settings.value(QStringLiteral("enabled")).toBool();
        }
        if (settings.contains(QStringLiteral("paths"))) {
            const QStringList raw =
                settings.value(QStringLiteral("paths")).toStringList();
            QStringList expanded;
            for (const QString &item : raw) {
                expanded += item.split(
                    QRegularExpression(QStringLiteral("[\\s,]+")),
                    Qt::SkipEmptyParts);
            }
            config.fim.paths = expanded;
        }
        const int interval = settings.value(QStringLiteral("interval_ms")).toInt();
        if (interval > 0) {
            config.fim.intervalMs = interval;
        }
        const QString algorithm =
            settings.value(QStringLiteral("algorithm")).toString();
        if (!algorithm.isEmpty()) {
            config.fim.algorithm = algorithm;
        }
        const int maxSize =
            settings.value(QStringLiteral("max_file_size_kb")).toInt();
        if (maxSize > 0) {
            config.fim.maxFileSizeKb = maxSize;
        }
        if (settings.contains(QStringLiteral("watch"))) {
            config.fim.watch = settings.value(QStringLiteral("watch")).toBool();
        }
    }
    settings.endGroup();

    validate(config);
    return config;
}

void saveAlertOverrides(const QString &path,
                        const QList<AlertRule> &rules) {
    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("alerts"));
    for (const AlertRule &rule : rules) {
        settings.setValue(rule.id + QStringLiteral("_threshold"),
                          rule.threshold);
        settings.setValue(rule.id + QStringLiteral("_cycles"),
                          rule.sustained);
        // M87: persist correlation + cadence so a configured group/owner
        // survives a restart (the editors only touch threshold/cycles).
        settings.setValue(rule.id + QStringLiteral("_group"), rule.group);
        settings.setValue(rule.id + QStringLiteral("_owner"), rule.owner);
        settings.setValue(rule.id + QStringLiteral("_group_wait_s"),
                          static_cast<double>(rule.groupWaitMs) / 1000.0);
        settings.setValue(rule.id + QStringLiteral("_repeat_s"),
                          static_cast<double>(rule.repeatIntervalMs) / 1000.0);
    }
    settings.endGroup();
    settings.sync();
}

void saveSilences(const QString &path, const QList<SilenceRule> &rules) {
    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("silences"));
    settings.remove(QString());
    for (const SilenceRule &rule : rules) {
        if (rule.matcher.isEmpty() || rule.endsAtMs <= 0) {
            continue;
        }
        settings.setValue(rule.matcher + QStringLiteral("_until"),
                          QString::number(rule.endsAtMs));
        if (!rule.comment.isEmpty()) {
            settings.setValue(rule.matcher + QStringLiteral("_comment"),
                              rule.comment);
        }
        if (!rule.author.isEmpty()) {
            settings.setValue(rule.matcher + QStringLiteral("_author"),
                              rule.author);
        }
    }
    settings.endGroup();
    settings.sync();
}

void saveOverlayConfig(const QString &path, const OverlayConfig &overlay) {
    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("overlay"));
    settings.setValue(QStringLiteral("click_through"), overlay.clickThrough);
    settings.setValue(QStringLiteral("sticky"), overlay.sticky);
    settings.setValue(QStringLiteral("x"), QString::number(overlay.x));
    settings.setValue(QStringLiteral("y"), QString::number(overlay.y));
    settings.setValue(QStringLiteral("width"), QString::number(overlay.width));
    settings.setValue(QStringLiteral("height"),
                      QString::number(overlay.height));
    settings.setValue(QStringLiteral("opacity"),
                      QString::number(overlay.opacity));
    settings.setValue(QStringLiteral("wallpaper"), overlay.wallpaper);
    settings.endGroup();
    settings.sync();
}

void saveThemeAccent(const QString &path, const QString &accent) {
    if (!looksLikeAccent(accent)) {
        return;
    }
    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("theme"));
    settings.setValue(QStringLiteral("accent"), accent.toLower());
    settings.endGroup();
    settings.sync();
}

namespace {

// One place reads the environment override, so both paths cannot quietly
// disagree about which variable wins.
QString envOverride(const char *name) {
    return QProcessEnvironment::systemEnvironment().value(QLatin1String(name));
}

} // namespace

QString defaultConfigPath() {
    const QString override = envOverride("CROWSNEST_CONFIG");
    if (!override.isEmpty()) {
        return override;
    }

#if defined(Q_OS_LINUX)
    const QString configHome =
        QProcessEnvironment::systemEnvironment().value(
            QStringLiteral("XDG_CONFIG_HOME"),
            QProcessEnvironment::systemEnvironment().value(
                QStringLiteral("HOME")) + QStringLiteral("/.config"));
    const QString path = configHome + QStringLiteral("/crowsnest/config.ini");
#else
    const QString path = QStandardPaths::writableLocation(
                             QStandardPaths::AppDataLocation)
        + QStringLiteral("/config.ini");
#endif
    return path;
}

QString defaultCloudRatePath() {
    const QString override = envOverride("CROWSNEST_CLOUD_RATES");
    if (!override.isEmpty()) {
        return override;
    }

#if defined(Q_OS_LINUX)
    const QString configHome =
        QProcessEnvironment::systemEnvironment().value(
            QStringLiteral("XDG_CONFIG_HOME"),
            QProcessEnvironment::systemEnvironment().value(
                QStringLiteral("HOME")) + QStringLiteral("/.config"));
    const QString path = configHome + QStringLiteral("/crowsnest/cloud-rates.txt");
#else
    const QString path = QStandardPaths::writableLocation(
                             QStandardPaths::AppDataLocation)
        + QStringLiteral("/cloud-rates.txt");
#endif
    return path;
}
