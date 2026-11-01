#include "WatchdogStatus.h"

#include <algorithm>
#include <cmath>

namespace {

double mean(const QList<double> &values) {
    if (values.isEmpty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (double v : values) {
        sum += v;
    }
    return sum / values.size();
}

double stddev(const QList<double> &values, double mu) {
    if (values.size() < 2) {
        return 0.0;
    }
    double sum = 0.0;
    for (double v : values) {
        sum += (v - mu) * (v - mu);
    }
    return std::sqrt(sum / (values.size() - 1));
}

double quantile(QList<double> values, double q) {
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double pos = q * (values.size() - 1);
    const int lo = static_cast<int>(std::floor(pos));
    const int hi = static_cast<int>(std::ceil(pos));
    const double frac = pos - lo;
    return values.at(lo) * (1.0 - frac) + values.at(hi) * frac;
}

double median(const QList<double> &values) {
    return quantile(values, 0.5);
}

} // namespace

QString anomalyModelName(AnomalyModel model) {
    switch (model) {
    case AnomalyModel::ZScore:
        return QStringLiteral("zscore");
    case AnomalyModel::Mad:
        return QStringLiteral("mad");
    case AnomalyModel::Quantile:
        return QStringLiteral("quantile");
    case AnomalyModel::PredictLinear:
        return QStringLiteral("predict_linear");
    }
    return QStringLiteral("unknown");
}

AnomalyResult detectAnomaly(const QList<double> &series, AnomalyModel model,
                            double threshold) {
    AnomalyResult result;
    result.model = anomalyModelName(model);
    if (series.isEmpty()) {
        return result;
    }
    result.value = series.last();
    if (series.size() < 5) {
        return result;
    }

    // Evaluate against the historical window (exclude the latest point).
    QList<double> history = series.mid(0, series.size() - 1);

    if (model == AnomalyModel::ZScore) {
        const double mu = mean(history);
        const double sd = stddev(history, mu);
        if (sd > 1e-9) {
            result.score = (result.value - mu) / sd;
        }
        result.expected = mu;
        result.lower = mu - threshold * sd;
        result.upper = mu + threshold * sd;
        result.anomalous = std::fabs(result.score) > threshold;
    } else if (model == AnomalyModel::Mad) {
        const double med = median(history);
        QList<double> deviations;
        for (double v : history) {
            deviations.append(std::fabs(v - med));
        }
        const double mad = median(deviations) * 1.4826;
        if (mad > 1e-9) {
            result.score = 0.6745 * (result.value - med) / mad;
        }
        result.expected = med;
        result.lower = med - threshold * mad;
        result.upper = med + threshold * mad;
        result.anomalous = std::fabs(result.score) > threshold;
    } else if (model == AnomalyModel::Quantile) {
        const double q1 = quantile(history, 0.25);
        const double q3 = quantile(history, 0.75);
        const double iqr = q3 - q1;
        result.expected = median(history);
        result.lower = q1 - 1.5 * iqr;
        result.upper = q3 + 1.5 * iqr;
        if (iqr > 1e-9) {
            result.score = (result.value - result.expected) / iqr;
        }
        result.anomalous =
            result.value < result.lower || result.value > result.upper;
    } else { // PredictLinear
        const int n = history.size();
        double sumX = 0.0;
        double sumY = 0.0;
        double sumXY = 0.0;
        double sumXX = 0.0;
        for (int i = 0; i < n; ++i) {
            sumX += i;
            sumY += history.at(i);
            sumXY += i * history.at(i);
            sumXX += static_cast<double>(i) * i;
        }
        const double denom = n * sumXX - sumX * sumX;
        const double slope = denom != 0.0 ? (n * sumXY - sumX * sumY) / denom : 0.0;
        const double intercept = (sumY - slope * sumX) / n;
        result.expected = slope * n + intercept; // predict the latest index
        double residual = 0.0;
        for (int i = 0; i < n; ++i) {
            const double predicted = slope * i + intercept;
            residual += (history.at(i) - predicted) * (history.at(i) - predicted);
        }
        const double residSd =
            n > 1 ? std::sqrt(residual / (n - 1)) : 0.0;
        if (residSd > 1e-9) {
            result.score = (result.value - result.expected) / residSd;
        }
        result.lower = result.expected - threshold * residSd;
        result.upper = result.expected + threshold * residSd;
        result.anomalous = std::fabs(result.score) > threshold;
    }
    return result;
}

QString watchdogText(const QList<WatchdogMetric> &metrics) {
    if (metrics.isEmpty()) {
        return QStringLiteral("watchdog: collecting");
    }
    QString text;
    for (const WatchdogMetric &metric : metrics) {
        const AnomalyResult &r = metric.result;
        text += QStringLiteral("%1 %2 %3=%4  [%5 .. %6]\n")
                    .arg(r.anomalous ? QStringLiteral("ANOMALY")
                                     : QStringLiteral("ok     "),
                         metric.name,
                         r.model,
                         QString::number(r.value, 'f', 1),
                         QString::number(r.lower, 'f', 1),
                         QString::number(r.upper, 'f', 1));
    }
    return text.trimmed();
}

ForecastResult forecastCrossing(const QList<double> &series, double threshold,
                                int horizon) {
    ForecastResult result;
    result.threshold = threshold;
    if (series.size() < 5) {
        return result;
    }
    const int n = series.size();
    const int window = std::min(n, 60);
    const int start = n - window;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXY = 0.0;
    double sumXX = 0.0;
    for (int i = 0; i < window; ++i) {
        const double y = series.at(start + i);
        sumX += i;
        sumY += y;
        sumXY += i * y;
        sumXX += static_cast<double>(i) * i;
    }
    const double denom = window * sumXX - sumX * sumX;
    const double slope = denom != 0.0 ? (window * sumXY - sumX * sumY) / denom : 0.0;
    const double intercept = (sumY - slope * sumX) / window;
    const double current = series.last();
    result.slope = slope;
    result.predicted = slope * window + intercept;
    result.direction = slope > 1e-9   ? QStringLiteral("rising")
                       : slope < -1e-9 ? QStringLiteral("falling")
                                       : QStringLiteral("flat");
    if (std::fabs(slope) > 1e-9) {
        const double steps = (threshold - current) / slope;
        if (steps >= 0.0 && steps <= horizon) {
            result.stepsToThreshold = static_cast<int>(std::ceil(steps));
            result.crosses = true;
        }
    }
    return result;
}

AnomalyResult holtWinters(const QList<double> &series, int season, double alpha,
                          double beta, double gamma, double band) {
    AnomalyResult result;
    result.model = QStringLiteral("holt_winters");
    if (series.isEmpty()) {
        return result;
    }
    result.value = series.last();
    const int n = series.size();
    if (season < 2 || n < season * 2) {
        return detectAnomaly(series, AnomalyModel::ZScore, band);
    }

    double level = 0.0;
    for (int i = 0; i < season; ++i) {
        level += series.at(i);
    }
    level /= season;
    double trend = (series.at(season) - series.at(0)) / season;
    QList<double> seasonal;
    for (int i = 0; i < season; ++i) {
        seasonal.append(series.at(i) - level);
    }

    QList<double> residuals;
    for (int t = season; t < n - 1; ++t) {
        const double x = series.at(t);
        const double s = seasonal.at(t % season);
        const double prevLevel = level;
        level = alpha * (x - s) + (1.0 - alpha) * (level + trend);
        trend = beta * (level - prevLevel) + (1.0 - beta) * trend;
        seasonal[t % season] = gamma * (x - level) + (1.0 - gamma) * s;
        residuals.append(x - (prevLevel + trend + s));
    }

    const double forecast = level + trend + seasonal.at((n - 1) % season);
    double mean = 0.0;
    for (double r : residuals) {
        mean += r;
    }
    mean /= residuals.size();
    double variance = 0.0;
    for (double r : residuals) {
        variance += (r - mean) * (r - mean);
    }
    variance /= std::max(1, static_cast<int>(residuals.size()) - 1);
    const double sd = std::sqrt(variance);

    result.expected = forecast;
    result.lower = forecast - band * sd;
    result.upper = forecast + band * sd;
    result.score = sd > 1e-9 ? (result.value - forecast) / sd : 0.0;
    result.anomalous = result.value < result.lower || result.value > result.upper;
    return result;
}

QString forecastText(const ForecastResult &cpu, const ForecastResult &hostCpu) {
    auto line = [](const QString &name, const ForecastResult &forecast) {
        QString text = QStringLiteral("%1 next %2  slope %3")
                           .arg(name.leftJustified(6, ' '),
                                QString::number(forecast.predicted, 'f', 1),
                                QString::number(forecast.slope, 'f', 2));
        if (forecast.crosses) {
            text += QStringLiteral("  -> %1 in ~%2 samples")
                        .arg(forecast.threshold)
                        .arg(forecast.stepsToThreshold);
        } else {
            text += QStringLiteral("  stable");
        }
        return text;
    };
    return line(QStringLiteral("cpu"), cpu) + QLatin1Char('\n')
           + line(QStringLiteral("host"), hostCpu);
}

QString watchdogInsightsText(const QList<WatchdogMetric> &metrics,
                             const QList<ForecastResult> &forecasts) {
    QStringList lines;
    int anomalies = 0;
    for (const WatchdogMetric &metric : metrics) {
        if (!metric.result.anomalous) {
            continue;
        }
        ++anomalies;
        lines.append(QStringLiteral("ANOMALY   %1  %2=%3  score %4")
                         .arg(metric.name.leftJustified(8, ' '),
                              metric.result.model,
                              QString::number(metric.result.value, 'f', 1),
                              QString::number(metric.result.score, 'f', 1)));
    }
    for (const ForecastResult &forecast : forecasts) {
        if (!forecast.crosses) {
            continue;
        }
        lines.append(QStringLiteral("FORECAST  %1 %2 will reach %3 in ~%4 samples")
                         .arg(forecast.name.isEmpty() ? QStringLiteral("metric")
                                                      : forecast.name,
                              forecast.direction,
                              QString::number(forecast.threshold, 'f', 0))
                         .arg(forecast.stepsToThreshold));
    }
    if (anomalies >= 2) {
        lines.prepend(QStringLiteral(
            "CORRELATED  multiple metrics anomalous (possible resource exhaustion)"));
    }
    if (lines.isEmpty()) {
        return QStringLiteral("no insights");
    }
    return lines.join(QLatin1Char('\n'));
}
