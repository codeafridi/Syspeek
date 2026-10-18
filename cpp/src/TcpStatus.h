#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QtGlobal>

using SnmpCounters = QHash<QString, quint64>;

struct TcpCounters {
    SnmpCounters tcp;
    SnmpCounters ext;
};

struct TcpHealth {
    bool available = false;
    double inSegsPerSec = 0.0;
    double outSegsPerSec = 0.0;
    double retransPerSec = 0.0;
    double retransPercent = 0.0;
    double inErrsPerSec = 0.0;
    double outRstsPerSec = 0.0;
    double timeoutsPerSec = 0.0;
    double synRetransPerSec = 0.0;
    double listenDropsPerSec = 0.0;
    quint64 currEstab = 0;
};

TcpCounters parseTcpCounters(const QByteArray &snmp, const QByteArray &netstat);
TcpHealth computeTcpHealth(const TcpCounters &previous,
                           const TcpCounters &current, double intervalMs);
bool tcpCountersRedacted(const TcpCounters &counters);
QString tcpHealthSummaryText(const TcpHealth &health);
QString tcpHealthDetailText(const TcpHealth &health);
