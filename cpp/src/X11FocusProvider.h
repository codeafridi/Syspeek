#pragma once

#include "FocusedWindowProvider.h"

#include <QString>

// X11 focus provider (M58): reads the focused window and its owning PID
// directly from libX11 - no external tool, so it covers every X11
// desktop (LXQt/XFCE/MATE/Cinnamon/Deepin/i3/...). The previous
// provider shelled xdotool, which most desktops do not ship.
// Mechanism grounded on upstream wmctrl 1.07 (Debian pristine tarball,
// /tmp/opencode/m58/wmctrl-1.07/main.c):
//   - XGetWindowProperty helper (main.c line 1429)
//   - _NET_ACTIVE_WINDOW read   (main.c line 1518)
//   - _NET_WM_PID read          (main.c lines 521/1323)
//
// When built without X11 dev headers (SYSPEEK_X11 undefined - e.g.
// macOS/Windows TUs or a bare Linux box), the inline stub keeps every
// consumer TU linking: the provider honestly reports no focus and the
// factory picks the null provider instead.
#if defined(SYSPEEK_X11)

struct _XDisplay;

class X11FocusProvider : public FocusedWindowProvider {
public:
    explicit X11FocusProvider(const QString &displayOverride = QString());
    ~X11FocusProvider() override;

    QString name() const override;
    int focusedPid() const override;

private:
    _XDisplay *m_display = nullptr;
};

#else

class X11FocusProvider : public FocusedWindowProvider {
public:
    explicit X11FocusProvider(const QString &displayOverride = QString()) {
        Q_UNUSED(displayOverride);
    }

    QString name() const override {
        return QStringLiteral("x11-native");
    }

    int focusedPid() const override {
        return -1;
    }
};

#endif
