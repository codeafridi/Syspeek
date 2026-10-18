#pragma once

#include "FocusedWindowProvider.h"

#include <QString>

class WinFocusProvider final : public FocusedWindowProvider {
public:
    WinFocusProvider() = default;
    ~WinFocusProvider() override = default;

    QString name() const override;
    int focusedPid() const override;
};
