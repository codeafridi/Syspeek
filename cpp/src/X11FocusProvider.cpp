#include "X11FocusProvider.h"

#if defined(Q_OS_LINUX) && defined(SYSPEEK_X11)

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <climits>

namespace {

// wmctrl 1.07 main.c line 1429 pattern: read a window/root property of
// the expected atom type into an unsigned long; false on any mismatch.
// XGetWindowProperty returns format-32 data as unsigned long words.
bool readWindowProperty(_XDisplay *display, Window window,
                        const char *atomName, Atom expectedType,
                        unsigned long *out) {
    Atom atom = XInternAtom(display, atomName, False);
    if (atom == None) {
        return false;
    }
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nitems = 0;
    unsigned long bytesAfter = 0;
    unsigned char *data = nullptr;
    if (XGetWindowProperty(display, window, atom, 0, 8, False, expectedType,
                           &actualType, &actualFormat, &nitems,
                           &bytesAfter, &data) != Success) {
        return false;
    }
    if (actualType != expectedType || nitems < 1) {
        XFree(data);
        return false;
    }
    const unsigned long *values =
        reinterpret_cast<const unsigned long *>(data);
    *out = values[0];
    XFree(data);
    return true;
}

} // namespace

X11FocusProvider::X11FocusProvider(const QString &displayOverride) {
    if (displayOverride.isEmpty()) {
        m_display = XOpenDisplay(nullptr);
        return;
    }
    const QByteArray name = displayOverride.toLocal8Bit();
    m_display = XOpenDisplay(name.constData());
}

X11FocusProvider::~X11FocusProvider() {
    if (m_display != nullptr) {
        XCloseDisplay(m_display);
    }
}

QString X11FocusProvider::name() const {
    return QStringLiteral("x11-native");
}

int X11FocusProvider::focusedPid() const {
    if (m_display == nullptr) {
        return -1;
    }
    _XDisplay *display = m_display;
    const Window root = DefaultRootWindow(display);

    // EWMH active window first (the window manager's focused window);
    // fall back to the keyboard focus (XGetInputFocus).
    Window focused = None;
    unsigned long activeWindow = 0;
    if (readWindowProperty(display, root, "_NET_ACTIVE_WINDOW", XA_WINDOW,
                           &activeWindow)) {
        focused = static_cast<Window>(activeWindow);
    } else {
        int revertTo = RevertToNone;
        XGetInputFocus(display, &focused, &revertTo);
    }
    if (focused == None || focused == PointerRoot) {
        return -1;
    }

    // wmctrl main.c lines 521/1323: the owning PID rides the
    // _NET_WM_PID (XA_CARDINAL) property on the client window.
    unsigned long pid = 0;
    if (!readWindowProperty(display, focused, "_NET_WM_PID", XA_CARDINAL,
                            &pid)) {
        return -1;
    }
    if (pid == 0 || pid > static_cast<unsigned long>(INT_MAX)) {
        return -1;
    }
    return static_cast<int>(pid);
}

#else

namespace {
// Non-Linux translation units keep the TU non-empty; linux builds only
// compile against X11 headers.
[[maybe_unused]] constexpr char kX11ProviderNotCompiled = 0;
} // namespace

#endif
