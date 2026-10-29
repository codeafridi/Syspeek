#pragma once

#include "KubeStatus.h"

#include <QList>
#include <QPair>
#include <QString>

// Requests-based allocation pricing, grounded on the shipped OpenCost
// container image (ghcr.io/opencost/opencost:latest, /go/bin/app):
//   "node_cpu_hourly_cost" / "node_ram_hourly_cost" / "node_gpu_hourly_cost"
//     ("hourly cost for each cpu on this node", "hourly cost for each gb of
//      ram on this node")
//   `json:"CPUHourlyCost"` / `json:"RAMHourlyCost"` / `json:"RAMGBHourlyCost"`
//   "CostModel: node pricing has illegal CostPerCPUHr; replacing with custom
//    pricing"
//   `MonthlyCost json:"monthlyCost"`, "HourlyCost", `json:"totalCost"`
//   `json:"efficiency"`, "EfficiencyBufferMultiplier"
//   "Computes CPU and memory efficiency (usage/request ratio), provides
//    recommended resource requests, and calculates potential cost savings.
//    Optional buffer_multiplier parameter (default: 1.2 for 20% headroom)"
inline constexpr double kCostHoursPerMonth = 730.0;

struct WorkloadCost {
    QString ns;
    QString pod;
    double monthly = 0.0;
    int missingRequests = 0;
};

struct CostSummary {
    QList<QPair<QString, double>> byNamespace; // monthly, desc
    QList<WorkloadCost> topWorkloads;          // monthly, desc, capped
    double totalMonthly = 0.0;
    int missingRequestContainers = 0;
    int podsPriced = 0;
};

struct CostSplit {
    double cpuMonthly = 0.0;
    double memMonthly = 0.0;
};

// usage/request efficiency over the pods that report BOTH a usage sample
// (metrics.k8s.io) and a declared request; wastedMonthly prices the request
// headroom above the measured usage.
struct CostEfficiency {
    int podsMeasured = 0;
    double cpuUsageCores = 0.0;
    double cpuRequestCores = 0.0;
    double memUsageBytes = 0.0;
    double memRequestBytes = 0.0;
    double wastedMonthly = 0.0;
};

// Requests-based monthly estimate (OpenCost-style allocation).
CostSummary computeClusterCost(const QList<KubePod> &pods,
                               double cpuPricePerCoreHour,
                               double memPricePerGbHour);

CostSplit computeCostSplit(const QList<KubePod> &pods,
                           double cpuPricePerCoreHour,
                           double memPricePerGbHour);

CostEfficiency computeCostEfficiency(const QList<KubePod> &pods,
                                     double cpuPricePerCoreHour,
                                     double memPricePerGbHour);

// -1 when no pod reported a usage sample against a declared request.
double costCpuEfficiency(const CostEfficiency &efficiency);
double costMemEfficiency(const CostEfficiency &efficiency);

QString formatCostMoney(double usd);
QString formatCostShare(double fraction);

QString costRateText(const CostSplit &split, double cpuPricePerCoreHour,
                     double memPricePerGbHour);
QString costEfficiencyText(const CostEfficiency &efficiency);
// Ratio form for callers that carry only the aggregate (KubeSnapshot):
// a negative ratio means "not measurable on this cluster".
QString costEfficiencyTextFromRatios(double cpuEfficiency,
                                     double memEfficiency, int podsMeasured,
                                     double wastedMonthly);
