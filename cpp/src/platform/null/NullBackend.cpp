#include "NullBackend.h"

QString NullBackend::platformName() const {
    return QStringLiteral("null");
}

PlatformCapabilities NullBackend::capabilities() const {
    return PlatformCapabilities{};
}
