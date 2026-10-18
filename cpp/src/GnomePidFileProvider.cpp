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

int GnomePidFileProvider::focusedPid() const {
    if (m_path.isEmpty()) {
        return -1;
    }

    QFile file(m_path);
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

QString GnomePidFileProvider::defaultPath() {
    const QString runtime =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty()) {
        return QString();
    }
    return runtime + QStringLiteral("/syspeek-active-pid");
}
