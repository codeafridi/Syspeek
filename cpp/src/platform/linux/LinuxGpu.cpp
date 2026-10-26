#include "LinuxGpu.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {

QString readText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromLatin1(file.read(512)).trimmed();
}

qint64 readNumber(const QString &path) {
    const QString text = readText(path);
    if (text.isEmpty()) {
        return -1;
    }
    bool ok = false;
    const qint64 value = text.toLongLong(&ok);
    return ok ? value : -1;
}

QString driverName(const QString &devicePath) {
    const QFileInfo link(devicePath + QStringLiteral("/driver"));
    const QString target = link.symLinkTarget();
    if (!target.isEmpty()) {
        return QFileInfo(target).fileName();
    }
    return QString();
}

QString pciIdOf(const QString &devicePath) {
    const QStringList lines =
        readText(devicePath + QStringLiteral("/uevent")).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("PCI_ID="))) {
            return line.mid(7).trimmed();
        }
    }
    return QString();
}

int clockFromDpm(const QString &path) {
    const QStringList lines =
        readText(path).split(QLatin1Char('\n'));
    int fallback = -1;
    for (const QString &line : lines) {
        const int mhz = line.indexOf(QStringLiteral("Mhz"));
        if (mhz <= 0) {
            continue;
        }
        int start = mhz;
        while (start > 0 && line.at(start - 1).isDigit()) {
            --start;
        }
        bool ok = false;
        const int value = line.mid(start, mhz - start).toInt(&ok);
        if (!ok) {
            continue;
        }
        if (line.trimmed().endsWith(QLatin1Char('*'))) {
            return value;
        }
        fallback = value;
    }
    return fallback;
}

QString hwmonFile(const QString &devicePath, const QStringList &names) {
    QDir hwmon(devicePath + QStringLiteral("/hwmon"));
    const QStringList dirs =
        hwmon.entryList(QStringList{QStringLiteral("hwmon*")}, QDir::Dirs);
    for (const QString &dir : dirs) {
        for (const QString &name : names) {
            const QString path =
                hwmon.filePath(dir + QLatin1Char('/') + name);
            if (QFileInfo::exists(path)) {
                return path;
            }
        }
    }
    return QString();
}

void applySensors(GpuDevice &device, const QString &devicePath) {
    const QString temp = hwmonFile(devicePath, {QStringLiteral("temp1_input")});
    if (!temp.isEmpty()) {
        const qint64 milli = readNumber(temp);
        if (milli > 0) {
            device.tempC = static_cast<double>(milli) / 1000.0;
        }
    }
    const QString power = hwmonFile(devicePath, {QStringLiteral("power1_average"),
                                                 QStringLiteral("power1_input")});
    if (!power.isEmpty()) {
        const qint64 micro = readNumber(power);
        if (micro > 0) {
            device.powerW = static_cast<double>(micro) / 1000000.0;
        }
    }
    qint64 gtClock = readNumber(devicePath + QStringLiteral("/gt_cur_freq_mhz"));
    if (gtClock <= 0) {
        gtClock = readNumber(QFileInfo(devicePath).path()
                             + QStringLiteral("/gt_cur_freq_mhz"));
    }
    if (gtClock > 0) {
        device.clockMhz = static_cast<int>(gtClock);
    } else {
        const QString dpm = devicePath + QStringLiteral("/pp_dpm_sclk");
        if (QFileInfo::exists(dpm)) {
            device.clockMhz = clockFromDpm(dpm);
        }
    }
}

QString commOf(int pid) {
    return readText(QStringLiteral("/proc/%1/comm").arg(pid));
}

}  // namespace

namespace linuxgpu {

GpuReading collect() {
    GpuReading reading;

    QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList entries =
        drm.entryList(QStringList{QStringLiteral("card*")}, QDir::Dirs
                          | QDir::NoDotAndDotDot);
    const QRegularExpression cardPattern(QStringLiteral("^card\\d+$"));
    for (const QString &entry : entries) {
        if (!cardPattern.match(entry).hasMatch()) {
            continue;
        }
        const QString devicePath =
            drm.filePath(entry + QStringLiteral("/device"));
        if (!QFileInfo::exists(devicePath)) {
            continue;
        }
        GpuDevice device;
        const QString vendorHex =
            readText(devicePath + QStringLiteral("/vendor"));
        device.vendor = gpuVendorFromPci(vendorHex, QString());
        device.driver = driverName(devicePath);
        device.pciId = pciIdOf(devicePath);
        const QString label = readText(devicePath + QStringLiteral("/label"));
        const QString deviceHex =
            readText(devicePath + QStringLiteral("/device"));
        if (!label.isEmpty()) {
            device.name = label;
        } else if (!device.pciId.isEmpty()) {
            device.name = QStringLiteral("%1 %2")
                              .arg(gpuVendorLabel(device.vendor), device.pciId);
        } else {
            device.name = QStringLiteral("%1 %2")
                              .arg(gpuVendorLabel(device.vendor),
                                   deviceHex.isEmpty() ? entry : deviceHex);
        }
        device.index = reading.devices.size();

        const qint64 busy = readNumber(devicePath + QStringLiteral("/gpu_busy_percent"));
        if (busy >= 0) {
            device.utilPercent =
                qMin(100.0, static_cast<double>(busy));
        }
        const qint64 vramUsed =
            readNumber(devicePath + QStringLiteral("/mem_info_vram_used"));
        const qint64 vramTotal =
            readNumber(devicePath + QStringLiteral("/mem_info_vram_total"));
        if (vramUsed >= 0) {
            device.memUsed = static_cast<quint64>(vramUsed);
        }
        if (vramTotal > 0) {
            device.memTotal = static_cast<quint64>(vramTotal);
        }
        applySensors(device, devicePath);
        if (device.utilPercent < 0.0) {
            device.utilFromClients = true;
        }
        reading.devices.append(device);
    }

    if (QFileInfo::exists(QStringLiteral("/usr/bin/nvidia-smi"))
        || QFileInfo::exists(QStringLiteral("/usr/local/bin/nvidia-smi"))) {
        QProcess process;
        process.start(QStringLiteral("nvidia-smi"),
                      {QStringLiteral("--query-gpu=index,name,utilization.gpu,"
                                      "memory.used,memory.total,temperature.gpu,"
                                      "power.draw,clocks.current.sm"),
                       QStringLiteral("--format=csv,noheader,nounits")});
        if (process.waitForFinished(1500) && process.exitCode() == 0) {
            QList<GpuDevice> devices =
                parseNvidiaSmiCsv(QString::fromLatin1(process.readAllStandardOutput()));
            int base = reading.devices.size();
            for (GpuDevice &device : devices) {
                device.index = base++;
                device.driver = QStringLiteral("nvidia");
                reading.devices.append(device);
            }
        }
        QProcess apps;
        apps.start(QStringLiteral("nvidia-smi"),
                   {QStringLiteral("--query-compute-apps=gpu_uuid,pid,"
                                   "process_name,used_gpu_memory"),
                    QStringLiteral("--format=csv,noheader,nounits")});
        if (apps.waitForFinished(1500) && apps.exitCode() == 0) {
            QList<GpuProcess> processes = parseNvidiaSmiAppsCsv(
                QString::fromLatin1(apps.readAllStandardOutput()));
            for (GpuProcess &process : processes) {
                if (process.name.isEmpty()) {
                    process.name = commOf(process.pid);
                }
                reading.processes.append(process);
            }
        }
    }

    reading.ok = !reading.devices.isEmpty();
    if (!reading.ok) {
        reading.note = QStringLiteral(
            "gpu: no readable DRM device under /sys/class/drm");
        return reading;
    }
    reading.vendor = reading.devices.at(0).vendor;
    for (const GpuDevice &device : reading.devices) {
        if (device.vendor != reading.vendor) {
            reading.vendor = QStringLiteral("multiple");
            break;
        }
    }
    return reading;
}

bool clients(const QList<int> &pids, QList<GpuEngineSample> &out) {
    QSet<quint64> seenClients;
    bool any = false;
    for (int pid : pids) {
        const QDir dir(QStringLiteral("/proc/%1/fdinfo").arg(pid));
        if (!dir.exists()) {
            continue;
        }
        const QStringList fds =
            dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QString &fd : fds) {
            QFile file(dir.filePath(fd));
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QByteArray content = file.read(4096);
            if (!content.contains("drm-driver")) {
                continue;
            }
            GpuEngineSample sample;
            if (!parseDrmFdinfo(content, sample)) {
                continue;
            }
            if (sample.clientId != 0 && seenClients.contains(sample.clientId)) {
                continue;
            }
            sample.pid = pid;
            sample.name = commOf(pid);
            if (sample.clientId != 0) {
                seenClients.insert(sample.clientId);
            }
            out.append(sample);
            any = true;
        }
    }
    return any;
}

}  // namespace linuxgpu
