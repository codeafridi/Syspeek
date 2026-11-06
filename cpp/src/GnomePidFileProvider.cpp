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

namespace {

int readPidFile(const QString &path, SystemBackend &backend) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }

    bool ok = false;
    const int pid = QString::fromUtf8(file.readAll()).trimmed().toInt(&ok);
    if (!ok || pid <= 0) {
        return -1;
    }
    return backend.exists(pid) ? pid : -1;
}

}

int GnomePidFileProvider::focusedPid() const {
    if (m_path.isEmpty()) {
        return -1;
    }

    const int pid = readPidFile(m_path, m_backend);
    if (pid > 0) {
        return pid;
    }
    // The focus bridge is a separately installed GNOME extension, so an
    // upgrade has the new app running against the previous release's bridge
    // until the extension is reinstalled. The old bridge writes the same pid
    // under the previous name; reading it keeps focus working across the
    // rename instead of reporting no-focus on every upgraded machine.
    QString old = m_path;
    const int slash = old.lastIndexOf(QLatin1Char('/'));
    old = old.left(slash + 1) + QStringLiteral("syspeek-active-pid");
    if (old == m_path) {
        return -1;
    }
    return readPidFile(old, m_backend);
}

QString GnomePidFileProvider::defaultPath() {
    const QString runtime =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty()) {
        return QString();
    }
    return runtime + QStringLiteral("/crowsnest-active-pid");
}
