#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

// Anomaly models (mirroring the forecasting/robust-stat functions used by
// metrics engines such as Prometheus: avg/stddev, quantile, predict_linear).
enum class AnomalyModel { ZScore, Mad, Quantile, PredictLinear };

struct AnomalyResult {
    bool anomalous = false;
    double score = 0.0;
    double value = 0.0;
    double expected = 0.0;
    double lower = 0.0;
    double upper = 0.0;
    QString model;
};

struct WatchdogMetric {
    QString name;
    AnomalyResult result;
};

// Burn rate per window (0=ok, 1=warning, 2=critical)
struct BurnRate {
    double ratePerHour = 0.0;   // dollars per hour
    int severity = 0;           // 0=ok, 1=warning, 2=critical
    QString text;               // formatted text
};

// Idle metric per container/process
struct IdleMetric {
    bool isIdle = false;
    double cpuPct = 0.0;
    double netBps = 0.0;
    qint64 idleSeconds = 0;
    QString text;
};

struct ForecastResult {
    QString name;
    double predicted = 0.0;   // next value
    double slope = 0.0;       // change per sample
    double threshold = 0.0;
    int stepsToThreshold = -1; // -1 when not reached within the horizon
    bool crosses = false;
    QString direction;        // "rising" / "falling" / "flat"
};

QString anomalyModelName(AnomalyModel model);
AnomalyResult detectAnomaly(const QList<double> &series, AnomalyModel model,
                            double threshold);
// Prometheus predict_linear-style forecast + time-to-threshold.
ForecastResult forecastCrossing(const QList<double> &series, double threshold,
                                int horizon = 60);
// Prometheus holt_winters-style additive seasonal decomposition.
AnomalyResult holtWinters(const QList<double> &series, int season,
                          double alpha, double beta, double gamma,
                          double band);
QString watchdogText(const QList<WatchdogMetric> &metrics);
QString forecastText(const ForecastResult &cpu, const ForecastResult &hostCpu);
// Narrative insights from anomalies + forecasts, with correlation.
QString watchdogInsightsText(const QList<WatchdogMetric> &metrics,
                             const QList<ForecastResult> &forecasts);

// Burn rate per window (dollars/hour from rate × time)
BurnRate burnRate(qint64 windowSeconds, double ratePerHour, double thresholdPerHour);
// Idle metric per container/process
IdleMetric idleMetric(double cpuPct, double netBps, qint64 idleSeconds,
                      double cpuThresholdPct, int windowMinutes);

// Narrative insights from anomalies + forecasts, with correlation.
QString watchdogInsightsText(const QList<WatchdogMetric> &metrics,
                             const QList<ForecastResult> &forecasts);
