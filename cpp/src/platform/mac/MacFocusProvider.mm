#include "MacFocusProvider.h"

#ifdef Q_OS_MACOS

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#import <Foundation/Foundation.h>

MacFocusProvider::MacFocusProvider() {
}

MacFocusProvider::~MacFocusProvider() = default;

QString MacFocusProvider::name() const {
    return QStringLiteral("mac-nsworkspace");
}

int MacFocusProvider::frontmostWorkspacePid() const {
    @autoreleasepool {
        NSWorkspace *ws = [NSWorkspace sharedWorkspace];
        NSRunningApplication *app = ws.frontmostApplication;
        if (app == nil) {
            return -1;
        }
        const pid_t pid = app.processIdentifier;
        return pid > 0 ? static_cast<int>(pid) : -1;
    }
}

int MacFocusProvider::topmostWindowPid() const {
    @autoreleasepool {
        CFArrayRef list = CGWindowListCopyWindowInfo(
            kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
            kCGNullWindowID);
        if (list == nullptr) {
            return -1;
        }
        int pid = -1;
        const CFIndex count = CFArrayGetCount(list);
        for (CFIndex i = 0; i < count; ++i) {
            CFDictionaryRef info = static_cast<CFDictionaryRef>(
                CFArrayGetValueAtIndex(list, i));
            if (info == nullptr) {
                continue;
            }
            CFNumberRef ownerPid = static_cast<CFNumberRef>(
                CFDictionaryGetValue(info, kCGWindowOwnerPID));
            if (ownerPid == nullptr) {
                continue;
            }
            int value = -1;
            if (CFNumberGetValue(ownerPid, kCFNumberIntType, &value)
                && value > 0) {
                pid = value;
                break;
            }
        }
        CFRelease(list);
        return pid;
    }
}

int MacFocusProvider::focusedPid() const {
    int pid = frontmostWorkspacePid();
    if (pid <= 0) {
        pid = topmostWindowPid();
    }
    return pid;
}

#endif // Q_OS_MACOS
