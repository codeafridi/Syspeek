#pragma once

#include "DiskStatus.h"

#include "platform/SystemBackend.h"

#include <QElapsedTimer>

#include <functional>

struct DiskSource {
    std::function<bool(QByteArray &)> diskstats;
    std::function<bool(QByteArray &)> pressure;
    std::function<bool(QList<DiskCounters> &)> counters;

    static DiskSource system();
    static DiskSource fromBackend(SystemBackend &backend);
};

class DiskSampler {
public:
    explicit DiskSampler(DiskSource source = DiskSource::system());

    DiskSnapshot sample();
    void reset();

private:
    DiskSource m_source;
    bool m_hasBaseline = false;
    QList<DiskCounters> m_last;
    QElapsedTimer m_timer;
};
