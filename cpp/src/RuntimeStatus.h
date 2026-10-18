#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class SystemBackend;

enum class Runtime { Unknown, Python, Node, Java, Ruby, Php, Dotnet, Go };

QString runtimeName(Runtime runtime);

// Datadog pkg/languagedetection rules: exact/prefix command matches + JRuby.
Runtime languageFromCommand(const QString &exe, const QStringList &cmdline);

// Datadog .NET detector: /proc/<pid>/maps contains /System.Runtime.dll.
bool mapsHasDotnet(const QByteArray &maps);

// Datadog Go detector: read the Go build info from an ELF executable.
// Returns the version (e.g. "go1.23.4"), "unknown" if Go but no version could
// be parsed, or an empty string if it is not a Go binary.
QString goVersionFromElf(const QString &path);

struct RuntimeInfo {
    Runtime runtime = Runtime::Unknown;
    QString name;
    QString version;
    QString exe;
    QString instrumentation;
};

RuntimeInfo detectRuntime(int pid);
RuntimeInfo detectRuntime(int pid, SystemBackend &backend);
QString runtimeSummaryText(const RuntimeInfo &info);
QString runtimeDetailText(const RuntimeInfo &info);
