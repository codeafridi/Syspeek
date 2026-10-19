#include "FocusProviderFactory.h"

#include "CachingFocusProvider.h"
#include "GnomePidFileProvider.h"
#include "HyprctlFocusProvider.h"
#include "KdeFocusProvider.h"
#include "NiriFocusProvider.h"
#include "NullFocusProvider.h"
#include "SwaymsgFocusProvider.h"
#include "X11FocusProvider.h"

#ifdef Q_OS_MACOS
#include "platform/mac/MacFocusProvider.h"
#endif

#ifdef Q_OS_WIN
#include "platform/win/WinFocusProvider.h"
#endif

#include <QProcessEnvironment>

FocusProviderKind selectProviderKind(const SessionInfo &info) {
    const QString desktop = info.desktop.toLower();

    if (info.sessionType.compare(QStringLiteral("wayland"),
                                 Qt::CaseInsensitive)
        == 0) {
        if (desktop.contains(QStringLiteral("gnome"))) {
            return FocusProviderKind::Gnome;
        }
        if (desktop.contains(QStringLiteral("kde"))
            || desktop.contains(QStringLiteral("plasma"))) {
            return FocusProviderKind::Kde;
        }
        if (desktop.contains(QStringLiteral("hyprland"))) {
            return FocusProviderKind::Hyprland;
        }
        if (desktop.contains(QStringLiteral("sway"))) {
            return FocusProviderKind::Sway;
        }
        if (desktop.contains(QStringLiteral("niri"))) {
            return FocusProviderKind::Niri;
        }
        if (info.hasHyprlandSignature) {
            return FocusProviderKind::Hyprland;
        }
        if (info.hasSwaySocket) {
            return FocusProviderKind::Sway;
        }
        if (info.hasNiriSocket) {
            return FocusProviderKind::Niri;
        }
        return FocusProviderKind::Null;
    }

    if (info.hasDisplay) {
        return FocusProviderKind::X11;
    }

    return FocusProviderKind::Null;
}

QString focusProviderKindName(FocusProviderKind kind) {
    switch (kind) {
    case FocusProviderKind::Gnome:
        return QStringLiteral("gnome-pidfile");
    case FocusProviderKind::Kde:
        return QStringLiteral("kde-dbus");
    case FocusProviderKind::Hyprland:
        return QStringLiteral("hyprctl");
    case FocusProviderKind::Sway:
        return QStringLiteral("swaymsg");
    case FocusProviderKind::Niri:
        return QStringLiteral("niri");
    case FocusProviderKind::X11:
        return QStringLiteral("x11-native");
    case FocusProviderKind::Mac:
        return QStringLiteral("mac-nsworkspace");
    case FocusProviderKind::Windows:
        return QStringLiteral("windows-getforegroundwindow");
    case FocusProviderKind::Null:
    default:
        return QStringLiteral("null");
    }
}

std::unique_ptr<FocusedWindowProvider> createFocusProvider(const Config &config) {
#ifdef Q_OS_MACOS
    Q_UNUSED(config);
    return std::make_unique<CachingFocusProvider>(
        std::make_unique<MacFocusProvider>(), config.focusCacheTtlMs);
#elif defined(Q_OS_WIN)
    return std::make_unique<CachingFocusProvider>(
        std::make_unique<WinFocusProvider>(), config.focusCacheTtlMs);
#else
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    SessionInfo info;
    info.sessionType = env.value(QStringLiteral("XDG_SESSION_TYPE"));
    info.desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP"));
    info.hasDisplay = !env.value(QStringLiteral("DISPLAY")).isEmpty();
    info.hasHyprlandSignature =
        !env.value(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE")).isEmpty();
    info.hasSwaySocket = !env.value(QStringLiteral("SWAYSOCK")).isEmpty();
    info.hasNiriSocket = !env.value(QStringLiteral("NIRI_SOCKET")).isEmpty();

    const qint64 ttl = config.focusCacheTtlMs;

    switch (selectProviderKind(info)) {
    case FocusProviderKind::Gnome:
        return std::make_unique<GnomePidFileProvider>();
    case FocusProviderKind::Kde:
        return std::make_unique<KdeFocusProvider>();
    case FocusProviderKind::Hyprland:
        return std::make_unique<CachingFocusProvider>(
            std::make_unique<HyprctlFocusProvider>(), ttl);
    case FocusProviderKind::Sway:
        return std::make_unique<CachingFocusProvider>(
            std::make_unique<SwaymsgFocusProvider>(), ttl);
    case FocusProviderKind::Niri:
        return std::make_unique<CachingFocusProvider>(
            std::make_unique<NiriFocusProvider>(), ttl);
    case FocusProviderKind::X11:
        // M58: native X11 provider (no external tool); covers LXQt/XFCE/
        // MATE/Cinnamon/Deepin/i3 and generic X sessions where xdotool
        // is not installed.
        return std::make_unique<CachingFocusProvider>(
            std::make_unique<X11FocusProvider>(), ttl);
    case FocusProviderKind::Null:
    default:
        return std::make_unique<NullFocusProvider>();
    }
#endif
}
