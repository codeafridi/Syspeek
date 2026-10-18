#pragma once

#include "SbomStatus.h"

#include <QHash>

// Resolves the focused process's loaded files to owning dpkg packages,
// using /var/lib/dpkg/status plus dpkg-query, with caching.
class SbomSource {
public:
    SbomReport resolve(const QStringList &paths);

private:
    SbomReport resolveMac(const QStringList &paths);
    SbomReport resolveWin(const QStringList &paths);
    void ensureLoaded();

    bool m_loaded = false;
    QHash<QString, DpkgPackage> m_packages;
    QHash<QString, QString> m_fileOwner;
};
