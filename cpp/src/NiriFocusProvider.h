#pragma once

#include "FocusedWindowProvider.h"
#include "platform/PlatformFactory.h"

#include <QByteArray>
#include <QString>

class NiriFocusProvider : public FocusedWindowProvider {
public:
    explicit NiriFocusProvider(QString command = QStringLiteral("niri"),
                      SystemBackend &backend = systemBackend());

    QString name() const override;
    int focusedPid() const override;

private:
    SystemBackend &m_backend;
    QString m_command;
};

int parseNiriPid(const QByteArray &output);
