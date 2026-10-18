#include "platform/win/WinFocusProvider.h"

#if defined(Q_OS_WIN)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

QString WinFocusProvider::name() const {
    return QStringLiteral("windows-getforegroundwindow");
}

int WinFocusProvider::focusedPid() const {
    const HWND window = ::GetForegroundWindow();
    if (window == nullptr) {
        return -1;
    }
    DWORD pid = 0;
    if (::GetWindowThreadProcessId(window, &pid) == 0 || pid == 0) {
        return -1;
    }
    return static_cast<int>(pid);
}

#endif
