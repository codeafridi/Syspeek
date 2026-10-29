#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QString>

// One sampled section of a sampler tick (focus, processes, host, disks, ...).
struct TickSection {
    QString name;
    double ms = 0.0;
};

// Cost of the sampler tick itself plus Syspeek's own footprint. Both are
// self-measured: a HUD that claims to be light has to print the number.
struct TickCost {
    bool available = false;
    int ticks = 0;
    double lastMs = -1.0;
    double avgMs = -1.0;
    double worstMs = -1.0;
    QList<TickSection> sections;
};

struct SelfCost {
    bool available = false;
    bool warming = false;
    double cpuPercent = -1.0;
    double rssKb = -1.0;
};

namespace tickbudget {

// Times one named block and appends the result when it goes out of scope.
class Section {
public:
    Section(QList<TickSection> *into, const char *name)
        : m_into(into), m_name(QString::fromLatin1(name)) {
        m_clock.start();
    }
    ~Section() { end(); }
    Section(const Section &) = delete;
    Section &operator=(const Section &) = delete;
    // Record the elapsed time now; the destructor will not append twice.
    void end();

private:
    QList<TickSection> *m_into;
    QString m_name;
    QElapsedTimer m_clock;
    bool m_done = false;
};

// Sections sorted slowest-first, totals averaged over the bounded window.
TickCost summarize(const QList<TickSection> &sections, double totalMs,
                   const QList<double> &recentTotals);

QString tickCostText(const TickCost &cost);
QString tickSectionText(const TickCost &cost);
QString selfCostText(const SelfCost &self);
QString tickBudgetLine(const SelfCost &self, const TickCost &cost);

} // namespace tickbudget
