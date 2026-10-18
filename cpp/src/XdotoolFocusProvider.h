#pragma once

#include "FocusedWindowProvider.h"
#include "platform/PlatformFactory.h"

#include <QString>

class XdotoolFocusProvider : public FocusedWindowProvider {
public:
    explicit XdotoolFocusProvider(QString command = QStringLiteral("xdotool"),
                      SystemBackend &backend = systemBackend());

    QString name() const override;
    int focusedPid() const override;

private:
    SystemBackend &m_backend;
    QString m_command;
};
