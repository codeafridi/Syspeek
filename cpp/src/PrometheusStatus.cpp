#include "PrometheusStatus.h"

#include "CloudStatus.h"

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

    if (!s.processNetTalkers.isEmpty()) {
        lines.append(QStringLiteral(
            "# HELP syspeek_process_net_bytes_per_second Per-process tcp throughput."));
        lines.append(QStringLiteral(
            "# TYPE syspeek_process_net_bytes_per_second gauge"));
        for (const ProcessNetTalker &talker : s.processNetTalkers) {
            const QString labels =
                QStringLiteral("process=\"%1\",pid=\"%2\"")
                    .arg(escapeLabel(talker.name))
                    .arg(talker.pid);
            lines.append(
                QStringLiteral("syspeek_process_net_bytes_per_second{%1,"
                               "direction=\"rx\"} %2")
                    .arg(labels, num(talker.rxPerSec)));
            lines.append(
                QStringLiteral("syspeek_process_net_bytes_per_second{%1,"
                               "direction=\"tx\"} %2")
                    .arg(labels, num(talker.txPerSec)));
        }
    }

    if (!s.processDiskTop.isEmpty()) {
        lines.append(QStringLiteral(
            "# HELP syspeek_process_disk_bytes_per_second Per-process block i/o "
            "throughput."));
        lines.append(QStringLiteral(
            "# TYPE syspeek_process_disk_bytes_per_second gauge"));
        for (const ProcessDiskConsumer &consumer : s.processDiskTop) {
            const QString labels =
                QStringLiteral("process=\"%1\",pid=\"%2\"")
                    .arg(escapeLabel(consumer.name))
                    .arg(consumer.pid);
            lines.append(
                QStringLiteral("syspeek_process_disk_bytes_per_second{%1,"
                               "direction=\"read\"} %2")
                    .arg(labels, num(consumer.readPerSec)));
            lines.append(
                QStringLiteral("syspeek_process_disk_bytes_per_second{%1,"
                               "direction=\"write\"} %2")
                    .arg(labels, num(consumer.writePerSec)));
        }
    }

    if (s.gpu.ok) {
        lines.append(QStringLiteral(
            "# HELP syspeek_gpu_utilization_percent GPU utilization."));
        lines.append(QStringLiteral(
            "# TYPE syspeek_gpu_utilization_percent gauge"));
        for (const GpuDevice &device : s.gpu.devices) {
            const QString labels =
                QStringLiteral("device=\"%1\",vendor=\"%2\",name=\"%3\"")
                    .arg(device.index)
                    .arg(escapeLabel(device.vendor))
                    .arg(escapeLabel(device.name));
            if (device.utilPercent >= 0.0) {
                lines.append(
                    QStringLiteral("syspeek_gpu_utilization_percent{%1} %2")
                        .arg(labels, num(device.utilPercent)));
            }
            if (device.memUsed > 0) {
                lines.append(
                    QStringLiteral("syspeek_gpu_memory_used_bytes{%1} %2")
                        .arg(labels)
                        .arg(device.memUsed));
            }
            if (device.memTotal > 0) {
                lines.append(
                    QStringLiteral("syspeek_gpu_memory_total_bytes{%1} %2")
                        .arg(labels)
                        .arg(device.memTotal));
            }
            if (device.tempC >= 0.0) {
                lines.append(
                    QStringLiteral("syspeek_gpu_temperature_celsius{%1} %2")
                        .arg(labels, num(device.tempC)));
            }
            if (device.powerW >= 0.0) {
                lines.append(
                    QStringLiteral("syspeek_gpu_power_watts{%1} %2")
                        .arg(labels, num(device.powerW)));
            }
            if (device.clockMhz > 0) {
                lines.append(
                    QStringLiteral("syspeek_gpu_clock_mhz{%1} %2")
                        .arg(labels)
                        .arg(device.clockMhz));
            }
        }
        if (!s.gpu.processes.isEmpty()) {
            lines.append(QStringLiteral(
                "# HELP syspeek_gpu_process_busy_percent Per-process gpu "
                "engine utilization."));
            lines.append(QStringLiteral(
                "# TYPE syspeek_gpu_process_busy_percent gauge"));
            for (const GpuProcess &process : s.gpu.processes) {
                const QString labels =
                    QStringLiteral("process=\"%1\",pid=\"%2\"")
                        .arg(escapeLabel(process.name))
                        .arg(process.pid);
                if (process.busyPercent >= 0.0) {
                    lines.append(
                        QStringLiteral(
                            "syspeek_gpu_process_busy_percent{%1} %2")
                            .arg(labels, num(process.busyPercent)));
                }
                if (process.memBytes > 0) {
                    lines.append(
                        QStringLiteral(
                            "syspeek_gpu_process_memory_bytes{%1} %2")
                            .arg(labels)
                            .arg(process.memBytes));
                }
            }
        }
    }

    if (s.sensors.available && !s.sensors.chips.isEmpty()) {
        lines.append(QStringLiteral(
            "# HELP syspeek_sensor_celsius hwmon temperature reading."));
        lines.append(QStringLiteral("# TYPE syspeek_sensor_celsius gauge"));
        lines.append(QStringLiteral(
            "# HELP syspeek_sensor_fan_rpm hwmon fan speed."));
        lines.append(QStringLiteral("# TYPE syspeek_sensor_fan_rpm gauge"));
        lines.append(QStringLiteral(
            "# HELP syspeek_sensor_power_watts hwmon power reading."));
        lines.append(QStringLiteral("# TYPE syspeek_sensor_power_watts gauge"));
        lines.append(QStringLiteral(
            "# HELP syspeek_sensor_limit_celsius Configured critical "
            "temperature for the sensor."));
        lines.append(QStringLiteral(
            "# TYPE syspeek_sensor_limit_celsius gauge"));
        for (const SensorChip &chip : s.sensors.chips) {
            for (const SensorReading &reading : chip.readings) {
                const QString labels =
                    QStringLiteral("chip=\"%1\",sensor=\"%2\",kind=\"%3\"")
                        .arg(escapeLabel(chip.name),
                             escapeLabel(reading.label),
                             sensorKindName(reading.kind));
                if (reading.kind == SensorKind::Temperature) {
                    lines.append(
                        QStringLiteral("syspeek_sensor_celsius{%1} %2")
                            .arg(labels, num(reading.value)));
                    if (reading.hasCrit) {
                        lines.append(
                            QStringLiteral(
                                "syspeek_sensor_limit_celsius{%1} %2")
                                .arg(labels, num(reading.crit)));
                    }
                } else if (reading.kind == SensorKind::Fan) {
                    lines.append(
                        QStringLiteral("syspeek_sensor_fan_rpm{%1} %2")
                            .arg(labels, num(reading.value)));
                } else if (reading.kind == SensorKind::Power) {
                    lines.append(
                        QStringLiteral("syspeek_sensor_power_watts{%1} %2")
                            .arg(labels, num(reading.value)));
                }
            }
        }
    }

    if (!s.sensors.batteries.isEmpty()) {
        lines.append(QStringLiteral(
            "# HELP syspeek_battery_charge_percent Battery charge."));
        lines.append(QStringLiteral(
            "# TYPE syspeek_battery_charge_percent gauge"));
        lines.append(QStringLiteral(
            "# HELP syspeek_battery_energy_watt_hours Battery energy "
            "remaining and full capacity."));
        lines.append(QStringLiteral(
            "# TYPE syspeek_battery_energy_watt_hours gauge"));
        lines.append(QStringLiteral(
            "# HELP syspeek_battery_power_watts Battery charge/discharge "
            "power."));
        lines.append(QStringLiteral(
            "# TYPE syspeek_battery_power_watts gauge"));
        for (const BatteryReading &battery : s.sensors.batteries) {
            const QString labels = QStringLiteral("battery=\"%1\"")
                                       .arg(escapeLabel(battery.name));
            if (battery.capacityPercent >= 0) {
                lines.append(
                    QStringLiteral("syspeek_battery_charge_percent{%1} %2")
                        .arg(labels)
                        .arg(battery.capacityPercent));
            }
            if (battery.energyNowWh >= 0.0) {
                lines.append(
                    QStringLiteral(
                        "syspeek_battery_energy_watt_hours{%1,state=\"now\"} "
                        "%2")
                        .arg(labels, num(battery.energyNowWh)));
            }
            if (battery.energyFullWh >= 0.0) {
                lines.append(
                    QStringLiteral(
                        "syspeek_battery_energy_watt_hours{%1,state=\"full\"}"
                        " %2")
                        .arg(labels, num(battery.energyFullWh)));
            }
            if (battery.powerW >= 0.0) {
                lines.append(
                    QStringLiteral("syspeek_battery_power_watts{%1} %2")
                        .arg(labels, num(battery.powerW)));
            }
        }
    }

    if (!s.sensors.adapters.isEmpty()) {
        lines.append(QStringLiteral(
            "# HELP syspeek_ac_online AC adapter online state."));
        lines.append(QStringLiteral("# TYPE syspeek_ac_online gauge"));
        for (const AdapterReading &adapter : s.sensors.adapters) {
            lines.append(
                QStringLiteral(
                    "syspeek_ac_online{adapter=\"%1\",online=\"%2\"} %3")
                    .arg(escapeLabel(adapter.name),
                         adapter.online ? QStringLiteral("true")
                                        : QStringLiteral("false"))
                    .arg(adapter.online ? 1 : 0));
        }
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

    // M85: what the provider billed this month. Cloud data
    // lags, so the gauges are point-in-time snapshots of the last query, not
    // an accounting ledger; the series are Syspeek-own because the provider
    // has no metric for "month to date at this moment".
    const CloudSpendStatus &spend = s.cloud.spend;
    if (spend.available && spend.monthToDateUsd >= 0.0) {
        const QString provider = cloudProviderName(spend.provider);
        lines.append(QStringLiteral(
            "# HELP syspeek_cloud_spend_month_usd Billed cost this month, as "
            "reported by the provider's cost API."));
        lines.append(
            QStringLiteral("# TYPE syspeek_cloud_spend_month_usd gauge"));
        lines.append(labeled("syspeek_cloud_spend_month_usd", QStringLiteral("provider"),
                             provider, spend.monthToDateUsd));
        if (spend.forecastUsd >= 0.0) {
            lines.append(QStringLiteral(
                "# HELP syspeek_cloud_spend_forecast_usd Month-end projection: "
                "billed so far plus the provider's forecast."));
            lines.append(QStringLiteral(
                "# TYPE syspeek_cloud_spend_forecast_usd gauge"));
            lines.append(labeled("syspeek_cloud_spend_forecast_usd",
                                 QStringLiteral("provider"), provider,
                                 spend.forecastUsd));
        }
        if (!spend.services.isEmpty()) {
            lines.append(QStringLiteral(
                "# HELP syspeek_cloud_spend_service_usd Billed cost this month "
                "per provider service."));
            lines.append(QStringLiteral(
                "# TYPE syspeek_cloud_spend_service_usd gauge"));
            for (const CloudSpendService &service : spend.services) {
                if (service.usd < 0.0) {
                    continue;
                }
                lines.append(
                    QStringLiteral(
                        "syspeek_cloud_spend_service_usd{provider=\"%1\","
                        "service=\"%2\"} %3")
                        .arg(escapeLabel(provider), escapeLabel(service.name),
                             num(service.usd)));
            }
        }
    }

    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}
