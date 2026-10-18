#pragma once

#include "Config.h"

#include <QString>

#include <memory>

class FocusedWindowProvider;

enum class FocusProviderKind {
    Gnome,
    Kde,
    Hyprland,
    Sway,
    Niri,
    X11,
    Mac,
    Windows,
    Null,
};

struct SessionInfo {
    QString sessionType;
    QString desktop;
    bool hasDisplay = false;
    bool hasHyprlandSignature = false;
    bool hasSwaySocket = false;
    bool hasNiriSocket = false;
};

FocusProviderKind selectProviderKind(const SessionInfo &info);

std::unique_ptr<FocusedWindowProvider> createFocusProvider(
    const Config &config = Config{});

QString focusProviderKindName(FocusProviderKind kind);
