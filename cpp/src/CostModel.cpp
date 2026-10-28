#include "CostModel.h"

#include "Theme.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kBytesPerGb = 1024.0 * 1024.0 * 1024.0;
} // namespace

CostSummary computeClusterCost(const QList<KubePod> &pods,
                               double cpuPricePerCoreHour,
                               double memPricePerGbHour) {
    CostSummary summary;
    QHash<QString, double> byNamespace;

    for (const KubePod &pod : pods) {
        const double cpuHour = pod.cpuRequestCores * cpuPricePerCoreHour;
        const double memHour =
            (pod.memRequestBytes / kBytesPerGb) * memPricePerGbHour;
        const double monthly = (cpuHour + memHour) * kCostHoursPerMonth;

        byNamespace[pod.ns] += monthly;
        summary.totalMonthly += monthly;
        ++summary.podsPriced;
        const int missing =
            pod.containersWithoutCpuRequest + pod.containersWithoutMemRequest;
        summary.missingRequestContainers += missing;

        WorkloadCost workload;
        workload.ns = pod.ns;
        workload.pod = pod.name;
        workload.monthly = monthly;
        workload.missingRequests = missing;
        summary.topWorkloads.append(workload);
    }

    for (auto it = byNamespace.constBegin(); it != byNamespace.constEnd(); ++it) {
        summary.byNamespace.append(qMakePair(it.key(), it.value()));
    }
    std::sort(summary.byNamespace.begin(), summary.byNamespace.end(),
              [](const QPair<QString, double> &a,
                 const QPair<QString, double> &b) {
                  return a.second > b.second;
              });

    std::sort(summary.topWorkloads.begin(), summary.topWorkloads.end(),
              [](const WorkloadCost &a, const WorkloadCost &b) {
                  return a.monthly > b.monthly;
              });
    if (summary.topWorkloads.size() > 20) {
        summary.topWorkloads = summary.topWorkloads.mid(0, 20);
    }

    return summary;
}

CostSplit computeCostSplit(const QList<KubePod> &pods,
                           double cpuPricePerCoreHour,
                           double memPricePerGbHour) {
    CostSplit split;
    for (const KubePod &pod : pods) {
        split.cpuMonthly += pod.cpuRequestCores * cpuPricePerCoreHour
                            * kCostHoursPerMonth;
        split.memMonthly += (pod.memRequestBytes / kBytesPerGb)
                            * memPricePerGbHour * kCostHoursPerMonth;
    }
    return split;
}

CostEfficiency computeCostEfficiency(const QList<KubePod> &pods,
                                     double cpuPricePerCoreHour,
                                     double memPricePerGbHour) {
    CostEfficiency efficiency;
    for (const KubePod &pod : pods) {
        const bool cpuMeasured = pod.cpuMillis >= 0.0 && pod.cpuRequestCores > 0.0;
        const bool memMeasured = pod.memBytes >= 0 && pod.memRequestBytes > 0.0;
        if (!cpuMeasured && !memMeasured) {
            continue;
        }
        ++efficiency.podsMeasured;
        if (cpuMeasured) {
            const double usage = pod.cpuMillis / 1000.0;
            efficiency.cpuUsageCores += usage;
            efficiency.cpuRequestCores += pod.cpuRequestCores;
            efficiency.wastedMonthly +=
                std::max(0.0, pod.cpuRequestCores - usage) * cpuPricePerCoreHour
                * kCostHoursPerMonth;
        }
        if (memMeasured) {
            const double usage = double(pod.memBytes);
            efficiency.memUsageBytes += usage;
            efficiency.memRequestBytes += pod.memRequestBytes;
            efficiency.wastedMonthly +=
                std::max(0.0, pod.memRequestBytes - usage) / kBytesPerGb
                * memPricePerGbHour * kCostHoursPerMonth;
        }
    }
    return efficiency;
}

double costCpuEfficiency(const CostEfficiency &efficiency) {
    if (efficiency.cpuRequestCores <= 0.0) {
        return -1.0;
    }
    return efficiency.cpuUsageCores / efficiency.cpuRequestCores;
}

double costMemEfficiency(const CostEfficiency &efficiency) {
    if (efficiency.memRequestBytes <= 0.0) {
        return -1.0;
    }
    return efficiency.memUsageBytes / efficiency.memRequestBytes;
}

QString formatCostMoney(double usd) {
    if (!std::isfinite(usd)) {
        return QStringLiteral("--");
    }
    if (qFuzzyIsNull(usd)) {
        return QStringLiteral("$0.00");
    }
    const double magnitude = qAbs(usd);
    if (magnitude >= 1e6) {
        return QStringLiteral("$%1M").arg(usd / 1e6, 0, 'f', 2);
    }
    if (magnitude >= 1e3) {
        return QStringLiteral("$%1K").arg(usd / 1e3, 0, 'f', 1);
    }
    if (magnitude >= 1.0) {
        return QStringLiteral("$%1").arg(usd, 0, 'f', 2);
    }
    return QStringLiteral("$%1").arg(usd, 0, 'f', 4);
}

QString formatCostShare(double fraction) {
    if (!std::isfinite(fraction) || fraction < 0.0) {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1%").arg(fraction * 100.0, 0, 'f', 1);
}

QString costRateText(const CostSplit &split, double cpuPricePerCoreHour,
                     double memPricePerGbHour) {
    QString text = QStringLiteral(
        "pricing    cpu $%1/core-hr - ram $%2/GB-hr")
        .arg(cpuPricePerCoreHour, 0, 'f', 6)
        .arg(memPricePerGbHour, 0, 'f', 6);
    text += QStringLiteral("\nallocated  cpu $%1 - ram $%2 at %3 h/month")
        .arg(split.cpuMonthly, 0, 'f', 2)
        .arg(split.memMonthly, 0, 'f', 2)
        .arg(kCostHoursPerMonth, 0, 'f', 0);
    return text;
}

QString costEfficiencyText(const CostEfficiency &efficiency) {
    return costEfficiencyTextFromRatios(
        costCpuEfficiency(efficiency), costMemEfficiency(efficiency),
        efficiency.podsMeasured, efficiency.wastedMonthly);
}

QString costEfficiencyTextFromRatios(double cpuEfficiency,
                                     double memEfficiency, int podsMeasured,
                                     double wastedMonthly) {
    if (podsMeasured == 0) {
        return QStringLiteral(
            "efficiency  -- no pod usage sample yet; the metrics API "
            "(metrics-server) reports no usage to compare against requests");
    }
    if (cpuEfficiency < 0.0 && memEfficiency < 0.0) {
        return QStringLiteral(
            "efficiency  -- no cpu/memory requests declared, so requests "
            "cannot be compared with usage");
    }
    auto cell = [](double ratio) {
        const QString color = ratio < 0.0
            ? ui_theme::muted()
            : ratio < 0.30 ? ui_theme::crit()
                           : ratio < 0.60 ? ui_theme::warn()
                                          : ui_theme::ok();
        return QStringLiteral("<span style=\"color:%1;\">%2</span>")
            .arg(color, ratio < 0.0 ? QStringLiteral("--")
                                    : formatCostShare(ratio));
    };
    auto pad = [](const QString &label) {
        QString padded = label;
        while (padded.size() < 11) {
            padded += QLatin1Char(' ');
        }
        padded.replace(QLatin1Char(' '), QStringLiteral("&nbsp;"));
        return padded;
    };
    QString text = pad(QStringLiteral("efficiency"))
        + QStringLiteral("cpu %1 - ram %2 of requests (%3 pod(s) with usage)")
              .arg(cell(cpuEfficiency), cell(memEfficiency))
              .arg(podsMeasured);
    text += QStringLiteral("<br/>") + pad(QStringLiteral("wasted"))
        + QStringLiteral("%1/month of the requested headroom above "
                         "measured usage")
              .arg(wastedMonthly > 0.0
                       ? QStringLiteral("<span style=\"color:%1;\">%2</span>")
                             .arg(ui_theme::warn(),
                                  formatCostMoney(wastedMonthly))
                       : formatCostMoney(wastedMonthly));
    text += QStringLiteral("<br/>") + pad(QStringLiteral("advice"))
        + QStringLiteral("right-size toward usage + 20% headroom "
                         "(OpenCost buffer 1.2)");
    return text;
}
