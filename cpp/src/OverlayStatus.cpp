#include "OverlayStatus.h"

#include <algorithm>

QString overlaySessionName(OverlaySession session) {
    switch (session) {
    case OverlaySession::X11:
        return QStringLiteral("x11");
    case OverlaySession::Wayland:
        return QStringLiteral("wayland");
    case OverlaySession::MacOS:
        return QStringLiteral("macos");
    case OverlaySession::Windows:
        return QStringLiteral("windows");
    case OverlaySession::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

OverlaySession detectOverlaySession(const QString &platformName,
                                    const QString &waylandDisplay,
                                    const QString &x11Display) {
    if (platformName.contains(QStringLiteral("wayland"), Qt::CaseInsensitive)
        || (platformName.isEmpty() && !waylandDisplay.isEmpty()
            && x11Display.isEmpty())) {
        return OverlaySession::Wayland;
    }
    if (platformName.contains(QStringLiteral("cocoa"), Qt::CaseInsensitive)
        || platformName.contains(QStringLiteral("macos"),
                                 Qt::CaseInsensitive)) {
        return OverlaySession::MacOS;
    }
    if (platformName.contains(QStringLiteral("windows"),
                              Qt::CaseInsensitive)) {
        return OverlaySession::Windows;
    }
    if (platformName.startsWith(QStringLiteral("xcb"), Qt::CaseInsensitive)
        || platformName.contains(QStringLiteral("x11"), Qt::CaseInsensitive)
        || (!x11Display.isEmpty() && waylandDisplay.isEmpty())) {
        return OverlaySession::X11;
    }
    if (platformName.isEmpty()) {
        if (!waylandDisplay.isEmpty()) {
            return OverlaySession::Wayland;
        }
        if (!x11Display.isEmpty()) {
            return OverlaySession::X11;
        }
    }
    return OverlaySession::Unknown;
}

QStringList parseNetWmStateAtoms(const QByteArray &property) {
    QStringList atoms;
    const QList<QByteArray> parts = property.split('\0');
    for (const QByteArray &part : parts) {
        const QString atom = QString::fromUtf8(part).trimmed();
        if (!atom.isEmpty() && !atoms.contains(atom)) {
            atoms.append(atom);
        }
    }
    return atoms;
}

bool overlayStateHas(const QStringList &atoms, const QString &atom) {
    return atoms.contains(atom);
}

bool overlayAboveConfirmed(const QStringList &atoms) {
    return overlayStateHas(atoms, QStringLiteral("_NET_WM_STATE_ABOVE"));
}

bool overlaySticky(const QStringList &atoms) {
    return overlayStateHas(atoms, QStringLiteral("_NET_WM_STATE_STICKY"));
}

bool overlayHidden(const QStringList &atoms) {
    return overlayStateHas(atoms, QStringLiteral("_NET_WM_STATE_HIDDEN"));
}

bool overlayDockWindow(const QStringList &windowTypes) {
    return overlayStateHas(windowTypes,
                           QStringLiteral("_NET_WM_WINDOW_TYPE_DOCK"));
}

bool overlayGeometryUsable(const QRect &rect) {
    return rect.width() > 0 && rect.height() > 0;
}

QRect clampOverlayGeometry(const QRect &rect, const QRect &available,
                           const QSize &minimum) {
    if (!overlayGeometryUsable(rect) || !available.isValid()) {
        return QRect();
    }

    const int minWidth = std::min(std::max(minimum.width(), 1), available.width());
    const int minHeight =
        std::min(std::max(minimum.height(), 1), available.height());

    int width = rect.width();
    int height = rect.height();
    width = std::clamp(width, minWidth, available.width());
    height = std::clamp(height, minHeight, available.height());

    const int maxX = available.left() + available.width() - width;
    const int maxY = available.top() + available.height() - height;
    const int x = std::clamp(rect.left(), available.left(), maxX);
    const int y = std::clamp(rect.top(), available.top(), maxY);
    return QRect(QPoint(x, y), QSize(width, height));
}

QRect restoreOverlayGeometry(const QRect &saved, const QList<QRect> &screens,
                             const QSize &minimum) {
    if (!overlayGeometryUsable(saved) || screens.isEmpty()) {
        return QRect();
    }

    QRect best;
    int bestArea = 0;
    for (const QRect &screen : screens) {
        if (!screen.isValid()) {
            continue;
        }
        const QRect overlap = screen.intersected(saved);
        const int area = overlayGeometryUsable(overlap)
                             ? overlap.width() * overlap.height()
                             : 0;
        if (area > bestArea) {
            bestArea = area;
            best = screen;
        }
    }

    if (bestArea == 0) {
        const QRect primary = screens.first();
        if (!primary.isValid()) {
            return QRect();
        }
        QRect placed(QPoint(primary.left() + primary.width() / 2,
                            primary.top() + primary.height() / 2),
                     saved.size());
        return clampOverlayGeometry(placed, primary, minimum);
    }

    return clampOverlayGeometry(saved, best, minimum);
}

QString overlayClickThroughNote(OverlaySession session, bool capable) {
    if (capable) {
        return QStringLiteral(
            "click-through: the HUD passes every click to the window below");
    }
    switch (session) {
    case OverlaySession::Wayland:
        return QStringLiteral(
            "click-through: not available on Wayland - the compositor owns "
            "input routing and wlr-layer-shell input regions are not exposed");
    case OverlaySession::MacOS:
        return QStringLiteral(
            "click-through: not exposed on macOS in this build - the HUD "
            "stays clickable");
    case OverlaySession::Windows:
        return QStringLiteral(
            "click-through: not exposed on Windows in this build - the HUD "
            "stays clickable");
    case OverlaySession::X11:
        return QStringLiteral(
            "click-through: needs the X Shape extension (libXext) - not "
            "present on this display");
    case OverlaySession::Unknown:
        break;
    }
    return QStringLiteral("click-through: no window server detected");
}

QString overlaySummaryText(OverlaySession session, const OverlayState &state) {
    if (session == OverlaySession::Unknown) {
        return QStringLiteral("overlay: no window server detected");
    }

    QStringList parts;
    if (!state.wmName.isEmpty()) {
        parts.append(QStringLiteral("wm %1").arg(state.wmName));
    } else {
        parts.append(QStringLiteral("session %1").arg(overlaySessionName(session)));
    }
    if (session == OverlaySession::X11) {
        parts.append(state.pinnedAbove ? QStringLiteral("pinned above")
                                       : QStringLiteral("pin not confirmed"));
    } else {
        parts.append(QStringLiteral("pinned above"));
    }
    if (state.dockWindow) {
        parts.append(QStringLiteral("dock window"));
    }
    if (session == OverlaySession::X11) {
        parts.append(state.sticky ? QStringLiteral("follows every desktop")
                                  : QStringLiteral("desktop-local"));
    }
    if (state.hidden) {
        parts.append(QStringLiteral("hidden by the wm"));
    }
    parts.append(state.clickThrough ? QStringLiteral("click-through on")
                                    : QStringLiteral("click-through off"));
    if (overlayGeometryUsable(state.geometry)) {
        parts.append(overlayGeometryText(state.geometry));
    }
    return QStringLiteral("overlay: %1").arg(parts.join(QStringLiteral(" - ")));
}

QString overlayGeometryText(const QRect &rect) {
    if (!overlayGeometryUsable(rect)) {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1x%2 at %3,%4")
        .arg(rect.width())
        .arg(rect.height())
        .arg(rect.left())
        .arg(rect.top());
}
