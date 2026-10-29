#include "TickBudget.h"

#include <QStringList>

#include <algorithm>

namespace tickbudget {

void Section::end() {
    if (m_done || m_into == nullptr) {
        return;
    }
    m_done = true;
    TickSection row;
    row.name = m_name;
    row.ms = static_cast<double>(m_clock.nsecsElapsed()) / 1.0e6;
    m_into->append(row);
}

TickCost summarize(const QList<TickSection> &sections, double totalMs,
                   const QList<double> &recentTotals) {
    TickCost cost;
    cost.available = totalMs >= 0.0;
    cost.sections = sections;
    std::stable_sort(cost.sections.begin(), cost.sections.end(),
                     [](const TickSection &a, const TickSection &b) {
                         return a.ms > b.ms;
                     });
    cost.ticks = static_cast<int>(recentTotals.size());
    cost.lastMs = totalMs;
    if (!recentTotals.isEmpty()) {
        double sum = 0.0;
        double worst = 0.0;
        for (double value : recentTotals) {
            sum += value;
            worst = std::max(worst, value);
        }
        cost.avgMs = sum / static_cast<double>(recentTotals.size());
        cost.worstMs = worst;
    }
    return cost;
}

namespace {

QString milliseconds(double ms) {
    if (ms < 0.0) {
        return QStringLiteral("--");
    }
    if (ms < 10.0) {
        return QStringLiteral("%1 ms").arg(ms, 0, 'f', 1);
    }
    return QStringLiteral("%1 ms").arg(qRound(ms));
}

} // namespace

QString tickCostText(const TickCost &cost) {
    if (!cost.available) {
        return QStringLiteral("measuring");
    }
    QString text = QStringLiteral("%1 last").arg(milliseconds(cost.lastMs));
    if (cost.ticks > 0) {
        text += QStringLiteral("  avg %1  worst %2  over %3 ticks")
                    .arg(milliseconds(cost.avgMs), milliseconds(cost.worstMs),
                         QString::number(cost.ticks));
    }
    return text;
}

QString tickSectionText(const TickCost &cost) {
    if (cost.sections.isEmpty()) {
        return QStringLiteral("no section timings yet");
    }
    QStringList parts;
    for (const TickSection &section : cost.sections) {
        if (section.ms < 0.05) {
            continue;
        }
        parts.append(QStringLiteral("%1 %2").arg(
            section.name, milliseconds(section.ms)));
        if (parts.size() >= 5) {
            break;
        }
    }
    if (parts.isEmpty()) {
        return QStringLiteral("every section under 0.05 ms");
    }
    return parts.join(QStringLiteral("  "));
}

QString selfCostText(const SelfCost &self) {
    if (!self.available) {
        return QStringLiteral("not measurable on this platform");
    }
    QStringList parts;
    if (self.warming || self.cpuPercent < 0.0) {
        parts.append(QStringLiteral("cpu warming"));
    } else {
        parts.append(QStringLiteral("cpu %1%").arg(self.cpuPercent, 0, 'f',
                                                   self.cpuPercent < 10.0 ? 2 : 1));
    }
    if (self.rssKb >= 0.0) {
        const double mb = self.rssKb / 1024.0;
        parts.append(QStringLiteral("rss %1 MB").arg(mb, 0, 'f',
                                                     mb < 100.0 ? 1 : 0));
    }
    return parts.join(QStringLiteral("  "));
}

QString tickBudgetLine(const SelfCost &self, const TickCost &cost) {
    return QStringLiteral("%1  tick %2")
        .arg(selfCostText(self), tickCostText(cost));
}

} // namespace tickbudget
