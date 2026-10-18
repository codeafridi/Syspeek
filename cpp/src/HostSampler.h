#pragma once

#include "LinuxProcess.h"

#include <QElapsedTimer>

#include <functional>

class SystemBackend;

struct HostSample {
    bool available = false;
    bool warming = false;
    double cpuPercent = 0.0;
    double netRxPerSec = 0.0;
    double netTxPerSec = 0.0;
    linuxproc::HostMem mem;
    bool memAvailable = false;
    unsigned long long diskTotalBytes = 0;
    unsigned long long diskFreeBytes = 0;
    bool diskAvailable = false;
    double load1 = 0.0;
    double load5 = 0.0;
    double load15 = 0.0;
    bool loadAvailable = false;
};

struct HostSource {
    std::function<bool(linuxproc::HostCpuTimes &)> cpu;
    std::function<bool(linuxproc::HostMem &)> mem;
    std::function<bool(linuxproc::NetTotals &)> net;
    std::function<bool(double &, double &, double &)> load;
    std::function<bool(unsigned long long &, unsigned long long &)> disk;

    static HostSource fromBackend(SystemBackend &backend);
    static HostSource system();
};

class HostSampler {
public:
    explicit HostSampler(HostSource source = HostSource::system());

    HostSample sample();
    void reset();

private:
    HostSource m_source;
    bool m_hasBaseline = false;
    linuxproc::HostCpuTimes m_lastCpu;
    linuxproc::NetTotals m_lastNet;
    QElapsedTimer m_timer;
};
