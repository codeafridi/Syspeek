#include "TcpStatus.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

void addSection(const QByteArray &data, const QString &label,
                SnmpCounters &out) {
    const QList<QByteArray> lines = data.split('\n');
    const QString prefix = label + QLatin1Char(':');
    for (int i = 0; i + 1 < lines.size(); ++i) {
        const QString header = QString::fromLatin1(lines.at(i)).trimmed();
        if (!header.startsWith(prefix)) {
            continue;
        }
        const QString values = QString::fromLatin1(lines.at(i + 1)).trimmed();
        if (!values.startsWith(prefix)) {
            continue;
        }
        static const QRegularExpression whitespace(
            QStringLiteral("\\s+"));
        const QStringList names = header.mid(prefix.size())
                                      .split(whitespace, Qt::SkipEmptyParts);
        const QStringList vals = values.mid(prefix.size())
                                     .split(whitespace, Qt::SkipEmptyParts);
        const int count = qMin(names.size(), vals.size());
        for (int k = 0; k < count; ++k) {
            out.insert(names.at(k), vals.at(k).toULongLong());
        }
        return;
    }
}

double rate(quint64 previous, quint64 current, double seconds) {
    if (seconds <= 0.0 || current < previous) {
        return 0.0;
    }
    return static_cast<double>(current - previous) / seconds;
}

quint64 value(const SnmpCounters &counters, const QString &key) {
    return counters.value(key, 0);
}

} // namespace

TcpCounters parseTcpCounters(const QByteArray &snmp,
                             const QByteArray &netstat) {
    TcpCounters counters;
    addSection(snmp, QStringLiteral("Tcp"), counters.tcp);
    addSection(netstat, QStringLiteral("TcpExt"), counters.ext);
    return counters;
}

bool tcpCountersRedacted(const TcpCounters &counters) {
    if (counters.tcp.isEmpty() && counters.ext.isEmpty()) {
        return true;
    }
    for (auto it = counters.tcp.constBegin(); it != counters.tcp.constEnd();
         ++it) {
        if (it.value() != 0) {
            return false;
        }
    }
    for (auto it = counters.ext.constBegin(); it != counters.ext.constEnd();
         ++it) {
        if (it.value() != 0) {
            return false;
        }
    }
    return true;
}

TcpHealth computeTcpHealth(const TcpCounters &previous,
                           const TcpCounters &current, double intervalMs) {
    TcpHealth health;
    if (intervalMs <= 0.0) {
        return health;
    }
    health.available = true;
    const double seconds = intervalMs / 1000.0;

    const double inSegs = rate(value(previous.tcp, QStringLiteral("InSegs")),
                               value(current.tcp, QStringLiteral("InSegs")),
                               seconds);
    const double outSegs = rate(value(previous.tcp, QStringLiteral("OutSegs")),
                                value(current.tcp, QStringLiteral("OutSegs")),
                                seconds);
    const double retrans =
        rate(value(previous.tcp, QStringLiteral("RetransSegs")),
             value(current.tcp, QStringLiteral("RetransSegs")), seconds);

    health.inSegsPerSec = inSegs;
    health.outSegsPerSec = outSegs;
    health.retransPerSec = retrans;
    health.retransPercent =
        outSegs > 0.0 ? retrans / outSegs * 100.0 : 0.0;
    health.inErrsPerSec = rate(value(previous.tcp, QStringLiteral("InErrs")),
                               value(current.tcp, QStringLiteral("InErrs")),
                               seconds);
    health.outRstsPerSec = rate(value(previous.tcp, QStringLiteral("OutRsts")),
                                value(current.tcp, QStringLiteral("OutRsts")),
                                seconds);
    health.timeoutsPerSec =
        rate(value(previous.ext, QStringLiteral("TCPTimeouts")),
             value(current.ext, QStringLiteral("TCPTimeouts")), seconds);
    health.synRetransPerSec =
        rate(value(previous.ext, QStringLiteral("TCPSynRetrans")),
             value(current.ext, QStringLiteral("TCPSynRetrans")), seconds);
    health.listenDropsPerSec =
        rate(value(previous.ext, QStringLiteral("ListenDrops")),
             value(current.ext, QStringLiteral("ListenDrops")), seconds);
    health.currEstab = value(current.tcp, QStringLiteral("CurrEstab"));
    return health;
}

QString tcpHealthSummaryText(const TcpHealth &health) {
    if (!health.available) {
        return QStringLiteral("tcp: unavailable");
    }
    return QStringLiteral("tcp %1/s out  retrans %2% (%3/s)  estab %4")
        .arg(QString::number(health.outSegsPerSec, 'f', 0),
             QString::number(health.retransPercent, 'f', 2),
             QString::number(health.retransPerSec, 'f', 1),
             QString::number(health.currEstab));
}

QString tcpHealthDetailText(const TcpHealth &health) {
    if (!health.available) {
        return QStringLiteral("tcp: unavailable");
    }
    return QStringLiteral("in       %1/s\nout      %2/s\nretrans  %3/s "
                          "(%4%)\nrst      %5/s\nerrs     %6/s\n"
                          "timeouts %7/s\nsyn-retr %8/s\nlisten-drop %9/s")
        .arg(QString::number(health.inSegsPerSec, 'f', 1),
             QString::number(health.outSegsPerSec, 'f', 1),
             QString::number(health.retransPerSec, 'f', 2),
             QString::number(health.retransPercent, 'f', 2),
             QString::number(health.outRstsPerSec, 'f', 2),
             QString::number(health.inErrsPerSec, 'f', 2),
             QString::number(health.timeoutsPerSec, 'f', 2),
             QString::number(health.synRetransPerSec, 'f', 2),
             QString::number(health.listenDropsPerSec, 'f', 2));
}
