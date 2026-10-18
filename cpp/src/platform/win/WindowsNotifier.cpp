#include "WindowsNotifier.h"

#include <QIcon>
#include <QPixmap>
#include <QtGlobal>

WindowsNotifier::WindowsNotifier() {
    m_tray = new QSystemTrayIcon();
    QPixmap icon(16, 16);
    icon.fill(Qt::transparent);
    m_tray->setIcon(QIcon(icon));
}

WindowsNotifier::~WindowsNotifier() {
    m_tray->hide();
    delete m_tray;
}

bool WindowsNotifier::send(const Alert &alert) {
    if (m_tray->isVisible()) {
        m_tray->showMessage(alert.title, alert.detail,
                            QSystemTrayIcon::Critical, 5000);
        return true;
    }
    m_tray->show();
    m_tray->showMessage(alert.title, alert.detail,
                        QSystemTrayIcon::Critical, 5000);
    return true;
}
