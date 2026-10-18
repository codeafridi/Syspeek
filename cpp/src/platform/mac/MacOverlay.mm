#include "MacOverlay.h"

#ifdef Q_OS_MACOS

#import <AppKit/AppKit.h>

#import <Foundation/Foundation.h>

bool applyMacOverlay(QWidget *widget) {
    if (widget == nullptr || !widget->testAttribute(Qt::WA_WState_ExplicitShowHide)) {
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
