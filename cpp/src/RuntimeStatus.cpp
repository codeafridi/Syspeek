#include "RuntimeStatus.h"

#include "platform/SystemBackend.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

QString runtimeName(Runtime runtime) {
    switch (runtime) {
    case Runtime::Python:
        return QStringLiteral("python");
    case Runtime::Node:
        return QStringLiteral("node");
    case Runtime::Java:
        return QStringLiteral("java");
    case Runtime::Ruby:
        return QStringLiteral("ruby");
    case Runtime::Php:
        return QStringLiteral("php");
    case Runtime::Dotnet:
        return QStringLiteral("dotnet");
    case Runtime::Go:
        return QStringLiteral("go");
    case Runtime::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

Runtime languageFromCommand(const QString &exe, const QStringList &cmdline) {
    // Ported from DataDog/datadog-agent pkg/languagedetection/detector.go
    if (exe == QStringLiteral("java")) {
        for (const QString &arg : cmdline) {
            if (arg.trimmed() == QStringLiteral("org.jruby.Main")) {
                return Runtime::Ruby; // JRubyDetector
            }
        }
    }

    static const QHash<QString, Runtime> exact = {
        {QStringLiteral("py"), Runtime::Python},
        {QStringLiteral("python"), Runtime::Python},
        {QStringLiteral("java"), Runtime::Java},
        {QStringLiteral("npm"), Runtime::Node},
        {QStringLiteral("node"), Runtime::Node},
        {QStringLiteral("dotnet"), Runtime::Dotnet},
        {QStringLiteral("ruby"), Runtime::Ruby},
        {QStringLiteral("rubyw"), Runtime::Ruby},
        {QStringLiteral("php"), Runtime::Php},
        {QStringLiteral("php-fpm"), Runtime::Php},
    };
    const auto exactIt = exact.constFind(exe);
    if (exactIt != exact.constEnd()) {
        return exactIt.value();
    }

    static const QRegularExpression rubyPattern(
        QStringLiteral("^ruby\\d+\\.\\d+$"));
    static const QRegularExpression phpPattern(
        QStringLiteral("^php(?:-fpm)?\\d(?:\\.\\d)?$"));
    if (exe.startsWith(QStringLiteral("python"))) {
        return Runtime::Python;
    }
    if (exe.startsWith(QStringLiteral("java")) && exe != QStringLiteral("javac")) {
        return Runtime::Java;
    }
    if (rubyPattern.match(exe).hasMatch()) {
        return Runtime::Ruby;
    }
    if (phpPattern.match(exe).hasMatch()) {
        return Runtime::Php;
    }
    return Runtime::Unknown;
}

bool mapsHasDotnet(const QByteArray &maps) {
    return maps.contains("/System.Runtime.dll");
}

QString goVersionFromElf(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    constexpr qint64 kMax = 64LL * 1024 * 1024;
    const QByteArray data = file.read(kMax);
    static const QByteArray magic("\xff Go buildinf:", 14);
    const int idx = data.indexOf(magic);
    if (idx < 0) {
        return QString();
    }
    // The version string lives inline right after the build-info header for
    // Go >= 1.18; scan a small window for a "go1.x.y" token.
    const QByteArray window = data.mid(idx, 512);
    static const QRegularExpression version(
        QStringLiteral("go1\\.[0-9]+(?:\\.[0-9]+)?"));
    const QRegularExpressionMatch match = version.match(
        QString::fromLatin1(window));
    if (match.hasMatch()) {
        return match.captured(0);
    }
    return QStringLiteral("unknown");
}

RuntimeInfo detectRuntime(int pid) {
    RuntimeInfo info;
    const QString base = QStringLiteral("/proc/%1").arg(pid);

    QString exeName;
    QStringList cmdline;
    QFile cmdFile(base + QStringLiteral("/cmdline"));
    if (cmdFile.open(QIODevice::ReadOnly)) {
        const QByteArray raw = cmdFile.readAll();
        for (const QByteArray &part : raw.split('\0')) {
            if (!part.isEmpty()) {
                cmdline.append(QString::fromUtf8(part));
            }
        }
        if (!cmdline.isEmpty()) {
            exeName = QFileInfo(cmdline.first()).fileName();
        }
    }

    info.exe = QFile::symLinkTarget(base + QStringLiteral("/exe"));
    if (exeName.isEmpty() && !info.exe.isEmpty()) {
        exeName = QFileInfo(info.exe).fileName();
    }

    info.runtime = languageFromCommand(exeName, cmdline);

    if (info.runtime == Runtime::Unknown) {        QFile maps(base + QStringLiteral("/maps"));
        if (maps.open(QIODevice::ReadOnly)
            && mapsHasDotnet(maps.readAll())) {
            info.runtime = Runtime::Dotnet;
        }
    }
    if (info.runtime == Runtime::Unknown && !info.exe.isEmpty()) {
        const QString goVersion = goVersionFromElf(info.exe);
        if (!goVersion.isEmpty()) {
            info.runtime = Runtime::Go;
            if (goVersion != QStringLiteral("unknown")) {
                info.version = goVersion;
            }
        }
    }

    info.name = runtimeName(info.runtime);

    // SSI / APM instrumentation hints (LD_PRELOAD, lang agent options).
    QFile envFile(base + QStringLiteral("/environ"));
    if (envFile.open(QIODevice::ReadOnly)) {
        const QString env = QString::fromUtf8(envFile.readAll());
        if (env.contains(QStringLiteral("dd-trace"))
            || env.contains(QStringLiteral("datadog"))
            || env.contains(QStringLiteral("dd-java-agent"))) {
            info.instrumentation = QStringLiteral("datadog");
        } else if (env.contains(QStringLiteral("opentelemetry"))
                   || env.contains(QStringLiteral("otel"))) {
            info.instrumentation = QStringLiteral("opentelemetry");
        }
    }
    if (info.instrumentation.isEmpty()) {
        QFile maps(base + QStringLiteral("/maps"));
        if (maps.open(QIODevice::ReadOnly)) {
            const QByteArray mapData = maps.readAll();
            if (mapData.contains("libdatadog")) {
                info.instrumentation = QStringLiteral("datadog");
            } else if (mapData.contains("opentelemetry")) {
                info.instrumentation = QStringLiteral("opentelemetry");
            }
        }
    }
    return info;
}

RuntimeInfo detectRuntime(int pid, SystemBackend &backend) {
    RuntimeInfo info;
    info.exe = backend.exePath(pid);

    QStringList cmdline;
    const QString joined = backend.processCmdline(pid);
    const QString exeName = !joined.isEmpty()
                                ? QFileInfo(
                                      joined.section(QLatin1Char(' '), 0, 0))
                                      .fileName()
                                : QFileInfo(info.exe).fileName();
    if (!joined.isEmpty()) {
        for (const QString &part : joined.split(QLatin1Char(' '))) {
            if (!part.isEmpty()) {
                cmdline.append(part);
            }
        }
    }

    info.runtime = languageFromCommand(exeName, cmdline);
    info.name = runtimeName(info.runtime);

    const QString env = QString::fromUtf8(backend.processEnv(pid));
    if (env.contains(QStringLiteral("dd-trace"))
        || env.contains(QStringLiteral("datadog"))
        || env.contains(QStringLiteral("dd-java-agent"))) {
        info.instrumentation = QStringLiteral("datadog");
    } else if (env.contains(QStringLiteral("opentelemetry"))
               || env.contains(QStringLiteral("otel"))) {
        info.instrumentation = QStringLiteral("opentelemetry");
    }
    return info;
}

QString runtimeSummaryText(const RuntimeInfo &info) {
    if (info.runtime == Runtime::Unknown) {
        return QStringLiteral("runtime: native/unknown");
    }
    return QStringLiteral("runtime: %1%2%3")
        .arg(info.name,
             info.version.isEmpty() ? QString() : QStringLiteral(" ") + info.version,
             info.instrumentation.isEmpty()
                 ? QString()
                 : QStringLiteral("  (instrumented: ") + info.instrumentation
                       + QStringLiteral(")"));
}

QString runtimeDetailText(const RuntimeInfo &info) {
    QString text = QStringLiteral("language  %1\nversion   %2\nexe       %3\n"
                                  "ssi/apm   %4")
                       .arg(info.name,
                            info.version.isEmpty() ? QStringLiteral("-")
                                                   : info.version,
                            info.exe.isEmpty() ? QStringLiteral("-") : info.exe,
                            info.instrumentation.isEmpty()
                                ? QStringLiteral("none")
                                : info.instrumentation);
    return text;
}
