#include "HostSampler.h"

#include "platform/PlatformFactory.h"

HostSource HostSource::fromBackend(SystemBackend &backend) {
    HostSource source;
    source.cpu = [&backend](linuxproc::HostCpuTimes &out) {
        return backend.hostCpuTimes(out);
    };
    source.mem = [&backend](linuxproc::HostMem &out) {
        return backend.hostMem(out);
    };
    source.net = [&backend](linuxproc::NetTotals &out) {
        return backend.hostNet(out);
    };
    source.load = [&backend](double &l1, double &l5, double &l15) {
        return backend.hostLoad(l1, l5, l15);
    };
    source.disk = [&backend](unsigned long long &total, unsigned long long &free) {
        return backend.diskUsage(QStringLiteral("/"), total, free);
    };
    return source;
}

HostSource HostSource::system() {
    return fromBackend(systemBackend());
}

HostSampler::HostSampler(HostSource source)
    : m_source(std::move(source)) {
}

void HostSampler::reset() {
    m_hasBaseline = false;
    m_lastCpu = linuxproc::HostCpuTimes{};
    m_lastNet = linuxproc::NetTotals{};
    m_timer.invalidate();
}

HostSample HostSampler::sample() {
    HostSample out;

    linuxproc::HostCpuTimes cpu;
    if (!m_source.cpu || !m_source.cpu(cpu)) {
        reset();
        return out;
    }

    if (m_source.mem) {
        out.memAvailable = m_source.mem(out.mem);
    }
    if (m_source.disk) {
        out.diskAvailable =
            m_source.disk(out.diskTotalBytes, out.diskFreeBytes);
    }
    if (m_source.load) {
        out.loadAvailable = m_source.load(out.load1, out.load5, out.load15);
    }

    linuxproc::NetTotals net;
    const bool netOk = m_source.net && m_source.net(net);

    if (!m_hasBaseline) {
        m_lastCpu = cpu;
        m_lastNet = net;
        m_hasBaseline = true;
        m_timer.restart();
        out.available = true;
        out.warming = true;
        return out;
    }

    double seconds = 0.0;
    if (m_timer.isValid()) {
        seconds = static_cast<double>(m_timer.restart()) / 1000.0;
    } else {
        m_timer.start();
    }

    out.cpuPercent = linuxproc::hostCpuBusyPercent(m_lastCpu, cpu);
    if (netOk) {
        out.netRxPerSec =
            linuxproc::netRatePerSec(m_lastNet.rxBytes, net.rxBytes, seconds);
        out.netTxPerSec =
            linuxproc::netRatePerSec(m_lastNet.txBytes, net.txBytes, seconds);
    }

    m_lastCpu = cpu;
    if (netOk) {
        m_lastNet = net;
    }
    out.available = true;
    out.warming = false;
    return out;
}
