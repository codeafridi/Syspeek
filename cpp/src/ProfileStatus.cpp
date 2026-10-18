#include "ProfileStatus.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#if defined(Q_OS_WIN)
#include <process.h>
#else
#include <unistd.h>
#endif

ThreadSample parseTaskStat(const QByteArray &stat, const QString &wchan,
                           int tid) {
    ThreadSample sample;
    sample.tid = tid;
    sample.waitChannel = wchan;
    const int open = stat.indexOf('(');
    const int close = stat.lastIndexOf(')');
    if (open < 0 || close <= open) {
        return sample;
    }
    sample.comm = QString::fromUtf8(stat.mid(open + 1, close - open - 1));
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QStringList fields =
        QString::fromUtf8(stat.mid(close + 2))
            .split(whitespace, Qt::SkipEmptyParts);
    // fields[0] = state (field 3); utime = field 14 -> index 11; stime = 15.
    if (fields.size() > 0) {
        sample.state = fields.at(0);
    }
    if (fields.size() > 11) {
        sample.utimeTicks = fields.at(11).toULongLong();
    }
    if (fields.size() > 12) {
        sample.stimeTicks = fields.at(12).toULongLong();
    }
    return sample;
}

QList<MemoryRegion> parseSmaps(const QByteArray &smaps) {
    QHash<QString, unsigned long long> sums;
    QString current;
    static const QRegularExpression header(
        QStringLiteral("^[0-9a-f]+-[0-9a-f]+\\s"));
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QList<QByteArray> lines = smaps.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromLatin1(raw);
        if (header.match(line).hasMatch()) {
            const QStringList parts = line.split(whitespace, Qt::SkipEmptyParts);
            current = parts.size() > 5 ? parts.mid(5).join(QLatin1Char(' '))
                                       : QStringLiteral("[anon]");
            if (current.isEmpty()) {
                current = QStringLiteral("[anon]");
            }
        } else if (line.startsWith(QStringLiteral("Pss:"))) {
            const QStringList parts = line.split(whitespace, Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                sums[current] += parts.at(1).toULongLong() * 1024ULL;
            }
        }
    }
    QList<MemoryRegion> regions;
    for (auto it = sums.constBegin(); it != sums.constEnd(); ++it) {
        MemoryRegion region;
        region.path = it.key();
        region.pssBytes = it.value();
        regions.append(region);
    }
    std::sort(regions.begin(), regions.end(),
              [](const MemoryRegion &a, const MemoryRegion &b) {
                  return a.pssBytes > b.pssBytes;
              });
    return regions;
}

void ProfileSampler::reset() {
    m_hasBaseline = false;
    m_lastTicks.clear();
    m_timer.invalidate();
}

ProfileSnapshot ProfileSampler::sample(int pid) {
    ProfileSnapshot snapshot;
    const QString base = QStringLiteral("/proc/%1").arg(pid);

    QFile cmd(base + QStringLiteral("/comm"));
    if (cmd.open(QIODevice::ReadOnly)) {
        snapshot.comm = QString::fromUtf8(cmd.readAll()).trimmed();
    }

    QDir taskDir(base + QStringLiteral("/task"));
    if (!taskDir.exists()) {
        return snapshot;
    }

#if defined(Q_OS_WIN)
    return snapshot;
#else
    const long hz = sysconf(_SC_CLK_TCK);
    const QStringList tids =
        taskDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QList<ThreadSample> samples;
    QHash<int, QPair<quint64, quint64>> current;
    for (const QString &entry : tids) {
        bool ok = false;
        const int tid = entry.toInt(&ok);
        if (!ok) {
            continue;
        }
        QFile statFile(base + QStringLiteral("/task/") + entry
                       + QStringLiteral("/stat"));
        if (!statFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        QString wchan;
        QFile wchanFile(base + QStringLiteral("/task/") + entry
                        + QStringLiteral("/wchan"));
        if (wchanFile.open(QIODevice::ReadOnly)) {
            wchan = QString::fromUtf8(wchanFile.readAll()).trimmed();
        }
        const ThreadSample sample =
            parseTaskStat(statFile.readAll(), wchan, tid);
        samples.append(sample);
        current.insert(tid, qMakePair(sample.utimeTicks, sample.stimeTicks));
    }

    snapshot.available = true;
    snapshot.threadCount = samples.size();
    snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();

    if (!m_hasBaseline) {
        m_lastTicks = current;
        m_hasBaseline = true;
        m_timer.restart();
        snapshot.warming = true;
        return snapshot;
    }

    const double seconds =
        static_cast<double>(m_timer.restart()) / 1000.0;
    QHash<QString, int> states;
    QHash<QString, int> waits;

    for (const ThreadSample &sample : samples) {
        ThreadProfile profile;
        profile.tid = sample.tid;
        profile.comm = sample.comm;
        profile.state = sample.state;
        profile.waitChannel = sample.waitChannel;
        if (m_lastTicks.contains(sample.tid) && seconds > 0.0 && hz > 0) {
            const QPair<quint64, quint64> last = m_lastTicks.value(sample.tid);
            const quint64 nowTicks = sample.utimeTicks + sample.stimeTicks;
            const quint64 lastTicks = last.first + last.second;
            if (nowTicks >= lastTicks) {
                profile.cpuPercent =
                    (static_cast<double>(nowTicks - lastTicks)
                     / static_cast<double>(hz) / seconds)
                    * 100.0;
            }
        }
        snapshot.totalCpuPercent += profile.cpuPercent;
        snapshot.threads.append(profile);

        states[sample.state] += 1;
        const QString wait =
            sample.waitChannel.isEmpty() ? QStringLiteral("-")
                                         : sample.waitChannel;
        waits[wait] += 1;
    }

    std::sort(snapshot.threads.begin(), snapshot.threads.end(),
              [](const ThreadProfile &a, const ThreadProfile &b) {
                  return a.cpuPercent > b.cpuPercent;
              });
    if (snapshot.threads.size() > 20) {
        snapshot.threads = snapshot.threads.mid(0, 20);
    }
    for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
        snapshot.states.append(qMakePair(it.key(), it.value()));
    }
    for (auto it = waits.constBegin(); it != waits.constEnd(); ++it) {
        snapshot.waitChannels.append(qMakePair(it.key(), it.value()));
    }
    std::sort(snapshot.states.begin(), snapshot.states.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  return a.second > b.second;
              });
    std::sort(snapshot.waitChannels.begin(), snapshot.waitChannels.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  return a.second > b.second;
              });
    if (snapshot.waitChannels.size() > 12) {
        snapshot.waitChannels = snapshot.waitChannels.mid(0, 12);
    }

    QFile smaps(base + QStringLiteral("/smaps"));
    if (smaps.open(QIODevice::ReadOnly)) {
        const QList<MemoryRegion> regions = parseSmaps(smaps.readAll());
        for (const MemoryRegion &region : regions) {
            snapshot.totalPssBytes += region.pssBytes;
        }
        snapshot.regions = regions.mid(0, 12);
    }

    m_lastTicks = current;
    return snapshot;
#endif
}

QString profileSummaryText(const ProfileSnapshot &snapshot) {
    if (!snapshot.available) {
        return QStringLiteral("profiles: unavailable");
    }
    if (snapshot.warming) {
        return QStringLiteral("profiles: sampling…");
    }
    QString topWait;
    if (!snapshot.waitChannels.isEmpty()) {
        topWait = QStringLiteral("  top wait %1")
                      .arg(snapshot.waitChannels.first().first);
    }
    return QStringLiteral("%1 threads  cpu %2%  rss %3 MB%4")
        .arg(snapshot.threadCount)
        .arg(QString::number(snapshot.totalCpuPercent, 'f', 0))
        .arg(QString::number(snapshot.totalPssBytes / (1024.0 * 1024.0), 'f', 1),
             topWait);
}

QString profileThreadsText(const ProfileSnapshot &snapshot) {
    if (snapshot.threads.isEmpty()) {
        return QStringLiteral("--");
    }
    QString text;
    for (const ThreadProfile &thread : snapshot.threads) {
        QString wait = thread.waitChannel;
        if (wait.isEmpty()) {
            wait = QStringLiteral("-");
        }
        text += QStringLiteral("tid %1  %2  %3%  %4  %5\n")
                    .arg(QString::number(thread.tid).rightJustified(7, ' '),
                         thread.comm.leftJustified(18, ' '),
                         QString::number(thread.cpuPercent, 'f', 1)
                             .rightJustified(5, ' '),
                         thread.state)
                    .arg(wait);
    }
    return text.trimmed();
}

QString profileOffCpuText(const ProfileSnapshot &snapshot) {
    QString states;
    for (const QPair<QString, int> &entry : snapshot.states) {
        states += QStringLiteral("%1:%2 ").arg(entry.first).arg(entry.second);
    }
    QString waits;
    for (const QPair<QString, int> &entry : snapshot.waitChannels) {
        waits += QStringLiteral("%1:%2 ").arg(entry.first).arg(entry.second);
    }
    return QStringLiteral("states  %1\noff-cpu %2")
        .arg(states.trimmed(), waits.trimmed());
}

QString profileMemoryText(const ProfileSnapshot &snapshot) {
    if (snapshot.regions.isEmpty()) {
        return QStringLiteral("--");
    }
    QString text;
    for (const MemoryRegion &region : snapshot.regions) {
        text += QStringLiteral("%1  %2 MB\n")
                    .arg(region.path.left(48).leftJustified(48, ' '),
                         QString::number(region.pssBytes / (1024.0 * 1024.0),
                                         'f', 1));
    }
    return text.trimmed();
}
