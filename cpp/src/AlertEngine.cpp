#include "AlertEngine.h"

namespace {

bool metricFor(const AlertRule &rule, const AlertInput &input, double &value,
               bool &valid) {
    if (rule.id == QLatin1String("app_cpu")) {
        value = input.appCpu;
        valid = input.appCpuValid;
    } else if (rule.id == QLatin1String("host_cpu")) {
        value = input.hostCpu;
        valid = input.hostCpuValid;
    } else if (rule.id == QLatin1String("host_mem")) {
        value = input.hostMemFreePct;
        valid = input.hostMemValid;
    } else if (rule.id == QLatin1String("disk")) {
        value = input.diskFreePct;
        valid = input.diskValid;
    } else if (rule.id == QLatin1String("services_failed")) {
        value = input.servicesFailed;
        valid = input.servicesValid;
    } else if (rule.id == QLatin1String("k8s_issues")) {
        value = input.k8sIssues;
        valid = input.k8sValid;
    } else if (rule.id == QLatin1String("journal_errors")) {
        value = input.journalErrors;
        valid = input.journalValid;
    } else if (rule.id == QLatin1String("watchdog_anomaly")) {
        value = input.watchdogScore;
        valid = input.watchdogValid;
    } else if (rule.id == QLatin1String("watchdog_forecast")) {
        value = input.watchdogForecastSteps;
        valid = input.watchdogValid;
    } else if (rule.id == QLatin1String("kernel_fd")) {
        value = input.kernelFdPct;
        valid = input.kernelFdValid;
    } else if (rule.id == QLatin1String("host_steal")) {
        value = input.stealPercent;
        valid = input.stealValid;
    } else if (rule.id == QLatin1String("host_temp")) {
        value = input.tempCelsius;
        valid = input.tempValid;
    } else if (rule.id == QLatin1String("battery_low")) {
        value = input.batteryPercent;
        valid = input.batteryValid;
    } else if (rule.id == QLatin1String("cloud_burn")) {
        value = input.cloudBurnUsdHour;
        valid = input.cloudBurnValid;
    } else {
        valid = false;
    }
    return valid;
}

Alert makeAlert(const AlertRule &rule, double value, qint64 nowMs) {
    Alert alert;
    alert.id = rule.id;
    alert.title = rule.title;
    alert.severity = rule.severity;
    alert.value = value;
    alert.atMs = nowMs;
    alert.group = rule.group;
    alert.owner = rule.owner;
    alert.detail = QStringLiteral("%1%2").arg(value, 0, 'f', 1).arg(rule.unit);
    return alert;
}

} // namespace

AlertEngine::AlertEngine() {
    m_rules = defaultAlertRules();
}

QList<AlertRule> defaultAlertRules() {
    // `group` is Alertmanager's group_by: rules sharing one fold onto a single
    // incident row ("compute pressure" instead of three separate pages).
    // Owners stay empty by default - an incident nobody owns should say so;
    // assign one with `[alerts] <id>_owner=`.
    const auto make = [](const char *id, const char *title, const char *severity,
                         double threshold, bool above, int sustained,
                         const QString &unit, const char *group) {
        AlertRule rule;
        rule.id = QString::fromLatin1(id);
        rule.title = QString::fromLatin1(title);
        rule.severity = QString::fromLatin1(severity);
        rule.threshold = threshold;
        rule.above = above;
        rule.sustained = sustained;
        rule.unit = unit;
        rule.group = QString::fromLatin1(group);
        return rule;
    };
    return {
        make("app_cpu", "Focused app CPU high", "critical", 90.0, true, 3,
             QStringLiteral("%"), "compute"),
        make("host_cpu", "Host CPU high", "critical", 90.0, true, 5,
             QStringLiteral("%"), "compute"),
        make("host_mem", "Low available memory", "critical", 10.0, false, 3,
             QStringLiteral("% free"), "capacity"),
        make("disk", "Low disk space", "critical", 10.0, false, 3,
             QStringLiteral("% free"), "capacity"),
        make("services_failed", "Systemd service failed", "critical", 0.0,
             true, 1, QString(), "services"),
        make("k8s_issues", "Kubernetes issues", "critical", 0.0, true, 1,
             QString(), "kubernetes"),
        make("journal_errors", "Journal error spike", "warning", 20.0, true, 2,
             QStringLiteral(" errors"), "logs"),
        make("watchdog_anomaly", "Watchdog anomaly detected", "warning", 5.0,
             true, 1, QStringLiteral("\u03c3"), "watchdog"),
        make("watchdog_forecast", "Watchdog forecast threshold", "warning", 0.0,
             true, 1, QStringLiteral(" samples"), "watchdog"),
        make("kernel_fd", "Kernel FD table pressure", "critical", 80.0, true, 3,
             QStringLiteral("% fd table"), "kernel"),
        make("host_steal", "Host CPU steal high", "warning", 5.0, true, 5,
             QStringLiteral("%"), "compute"),
        make("host_temp", "Host temperature high", "warning", 85.0, true, 3,
             QStringLiteral(" \u00b0C"), "thermal"),
        make("battery_low", "Battery low", "warning", 20.0, false, 3,
             QStringLiteral("%"), "thermal"),
        make("cloud_burn", "Cloud cost burn high", "warning", 5.0, true, 3,
             QStringLiteral(" $/h"), "cost"),
    };
}

QList<Alert> AlertEngine::update(const AlertInput &input, qint64 nowMs) {
    QList<Alert> fired;
    QList<Alert> stillActive;

    for (const AlertRule &rule : m_rules) {
        double value = 0.0;
        bool valid = false;
        metricFor(rule, input, value, valid);

        State &state = m_state[rule.id];
        const bool breach =
            valid && (rule.above ? value > rule.threshold
                                 : value < rule.threshold);
        if (breach) {
            ++state.streak;
        } else {
            state.streak = 0;
        }

        if (breach && state.streak >= rule.sustained) {
            if (!state.firing) {
                state.firing = true;
                fired.append(makeAlert(rule, value, nowMs));
            }
            stillActive.append(makeAlert(rule, value, nowMs));
        } else {
            state.firing = false;
        }
    }

    m_active = stillActive;
    return fired;
}

void AlertEngine::reset() {
    m_state.clear();
    m_active.clear();
}

void AlertEngine::clearRuleState(const QString &id) {
    State &state = m_state[id];
    state.streak = 0;
    state.firing = false;
}

bool AlertEngine::setRuleThreshold(const QString &id, double threshold) {
    for (AlertRule &rule : m_rules) {
        if (rule.id == id) {
            if (!(threshold >= 0.0) || threshold > 1e9) {
                return false;
            }
            if (rule.threshold == threshold) {
                return true;
            }
            rule.threshold = threshold;
            clearRuleState(id);
            return true;
        }
    }
    return false;
}

bool AlertEngine::setRuleCorrelation(const QString &id, const QString &group,
                                    const QString &owner, qint64 groupWaitMs,
                                    qint64 repeatIntervalMs) {
    for (AlertRule &rule : m_rules) {
        if (rule.id != id) {
            continue;
        }
        if (group.size() > 64 || owner.size() > 64) {
            return false;
        }
        rule.group = group.trimmed();
        rule.owner = owner.trimmed();
        if (groupWaitMs > 0 && groupWaitMs <= 86400000
            && repeatIntervalMs >= groupWaitMs
            && repeatIntervalMs <= 86400000) {
            rule.groupWaitMs = groupWaitMs;
            rule.repeatIntervalMs = repeatIntervalMs;
        }
        return true;
    }
    return false;
}

bool AlertEngine::setRuleSustained(const QString &id, int sustained) {
    for (AlertRule &rule : m_rules) {
        if (rule.id == id) {
            if (sustained < 1 || sustained > 100) {
                return false;
            }
            if (rule.sustained == sustained) {
                return true;
            }
            rule.sustained = sustained;
            clearRuleState(id);
            return true;
        }
    }
    return false;
}

void AlertEngine::resetRules() {
    m_rules = defaultAlertRules();
    m_state.clear();
    m_active.clear();
}
