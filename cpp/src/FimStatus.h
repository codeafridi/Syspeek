#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QtGlobal>

// File Integrity Monitoring (Datadog CSM/CWS analogue).
//
// Data model mirrors the Datadog agent's pbgo/core file-integrity protos:
//   core.TargetFileMeta { file_path, file_size, file_uid, file_gid, file_mode,
//                         file_hashes }            (TargetFileHash{algorithm,hash})
//   core.FileMetaState  { ... }                    (per-file cached state)
// Config keys ported: runtime_security_config.fim_enabled,
//   .file_size_threshold, .hash_resolver.hash_algorithms, .hash_resolver.enabled.

struct FimFileMeta {
    QString path;
    quint64 size = 0;
    unsigned int uid = 0;
    unsigned int gid = 0;
    unsigned int mode = 0; // permission bits
    qint64 mtimeMs = 0;
    qint64 ctimeMs = 0;
    quint64 inode = 0;
    QString hash; // hex digest, empty when the file was not hashed
};

enum class FimChangeKind { Created, Modified, Metadata, Deleted, Renamed };

struct FimChange {
    FimChangeKind kind = FimChangeKind::Modified;
    QString path;
    QString oldPath; // source path for a rename
    QString hash;    // new/current digest (short)
    QString oldHash; // previous digest (short)
    qint64 atMs = 0;
};

struct FimSnapshot {
    bool enabled = false;
    bool available = false;
    bool baseline = false; // first scan, no prior state to diff against
    int watchedPaths = 0;
    int fileCount = 0;
    quint64 bytesHashed = 0;
    int maxFileSizeKb = 0;
    QString algorithm = QStringLiteral("sha256");
    QList<FimChange> changes; // bounded recent drift
    qint64 sampledAtMs = 0;
};

QString fimChangeKindText(FimChangeKind kind);

// Pure drift computation between a previous baseline and the current scan.
// Detects create / content-modify / metadata-change / delete and rename
// (renames matched by (inode) or (hash,size) across deleted/created pairs).
QList<FimChange> diffFim(const QHash<QString, FimFileMeta> &previous,
                         const QHash<QString, FimFileMeta> &current,
                         qint64 nowMs);

QString fimSummaryText(const FimSnapshot &snapshot);
QString fimDetailText(const FimSnapshot &snapshot);

// macOS FSEvents item-event flags (Apple FSEvents.h bit masks) decoded to the
// scan-worthy kinds this source consumes. Pure so it is unit-testable on every
// platform; mirrors osquery's FSEventsEventPublisher name table
// (ItemCreated -> CREATED, ItemRemoved -> DELETED, ItemModified -> UPDATED,
// ItemRenamed -> MOVED_TO, ItemChangeOwner/ItemXattrMod/ItemInodeMetaMod ->
// ATTRIBUTES_MODIFIED). Pure directory-level events without FileEvents cannot
// produce these item flags.
struct FimEventFlags {
    bool created = false;      // kFSEventStreamEventFlagItemCreated 0x100
    bool removed = false;      // kFSEventStreamEventFlagItemRemoved 0x200
    bool modified = false;     // kFSEventStreamEventFlagItemModified 0x1000
    bool renamed = false;      // kFSEventStreamEventFlagItemRenamed 0x800
    bool metadata = false;     // owner/xattr/inode-meta/finder-info only
    bool rootChanged = false;  // kFSEventStreamEventFlagRootChanged 0x20
    bool dropped = false;      // MustScanSubDirs 0x1 / UserDropped 0x2 /
                               // KernelDropped 0x4 / EventIdsWrapped 0x8
    bool anything = false;     // at least one item/root/drop signal above

    // Whether an event batch containing this flag mask should trigger the
    // debounced rescan (metadata-only drift is still caught by the periodic
    // stat pass, but item/content/structure changes rescan immediately).
    bool scanWorthy() const {
        return created || removed || modified || renamed || rootChanged
                   || dropped;
    }
};

FimEventFlags decodeFimEventFlags(quint32 flags);
