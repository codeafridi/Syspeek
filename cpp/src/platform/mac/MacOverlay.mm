#include "MacOverlay.h"

#ifdef Q_OS_MACOS

#import <AppKit/AppKit.h>

#import <Foundation/Foundation.h>

#include <QGuiApplication>
#include <QString>

bool applyMacOverlay(QWidget *widget) {
    if (widget == nullptr || !widget->testAttribute(Qt::WA_WState_ExplicitShowHide)) {
        return false;
    }
    // winId() is an NSView pointer only under the cocoa platform plugin. The
    // offscreen/minimal plugins hand back a synthetic id, and sending that
    // address an Objective-C message segfaults (hit by the headless test
    // suite the first time it showed a window on the mac runner).
    if (QGuiApplication::platformName() != QStringLiteral("cocoa")) {
        return false;
    }
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    if (view == nil) {
        return false;
    }
    NSWindow *window = view.window;
    if (window == nil) {
        return false;
    }
    window.level = NSFloatingWindowLevel;
    window.collectionBehavior =
        NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorFullScreenAuxiliary
        | NSWindowCollectionBehaviorStationary;
    window.hidesOnDeactivate = NO;
    return true;
}

#else // Q_OS_MACOS

bool applyMacOverlay(QWidget *widget) {
    Q_UNUSED(widget);
    return false;
}

#endif // Q_OS_MACOS
