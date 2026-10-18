#pragma once

#include "../../Notifier.h"

class MacNotifier : public Notifier {
public:
    QString name() const override { return QStringLiteral("osascript"); }
    bool send(const Alert &alert) override;
};
