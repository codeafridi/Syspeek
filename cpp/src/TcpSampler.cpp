#include "TcpSampler.h"

#include "platform/SystemBackend.h"

#include <QFile>

TcpSource TcpSource::system() {
    TcpSource source;
    source.snmp = [](QByteArray &out) {
        QFile file(QStringLiteral("/proc/net/snmp"));
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        out = file.readAll();
        return true;
    };
    source.netstat = [](QByteArray &out) {
        QFile file(QStringLiteral("/proc/net/netstat"));
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        out = file.readAll();
        return true;
    };
    return source;
}

TcpSource TcpSource::fromBackend(SystemBackend &backend) {
    TcpSource source;
    source.counters = [&backend](TcpCounters &out) {
        return backend.readTcpCounters(out);
    };
    return source;
}

TcpSampler::TcpSampler(TcpSource source)
    : m_source(std::move(source)) {
}

void TcpSampler::reset() {
    m_hasBaseline = false;
    m_last = TcpCounters{};
    m_timer.invalidate();
}

TcpHealth TcpSampler::sample() {
    TcpHealth out;
    TcpCounters counters;
    if (m_source.counters) {
        if (!m_source.counters(counters)) {
            return out;
        }
    } else {
        QByteArray snmp;
        QByteArray netstat;
        if (!m_source.snmp || !m_source.netstat || !m_source.snmp(snmp)
            || !m_source.netstat(netstat)) {
            return out;
        }
        counters = parseTcpCounters(snmp, netstat);
    }

    if (!m_hasBaseline) {
        m_last = counters;
        m_hasBaseline = true;
        m_timer.restart();
        out.available = false;
        return out;
    }

    double intervalMs = 0.0;
    if (m_timer.isValid()) {
        intervalMs = static_cast<double>(m_timer.restart());
    } else {
        m_timer.start();
    }

    out = computeTcpHealth(m_last, counters, intervalMs);
    m_last = counters;
    return out;
}
