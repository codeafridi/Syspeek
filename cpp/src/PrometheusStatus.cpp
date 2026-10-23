#include "PrometheusStatus.h"

#include <QList>
#include <QStringList>

namespace {

QString escapeLabel(const QString &value) {
    QString out = value;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    out.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return out;
}

QString num(double value) {
    return QString::number(value, 'f', 3);
}

QString gauge(const char *name, double value) {
    return QStringLiteral("%1 %2").arg(name, num(value));
}

QString gaugeInt(const char *name, long long value) {
    return QStringLiteral("%1 %2").arg(name).arg(value);
}

QString labeled(const char *name, const QString &label, const QString &value,
                double v) {
    return QStringLiteral("%1{%2=\"%3\"} %4")
        .arg(name, label, escapeLabel(value), num(v));
}

} // namespace

QString prometheusText(const DisplaySnapshot &s) {
    QStringList lines;
    lines.reserve(96);

    if (s.hostAvailable) {
        lines.append(QStringLiteral(
            "# HELP syspeek_host_cpu_percent Host CPU busy percent."));
        lines.append(QStringLiteral("# TYPE syspeek_host_cpu_percent gauge"));
        lines.append(gauge("syspeek_host_cpu_percent", s.hostCpuPercent));
        lines.append(QStringLiteral(
            "# HELP syspeek_host_steal_percent Host CPU steal percent."));
        lines.append(QStringLiteral("# TYPE syspeek_host_steal_percent gauge"));
        if (s.hostStealPercent >= 0.0) {
            lines.append(
                gauge("syspeek_host_steal_percent", s.hostStealPercent));
        }
        lines.append(QStringLiteral(
            "# HELP syspeek_host_mem_total_bytes Host physical memory size."));
        lines.append(QStringLiteral("# TYPE syspeek_host_mem_total_bytes gauge"));
        if (s.hostMemTotalKb > 0) {
            lines.append(gaugeInt("syspeek_host_mem_total_bytes",
                                  static_cast<long long>(s.hostMemTotalKb)
                                      * 1024));
        }
        lines.append(QStringLiteral(
            "# HELP syspeek_host_mem_available_bytes Host available memory."));
        lines.append(
            QStringLiteral("# TYPE syspeek_host_mem_available_bytes gauge"));
        if (s.hostMemTotalKb > 0) {
            lines.append(gaugeInt("syspeek_host_mem_available_bytes",
                                  static_cast<long long>(s.hostMemAvailableKb)
                                      * 1024));
        }
        lines.append(QStringLiteral(
            "# HELP syspeek_host_disk_free_bytes Host free disk space."));
        lines.append(QStringLiteral("# TYPE syspeek_host_disk_free_bytes gauge"));
        if (s.hostDiskTotalBytes > 0) {
            lines.append(gaugeInt("syspeek_host_disk_free_bytes",
                                  static_cast<long long>(s.hostDiskFreeBytes)));
        }
        lines.append(QStringLiteral("# HELP syspeek_host_load1 Host 1m load."));
        lines.append(QStringLiteral("# TYPE syspeek_host_load1 gauge"));
        lines.append(gauge("syspeek_host_load1", s.hostLoad1));
        if (s.kernelLimits.available) {
            lines.append(QStringLiteral(
                "# HELP syspeek_kernel_fd_allocated Allocated file descriptors."));
            lines.append(
                QStringLiteral("# TYPE syspeek_kernel_fd_allocated gauge"));
            lines.append(gaugeInt("syspeek_kernel_fd_allocated",
                                  static_cast<long long>(
                                      s.kernelLimits.fdAllocated)));
            lines.append(QStringLiteral(
                "# HELP syspeek_kernel_fd_max File descriptor limit."));
            lines.append(QStringLiteral("# TYPE syspeek_kernel_fd_max gauge"));
            lines.append(gaugeInt("syspeek_kernel_fd_max",
                                  static_cast<long long>(
                                      s.kernelLimits.fdMax)));
            if (s.kernelLimits.runningProcs >= 0) {
                lines.append(QStringLiteral(
                    "# HELP syspeek_kernel_pids_running Runnable processes."));
                lines.append(
                    QStringLiteral("# TYPE syspeek_kernel_pids_running gauge"));
                lines.append(gaugeInt("syspeek_kernel_pids_running",
                                      s.kernelLimits.runningProcs));
                lines.append(QStringLiteral(
                    "# HELP syspeek_kernel_pids_blocked Uninterruptible "
                    "processes."));
                lines.append(
                    QStringLiteral("# TYPE syspeek_kernel_pids_blocked gauge"));
                lines.append(gaugeInt("syspeek_kernel_pids_blocked",
                                      s.kernelLimits.blockedProcs));
            }
        }
    }

    if (s.cpuValid) {
        const QString labels =
            QStringLiteral("app=\"%1\",pid=\"%2\"")
                .arg(escapeLabel(s.appName), escapeLabel(s.pid));
        lines.append(QStringLiteral(
            "# HELP syspeek_app_cpu_percent Focused app CPU percent."));
        lines.append(QStringLiteral("# TYPE syspeek_app_cpu_percent gauge"));
        lines.append(
            QStringLiteral("syspeek_app_cpu_percent{%1} %2")
                .arg(labels, num(s.cpuPercent)));
        lines.append(QStringLiteral(
            "# HELP syspeek_app_rss_bytes Focused app resident memory."));
        lines.append(QStringLiteral("# TYPE syspeek_app_rss_bytes gauge"));
        lines.append(QStringLiteral("syspeek_app_rss_bytes{%1} %2")
                         .arg(labels)
                         .arg(static_cast<long long>(s.rssKb) * 1024));
        lines.append(QStringLiteral(
            "# HELP syspeek_app_threads Focused app threads."));
        lines.append(QStringLiteral("# TYPE syspeek_app_threads gauge"));
        int threads = 0;
        for (const ProcessRow &row : s.processes) {
            threads += row.threads;
        }
        lines.append(
            QStringLiteral("syspeek_app_threads{%1} %2").arg(labels).arg(threads));
        lines.append(QStringLiteral(
            "# HELP syspeek_app_fds Focused app open file descriptors."));
        lines.append(QStringLiteral("# TYPE syspeek_app_fds gauge"));
        int fds = 0;
        bool fdsValid = false;
        for (const ProcessRow &row : s.processes) {
            if (row.fds >= 0) {
                fds += row.fds;
                fdsValid = true;
            }
        }
        if (fdsValid) {
            lines.append(QStringLiteral("syspeek_app_fds{%1} %2")
                             .arg(labels)
                             .arg(fds));
        }
    }

    lines.append(
        QStringLiteral("# HELP syspeek_tcp_sockets TCP sockets by state."));
    lines.append(QStringLiteral("# TYPE syspeek_tcp_sockets gauge"));
    for (const auto &pair : s.socketStateCounts) {
        lines.append(labeled("syspeek_tcp_sockets", QStringLiteral("state"),
                             pair.first, pair.second));
    }

    if (!s.interfaces.isEmpty()) {
        lines.append(QStringLiteral(
            "# HELP syspeek_net_rx_bytes_total Network receive bytes."));
        lines.append(QStringLiteral("# TYPE syspeek_net_rx_bytes_total counter"));
        lines.append(QStringLiteral(
            "# HELP syspeek_net_tx_bytes_total Network transmit bytes."));
        lines.append(QStringLiteral("# TYPE syspeek_net_tx_bytes_total counter"));
        lines.append(QStringLiteral(
            "# HELP syspeek_net_rx_bytes_per_second Network receive rate."));
        lines.append(
            QStringLiteral("# TYPE syspeek_net_rx_bytes_per_second gauge"));
        lines.append(QStringLiteral(
            "# HELP syspeek_net_tx_bytes_per_second Network transmit rate."));
        lines.append(
            QStringLiteral("# TYPE syspeek_net_tx_bytes_per_second gauge"));
        for (const InterfaceRow &iface : s.interfaces) {
            lines.append(labeled("syspeek_net_rx_bytes_total",
                                 QStringLiteral("device"), iface.name,
                                 static_cast<double>(iface.rxBytes)));
            lines.append(labeled("syspeek_net_tx_bytes_total",
                                 QStringLiteral("device"), iface.name,
                                 static_cast<double>(iface.txBytes)));
            lines.append(labeled("syspeek_net_rx_bytes_per_second",
                                 QStringLiteral("device"), iface.name,
                                 iface.rxPerSec));
            lines.append(labeled("syspeek_net_tx_bytes_per_second",
                                 QStringLiteral("device"), iface.name,
                                 iface.txPerSec));
        }
    }

    if (s.disksAvailable && !s.disksWarming) {
        lines.append(QStringLiteral(
            "# HELP syspeek_disk_read_bytes_per_second Disk read rate."));
        lines.append(QStringLiteral(
            "# TYPE syspeek_disk_read_bytes_per_second gauge"));
        lines.append(QStringLiteral(
            "# HELP syspeek_disk_write_bytes_per_second Disk write rate."));
        lines.append(QStringLiteral(
            "# TYPE syspeek_disk_write_bytes_per_second gauge"));
        lines.append(QStringLiteral(
            "# HELP syspeek_disk_io_util_percent Disk busy percent."));
        lines.append(QStringLiteral("# TYPE syspeek_disk_io_util_percent gauge"));
        lines.append(QStringLiteral(
            "# HELP syspeek_disk_await_seconds Average I/O wait per op."));
        lines.append(QStringLiteral("# TYPE syspeek_disk_await_seconds gauge"));
        for (const DiskRow &disk : s.disks) {
            lines.append(labeled("syspeek_disk_read_bytes_per_second",
                                 QStringLiteral("device"), disk.name,
                                 disk.readKbPerSec * 1024.0));
            lines.append(labeled("syspeek_disk_write_bytes_per_second",
                                 QStringLiteral("device"), disk.name,
                                 disk.writeKbPerSec * 1024.0));
            lines.append(labeled("syspeek_disk_io_util_percent",
                                 QStringLiteral("device"), disk.name,
                                 disk.utilPercent));
            lines.append(labeled("syspeek_disk_await_seconds",
                                 QStringLiteral("device"), disk.name,
                                 disk.readAwaitMs / 1000.0));
        }
    }

    if (s.cgroupAvailable) {
        lines.append(QStringLiteral(
            "# HELP syspeek_cgroup_mem_usage_bytes Focused app cgroup memory."));
        lines.append(QStringLiteral("# TYPE syspeek_cgroup_mem_usage_bytes gauge"));
        lines.append(gaugeInt("syspeek_cgroup_mem_usage_bytes",
                              static_cast<long long>(s.cgroupMemUsage)));
        if (s.cgroupMemLimit >= 0) {
            lines.append(QStringLiteral(
                "# HELP syspeek_cgroup_mem_limit_bytes Focused app cgroup "
                "memory limit."));
            lines.append(QStringLiteral(
                "# TYPE syspeek_cgroup_mem_limit_bytes gauge"));
            lines.append(gaugeInt("syspeek_cgroup_mem_limit_bytes",
                                  s.cgroupMemLimit));
        }
        if (s.cgroupHasMemPeak) {
            lines.append(QStringLiteral(
                "# HELP syspeek_cgroup_mem_peak_bytes Focused app cgroup peak "
                "memory."));
            lines.append(QStringLiteral(
                "# TYPE syspeek_cgroup_mem_peak_bytes gauge"));
            lines.append(gaugeInt("syspeek_cgroup_mem_peak_bytes",
                                  static_cast<long long>(s.cgroupMemPeakBytes)));
        }
        lines.append(QStringLiteral(
            "# HELP syspeek_cgroup_oom_events_total cgroup v2 oom events."));
        lines.append(
            QStringLiteral("# TYPE syspeek_cgroup_oom_events_total counter"));
        lines.append(gaugeInt("syspeek_cgroup_oom_events_total",
                              static_cast<long long>(s.cgroupOomEvents)));
        lines.append(QStringLiteral(
            "# HELP syspeek_cgroup_oom_kills_total cgroup v2 oom kills."));
        lines.append(
            QStringLiteral("# TYPE syspeek_cgroup_oom_kills_total counter"));
        lines.append(gaugeInt("syspeek_cgroup_oom_kills_total",
                              static_cast<long long>(s.cgroupOomKills)));
        if (s.cgroupPidsCurrent >= 0) {
            lines.append(QStringLiteral(
                "# HELP syspeek_cgroup_pids Focused app cgroup task count."));
            lines.append(QStringLiteral("# TYPE syspeek_cgroup_pids gauge"));
            lines.append(gaugeInt("syspeek_cgroup_pids", s.cgroupPidsCurrent));
        }
    }

    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}
