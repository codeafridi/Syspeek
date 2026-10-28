#pragma once

#include <QByteArray>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

enum class OverlaySession { Unknown, X11, Wayland, MacOS, Windows };

struct OverlayState {
    bool clickThrough = false;
    bool clickThroughCapable = false;
    bool pinnedAbove = false;
    bool sticky = false;
    bool hidden = false;
    bool dockWindow = false;
    QRect geometry;
    QRect workArea;
    QString wmName;
    QString note;
};

QString overlaySessionName(OverlaySession session);

OverlaySession detectOverlaySession(const QString &platformName,
                                    const QString &waylandDisplay,
                                    const QString &x11Display);

QStringList parseNetWmStateAtoms(const QByteArray &property);

bool overlayStateHas(const QStringList &atoms, const QString &atom);

bool overlayAboveConfirmed(const QStringList &atoms);

bool overlaySticky(const QStringList &atoms);

bool overlayHidden(const QStringList &atoms);

bool overlayDockWindow(const QStringList &windowTypes);

QRect restoreOverlayGeometry(const QRect &saved, const QList<QRect> &screens,
                             const QSize &minimum);

QRect clampOverlayGeometry(const QRect &rect, const QRect &available,
                           const QSize &minimum);

QString overlayClickThroughNote(OverlaySession session, bool capable);

QString overlaySummaryText(OverlaySession session, const OverlayState &state);

QString overlayGeometryText(const QRect &rect);

bool overlayGeometryUsable(const QRect &rect);
