#include "DiskSampler.h"

#include <QDateTime>
#include <QFile>

DiskSource DiskSource::system() {
    DiskSource source;
    source.diskstats = [](QByteArray &out) {
        QFile file(QStringLiteral("/proc/diskstats"));
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        out = file.readAll();
        return true;
    };
    source.pressure = [](QByteArray &out) {
        QFile file(QStringLiteral("/proc/pressure/io"));
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        out = file.readAll();
        return true;
    };
    return source;
}

DiskSource DiskSource::fromBackend(SystemBackend &backend) {
    DiskSource source = system();
    source.counters = [&backend](QList<DiskCounters> &out) {
        return backend.readDiskCounters(out);
    };
    return source;
}

DiskSampler::DiskSampler(DiskSource source)
    : m_source(std::move(source)) {
}

void DiskSampler::reset() {
    m_hasBaseline = false;
    m_last.clear();
    m_timer.invalidate();
}

DiskSnapshot DiskSampler::sample() {
    DiskSnapshot out;

    QList<DiskCounters> counters;
    if (m_source.counters) {
        if (!m_source.counters(counters)) {
            return out;
        }
    } else {
        QByteArray raw;
        if (!m_source.diskstats || !m_source.diskstats(raw)) {
            return out;
        }
        counters = parseDiskstats(raw);
    }

    QByteArray pressureRaw;
    if (m_source.pressure && m_source.pressure(pressureRaw)) {
        out.pressure = parsePressureIo(pressureRaw);
    }

    out.available = true;
    out.sampledAtMs = QDateTime::currentMSecsSinceEpoch();

    if (!m_hasBaseline) {
        m_last = counters;
        m_hasBaseline = true;
        m_timer.restart();
        out.warming = true;
        return out;
    }

    double intervalMs = 0.0;
    if (m_timer.isValid()) {
        intervalMs = static_cast<double>(m_timer.restart());
    } else {
        m_timer.start();
    }

    out.intervalMs = intervalMs;
    out.disks = computeDiskRates(m_last, counters, intervalMs);
    m_last = counters;
    out.warming = false;
    return out;
}
