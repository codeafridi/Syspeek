#pragma once

#include "FocusedWindowProvider.h"
#include "platform/PlatformFactory.h"

#include <QByteArray>
#include <QString>

class HyprctlFocusProvider : public FocusedWindowProvider {
public:
    explicit HyprctlFocusProvider(QString command = QStringLiteral("hyprctl"),
                      SystemBackend &backend = systemBackend());

    QString name() const override;
    int focusedPid() const override;

private:
    SystemBackend &m_backend;
    QString m_command;
};

int parseHyprctlPid(const QByteArray &output);
