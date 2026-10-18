#pragma once

#include "../SystemBackend.h"

class NullBackend : public SystemBackend {
public:
    QString platformName() const override;
    PlatformCapabilities capabilities() const override;
};
