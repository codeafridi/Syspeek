#pragma once

#include "../../Notifier.h"

#include <QSystemTrayIcon>

// Tray notifications via Shell_NotifyIconW. Grounded on a shipped
// tray-notification user: voidtools Everything 1.4.1.1032 x64 imports
// Shell_NotifyIconW (SHELL32.dll block) and ships a tray icon with
// notification strings ("Show tray &icon", taskbar_notification_title_...).
class WindowsNotifier : public Notifier {
public:
    WindowsNotifier();
    ~WindowsNotifier() override;
    QString name() const override { return QStringLiteral("tray"); }
    bool send(const Alert &alert) override;

private:
    QSystemTrayIcon *m_tray = nullptr;
};
