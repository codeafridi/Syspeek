#pragma once

#include "FocusedWindowProvider.h"
#include "platform/PlatformFactory.h"

#include <QString>

class GnomePidFileProvider : public FocusedWindowProvider {
public:
    explicit GnomePidFileProvider(QString path = defaultPath(),
                      SystemBackend &backend = systemBackend());

    QString name() const override;
    int focusedPid() const override;

    static QString defaultPath();

private:
    SystemBackend &m_backend;
    QString m_path;
};
