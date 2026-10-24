#include "FimStatus.h"

#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {

QString shortHash(const QString &hash) {
    if (hash.isEmpty()) {
        return QStringLiteral("?");
    }
    return hash.left(8);
}

QString renameKey(const FimFileMeta &meta) {
    if (meta.inode != 0) {
        return QStringLiteral("i:%1").arg(meta.inode);
    }
    if (!meta.hash.isEmpty()) {
        return QStringLiteral("h:%1:%2").arg(meta.hash).arg(meta.size);
    }
    return QString();
}

bool contentChanged(const FimFileMeta &a, const FimFileMeta &b) {
    if (!a.hash.isEmpty() || !b.hash.isEmpty()) {
        return a.hash != b.hash;
    }
    return a.size != b.size;
}

bool metadataChanged(const FimFileMeta &a, const FimFileMeta &b) {
    return a.mode != b.mode || a.uid != b.uid || a.gid != b.gid;
}

} // namespace

QString fimChangeKindText(FimChangeKind kind) {
    switch (kind) {
    case FimChangeKind::Created:
        return QStringLiteral("created");
    case FimChangeKind::Modified:
        return QStringLiteral("modified");
    case FimChangeKind::Metadata:
        return QStringLiteral("metadata");
    case FimChangeKind::Deleted:
        return QStringLiteral("deleted");
    case FimChangeKind::Renamed:
        return QStringLiteral("renamed");
    }
    return QStringLiteral("changed");
}

QList<FimChange> diffFim(const QHash<QString, FimFileMeta> &previous,
                         const QHash<QString, FimFileMeta> &current,
                         qint64 nowMs) {
    QList<FimChange> changes;

    QStringList prevPaths = previous.keys();
    QStringList currPaths = current.keys();
    std::sort(prevPaths.begin(), prevPaths.end());
    std::sort(currPaths.begin(), currPaths.end());

    QStringList createdPaths;
    QStringList deletedPaths;
    for (const QString &path : currPaths) {
        if (!previous.contains(path)) {
            createdPaths.append(path);
        }
    }
    for (const QString &path : prevPaths) {
        if (!current.contains(path)) {
            deletedPaths.append(path);
        }
    }

    // Content / metadata drift on files present in both scans.
    for (const QString &path : currPaths) {
        const auto it = previous.constFind(path);
        if (it == previous.constEnd()) {
            continue;
        }
        const FimFileMeta &before = it.value();
        const FimFileMeta &after = current.value(path);
        FimChange change;
        change.path = path;
        change.atMs = nowMs;
        change.hash = shortHash(after.hash);
        change.oldHash = shortHash(before.hash);
        if (contentChanged(before, after) || before.mtimeMs != after.mtimeMs) {
            change.kind = FimChangeKind::Modified;
            changes.append(change);
        } else if (metadataChanged(before, after)) {
            change.kind = FimChangeKind::Metadata;
            changes.append(change);
        }
    }

    // Pair deletions with creations that share identity -> rename.
    QHash<QString, QStringList> deletedByKey;
    for (const QString &path : deletedPaths) {
        const QString key = renameKey(previous.value(path));
        if (!key.isEmpty()) {
            deletedByKey[key].append(path);
        }
    }

    QSet<QString> consumedDeleted;
    for (const QString &path : createdPaths) {
        const QString key = renameKey(current.value(path));
        QStringList &candidates = deletedByKey[key];
        if (!key.isEmpty() && !candidates.isEmpty()) {
            const QString oldPath = candidates.takeFirst();
            consumedDeleted.insert(oldPath);
            FimChange change;
            change.kind = FimChangeKind::Renamed;
            change.path = path;
            change.oldPath = oldPath;
            change.atMs = nowMs;
            change.hash = shortHash(current.value(path).hash);
            change.oldHash = shortHash(previous.value(oldPath).hash);
            changes.append(change);
        } else {
            FimChange change;
            change.kind = FimChangeKind::Created;
            change.path = path;
            change.atMs = nowMs;
            change.hash = shortHash(current.value(path).hash);
            changes.append(change);
        }
    }

    for (const QString &path : deletedPaths) {
        if (consumedDeleted.contains(path)) {
            continue;
        }
        FimChange change;
        change.kind = FimChangeKind::Deleted;
        change.path = path;
        change.atMs = nowMs;
        change.hash = shortHash(previous.value(path).hash);
        changes.append(change);
    }

    return changes;
}

QString fimSummaryText(const FimSnapshot &snapshot) {
    if (!snapshot.enabled) {
        return QStringLiteral("FIM disabled");
    }
    if (!snapshot.available) {
        return QStringLiteral("scanning...");
    }
    if (snapshot.baseline) {
        return QStringLiteral("baseline: %1 file(s) %2")
            .arg(snapshot.fileCount)
            .arg(snapshot.algorithm);
    }
    return QStringLiteral("%1 change(s)  %2 file(s)  %3")
        .arg(snapshot.changes.size())
        .arg(snapshot.fileCount)
        .arg(snapshot.algorithm);
}

QString fimDetailText(const FimSnapshot &snapshot) {
    if (!snapshot.enabled) {
        return QStringLiteral("file integrity monitoring disabled");
    }
    if (!snapshot.available) {
        return QStringLiteral("no scan yet");
    }
    if (snapshot.changes.isEmpty()) {
        return snapshot.baseline ? QStringLiteral("baseline established, no drift")
                                 : QStringLiteral("no drift since baseline");
    }
    QStringList lines;
    for (const FimChange &change : snapshot.changes) {
        const QString kind =
            fimChangeKindText(change.kind).toUpper().leftJustified(8);
        QString location = change.path;
        if (change.kind == FimChangeKind::Renamed) {
            location = QStringLiteral("%1 -> %2").arg(change.oldPath, change.path);
        }
        QString digest;
        switch (change.kind) {
        case FimChangeKind::Created:
            digest = QStringLiteral("  %1").arg(change.hash);
            break;
        case FimChangeKind::Modified:
            digest = QStringLiteral("  %1 -> %2").arg(change.oldHash, change.hash);
            break;
        case FimChangeKind::Deleted:
            digest = QStringLiteral("  %1").arg(change.hash);
            break;
        case FimChangeKind::Metadata:
        case FimChangeKind::Renamed:
            break;
        }
        lines.append(QStringLiteral("%1 %2%3").arg(kind, location, digest));
    }
    return lines.join(QLatin1Char('\n'));
}

FimEventFlags decodeFimEventFlags(quint32 flags) {
    FimEventFlags out;
    constexpr quint32 kItemCreated = 0x00000100;
    constexpr quint32 kItemRemoved = 0x00000200;
    constexpr quint32 kItemInodeMetaMod = 0x00000400;
    constexpr quint32 kItemRenamed = 0x00000800;
    constexpr quint32 kItemModified = 0x00001000;
    constexpr quint32 kItemFinderInfoMod = 0x00002000;
    constexpr quint32 kItemChangeOwner = 0x00004000;
    constexpr quint32 kItemXattrMod = 0x00008000;
    constexpr quint32 kItemIsFile = 0x00010000;
    constexpr quint32 kItemIsDir = 0x00020000;
    constexpr quint32 kItemIsSymlink = 0x00040000;
    Q_UNUSED(kItemIsFile);
    Q_UNUSED(kItemIsDir);
    Q_UNUSED(kItemIsSymlink);

    if ((flags & 0x1u) || (flags & 0x2u) || (flags & 0x4u)
        || (flags & 0x8u)) {
        out.dropped = true;
    }
    if (flags & 0x20u) {
        out.rootChanged = true;
    }
    if (flags & kItemCreated) {
        out.created = true;
    }
    if (flags & kItemRemoved) {
        out.removed = true;
    }
    if (flags & kItemRenamed) {
        out.renamed = true;
    }
    if (flags & kItemModified) {
        out.modified = true;
    }
    out.metadata = (flags & (kItemInodeMetaMod | kItemFinderInfoMod
                             | kItemChangeOwner | kItemXattrMod))
                       != 0u
                   && !(out.created || out.removed || out.modified
                        || out.renamed);
    out.anything = out.created || out.removed || out.modified || out.renamed
                   || out.rootChanged || out.dropped || out.metadata;
    return out;
}
