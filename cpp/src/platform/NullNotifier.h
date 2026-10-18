#pragma once

#include "../Notifier.h"

class NullNotifier : public Notifier {
public:
    QString name() const override { return QStringLiteral("null"); }
    bool send(const Alert &alert) override {
        Q_UNUSED(alert);
        return false;
    }
};
