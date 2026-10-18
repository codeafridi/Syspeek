#include "Notifier.h"

#ifdef Q_OS_LINUX

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QStringList>
#include <QVariantMap>

bool DesktopNotifier::send(const Alert &alert) {
    QDBusInterface iface(QStringLiteral("org.freedesktop.Notifications"),
                         QStringLiteral("/org/freedesktop/Notifications"),
                         QStringLiteral("org.freedesktop.Notifications"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return false;
    }

    QVariantMap hints;
    hints.insert(QStringLiteral("urgency"),
                 alert.severity == QStringLiteral("critical") ? 2 : 1);

    const QDBusMessage reply =
        iface.call(QStringLiteral("Notify"), QStringLiteral("Syspeek"),
                   static_cast<uint>(0), QStringLiteral(""), alert.title,
                   alert.detail, QStringList(), hints, 8000);
    return reply.type() != QDBusMessage::ErrorMessage;
}

#else

bool DesktopNotifier::send(const Alert &alert) {
    Q_UNUSED(alert);
    return false;
}

#endif
