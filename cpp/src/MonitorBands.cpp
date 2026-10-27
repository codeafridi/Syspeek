#include "MonitorBands.h"

#include <algorithm>

QString bandStateForSeverity(const QString &severity) {
    if (severity.compare(QStringLiteral("critical"), Qt::CaseInsensitive) == 0
        || severity.compare(QStringLiteral("high"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Error");
    }
    if (severity.compare(QStringLiteral("warning"), Qt::CaseInsensitive) == 0
        || severity.compare(QStringLiteral("warn"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Warning");
    }
    return QStringLiteral("OK");
}

QList<MonitorBand> monitorBandsFor(const QList<AlertRule> &rules,
                                   const QStringList &ruleIds) {
    QList<MonitorBand> bands;
    for (const AlertRule &rule : rules) {
        if (!ruleIds.contains(rule.id)) {
            continue;
        }
        MonitorBand band;
        band.id = rule.id;
        band.state = bandStateForSeverity(rule.severity);
        band.title = rule.title;
        band.unit = rule.unit;
        band.value = rule.threshold;
        band.above = rule.above;
        bands.append(band);
    }
    std::sort(bands.begin(), bands.end(),
              [](const MonitorBand &a, const MonitorBand &b) {
                  if (a.value == b.value) {
                      return a.id < b.id;
                  }
                  return a.value < b.value;
              });
    return bands;
}

bool bandVisible(const MonitorBand &band, double axisMin, double axisMax) {
    return band.value > axisMin && band.value < axisMax;
}

bool processRowBreaches(double cpuPercent, double rssGb,
                        double cpuLimitPercent, double rssLimitGb) {
    if (cpuLimitPercent > 0.0 && cpuPercent > cpuLimitPercent) {
        return true;
    }
    if (rssLimitGb > 0.0 && rssGb > rssLimitGb) {
        return true;
    }
    return false;
}

QString bandLabelText(const MonitorBand &band) {
    return QStringLiteral("%1 %2%3")
        .arg(band.state)
        .arg(band.value, 0, 'f', band.value == double(int(band.value)) ? 0 : 1)
        .arg(band.unit);
}
