#include "Config.h"

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
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
