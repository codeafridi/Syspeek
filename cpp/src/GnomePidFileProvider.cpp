#include "GnomePidFileProvider.h"

#include <QFile>
#include <QProcessEnvironment>

GnomePidFileProvider::GnomePidFileProvider(QString path, SystemBackend &backend)
    : m_backend(backend)
    , m_path(std::move(path)) {
}

QString GnomePidFileProvider::name() const {
    return QStringLiteral("gnome-pidfile");
}

int GnomePidFileProvider::readPidFile(const QString &path) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }

    bool ok = false;
    const int pid = QString::fromUtf8(file.readAll()).trimmed().toInt(&ok);
    if (!ok || pid <= 0) {
        return -1;
    }

    if (!m_backend.exists(pid)) {
        return -1;
    }

    return pid;
}

int GnomePidFileProvider::focusedPid() const {
    if (m_path.isEmpty()) {
        return -1;
    }

    const int pid = readPidFile(m_path);
    if (pid > 0) {
        return pid;
    }

    // Legacy bridge filename from the pre-rename branding: sessions that
    // installed the old extension still write under the old name until
    // the new one loads.
    const int slash = m_path.lastIndexOf(QLatin1Char('/'));
    if (slash < 0) {
        return -1;
    }
    return readPidFile(m_path.left(slash + 1)
                       + QStringLiteral("syspeek-active-pid"));
}

QString GnomePidFileProvider::defaultPath() {
    const QString runtime =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty()) {
        return QString();
    }
    return runtime + QStringLiteral("/crowsnest-active-pid");
}
