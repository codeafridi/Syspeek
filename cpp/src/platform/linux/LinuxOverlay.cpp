#include "LinuxOverlay.h"

#if defined(Q_OS_LINUX) && defined(SYSPEEK_OVERLAY_X11)

// Qt first: X11/Xlib.h defines Status/Bool/None, which QtCore rejects when it
// is pulled in afterwards (qdatastream.h:19 static guard).
#include <QLibrary>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <vector>

namespace {

// X11 shape extension constants (X11/extensions/shape.h). The dev package is
// not guaranteed on a build host (this box has no libxext-dev), so the
// library is resolved at runtime instead of link time: the binary keeps
// building everywhere and the capability is probed on the live display.
constexpr int kShapeInput = 2;
constexpr int kShapeSet = 0;
constexpr int kUnsorted = 0;

using ShapeCombineRectanglesFn = void (*)(_XDisplay *, Window, int, int, int,
                                          XRectangle *, int, int, int);
using ShapeCombineMaskFn = void (*)(_XDisplay *, Window, int, int, int, Pixmap,
                                    int);

bool g_shapeError = false;

int shapeErrorHandler(_XDisplay *, XErrorEvent *) {
    g_shapeError = true;
    return 0;
}

struct ShapeLibrary {
    QLibrary library{ QStringLiteral("Xext"), QStringLiteral("6") };
    ShapeCombineRectanglesFn combineRectangles = nullptr;
    ShapeCombineMaskFn combineMask = nullptr;
    bool probed = false;

    bool load() {
        if (!probed) {
            probed = true;
            if (library.load()) {
                combineRectangles =
                    reinterpret_cast<ShapeCombineRectanglesFn>(
                        reinterpret_cast<void *>(
                            library.resolve("XShapeCombineRectangles")));
                combineMask = reinterpret_cast<ShapeCombineMaskFn>(
                    reinterpret_cast<void *>(
                        library.resolve("XShapeCombineMask")));
            }
        }
        return combineRectangles != nullptr && combineMask != nullptr;
    }
};

ShapeLibrary &shapeLibrary() {
    static ShapeLibrary instance;
    return instance;
}

struct DisplayHandle {
    _XDisplay *display = nullptr;
    explicit DisplayHandle() {
        display = XOpenDisplay(nullptr);
    }
    ~DisplayHandle() {
        if (display != nullptr) {
            XCloseDisplay(display);
        }
    }
    bool ok() const { return display != nullptr; }
};

bool readProperty(_XDisplay *display, Window window, const char *name,
                  Atom expectedType, Atom *typeOut, int *formatOut,
                  unsigned long *countOut, unsigned char **dataOut) {
    const Atom atom = XInternAtom(display, name, True);
    if (atom == None) {
        return false;
    }
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    unsigned char *data = nullptr;
    if (XGetWindowProperty(display, window, atom, 0, 4096, False, expectedType,
                           &actualType, &actualFormat, &count, &after,
                           &data) != Success) {
        return false;
    }
    if (actualType == None || data == nullptr) {
        if (data != nullptr) {
            XFree(data);
        }
        return false;
    }
    if (typeOut != nullptr) {
        *typeOut = actualType;
    }
    if (formatOut != nullptr) {
        *formatOut = actualFormat;
    }
    if (countOut != nullptr) {
        *countOut = count;
    }
    if (dataOut != nullptr) {
        *dataOut = data;
    } else {
        XFree(data);
    }
    return true;
}

QString readTextProperty(_XDisplay *display, Window window, const char *name) {
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned char *data = nullptr;
    if (!readProperty(display, window, name, AnyPropertyType, &type, &format,
                      &count, &data)) {
        return QString();
    }
    QString text;
    if (format == 8) {
        text = QString::fromUtf8(reinterpret_cast<const char *>(data),
                                 int(count));
    }
    XFree(data);
    return text.trimmed();
}

bool readWindowId(_XDisplay *display, Window window, const char *name,
                  Window *out) {
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned char *data = nullptr;
    if (!readProperty(display, window, name, XA_WINDOW, &type, &format, &count,
                      &data)) {
        return false;
    }
    if (format != 32 || count < 1) {
        XFree(data);
        return false;
    }
    const unsigned long *values =
        reinterpret_cast<const unsigned long *>(data);
    *out = Window(values[0]);
    XFree(data);
    return true;
}

QStringList readAtomList(_XDisplay *display, Window window, const char *name) {
    QStringList atoms;
    if (window == 0) {
        return atoms;
    }
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned char *data = nullptr;
    if (!readProperty(display, window, name, XA_ATOM, &type, &format, &count,
                      &data)) {
        return atoms;
    }
    if (format != 32) {
        XFree(data);
        return atoms;
    }
    const unsigned long *values =
        reinterpret_cast<const unsigned long *>(data);
    for (unsigned long index = 0; index < count; ++index) {
        char *atomName = XGetAtomName(display, Atom(values[index]));
        if (atomName != nullptr) {
            const QString text = QString::fromUtf8(atomName).trimmed();
            if (!text.isEmpty() && !atoms.contains(text)) {
                atoms.append(text);
            }
            XFree(atomName);
        }
    }
    XFree(data);
    return atoms;
}

QString windowManagerName(_XDisplay *display, Window root) {
    Window supporting = 0;
    if (!readWindowId(display, root, "_NET_SUPPORTING_WM_CHECK", &supporting)
        || supporting == 0) {
        return QString();
    }
    QString name = readTextProperty(display, supporting, "_NET_WM_NAME");
    if (name.isEmpty()) {
        name = readTextProperty(display, supporting, "WM_NAME");
    }
    return name;
}

QRect desktopWorkArea(_XDisplay *display, Window root) {
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned char *data = nullptr;
    if (!readProperty(display, root, "_NET_WORKAREA", XA_CARDINAL, &type,
                      &format, &count, &data)) {
        return QRect();
    }
    if (format != 32 || count < 4) {
        XFree(data);
        return QRect();
    }
    const unsigned long *values =
        reinterpret_cast<const unsigned long *>(data);
    const QRect area{QPoint(int(values[0]), int(values[1])),
                     QSize(int(values[2]), int(values[3]))};
    XFree(data);
    return area.isValid() ? area : QRect();
}

bool sendClientMessage(_XDisplay *display, Window window, const char *message,
                       long data0, long data1, long data2, long data3,
                       long data4) {
    const Atom messageAtom = XInternAtom(display, message, False);
    if (messageAtom == None) {
        return false;
    }
    XEvent event = {};
    event.xclient.type = ClientMessage;
    event.xclient.serial = 0;
    event.xclient.send_event = True;
    event.xclient.display = display;
    event.xclient.window = window;
    event.xclient.message_type = messageAtom;
    event.xclient.format = 32;
    event.xclient.data.l[0] = data0;
    event.xclient.data.l[1] = data1;
    event.xclient.data.l[2] = data2;
    event.xclient.data.l[3] = data3;
    event.xclient.data.l[4] = data4;
    const long masks = SubstructureRedirectMask | SubstructureNotifyMask;
    XSendEvent(display, DefaultRootWindow(display), False, masks, &event);
    XFlush(display);
    return true;
}

}  // namespace

namespace linuxoverlay {

X11Probe probe() {
    X11Probe out;
    DisplayHandle handle;
    if (!handle.ok()) {
        return out;
    }
    out.display = true;
    const Window root = DefaultRootWindow(handle.display);
    out.wmName = windowManagerName(handle.display, root);
    out.workArea = desktopWorkArea(handle.display, root);
    out.shapeAvailable = shapeLibrary().load();
    // wmctrl main.c line 795 reads the root _NET_SUPPORTED list before it
    // sends a state request, so the caller can state an honest gate instead
    // of sending messages the wm will ignore.
    out.stickySupported = readAtomList(handle.display, root, "_NET_SUPPORTED")
                              .contains(QStringLiteral("_NET_WM_STATE_STICKY"));
    return out;
}

QStringList windowStates(quintptr winId) {
    DisplayHandle handle;
    if (!handle.ok() || winId == 0) {
        return QStringList();
    }
    return readAtomList(handle.display, Window(winId), "_NET_WM_STATE");
}

QStringList windowTypes(quintptr winId) {
    DisplayHandle handle;
    if (!handle.ok() || winId == 0) {
        return QStringList();
    }
    return readAtomList(handle.display, Window(winId), "_NET_WM_WINDOW_TYPE");
}

bool setClickThrough(quintptr winId, bool on, const QRect &keepRect,
                     QString *note) {
    if (winId == 0) {
        if (note != nullptr) {
            *note = QStringLiteral("x11: the hud window is not mapped yet");
        }
        return false;
    }
    if (!shapeLibrary().load()) {
        if (note != nullptr) {
            *note = QStringLiteral(
                "x11: libXext is not available - click-through disabled");
        }
        return false;
    }

    DisplayHandle handle;
    if (!handle.ok()) {
        if (note != nullptr) {
            *note = QStringLiteral("x11: display unavailable");
        }
        return false;
    }

    g_shapeError = false;
    const XErrorHandler previous = XSetErrorHandler(shapeErrorHandler);

    if (on) {
        // ShapeSet on ShapeInput with the kept rectangle replaces the whole
        // input region: the window receives events only inside keepRect, so
        // every other click lands on whatever is below the HUD.
        std::vector<XRectangle> rects;
        if (!keepRect.isEmpty()) {
            XRectangle rect = {};
            rect.x = short(keepRect.left());
            rect.y = short(keepRect.top());
            rect.width = ushort(keepRect.width());
            rect.height = ushort(keepRect.height());
            rects.push_back(rect);
        }
        shapeLibrary().combineRectangles(handle.display, Window(winId),
                                         kShapeInput, 0, 0,
                                         rects.empty() ? nullptr : rects.data(),
                                         int(rects.size()), kShapeSet,
                                         kUnsorted);
    } else {
        // None restores the default: no input shape, so the bounding region
        // (the full window) takes clicks again.
        shapeLibrary().combineMask(handle.display, Window(winId), kShapeInput,
                                   0, 0, None, kShapeSet);
    }

    XSync(handle.display, False);
    XSetErrorHandler(previous);

    if (g_shapeError) {
        if (note != nullptr) {
            *note = QStringLiteral("x11: the window manager rejected the shape "
                                   "request (X error)");
        }
        return false;
    }
    return true;
}

bool requestAbove(quintptr winId, QString *note) {
    if (winId == 0) {
        if (note != nullptr) {
            *note = QStringLiteral("x11: the hud window is not mapped yet");
        }
        return false;
    }

    DisplayHandle handle;
    if (!handle.ok()) {
        if (note != nullptr) {
            *note = QStringLiteral("x11: display unavailable");
        }
        return false;
    }

    const Atom above =
        XInternAtom(handle.display, "_NET_WM_STATE_ABOVE", False);
    if (above == None) {
        if (note != nullptr) {
            *note = QStringLiteral(
                "x11: the window manager does not advertise "
                "_NET_WM_STATE_ABOVE");
        }
        return false;
    }

    // _NET_WM_STATE_ADD (wmctrl main.c line 40) with the ABOVE atom, from a
    // normal application source indication (1).
    if (!sendClientMessage(handle.display, Window(winId), "_NET_WM_STATE", 1,
                           long(above), 0, 1, 0)) {
        if (note != nullptr) {
            *note = QStringLiteral("x11: could not send the _NET_WM_STATE "
                                   "client message");
        }
        return false;
    }

    XSync(handle.display, False);
    return true;
}

bool requestSticky(quintptr winId, QString *note) {
    if (winId == 0) {
        if (note != nullptr) {
            *note = QStringLiteral("x11: the hud window is not mapped yet");
        }
        return false;
    }

    DisplayHandle handle;
    if (!handle.ok()) {
        if (note != nullptr) {
            *note = QStringLiteral("x11: display unavailable");
        }
        return false;
    }

    const Atom sticky =
        XInternAtom(handle.display, "_NET_WM_STATE_STICKY", False);
    if (sticky == None) {
        if (note != nullptr) {
            *note = QStringLiteral(
                "x11: the window manager does not support sticky windows");
        }
        return false;
    }

    // wmctrl `-b add,sticky`: _NET_WM_STATE_REMOVE/ADD/TOGGLE 0/1/2 plus the
    // _NET_WM_STATE atom, source indication 1 for a normal application.
    if (!sendClientMessage(handle.display, Window(winId), "_NET_WM_STATE", 1,
                           long(sticky), 0, 1, 0)) {
        if (note != nullptr) {
            *note = QStringLiteral("x11: could not send the _NET_WM_STATE "
                                   "client message");
        }
        return false;
    }

    // Some window managers only move a window to "every desktop" through the
    // _NET_WM_DESKTOP message. Measured on this box: mutter/GNOME Shell
    // ignored the state request above and answered the desktop message by
    // setting _NET_WM_DESKTOP=4294967295 and adding _NET_WM_STATE_STICKY.
    sendClientMessage(handle.display, Window(winId), "_NET_WM_DESKTOP",
                      long(0xFFFFFFFF), 1, 0, 0, 0);

    XSync(handle.display, False);
    return true;
}

}  // namespace linuxoverlay

#endif
