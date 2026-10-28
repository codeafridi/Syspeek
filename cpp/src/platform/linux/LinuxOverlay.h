#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#if defined(SYSPEEK_X11)
#define SYSPEEK_OVERLAY_X11 1
#endif

namespace linuxoverlay {

struct X11Probe {
    bool display = false;
    QString wmName;
    QRect workArea;
    bool shapeAvailable = false;
    bool stickySupported = false;
};

#if defined(SYSPEEK_OVERLAY_X11)

X11Probe probe();

QStringList windowStates(quintptr winId);

QStringList windowTypes(quintptr winId);

bool setClickThrough(quintptr winId, bool on, const QRect &keepRect,
                     QString *note);

bool requestAbove(quintptr winId, QString *note);

bool requestSticky(quintptr winId, QString *note);

#else

inline X11Probe probe() { return X11Probe{}; }

inline QStringList windowStates(quintptr) { return QStringList(); }

inline QStringList windowTypes(quintptr) { return QStringList(); }

inline bool setClickThrough(quintptr, bool, const QRect &, QString *note) {
    if (note != nullptr) {
        *note = QStringLiteral("x11: built without X11 support");
    }
    return false;
}

inline bool requestAbove(quintptr, QString *note) {
    if (note != nullptr) {
        *note = QStringLiteral("x11: built without X11 support");
    }
    return false;
}

inline bool requestSticky(quintptr, QString *note) {
    if (note != nullptr) {
        *note = QStringLiteral("x11: built without X11 support");
    }
    return false;
}

#endif

}  // namespace linuxoverlay
