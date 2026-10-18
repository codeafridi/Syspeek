#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

struct DpkgPackage {
    QString name;
    QString version;
    QString arch;
    QString source;
    QString maintainer;
};

struct SbomEntry {
    QString package;
    QString version;
    QString arch;
    QString purl;
    QStringList files;
};

struct SbomReport {
    bool available = false;
    int libraryCount = 0;
    int resolvedCount = 0;
    QList<SbomEntry> entries;
};

// Parse /var/lib/dpkg/status into packages.
QList<DpkgPackage> parseDpkgStatus(const QByteArray &text);

// purl for a Debian package: pkg:deb/ubuntu/<name>@<version>?arch=<arch>
QString makePurl(const DpkgPackage &pkg);

#if defined(Q_OS_WIN)
// Installed programs from the registry Uninstall keys (osquery
// programs-table model: DisplayName / DisplayVersion / InstallLocation).
QList<SbomEntry> enumerateWindowsSoftware();
#endif

// Resolve filesystem paths to owning packages and build the SBOM.
SbomReport buildSbom(const QStringList &paths,
                     const QHash<QString, DpkgPackage> &packages,
                     const QHash<QString, QString> &fileOwner);

// macOS path classification: Homebrew Cellar formula/cask, /Applications
// bundle, Apple system path, or generic file.
struct MacPathPackage {
    QString name;
    QString version;
    QString kind; // "brew" | "bundle" | "system" | "generic"
};

bool classifyMacPath(const QString &path, MacPathPackage &out);
QString macPurl(const MacPathPackage &pkg);

QString sbomSummaryText(const SbomReport &report);
QString sbomDetailText(const SbomReport &report);
