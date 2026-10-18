#pragma once

#include "ProcessIdentity.h"
#include "platform/PlatformFactory.h"

#include <QString>

class FocusedWindowProvider;

struct FocusSample {
    bool available = false;
    ProcessIdentity identity;
    QString appName = QStringLiteral("--");
    QString providerName;
};

class FocusSampler {
public:
    explicit FocusSampler(const FocusedWindowProvider *provider,
                          SystemBackend &backend = systemBackend());

    FocusSample sample() const;

private:
    const FocusedWindowProvider *m_provider;
    SystemBackend &m_backend;
    mutable ProcessIdentity m_lastIdentity;
    mutable QString m_lastName;
};
