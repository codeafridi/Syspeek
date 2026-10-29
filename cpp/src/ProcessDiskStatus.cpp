#include "ProcessDiskStatus.h"

#include "ProcessNetStatus.h"

#include <algorithm>

namespace {

constexpr int kPriorCap = 4096;

double diskRate(long long current, long long previous, qint64 windowMs) {
    if (current < 0 || previous < 0 || windowMs <= 0) {
        return -1.0;
    }
    if (current < previous) {
        return 0.0;
    }
    return static_cast<double>(current - previous) * 1000.0
           / static_cast<double>(windowMs);
}

ProcessDiskConsumer consumerFrom(int pid, const ProcessDiskRate &rate) {
    ProcessDiskConsumer consumer;
    consumer.pid = pid;
    consumer.readPerSec = rate.readPerSec;
    consumer.writePerSec = rate.writePerSec;
    return consumer;
}

}  // namespace

QHash<int, ProcessDiskRate> processDiskRates(
    const QHash<int, ProcessDiskCounters> &counters, qint64 windowMs,
    QHash<int, ProcessDiskCounters> &prior) {

    QHash<int, ProcessDiskRate> rates;
    rates.reserve(counters.size());

    for (auto it = counters.constBegin(); it != counters.constEnd(); ++it) {
        ProcessDiskRate rate;
        const auto previous = prior.constFind(it.key());
        if (it->readBytes >= 0 && it->writeBytes >= 0 && previous != prior.constEnd()
            && windowMs > 0) {
            rate.readPerSec = diskRate(it->readBytes, previous->readBytes, windowMs);
            rate.writePerSec =
                diskRate(it->writeBytes, previous->writeBytes, windowMs);
        }
        // A counter that went backwards means the pid was recycled or the
        // block layer reset its accounting: rebase to zero instead of
        // reporting a negative spike (an unsigned wrap would otherwise
        // print petabytes per second).
        if (previous != prior.constEnd() && it->readBytes >= 0
            && previous->readBytes >= 0 && it->readBytes < previous->readBytes) {
            rate.readPerSec = 0.0;
        }
        if (previous != prior.constEnd() && it->writeBytes >= 0
            && previous->writeBytes >= 0 && it->writeBytes < previous->writeBytes) {
            rate.writePerSec = 0.0;
        }
        rates.insert(it.key(), rate);
    }

    // Keep the prior map bounded and free of dead pids: only pids seen in
    // this sample stay resident, so a long-running HUD cannot grow.
    if (counters.size() > kPriorCap) {
        prior.clear();
    } else {
        for (auto it = prior.begin(); it != prior.end();) {
            if (!counters.contains(it.key())) {
                it = prior.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto it = counters.constBegin(); it != counters.constEnd(); ++it) {
        prior.insert(it.key(), it.value());
    }
    return rates;
}

QList<ProcessDiskConsumer> processDiskTopConsumers(
    const QHash<int, ProcessDiskRate> &rates, const QHash<int, QString> &names,
    int limit) {

    QList<ProcessDiskConsumer> out;
    out.reserve(rates.size());
    for (auto it = rates.constBegin(); it != rates.constEnd(); ++it) {
        if (it->readPerSec < 0.0 && it->writePerSec < 0.0) {
            continue;
        }
        ProcessDiskConsumer consumer = consumerFrom(it.key(), it.value());
        consumer.name = names.value(it.key());
        if (consumer.name.isEmpty()) {
            consumer.name = QStringLiteral("pid %1").arg(it.key());
        }
        if (consumer.readPerSec + consumer.writePerSec <= 0.0) {
            continue;
        }
        out.append(consumer);
    }
    std::sort(out.begin(), out.end(),
              [](const ProcessDiskConsumer &a, const ProcessDiskConsumer &b) {
                  const double totalA =
                      std::max(0.0, a.readPerSec) + std::max(0.0, a.writePerSec);
                  const double totalB =
                      std::max(0.0, b.readPerSec) + std::max(0.0, b.writePerSec);
                  if (totalA != totalB) {
                      return totalA > totalB;
                  }
                  return a.pid < b.pid;
              });
    if (limit > 0 && out.size() > limit) {
        out.resize(limit);
    }
    return out;
}

double processDiskTotal(const QHash<int, ProcessDiskRate> &rates, bool read) {
    double total = 0.0;
    bool seen = false;
    for (auto it = rates.constBegin(); it != rates.constEnd(); ++it) {
        const double value = read ? it->readPerSec : it->writePerSec;
        if (value >= 0.0) {
            total += value;
            seen = true;
        }
    }
    return seen ? total : -1.0;
}

QString processDiskSummaryText(int tracked, int unreadable,
                               double totalReadPerSec,
                               double totalWritePerSec) {
    if (tracked <= 0) {
        return QStringLiteral("no process block i/o this tick");
    }
    QString text = QStringLiteral("%1 processes tracked").arg(tracked);
    if (totalReadPerSec >= 0.0 || totalWritePerSec >= 0.0) {
        text += QStringLiteral(" - read %1 / write %2")
                    .arg(formatNetRate(totalReadPerSec),
                         formatNetRate(totalWritePerSec));
    }
    if (unreadable > 0) {
        text += QStringLiteral(", %1 other users").arg(unreadable);
    }
    return text;
}

QString processDiskConsumerText(const QList<ProcessDiskConsumer> &consumers) {
    if (consumers.isEmpty()) {
        return QStringLiteral("no block i/o this tick");
    }
    QStringList lines;
    for (const ProcessDiskConsumer &consumer : consumers) {
        lines.append(QStringLiteral("%1  pid %2  read %3  write %4")
                         .arg(consumer.name)
                         .arg(consumer.pid)
                         .arg(formatNetRate(consumer.readPerSec),
                              formatNetRate(consumer.writePerSec)));
    }
    return lines.join(QLatin1Char('\n'));
}
