#pragma once

#include "FocusedWindowProvider.h"

#include <QString>

class MacFocusProvider : public FocusedWindowProvider {
public:
    explicit MacFocusProvider();
    ~MacFocusProvider() override;

    QString name() const override;
    int focusedPid() const override;

private:
    int frontmostWorkspacePid() const;
    int topmostWindowPid() const;
};
