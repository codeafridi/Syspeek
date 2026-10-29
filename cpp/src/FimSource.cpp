#include "FimSource.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>

#include <sys/stat.h>

#if defined(Q_OS_MACOS)
#include "platform/mac/MacFimStream.h"
#endif

namespace {

constexpr int kMaxFiles = 5000;
constexpr int kMaxDepth = 8;
constexpr int kMaxChanges = 100;

QCryptographicHash::Algorithm algorithmFor(const QString &name) {
    const QString lower = name.toLower();
    if (lower == QLatin1String("md5")) {
        return QCryptographicHash::Md5;
    }
    if (lower == QLatin1String("sha1")) {
        return QCryptographicHash::Sha1;
    }
    if (lower == QLatin1String("sha512")) {
        return QCryptographicHash::Sha512;
    }
    return QCryptographicHash::Sha256;
}

QString hashFile(const QString &path, QCryptographicHash::Algorithm algorithm,
                 quint64 *bytesHashed) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QCryptographicHash hasher(algorithm);
    if (hasher.addData(&file) && bytesHashed != nullptr) {
        *bytesHashed += static_cast<quint64>(file.size());
    }
    return QString::fromLatin1(hasher.result().toHex());
}

void collectDir(const QString &dirPath, int depth, QStringList &files,
                int &guard) {
    if (depth > kMaxDepth || guard >= kMaxFiles) {
        return;
    }
    QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden
            | QDir::System,
        QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink()) {
            continue;
        }
        if (entry.isDir()) {
            collectDir(entry.absoluteFilePath(), depth + 1, files, guard);
        } else if (entry.isFile()) {
            files.append(entry.absoluteFilePath());
            if (++guard >= kMaxFiles) {
                return;
            }
        }
    }
}

void collectGlob(const QString &pattern, QStringList &files, int &guard) {
    const int slash = pattern.lastIndexOf(QLatin1Char('/'));
    const QString dirPath =
        slash <= 0 ? QStringLiteral("/") : pattern.left(slash);
    const QString namePattern = slash < 0 ? pattern : pattern.mid(slash + 1);
    QDir dir(dirPath);
    if (!dir.exists()) {
        return;
    }
    const QRegularExpression re(
        QRegularExpression::wildcardToRegularExpression(namePattern));
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden
            | QDir::System,
        QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink() || !re.match(entry.fileName()).hasMatch()) {
            continue;
        }
        if (entry.isDir()) {
            collectDir(entry.absoluteFilePath(), 1, files, guard);
        } else if (entry.isFile()) {
            files.append(entry.absoluteFilePath());
            if (++guard >= kMaxFiles) {
                return;
            }
        }
    }
}

QStringList expandPaths(const QStringList &paths) {
    QStringList files;
    int guard = 0;
    for (const QString &raw : paths) {
        const QString path = QDir::cleanPath(raw);
        if (path.contains(QLatin1Char('*')) || path.contains(QLatin1Char('?'))
            || path.contains(QLatin1Char('['))) {
            collectGlob(path, files, guard);
            continue;
        }
        const QFileInfo info(path);
        if (info.isDir() && !info.isSymLink()) {
            collectDir(path, 0, files, guard);
        } else if (info.isFile()) {
            files.append(path);
            ++guard;
        }
        if (guard >= kMaxFiles) {
            break;
        }
    }
    files.removeDuplicates();
    return files;
}

} // namespace

QStringList fimWatchRoots(const QStringList &paths) {
    QStringList roots;
    roots.reserve(paths.size());
    for (const QString &raw : paths) {
        const QString path = QDir::cleanPath(raw);
        if (path.contains(QLatin1Char('*')) || path.contains(QLatin1Char('?'))
            || path.contains(QLatin1Char('['))) {
            const int slash = path.lastIndexOf(QLatin1Char('/'));
            roots.append(slash <= 0 ? QStringLiteral("/")
                                    : path.left(slash));
            continue;
        }
        const QFileInfo info(path);
        if (info.isDir() && !info.isSymLink()) {
            roots.append(path);
            continue;
        }
        const QString parent = info.absolutePath();
        if (info.isFile()
            || (QFileInfo(parent).isDir()
                && !QFileInfo(parent).isSymLink())) {
            const QString canonical =
                QFileInfo(parent).canonicalFilePath();
            roots.append(canonical.isEmpty() ? parent : canonical);
        }
    }
    roots.removeAll(QString());
    roots.removeDuplicates();
    return roots;
}

FimSource::FimSource(const FimConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config) {
}

FimSource::~FimSource() {
    stop();
}

void FimSource::start() {
    if (!m_config.enabled) {
        emit fimReady(FimSnapshot{});
        return;
    }
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setInterval(m_config.intervalMs);
        connect(m_timer, &QTimer::timeout, this, &FimSource::onTick);
    }
    m_timer->start();

    if (m_config.watch && m_debounce == nullptr) {
        m_debounce = new QTimer(this);
        m_debounce->setSingleShot(true);
        m_debounce->setInterval(400);
        connect(m_debounce, &QTimer::timeout, this, &FimSource::onTick);

#if defined(Q_OS_MACOS)
        auto *stream = new MacFimStream();
        if (stream->start(
                fimWatchRoots(m_config.paths),
                this,
                [this](const QStringList &) { onWatchEvent(); })) {
            m_stream = stream;
        } else {
            delete stream;
        }
        if (m_stream == nullptr) {
#endif
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
                &FimSource::onWatchEvent);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
                &FimSource::onWatchEvent);
#if defined(Q_OS_MACOS)
        }
#endif
    }
    if (m_config.watch && m_stream == nullptr) {
        armWatchers();
    }

    onTick();
}

void FimSource::stop() {
#if defined(Q_OS_MACOS)
    if (m_stream != nullptr) {
        auto *stream = m_stream;
        m_stream = nullptr;
        stream->stop();
        delete stream;
    }
#endif
    if (m_timer != nullptr) {
        m_timer->stop();
    }
    if (m_debounce != nullptr) {
        m_debounce->stop();
    }
}

void FimSource::armWatchers() {
    if (m_watcher == nullptr) {
        return;
    }
    for (const QString &raw : m_config.paths) {
        const QString path = QDir::cleanPath(raw);
        if (path.contains(QLatin1Char('*')) || path.contains(QLatin1Char('?'))
            || path.contains(QLatin1Char('['))) {
            const int slash = path.lastIndexOf(QLatin1Char('/'));
            const QString dirPath =
                slash <= 0 ? QStringLiteral("/") : path.left(slash);
            if (QFileInfo(dirPath).isDir()) {
                m_watcher->addPath(dirPath);
            }
            continue;
        }
        const QFileInfo info(path);
        if (info.exists()) {
            m_watcher->addPath(path);
        }
    }
}

void FimSource::onWatchEvent() {
    if (m_debounce != nullptr) {
        m_debounce->start();
    }
}

void FimSource::onTick() {
    FimSnapshot snapshot;
    snapshot.enabled = true;
    snapshot.available = true;
    snapshot.algorithm = m_config.algorithm;
    snapshot.maxFileSizeKb = m_config.maxFileSizeKb;
    snapshot.watchedPaths = m_config.paths.size();
    snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();

    const QCryptographicHash::Algorithm algorithm =
        algorithmFor(m_config.algorithm);
    const quint64 maxBytes =
        m_config.maxFileSizeKb > 0
            ? static_cast<quint64>(m_config.maxFileSizeKb) * 1024ULL
            : 0ULL;

    const QStringList files = expandPaths(m_config.paths);
    QHash<QString, FimFileMeta> current;
    quint64 bytesHashed = 0;
    for (const QString &path : files) {
        struct stat st;
        const QByteArray native = path.toLocal8Bit();
        if (::stat(native.constData(), &st) != 0) {
            continue;
        }
        FimFileMeta meta;
        meta.path = path;
        meta.size = static_cast<quint64>(st.st_size);
        meta.uid = static_cast<unsigned int>(st.st_uid);
        meta.gid = static_cast<unsigned int>(st.st_gid);
        meta.mode = static_cast<unsigned int>(st.st_mode) & 0x0FFFu;
#if defined(Q_OS_MACOS)
        meta.mtimeMs = static_cast<qint64>(st.st_mtimespec.tv_sec) * 1000
                       + st.st_mtimespec.tv_nsec / 1000000;
        meta.ctimeMs = static_cast<qint64>(st.st_ctimespec.tv_sec) * 1000
                       + st.st_ctimespec.tv_nsec / 1000000;
#elif defined(Q_OS_WIN)
        meta.mtimeMs = static_cast<qint64>(st.st_mtime) * 1000;
        meta.ctimeMs = static_cast<qint64>(st.st_ctime) * 1000;
#else
        meta.mtimeMs = static_cast<qint64>(st.st_mtim.tv_sec) * 1000
                       + st.st_mtim.tv_nsec / 1000000;
        meta.ctimeMs = static_cast<qint64>(st.st_ctim.tv_sec) * 1000
                       + st.st_ctim.tv_nsec / 1000000;
#endif
        meta.inode = static_cast<quint64>(st.st_ino);
        if (maxBytes == 0 || meta.size <= maxBytes) {
            meta.hash = hashFile(path, algorithm, &bytesHashed);
        }
        current.insert(path, meta);
    }

    snapshot.fileCount = current.size();
    snapshot.bytesHashed = bytesHashed;

    if (m_primed) {
        const QList<FimChange> drift =
            diffFim(m_baseline, current, snapshot.sampledAtMs);
        for (const FimChange &change : drift) {
            m_changes.append(change);
        }
        while (m_changes.size() > kMaxChanges) {
            m_changes.removeFirst();
        }
    } else {
        snapshot.baseline = true;
    }

    m_baseline = current;
    m_primed = true;
    snapshot.changes = m_changes;

    emit fimReady(snapshot);
}
