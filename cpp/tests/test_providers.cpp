#include "FocusProviderFactory.h"
#include "CachingFocusProvider.h"
#include "FocusedWindowProvider.h"
#include "HyprctlFocusProvider.h"
#include "KdeFocusProvider.h"
#include "KdeFocusService.h"
#include "KdeFocusState.h"
#include "LinuxProcess.h"
#include "NullFocusProvider.h"
#include "SwaymsgFocusProvider.h"
#include "X11FocusProvider.h"

#include <QCoreApplication>

#include <cstdio>
#include <memory>

namespace {

class ToggleProvider : public FocusedWindowProvider {
public:
    QString name() const override { return QStringLiteral("toggle"); }
    int focusedPid() const override { return pid; }

    int pid = -1;
};

} // namespace

namespace {

SessionInfo makeInfo(const char *session, const char *desktop, bool display,
                     bool hypr = false, bool sway = false) {
    SessionInfo info;
    info.sessionType = QString::fromUtf8(session);
    info.desktop = QString::fromUtf8(desktop);
    info.hasDisplay = display;
    info.hasHyprlandSignature = hypr;
    info.hasSwaySocket = sway;
    return info;
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    int failures = 0;
    auto check = [&failures](bool condition, const char *message) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition) {
            ++failures;
        }
    };

    check(selectProviderKind(makeInfo("wayland", "ubuntu:GNOME", true))
              == FocusProviderKind::Gnome,
          "wayland+GNOME -> Gnome");
    check(selectProviderKind(makeInfo("wayland", "KDE", true))
              == FocusProviderKind::Kde,
          "wayland+KDE -> Kde");
    check(selectProviderKind(makeInfo("wayland", "plasma", true))
              == FocusProviderKind::Kde,
          "wayland+plasma -> Kde");
    check(selectProviderKind(makeInfo("wayland", "Hyprland", true))
              == FocusProviderKind::Hyprland,
          "wayland+Hyprland -> Hyprland");
    check(selectProviderKind(makeInfo("wayland", "", false, true))
              == FocusProviderKind::Hyprland,
          "wayland + HYPRLAND_INSTANCE_SIGNATURE -> Hyprland");
    check(selectProviderKind(makeInfo("wayland", "sway", true))
              == FocusProviderKind::Sway,
          "wayland+sway -> Sway");
    check(selectProviderKind(makeInfo("wayland", "", false, false, true))
              == FocusProviderKind::Sway,
          "wayland + SWAYSOCK -> Sway");
    check(selectProviderKind(makeInfo("wayland", "niri", true))
              == FocusProviderKind::Niri,
          "wayland+niri -> Niri");
    check(selectProviderKind(makeInfo("wayland", "", false))
              == FocusProviderKind::Null,
          "wayland+unknown -> Null");
    check(selectProviderKind(makeInfo("x11", "ubuntu:GNOME", true))
              == FocusProviderKind::X11,
          "x11 -> X11");
    check(selectProviderKind(makeInfo("", "", true)) == FocusProviderKind::X11,
          "empty session + display -> X11");
    check(selectProviderKind(makeInfo("", "", false)) == FocusProviderKind::Null,
          "empty session + no display -> Null");

    check(focusProviderKindName(FocusProviderKind::Gnome)
              == QStringLiteral("gnome-pidfile"),
          "Gnome kind name");
    check(focusProviderKindName(FocusProviderKind::X11)
              == QStringLiteral("x11-native"),
          "X11 kind name");
    check(focusProviderKindName(FocusProviderKind::Hyprland)
              == QStringLiteral("hyprctl"),
          "Hyprland kind name");
    check(focusProviderKindName(FocusProviderKind::Sway)
              == QStringLiteral("swaymsg"),
          "Sway kind name");
    check(focusProviderKindName(FocusProviderKind::Niri)
              == QStringLiteral("niri"),
          "Niri kind name");
    check(focusProviderKindName(FocusProviderKind::Kde)
              == QStringLiteral("kde-dbus"),
          "Kde kind name");

    check(parseHyprctlPid(
              QByteArrayLiteral("{\"address\":\"0x1\",\"pid\":4242,\"class\":\"x\"}"))
              == 4242,
          "parseHyprctlPid extracts pid");
    check(parseHyprctlPid(QByteArrayLiteral("{}")) == -1,
          "parseHyprctlPid empty object -> -1");
    check(parseHyprctlPid(QByteArrayLiteral("No active window")) == -1,
          "parseHyprctlPid non-json -> -1");
    check(parseHyprctlPid(QByteArrayLiteral("{\"address\":\"0x1\"}")) == -1,
          "parseHyprctlPid missing pid -> -1");

    const QByteArray swayTree = QByteArrayLiteral(
        "{\"type\":\"root\",\"nodes\":["
        "{\"type\":\"output\",\"nodes\":["
        "{\"type\":\"workspace\",\"focused\":false,\"nodes\":["
        "{\"type\":\"con\",\"focused\":true,\"pid\":777,\"nodes\":[]}]}]},"
        "{\"type\":\"output\",\"nodes\":[]}]}");
    check(parseSwayFocusedPid(swayTree) == 777,
          "parseSwayFocusedPid finds nested focused pid");
    check(parseSwayFocusedPid(
              QByteArrayLiteral("{\"type\":\"root\",\"focused\":true,\"pid\":99}"))
              == 99,
          "parseSwayFocusedPid finds top-level focused pid");
    check(parseSwayFocusedPid(QByteArrayLiteral("{}")) == -1,
          "parseSwayFocusedPid empty -> -1");
    check(parseSwayFocusedPid(QByteArrayLiteral("not json")) == -1,
          "parseSwayFocusedPid non-json -> -1");

    HyprctlFocusProvider missingHyprctl(QStringLiteral("/nonexistent/hyprctl"));
    check(missingHyprctl.name() == QStringLiteral("hyprctl"),
          "hyprctl provider name");
    check(missingHyprctl.focusedPid() == -1, "missing hyprctl command -> -1");

    SwaymsgFocusProvider missingSway(QStringLiteral("/nonexistent/swaymsg"));
    check(missingSway.name() == QStringLiteral("swaymsg"), "swaymsg provider name");
    check(missingSway.focusedPid() == -1, "missing swaymsg command -> -1");

    NullFocusProvider nullProvider;
    check(nullProvider.name() == QStringLiteral("null"), "null provider name");
    check(nullProvider.focusedPid() == -1, "null provider returns -1");

    X11FocusProvider unnamed(QStringLiteral(":99"));
    check(unnamed.name() == QStringLiteral("x11-native"),
          "x11 native provider name");
    check(unnamed.focusedPid() == -1, "unopenable DISPLAY -> -1");

    {
        qint64 fakeNow = 1000;
        auto clock = [&fakeNow]() { return fakeNow; };
        auto inner = std::make_unique<ToggleProvider>();
        ToggleProvider *raw = inner.get();
        CachingFocusProvider cache(std::move(inner), 1500, clock);

        check(cache.name() == QStringLiteral("toggle"), "cache delegates name");

        raw->pid = 42;
        check(cache.focusedPid() == 42, "cache passes through valid pid");

        raw->pid = -1;
        fakeNow += 1000;
        check(cache.focusedPid() == 42, "cache returns last valid within TTL");

        fakeNow += 1000;
        check(cache.focusedPid() == -1, "cache expires after TTL");

        raw->pid = 7;
        check(cache.focusedPid() == 7, "cache updates on new valid pid");
    }

    {
        qint64 fakeNow = 1000;
        auto clock = [&fakeNow]() { return fakeNow; };
        KdeFocusProvider provider(5000, clock);

        kdeFocusState().pid.store(-1);
        kdeFocusState().stampMs.store(0);
        check(provider.name() == QStringLiteral("kde-dbus"), "kde provider name");
        check(provider.focusedPid() == -1, "kde provider -1 before any push");

        kdeFocusState().pid.store(123);
        kdeFocusState().stampMs.store(fakeNow);
        check(provider.focusedPid() == 123, "kde provider returns pushed pid");

        fakeNow += 6000;
        check(provider.focusedPid() == -1, "kde provider expires after TTL");
    }

    {
        KdeFocusService service;
        service.setFocusedPid(4242);
        check(kdeFocusState().pid.load() == 4242, "kde service stores pid");
        service.setFocusedPid(0);
        check(kdeFocusState().pid.load() == -1,
              "kde service clears on non-positive pid");
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
