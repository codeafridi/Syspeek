#include "KdeFocusService.h"

#include "KdeFocusState.h"

#include <QDateTime>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#endif

KdeFocusService::KdeFocusService(QObject *parent)
    : QObject(parent) {
}

bool KdeFocusService::registerService() {
#ifdef Q_OS_LINUX
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }

    if (!bus.registerService(QStringLiteral("org.syspeek.Focus"))) {
        return false;
    }

    return bus.registerObject(QStringLiteral("/Focus"),
                              QStringLiteral("org.syspeek.Focus"),
                              this, QDBusConnection::ExportAllSlots);
#else
    return false;
#endif
}

void KdeFocusService::setFocusedPid(int pid) {
    kdeFocusState().pid.store(pid > 0 ? pid : -1);
    kdeFocusState().stampMs.store(QDateTime::currentMSecsSinceEpoch());
}
