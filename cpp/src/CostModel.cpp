#include "CostModel.h"

#include "MemoryFormat.h"

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

namespace {

double gbOf(double bytes) {
    return bytes / (1024.0 * 1024.0 * 1024.0);
}

} // namespace

QList<PodRightSizing> computePodRightSizing(const QList<KubePod> &pods,
                                            double cpuPricePerCoreHour,
                                            double memPricePerGbHour,
                                            double bufferMultiplier) {
    // OpenCost's buffer_multiplier is 1.2 by default and must be > 1 to mean
    // anything; anything else falls back to the documented default.
    const double buffer = bufferMultiplier > 1.0 ? bufferMultiplier
                                                 : kOpenCostBuffer;
    QList<PodRightSizing> rows;
    for (const KubePod &pod : pods) {
        const bool cpuSample = pod.cpuMillis >= 0.0;
        const bool memSample = pod.memBytes >= 0;
        if (!cpuSample && !memSample) {
            continue;
        }
        PodRightSizing row;
        row.ns = pod.ns;
        row.pod = pod.name;
        row.cpuUsageCores = cpuSample ? pod.cpuMillis / 1000.0 : 0.0;
        row.cpuRequestCores = pod.cpuRequestCores;
        row.memUsageBytes = memSample ? static_cast<double>(pod.memBytes) : 0.0;
        row.memRequestBytes = pod.memRequestBytes;
        if (cpuSample && pod.cpuRequestCores > 0.0) {
            row.hasCpu = true;
            row.cpuRatio = row.cpuUsageCores / pod.cpuRequestCores;
            row.suggestedCpuCores = qMax(row.cpuUsageCores * buffer,
                                         kRightSizingCpuFloorCores);
            row.underCpu = row.cpuUsageCores > pod.cpuRequestCores;
        }
        if (memSample && pod.memRequestBytes > 0.0) {
            row.hasMem = true;
            row.memRatio = row.memUsageBytes / pod.memRequestBytes;
            row.suggestedMemBytes = qMax(row.memUsageBytes * buffer,
                                         kRightSizingMemFloorBytes);
            row.underMem = row.memUsageBytes > pod.memRequestBytes;
        }
        // A pod with no declared request cannot be right-sized against one -
        // it is already unpriced, which the allocation card states instead.
        if (!row.hasCpu && !row.hasMem) {
            continue;
        }
        // A pod that needs MORE of either resource is not a savings
        // candidate: its recommendation is a raise, and pricing the other
        // resource's headroom next to it would read as free money.
        double savings = 0.0;
        if (!row.underCpu && !row.underMem) {
            if (row.hasCpu) {
                savings += (pod.cpuRequestCores - row.suggestedCpuCores)
                           * cpuPricePerCoreHour * kCostHoursPerMonth;
            }
            if (row.hasMem) {
                savings += gbOf(row.memRequestBytes - row.suggestedMemBytes)
                           * memPricePerGbHour * kCostHoursPerMonth;
            }
        }
        row.monthlySavings = savings > 0.0 ? savings : 0.0;
        rows.append(row);
    }
    std::sort(rows.begin(), rows.end(),
              [](const PodRightSizing &a, const PodRightSizing &b) {
                  if (!qFuzzyCompare(a.monthlySavings + 1.0,
                                     b.monthlySavings + 1.0)) {
                      return a.monthlySavings > b.monthlySavings;
                  }
                  if (a.ns != b.ns) {
                      return a.ns < b.ns;
                  }
                  return a.pod < b.pod;
              });
    return rows;
}

QString podRightSizingText(const PodRightSizing &row) {
    QStringList parts;
    if (row.hasCpu) {
        parts << QStringLiteral("cpu %1 -> %2 cores%3")
                     .arg(row.cpuRequestCores, 0, 'f', 2)
                     .arg(row.suggestedCpuCores, 0, 'f', 2)
                     .arg(row.underCpu
                              ? QStringLiteral(" (under-provisioned: raise)")
                              : QString());
    }
    if (row.hasMem) {
        // Compact units, not "GB" with one decimal: a floored 32 Mi
        // suggestion rounded to "0.0 GB", which read as the zero request the
        // floor exists to prevent.
        parts << QStringLiteral("mem %1 -> %2%3")
                     .arg(formatBytesCompact(static_cast<unsigned long long>(
                              row.memRequestBytes)),
                          formatBytesCompact(static_cast<unsigned long long>(
                              row.suggestedMemBytes)))
                     .arg(row.underMem
                              ? QStringLiteral(" (under-provisioned: raise)")
                              : QString());
    }
    QString text = parts.join(QStringLiteral(", "));
    if (!row.underCpu && !row.underMem) {
        text += QStringLiteral(", save %1/mo")
                    .arg(formatCostMoney(row.monthlySavings));
    }
    return text;
}

QString rightSizingSummaryText(const QList<PodRightSizing> &rows, int pods) {
    if (rows.isEmpty()) {
        return QStringLiteral(
            "right-sizing: no pod reports both a request and a usage sample "
            "(metrics.k8s.io) - install metrics-server to measure");
    }
    double savings = 0.0;
    int shrink = 0;
    int under = 0;
    for (const PodRightSizing &row : rows) {
        savings += row.monthlySavings;
        if (row.underCpu || row.underMem) {
            ++under;
        } else {
            ++shrink;
        }
    }
    return QStringLiteral(
               "%1 of %2 pod(s) measured - %3 shrink suggestion(s), %4 "
               "under-provisioned - %5/month of request headroom at the "
               "buffer of %6")
        .arg(rows.size())
        .arg(pods)
        .arg(shrink)
        .arg(under)
        .arg(formatCostMoney(savings))
        .arg(kOpenCostBuffer, 0, 'f', 1);
}
