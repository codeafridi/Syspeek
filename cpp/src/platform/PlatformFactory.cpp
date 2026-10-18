#include "PlatformFactory.h"

#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include "linux/LinuxBackend.h"
#elif defined(Q_OS_MACOS)
#include "mac/MacBackend.h"
#elif defined(Q_OS_WIN)
#include "win/WindowsBackend.h"
#else
#include "null/NullBackend.h"
#endif

std::unique_ptr<SystemBackend> createSystemBackend() {
#if defined(Q_OS_LINUX)
    return std::make_unique<LinuxBackend>();
#elif defined(Q_OS_MACOS)
    return std::make_unique<MacBackend>();
#elif defined(Q_OS_WIN)
    return std::make_unique<WindowsBackend>();
#else
    return std::make_unique<NullBackend>();
#endif
}

SystemBackend &systemBackend() {
    static std::unique_ptr<SystemBackend> backend = createSystemBackend();
    return *backend;
}

QString platformName() {
    return systemBackend().platformName();
}

PlatformCapabilities backendCapabilities() {
    return systemBackend().capabilities();
}
