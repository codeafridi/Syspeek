#include "SbomStatus.h"
#include <QSettings>
#include <QSet>

#include <algorithm>

QList<DpkgPackage> parseDpkgStatus(const QByteArray &text) {
    QList<DpkgPackage> packages;
    DpkgPackage current;
    bool has = false;
    auto flush = [&]() {
        if (has && !current.name.isEmpty()) {
            packages.append(current);
        }
        current = DpkgPackage{};
        has = false;
    };

    const QList<QByteArray> lines = text.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromLatin1(raw);
        if (line.trimmed().isEmpty()) {
            flush();
            continue;
        }
        if (line.startsWith(QLatin1Char(' '))) {
            continue; // continuation of a multi-line field
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            continue;
        }
        const QString key = line.left(colon);
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QStringLiteral("Package")) {
            current.name = value;
            has = true;
        } else if (key == QStringLiteral("Version")) {
            current.version = value;
        } else if (key == QStringLiteral("Architecture")) {
            current.arch = value;
        } else if (key == QStringLiteral("Source")) {
            current.source = value.section(QLatin1Char(' '), 0, 0);
        } else if (key == QStringLiteral("Maintainer")) {
            current.maintainer = value;
        }
    }
    flush();
    return packages;
}

QString makePurl(const DpkgPackage &pkg) {
    QString purl = QStringLiteral("pkg:deb/ubuntu/%1").arg(pkg.name);
    if (!pkg.version.isEmpty()) {
        purl += QStringLiteral("@") + pkg.version;
    }
    if (!pkg.arch.isEmpty()) {
        purl += QStringLiteral("?arch=") + pkg.arch;
    }
    return purl;
}

SbomReport buildSbom(const QStringList &paths,
                     const QHash<QString, DpkgPackage> &packages,
                     const QHash<QString, QString> &fileOwner) {
    SbomReport report;
    report.available = true;
    report.libraryCount = paths.size();

    QHash<QString, SbomEntry> byPackage;
    QStringList order;
    for (const QString &rawPath : paths) {
        QString path = rawPath;
        if (path.endsWith(QStringLiteral(" (deleted)"))) {
            path.chop(10);
        }
        if (path.isEmpty() || !path.startsWith(QLatin1Char('/'))) {
            continue;
        }
        const QString owner = fileOwner.value(path);
        if (owner.isEmpty() || !packages.contains(owner)) {
            continue;
        }
        SbomEntry &entry = byPackage[owner];
        if (entry.package.isEmpty()) {
            const DpkgPackage &pkg = packages.value(owner);
            entry.package = owner;
            entry.version = pkg.version;
            entry.arch = pkg.arch;
            entry.purl = makePurl(pkg);
            order.append(owner);
        }
        if (!entry.files.contains(path)) {
            entry.files.append(path);
        }
    }

    for (const QString &name : order) {
        report.entries.append(byPackage.value(name));
    }
    std::sort(report.entries.begin(), report.entries.end(),
              [](const SbomEntry &a, const SbomEntry &b) {
                  return a.package < b.package;
              });
    report.resolvedCount = report.entries.size();
    return report;
}

bool classifyMacPath(const QString &path, MacPathPackage &out) {
    out = MacPathPackage();
    const QString normalized = path.startsWith(QStringLiteral("/private/"))
                                   ? path.mid(8) + QStringLiteral("/")
                                   : path;
    const int cellar = normalized.indexOf(QStringLiteral("/Cellar/"));
    if (cellar >= 0) {
        const QStringList rest = normalized.mid(cellar + 8)
                                     .split(QLatin1Char('/'));
        if (rest.size() >= 2 && !rest.at(0).isEmpty()) {
            out.kind = QStringLiteral("brew");
            out.name = rest.at(0);
            out.version = rest.at(1);
            return true;
        }
        return false;
    }
    if (normalized.startsWith(QStringLiteral("/Applications/"))
        && normalized.contains(QStringLiteral(".app/"))) {
        const QStringList parts = normalized.split(QLatin1Char('/'));
        for (const QString &part : parts) {
            if (part.endsWith(QStringLiteral(".app"))) {
                out.kind = QStringLiteral("bundle");
                out.name = part.left(part.size()
                                     - QStringLiteral(".app").size());
                return true;
            }
        }
        return false;
    }
    if (normalized.startsWith(QStringLiteral("/System/"))) {
        out.kind = QStringLiteral("system");
        out.name = normalized.section(QLatin1Char('/'), -1).section(
            QLatin1Char('.'), 0, 0);
        return true;
    }
    out.kind = QStringLiteral("generic");
    out.name = normalized.section(QLatin1Char('/'), -1).section(
        QLatin1Char('.'), 0, 0);
    out.version = QString();
    return !out.name.isEmpty();
}

QString macPurl(const MacPathPackage &pkg) {
    if (pkg.kind == QStringLiteral("brew")) {
        return QStringLiteral("pkg:generic/brew/%1@%2").arg(pkg.name,
                                                           pkg.version);
    }
    if (pkg.kind == QStringLiteral("bundle")) {
        return QStringLiteral("pkg:generic/bundle/%1").arg(pkg.name);
    }
    if (pkg.kind == QStringLiteral("system")) {
        return QStringLiteral("pkg:generic/apple/%1").arg(pkg.name);
    }
    return QStringLiteral("pkg:generic/%1").arg(pkg.name);
}

QString sbomSummaryText(const SbomReport &report) {
    if (!report.available) {
        return QStringLiteral("sbom: unavailable");
    }
    return QStringLiteral("%1 packages from %2 libraries")
        .arg(report.resolvedCount)
        .arg(report.libraryCount);
}

#if defined(Q_OS_WIN)
QList<SbomEntry> enumerateWindowsSoftware() {
    // osquery programs table model: the Uninstall registry keys with
    // DisplayName / DisplayVersion / InstallLocation (kept strings:
    // "programsTablePlugin", "UninstallString", DisplayName x33 in
    // agent.exe; "Uninstall" in windows_exporter + SystemInformer).
    QList<SbomEntry> entries;
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
                root.value(QStringLiteral("DisplayName")).toString().trimmed();
            const QString version =
                root.value(QStringLiteral("DisplayVersion")).toString().trimmed();
            root.endGroup();
            if (name.isEmpty()) {
                continue;
            }
            const QPair<QString, QString> key(name, version);
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            SbomEntry entry;
            entry.package = name;
            entry.version = version;
            entry.arch = branch.contains(QStringLiteral("Wow6432Node"))
                             ? QStringLiteral("x86")
                             : QStringLiteral("x64");
            entry.purl = version.isEmpty()
                             ? QStringLiteral("pkg:generic/%1").arg(name)
                             : QStringLiteral("pkg:generic/%1@%2")
                                   .arg(name, version);
            entries.append(entry);
        }
    }
    return entries;
}
#endif

QString sbomDetailText(const SbomReport &report) {
    if (!report.available) {
        return QStringLiteral("sbom: unavailable");
    }
    if (report.entries.isEmpty()) {
        return QStringLiteral("no package-owned libraries matched");
    }
    QString text;
    for (const SbomEntry &entry : report.entries) {
        text += QStringLiteral("%1@%2  [%3]  %4 lib(s)\n")
                    .arg(entry.package, entry.version, entry.arch)
                    .arg(entry.files.size());
    }
    return text.trimmed();
}
