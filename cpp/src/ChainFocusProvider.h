#pragma once

#include "FocusedWindowProvider.h"

#include <QString>

#include <memory>

// Reports the first provider in the chain that answers with a usable pid,
// and names the provider that last produced one (primary before the first
// answer). Used to shadow a Wayland-native bridge with the XWayland
// provider so a session whose bridge is not loaded yet still tracks
// X11 windows; a loaded bridge keeps the primary.
class ChainFocusProvider : public FocusedWindowProvider {
public:
    explicit ChainFocusProvider(
        std::unique_ptr<FocusedWindowProvider> primary,
        std::unique_ptr<FocusedWindowProvider> fallback);

    QString name() const override;
    int focusedPid() const override;

private:
    std::unique_ptr<FocusedWindowProvider> m_primary;
    std::unique_ptr<FocusedWindowProvider> m_fallback;
    mutable QString m_activeName;
};
