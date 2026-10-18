#pragma once

#include "FocusedWindowProvider.h"
#include "platform/PlatformFactory.h"

#include <QByteArray>
#include <QString>

class SwaymsgFocusProvider : public FocusedWindowProvider {
public:
    explicit SwaymsgFocusProvider(QString command = QStringLiteral("swaymsg"),
                      SystemBackend &backend = systemBackend());

    QString name() const override;
    int focusedPid() const override;

private:
    SystemBackend &m_backend;
    QString m_command;
};

int parseSwayFocusedPid(const QByteArray &output);
