#pragma once

#include "AlertEngine.h"

#include <QList>
#include <QString>

// Monitor-driven chart bands + the "healthy only" process filter.
//
// Grounding (shipped Grafana panel schema, fetched source kept at
// /tmp/opencode/m75/thresholds.ts + common.gen.ts):
//   export interface Threshold { value: number; color: string;
//     /** Warning, Error, LowLow, Low, OK, High, HighHigh etc */
//     state?: string; }
//   export enum ThresholdsMode { Absolute = 'absolute',
//                                Percentage = 'percentage' }
//   export interface ThresholdsConfig { mode: ThresholdsMode;
//     /** Must be sorted by 'value', first value is always -Infinity */
//     steps: Threshold[]; }
//   export enum GraphThresholdsStyleMode { Area = 'area', Dashed = 'dashed',
//     DashedAndArea = 'dashed+area', Line = 'line', LineAndArea = 'line+area',
//     Off = 'off', Series = 'series' }
// Syspeek draws the "line+area" style in absolute mode: a dashed line at the
// monitor's threshold value, a faint fill on the breaching side, and the
// Grafana state word next to it. The bands come from the SAME editable
// monitors the alert engine evaluates (M66), so editing a threshold moves the
// chart line instead of leaving the chart and the alert disagreeing.
struct MonitorBand {
    QString id;
    QString state;      // Grafana vocabulary: "Warning" | "Error"
    QString title;
    QString unit;
    double value = 0.0;
    bool above = true;  // breach side: above the line, or below it
};

// Bands for the listed monitor ids, sorted ascending by value (Grafana keeps
// steps sorted; the baseline step is implicit). Rules that are not in `above`
// mode keep `above == false` so the caller shades the floor instead of a
// ceiling.
QList<MonitorBand> monitorBandsFor(const QList<AlertRule> &rules,
                                   const QStringList &ruleIds);

QString bandStateForSeverity(const QString &severity);

// True when the chart should paint anything at all for this band: a monitor
// disabled by a zero threshold still has a meaningful 0 line only for count
// rules, so callers pass the visible axis range.
bool bandVisible(const MonitorBand &band, double axisMin, double axisMax);

// "healthy only" filter on the process tree: a row is unhealthy when the
// focused-app CPU monitor is breached or the RSS warning line is crossed.
bool processRowBreaches(double cpuPercent, double rssGb,
                        double cpuLimitPercent, double rssLimitGb);

QString bandLabelText(const MonitorBand &band);
