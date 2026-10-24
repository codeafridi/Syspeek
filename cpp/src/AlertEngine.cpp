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
    alert.detail = QStringLiteral("%1%2").arg(value, 0, 'f', 1).arg(rule.unit);
    return alert;
}

} // namespace

AlertEngine::AlertEngine() {
    m_rules = defaultAlertRules();
}

QList<AlertRule> defaultAlertRules() {
    return {
        {QStringLiteral("app_cpu"), QStringLiteral("Focused app CPU high"),
         QStringLiteral("critical"), 90.0, true, 3, QStringLiteral("%")},
        {QStringLiteral("host_cpu"), QStringLiteral("Host CPU high"),
         QStringLiteral("critical"), 90.0, true, 5, QStringLiteral("%")},
        {QStringLiteral("host_mem"), QStringLiteral("Low available memory"),
         QStringLiteral("critical"), 10.0, false, 3, QStringLiteral("% free")},
        {QStringLiteral("disk"), QStringLiteral("Low disk space"),
         QStringLiteral("critical"), 10.0, false, 3, QStringLiteral("% free")},
        {QStringLiteral("services_failed"), QStringLiteral("Systemd service failed"),
         QStringLiteral("critical"), 0.0, true, 1, QString()},
        {QStringLiteral("k8s_issues"), QStringLiteral("Kubernetes issues"),
         QStringLiteral("critical"), 0.0, true, 1, QString()},
        {QStringLiteral("journal_errors"), QStringLiteral("Journal error spike"),
         QStringLiteral("warning"), 20.0, true, 2, QStringLiteral(" errors")},
        {QStringLiteral("watchdog_anomaly"),
         QStringLiteral("Watchdog anomaly detected"), QStringLiteral("warning"),
         5.0, true, 1, QStringLiteral("σ")},
        {QStringLiteral("watchdog_forecast"),
         QStringLiteral("Watchdog forecast threshold"), QStringLiteral("warning"),
         0.0, true, 1, QStringLiteral(" samples")},
        {QStringLiteral("kernel_fd"),
         QStringLiteral("Kernel FD table pressure"), QStringLiteral("critical"),
         80.0, true, 3, QStringLiteral("% fd table")},
        {QStringLiteral("host_steal"),
         QStringLiteral("Host CPU steal high"), QStringLiteral("warning"),
         5.0, true, 5, QStringLiteral("%")},
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
