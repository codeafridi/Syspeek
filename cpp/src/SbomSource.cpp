#include "SbomSource.h"

#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QDateTime>
#include <QFileInfo>
#include <QMutex>
#include <QSet>
#include <QStringList>

void SbomSource::ensureLoaded() {
    if (m_loaded) {
        return;
    }
    m_loaded = true;
    QFile status(QStringLiteral("/var/lib/dpkg/status"));
    if (!status.open(QIODevice::ReadOnly)) {
        return;
    }
    const QList<DpkgPackage> packages = parseDpkgStatus(status.readAll());
    for (const DpkgPackage &pkg : packages) {
        m_packages.insert(pkg.name, pkg);
    }
}

SbomReport SbomSource::resolve(const QStringList &paths) {
#if defined(Q_OS_MACOS)
    return resolveMac(paths);
#elif defined(Q_OS_WIN)
    return resolveWin(paths);
#else
    ensureLoaded();
#endif

    QStringList unknown;
    for (const QString &path : paths) {
        if (!path.startsWith(QLatin1Char('/'))) {
            continue;
        }
        if (!m_fileOwner.contains(path)) {
            unknown.append(path);
        }
    }

    // Batch-resolve owners for unseen paths via dpkg-query (cached after).
    for (int i = 0; i < unknown.size(); i += 100) {
        const QStringList chunk = unknown.mid(i, 100);
        QProcess process;
        QStringList args{QStringLiteral("-S")};
        args.append(chunk);
        process.start(QStringLiteral("dpkg-query"), args);
        if (!process.waitForStarted(1000)
            || !process.waitForFinished(5000)) {
            break;
        }
        const QString output = QString::fromUtf8(process.readAllStandardOutput());
        for (const QString &line : output.split(QLatin1Char('\n'))) {
            const int sep = line.indexOf(QStringLiteral(": "));
            if (sep < 0) {
                continue;
            }
            QString owner = line.left(sep);
            const QString file = line.mid(sep + 2).trimmed();
            const int archSep = owner.indexOf(QLatin1Char(':'));
            if (archSep >= 0) {
                owner = owner.left(archSep);
            }
            if (file.startsWith(QLatin1Char('/'))) {
                m_fileOwner.insert(file, owner);
            }
        }
        // Mark unresolved paths so we do not re-query them.
        for (const QString &path : chunk) {
            if (!m_fileOwner.contains(path)) {
                m_fileOwner.insert(path, QString());
            }
        }
    }

    return buildSbom(paths, m_packages, m_fileOwner);
}

SbomReport SbomSource::resolveWin(const QStringList &paths) {
    // Windows software inventory: registry Uninstall keys, the exact
    // source osquery's programs table reads (kept strings:
    // programsTablePlugin / UninstallString; DisplayName / DisplayVersion
    // in windows_exporter + the Datadog agent). Matches the focused
    // app's file paths against InstallLocation prefixes; the 60 s TTL
    // keeps per-tick sampling cheap.
    static QMutex mutex;
    static QList<QPair<QString, SbomEntry>> cached;
    static qint64 refreshedAt = 0;
    {
        QMutexLocker locker(&mutex);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (refreshedAt == 0 || now - refreshedAt > 60000) {
            cached.clear();
            const QString branches[] = {
                QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft"
                               "\\Windows\\CurrentVersion\\Uninstall"),
                QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node"
                               "\\Microsoft\\Windows\\CurrentVersion"
                               "\\Uninstall"),
                QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft"
                               "\\Windows\\CurrentVersion\\Uninstall"),
            };
            QSet<QPair<QString, QString>> seen;
            for (const QString &branch : branches) {
                QSettings root(branch, QSettings::NativeFormat);
                const QStringList products = root.childGroups();
                for (const QString &product : products) {
                    root.beginGroup(product);
                    const QString name =
                        root.value(QStringLiteral("DisplayName"))
                            .toString()
                            .trimmed();
                    const QString version =
                        root.value(QStringLiteral("DisplayVersion"))
                            .toString()
                            .trimmed();
                    QString location =
                        root.value(QStringLiteral("InstallLocation"))
                            .toString()
                            .trimmed();
                    const bool wow =
                        branch.contains(QStringLiteral("Wow6432Node"));
                    root.endGroup();
                    if (name.isEmpty()) {
                        continue;
                    }
                    const QPair<QString, QString> key(name, version);
                    if (seen.contains(key)) {
                        continue;
                    }
                    seen.insert(key);
                    location = location.toLower();
                    location.replace(QLatin1Char('\\'), QLatin1Char('/'));
                    while (location.endsWith(QLatin1Char('/'))) {
                        location.chop(1);
                    }
                    SbomEntry entry;
                    entry.package = name;
                    entry.version = version;
                    entry.arch = wow ? QStringLiteral("x86")
                                     : QStringLiteral("x64");
                    entry.purl =
                        version.isEmpty()
                            ? QStringLiteral("pkg:generic/%1").arg(name)
                            : QStringLiteral("pkg:generic/%1@%2")
                                  .arg(name, version);
                    cached.append({location, entry});
                }
            }
            refreshedAt = now;
        }
    }

    SbomReport report;
    report.available = !cached.isEmpty();
    report.libraryCount = paths.size();

    QMutexLocker locker(&mutex);
    QHash<QPair<QString, QString>, int> index;
    for (const QString &rawPath : paths) {
        QString path = rawPath;
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (path.isEmpty()) {
            continue;
        }
        const QString dir = path.section(QLatin1Char('/'), 0, -2).toLower();
        for (const auto &program : cached) {
            const QString &rootPath = program.first;
            const SbomEntry &pkg = program.second;
            if (rootPath.isEmpty() || !dir.startsWith(rootPath)) {
                continue;
            }
            const QPair<QString, QString> key(pkg.package, pkg.version);
            if (!index.contains(key)) {
                index.insert(key, report.entries.size());
                SbomEntry copy = pkg;
                copy.files.append(rawPath);
                report.entries.append(copy);
            } else {
                SbomEntry &entry = report.entries[index.value(key)];
                if (!entry.files.contains(rawPath)) {
                    entry.files.append(rawPath);
                }
            }
            break;
        }
    }
    report.resolvedCount = report.entries.size();
    return report;
}

SbomReport SbomSource::resolveMac(const QStringList &paths) {
    // osquery darwin grounding: apps table reads /Applications bundles'
    // Info.plist keys (CFBundleShortVersionString x6 in strings) and the
    // homebrew_packages table reads Cellar dirs (STR Cellar).
    SbomReport report;
    report.available = true;
    report.libraryCount = paths.size();

    QHash<QPair<QString, QString>, int> index;
    QHash<QString, QString> bundleVersions;
    for (const QString &path : paths) {
        if (path.isEmpty() || !path.startsWith(QLatin1Char('/'))) {
            continue;
        }
        MacPathPackage pkg;
        if (!classifyMacPath(path, pkg)) {
            continue;
        }
        if (pkg.kind == QStringLiteral("bundle")) {
            if (!bundleVersions.contains(pkg.name)) {
                const QString appRoot = path.section(
                    QStringLiteral(".app/"), 0, 0);
                QSettings info(appRoot + QStringLiteral(".app/Contents/Info.plist"),
                               QSettings::NativeFormat);
                bundleVersions.insert(
                    pkg.name,
                    info.value(QStringLiteral("CFBundleShortVersionString"))
                        .toString());
            }
            pkg.version = bundleVersions.value(pkg.name);
        }
        const QPair<QString, QString> key(pkg.name, pkg.version);
        if (!index.contains(key)) {
            SbomEntry entry;
            entry.package = pkg.name;
            entry.version = pkg.version;
            entry.arch = QStringLiteral("universal");
            entry.purl = macPurl(pkg);
            index.insert(key, report.entries.size());
            report.entries.append(entry);
        }
        SbomEntry &entry = report.entries[index.value(key)];
        if (!entry.files.contains(path)) {
            entry.files.append(path);
        }
    }
    report.resolvedCount = report.entries.size();
    return report;
}
