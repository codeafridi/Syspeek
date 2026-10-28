#include "Config.h"
#include "MetricsEndpoint.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

namespace {

QString writeConfig(QTemporaryDir &dir, const QByteArray &contents) {
    const QString path = dir.path() + QStringLiteral("/config.ini");
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(contents);
        file.close();
    }
    return path;
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    int failures = 0;
    auto check = [&failures](bool condition, const char *message) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition) {
            ++failures;
        }
    };

    const Config defaults = defaultConfig();
    check(defaults.health.cpuWarnPercent == 70.0, "default cpu warn 70");
    check(defaults.health.cpuHighPercent == 90.0, "default cpu high 90");
    check(defaults.health.sustainedSamples == 3, "default sustain 3");
    check(defaults.docker.healthyMs == 20000, "default docker healthy 20s");
    check(defaults.sampleIntervalMs == 1000, "default sample interval 1s");
    check(defaults.fim.enabled && !defaults.fim.paths.isEmpty(),
          "default fim enabled with paths");
    check(defaults.fim.algorithm == QStringLiteral("sha256"),
          "default fim algorithm sha256");

    {
        QString error = QStringLiteral("x");
        const Config c = loadConfig(QStringLiteral("/nonexistent/config.ini"), &error);
        check(error.isEmpty(), "missing file -> no error");
        check(c.health.cpuWarnPercent == 70.0, "missing file -> defaults");
    }

    QTemporaryDir dir;
    check(dir.isValid(), "temp dir created");

    {
        const QString path = writeConfig(dir, QByteArrayLiteral(
            "[health]\n"
            "cpu_warn_percent=50\n"
            "cpu_high_percent=80\n"
            "sustained_samples=5\n"
            "rss_warn_kb=524288\n"
            "[docker]\n"
            "healthy_ms=30000\n"
            "[overlay]\n"
            "sample_interval_ms=2000\n"));

        QString error;
        const Config c = loadConfig(path, &error);
        check(error.isEmpty(), "valid config -> no error");
        check(c.health.cpuWarnPercent == 50.0, "loaded cpu warn");
        check(c.health.cpuHighPercent == 80.0, "loaded cpu high");
        check(c.health.sustainedSamples == 5, "loaded sustain");
        check(c.health.rssWarnKb == 524288ULL, "loaded rss warn");
        check(c.docker.healthyMs == 30000, "loaded docker healthy");
        check(c.sampleIntervalMs == 2000, "loaded sample interval");
    }

    {
        QTemporaryDir dir2;
        const QString path = writeConfig(dir2, QByteArrayLiteral(
            "[health]\n"
            "cpu_warn_percent=120\n"       // out of range -> clamp 100
            "cpu_high_percent=10\n"        // below warn -> raised to warn
            "sustained_samples=0\n"        // invalid -> default 3
            "[docker]\n"
            "healthy_ms=-5\n"));           // invalid -> default

        QString error;
        const Config c = loadConfig(path, &error);
        check(c.health.cpuWarnPercent == 100.0, "cpu warn clamped to 100");
        check(c.health.cpuHighPercent == 100.0, "cpu high raised to >= warn");
        check(c.health.sustainedSamples == 3, "invalid sustain -> default");
        check(c.docker.healthyMs == 20000, "invalid docker healthy -> default");
    }

    {
        QTemporaryDir dir3;
        const QString path = writeConfig(dir3, QByteArrayLiteral(
            "[health]\n"
            "cpu_warn_percent=notanumber\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(c.health.cpuWarnPercent == 70.0,
              "non-numeric value -> default kept");
    }

    {
        // M66: [alerts] overrides load over the engine defaults, reject
        // invalid values, and a save/load roundtrip preserves an edit.
        QTemporaryDir dir66;
        const QString path = writeConfig(dir66, QByteArrayLiteral(
            "[alerts]\n"
            "host_cpu_threshold=72.5\n"
            "host_cpu_cycles=2\n"
            "journal_errors_threshold=500\n"
            "kernel_fd_cycles=101\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        const AlertRule *hostCpu = nullptr;
        const AlertRule *journal = nullptr;
        const AlertRule *steal = nullptr;
        for (const AlertRule &rule : c.alertRules) {
            if (rule.id == QStringLiteral("host_cpu")) {
                hostCpu = &rule;
            } else if (rule.id == QStringLiteral("journal_errors")) {
                journal = &rule;
            } else if (rule.id == QStringLiteral("host_steal")) {
                steal = &rule;
            }
        }
        check(hostCpu != nullptr && journal != nullptr && steal != nullptr,
              "alerts: engine defaults loaded into config");
        check(hostCpu->threshold == 72.5 && hostCpu->sustained == 2,
              "alerts: threshold + cycles override applied");
        check(journal->threshold == 500.0 && journal->sustained == 2,
              "alerts: single-key override keeps default cycles");
        check(hostCpu != nullptr && steal != nullptr
                  && steal->threshold == 5.0 && steal->sustained == 5,
              "alerts: unkeyed rules keep defaults");
        check(defaults.alertRules.isEmpty(),
              "alerts: defaultConfig() carries no rules (engine defaults "
              "live in AlertEngine)");
    }
    {
        // saveAlertOverrides roundtrip: edit one rule, reload, everything
        // else intact.
        QTemporaryDir dir67;
        const QString path = writeConfig(dir67, QByteArrayLiteral(
            "[alerts]\n"
            "host_cpu_threshold=72.5\n"));
        QList<AlertRule> rules = defaultAlertRules();
        for (AlertRule &rule : rules) {
            if (rule.id == QStringLiteral("disk")) {
                rule.threshold = 22.0;
                rule.sustained = 4;
            }
        }
        saveAlertOverrides(path, rules);
        QString error;
        const Config loaded = loadConfig(path, &error);
        bool edited = false;
        bool intact = false;
        bool otherGroups = false;
        for (const AlertRule &rule : loaded.alertRules) {
            if (rule.id == QStringLiteral("disk")) {
                edited = rule.threshold == 22.0 && rule.sustained == 4;
            }
            if (rule.id == QStringLiteral("app_cpu")) {
                intact = rule.threshold == 90.0 && rule.sustained == 3;
            }
        }
        otherGroups = loaded.health.cpuWarnPercent == 70.0;
        check(edited, "alerts: saveAlertOverrides writes the edit");
        check(intact, "alerts: save preserves untouched rules");
        check(otherGroups, "alerts: save keeps the rest of the file");
    }

    {
        // M75: [silences] — a rolling duration authored in the file and an
        // absolute window the HUD wrote (survives a restart).
        QTemporaryDir dirSilence;
        const qint64 absoluteEnd = 4102444800000LL;
        const QString path = writeConfig(dirSilence, QByteArrayLiteral(
            "[silences]\n"
            "host_cpu=30m\n"
            "host_cpu_comment=disk rebuild\n"
            "host_cpu_author=ops\n"
            "app_cpu_until=4102444800000\n"
            "app_cpu_comment=rollout\n"
            "journal_errors=10x\n"
            "bogus=0s\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(c.silences.rules.size() == 3,
              "silences: rolling, absolute and zero-second windows all load");
        const SilenceRule *rolling = nullptr;
        const SilenceRule *absolute = nullptr;
        for (const SilenceRule &rule : c.silences.rules) {
            if (rule.matcher == QStringLiteral("host_cpu")) {
                rolling = &rule;
            } else if (rule.matcher == QStringLiteral("app_cpu")) {
                absolute = &rule;
            }
        }
        check(rolling != nullptr
                  && rolling->endsAtMs - rolling->startsAtMs == 1800000,
              "silences: a duration key becomes a milliseconds window");
        check(rolling != nullptr
                  && rolling->comment == QStringLiteral("disk rebuild")
                  && rolling->author == QStringLiteral("ops"),
              "silences: comment and author ride the matcher key");
        check(absolute != nullptr && absolute->endsAtMs == absoluteEnd
                  && absolute->comment == QStringLiteral("rollout"),
              "silences: an _until key keeps its absolute expiry");
    }
    {
        // saveSilences roundtrip (the HUD writes what the page authored).
        QTemporaryDir dirSilenceSave;
        const QString path = writeConfig(dirSilenceSave, QByteArrayLiteral(
            "[silences]\n"
            "host_cpu=30m\n"));
        QList<SilenceRule> rules;
        SilenceRule rule;
        rule.matcher = QStringLiteral("host_cpu");
        rule.startsAtMs = 1000;
        rule.endsAtMs = 4102444800000LL;
        rule.comment = QStringLiteral("snoozed from the alerts page");
        rule.author = QStringLiteral("local");
        rules.append(rule);
        saveSilences(path, rules);
        QString error;
        const Config loaded = loadConfig(path, &error);
        check(loaded.silences.rules.size() == 1
                  && loaded.silences.rules.at(0).endsAtMs == 4102444800000LL
                  && loaded.silences.rules.at(0).comment
                         == QStringLiteral("snoozed from the alerts page"),
              "silences: saveSilences replaces the group and keeps the window");
        check(loaded.health.cpuWarnPercent == 70.0,
              "silences: saving keeps the rest of the file");
    }

    {
        // M78: [actions] enabled is the master switch for every verb; the
        // group is optional and an explicit false must survive the load.
        QTemporaryDir actionsDir;
        const QString path = writeConfig(actionsDir, QByteArrayLiteral(
            "[actions]\n"
            "enabled=false\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(!c.actions.enabled,
              "actions: enabled=false disables the verbs");

        QTemporaryDir actionsDefaultDir;
        const QString defaults = writeConfig(actionsDefaultDir,
                                            QByteArrayLiteral(
            "[health]\n"
            "cpu_warn_percent=65\n"));
        const Config d = loadConfig(defaults, &error);
        check(d.actions.enabled,
              "actions: verbs are on by default");
        check(d.health.cpuWarnPercent == 65.0,
              "actions: the group does not disturb its neighbours");
    }

    {
        // M76: [metrics] bind + token — the scrape endpoint can move off
        // loopback only when a bearer token guards it.
        QTemporaryDir metricsDir;
        const QString path = writeConfig(metricsDir, QByteArrayLiteral(
            "[metrics]\n"
            "enabled=true\n"
            "port=9200\n"
            "bind=0.0.0.0\n"
            "token=  s3cret-token  \n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(c.metrics.enabled && c.metrics.port == 9200,
              "metrics: enabled and port still load");
        check(c.metrics.bindAddress == QStringLiteral("0.0.0.0"),
              "metrics: the bind address loads");
        check(c.metrics.token == QStringLiteral("s3cret-token"),
              "metrics: the token loads trimmed");

        const MetricsEndpoint endpoint = resolveMetricsEndpoint(c.metrics);
        check(endpoint.bindAddress == QStringLiteral("0.0.0.0")
                  && endpoint.authRequired && !endpoint.bindDowngraded,
              "metrics: an authored LAN bind with a token is honoured");
    }
    {
        QTemporaryDir metricsDir;
        const QString path = writeConfig(metricsDir, QByteArrayLiteral(
            "[metrics]\n"
            "bind=0.0.0.0\n"
            "token=\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(c.metrics.bindAddress == QStringLiteral("0.0.0.0")
                  && c.metrics.token.isEmpty(),
              "metrics: an empty token stays empty");
        const MetricsEndpoint endpoint = resolveMetricsEndpoint(c.metrics);
        check(endpoint.bindAddress == QStringLiteral("127.0.0.1")
                  && endpoint.bindDowngraded,
              "metrics: a LAN bind without a token downgrades to loopback");
    }
    {
        QTemporaryDir metricsDir;
        const QString path = writeConfig(metricsDir, QByteArrayLiteral(
            "[metrics]\n"
            "port=99999\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(c.metrics.port == 65535, "metrics: an out-of-range port clamps");
        check(c.metrics.bindAddress == QStringLiteral("127.0.0.1")
                  && c.metrics.token.isEmpty(),
              "metrics: untouched keys keep the loopback default");
    }

    {
        QTemporaryDir dir4;
        const QString path = writeConfig(dir4, QByteArrayLiteral(
            "[fim]\n"
            "enabled=true\n"
            "paths=/etc/passwd, /etc/shadow /etc/cron.d\n"
            "interval_ms=15000\n"
            "algorithm=sha512\n"
            "max_file_size_kb=2048\n"
            "watch=false\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(error.isEmpty(), "fim config -> no error");
        check(c.fim.enabled, "fim enabled loaded");
        check(c.fim.paths.size() == 3
                  && c.fim.paths.contains(QStringLiteral("/etc/shadow")),
              "fim paths parsed");
        check(c.fim.intervalMs == 15000, "fim interval loaded");
        check(c.fim.algorithm == QStringLiteral("sha512"), "fim algorithm loaded");
        check(c.fim.maxFileSizeKb == 2048, "fim max file size loaded");
        check(!c.fim.watch, "fim watch loaded");
    }

#ifdef Q_OS_LINUX
    check(defaultConfigPath().endsWith(QStringLiteral("syspeek/config.ini"))
              || qEnvironmentVariableIsSet("SYSPEEK_CONFIG"),
          "default config path shape");
#else
    check(defaultConfigPath().endsWith(QStringLiteral("config.ini"))
              || qEnvironmentVariableIsSet("SYSPEEK_CONFIG"),
          "default config path shape");
#endif
    {
        QTemporaryDir webhookDir;
        const QString path = writeConfig(webhookDir, QByteArrayLiteral(
            "[webhook]\n"
            "enabled = true\n"
            "url = https://hooks.slack.com/services/T000/B000/xxxx\n"
            "format = slack\n"
            "timeout_ms = 2500\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(error.isEmpty(), "webhook config -> no error");
        check(c.webhook.enabled, "webhook enabled loaded");
        check(c.webhook.url
                  == QStringLiteral("https://hooks.slack.com/services/T000/B000/xxxx"),
              "webhook url loaded");
        check(c.webhook.format == QStringLiteral("slack"),
              "webhook format loaded");
        check(c.webhook.timeoutMs == 2500, "webhook timeout loaded");
    }

    {
        QTemporaryDir badDir;
        const QString path = writeConfig(badDir, QByteArrayLiteral(
            "[webhook]\n"
            "format = carrier-pigeon\n"
            "timeout_ms = 9999999\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(!c.webhook.enabled, "webhook default off");
        check(c.webhook.format == QStringLiteral("generic"),
              "unknown webhook format ignored");
        check(c.webhook.timeoutMs == 60000, "webhook timeout clamped");
    }

    {
        QTemporaryDir dir;
        const QString path = writeConfig(dir, QByteArrayLiteral(
            "[overlay]\n"
            "click_through = true\n"
            "x = 120\n"
            "y = -40\n"
            "width = 900\n"
            "height = 640\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(error.isEmpty(), "overlay config -> no error");
        check(c.overlay.clickThrough, "overlay click_through loaded");
        check(c.overlay.x == 120 && c.overlay.y == -40,
              "overlay position loaded, negative y kept");
        check(c.overlay.width == 900 && c.overlay.height == 640,
              "overlay size loaded");
        check(c.overlay.sticky, "overlay sticky defaults on");
    }

    {
        QTemporaryDir dir;
        const QString path = writeConfig(dir, QByteArrayLiteral(
            "[overlay]\n"
            "focus_cache_ttl_ms = 900\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(!c.overlay.clickThrough && c.overlay.x == -1
                  && c.overlay.width == -1,
              "overlay defaults: click-through off, no saved geometry");
        check(c.focusCacheTtlMs == 900,
              "overlay: the pre-existing ttl key still loads from the same "
              "group");
    }

    {
        QTemporaryDir dir;
        const QString path = writeConfig(dir, QByteArrayLiteral(
            "[overlay]\n"
            "sticky = false\n"
            "x = not-a-number\n"
            "width = 12x\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(c.overlay.x == -1 && c.overlay.width == -1,
              "overlay: garbage geometry is ignored, not guessed");
        check(!c.overlay.sticky, "overlay: sticky can be turned off");
    }

    {
        QTemporaryDir dir;
        const QString path = writeConfig(dir, QByteArrayLiteral(
            "[history]\n"
            "samples = 600\n"
            "[overlay]\n"
            "click_through = true\n"));
        OverlayConfig saved;
        saved.clickThrough = false;
        saved.x = 64;
        saved.y = 32;
        saved.width = 820;
        saved.height = 560;
        saved.sticky = false;
        saveOverlayConfig(path, saved);
        QString error;
        const Config c = loadConfig(path, &error);
        check(!c.overlay.clickThrough && c.overlay.x == 64
                  && c.overlay.y == 32 && c.overlay.width == 820
                  && c.overlay.height == 560,
              "overlay: a saved rectangle round-trips through the file");
        check(!c.overlay.sticky,
              "overlay: the sticky preference round-trips too");
        check(c.history.samples == 600,
              "overlay: saving the overlay keeps every other group");
    }

    {
        // M82: [cloud] is on by default (link-local IMDS read only).
        QTemporaryDir dir;
        const QString path = writeConfig(dir, QByteArrayLiteral(""));
        QString error;
        const Config c = loadConfig(path, &error);
        check(c.cloud.enabled, "cloud: the metadata probe defaults to on");
        check(c.cloud.timeoutMs == 450, "cloud: the default timeout is 450 ms");
        check(c.cloud.ratesPath.isEmpty(),
              "cloud: no authored path means the default rate file");
        check(c.cloud.inventory,
              "cloud: the account inventory defaults to on");
        check(c.cloud.inventoryTimeoutMs == 25000,
              "cloud: the listing budget defaults to 25 s");
    }

    {
        QTemporaryDir dir;
        const QString path = writeConfig(dir, QByteArrayLiteral(
            "[cloud]\n"
            "enabled = false\n"
            "rates = /tmp/owned-rates.txt\n"
            "timeout_ms = 900\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(!c.cloud.enabled, "cloud: enabled=false is honoured");
        check(c.cloud.ratesPath == QStringLiteral("/tmp/owned-rates.txt")
                  && c.cloud.timeoutMs == 900,
              "cloud: the rate path and timeout load");
    }

    {
        QTemporaryDir dir;
        const QString path = writeConfig(dir, QByteArrayLiteral(
            "[cloud]\n"
            "timeout_ms = 90000\n"
            "rates =   \n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(c.cloud.timeoutMs == 5000,
              "cloud: an absurd timeout clamps to the ceiling");
        check(c.cloud.ratesPath.isEmpty(),
              "cloud: a whitespace path falls back to the default");
        check(c.cloud.enabled, "cloud: untouched keys keep the default on");
    }

    {
        // M83: the listing is a network call to the provider API, so it is
        // switchable and separately budgeted from the link-local read.
        QTemporaryDir dir;
        const QString path = writeConfig(dir, QByteArrayLiteral(
            "[cloud]\n"
            "inventory = false\n"
            "inventory_timeout_ms = 4000\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(!c.cloud.inventory,
              "cloud: inventory=false stops every cloud CLI spawn");
        check(c.cloud.inventoryTimeoutMs == 4000,
              "cloud: the listing budget loads");
        check(c.cloud.enabled && c.cloud.timeoutMs == 450,
              "cloud: the metadata read keeps its own budget and switch");
    }

    {
        QTemporaryDir dir;
        const QString path = writeConfig(dir, QByteArrayLiteral(
            "[cloud]\n"
            "inventory_timeout_ms = 500\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(c.cloud.inventoryTimeoutMs == 2000,
              "cloud: a sub-second listing budget clamps to the floor");
    }

    {
        QTemporaryDir dir;
        const QString path = writeConfig(dir, QByteArrayLiteral(
            "[cloud]\n"
            "inventory_timeout_ms = 600000\n"));
        QString error;
        const Config c = loadConfig(path, &error);
        check(c.cloud.inventoryTimeoutMs == 120000,
              "cloud: an absurd listing budget clamps to the ceiling");
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
