#include "GpuStatus.h"

#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {

constexpr int kPriorCap = 4096;

double numberFrom(const QString &text) {
    QString value = text.trimmed();
    if (value.isEmpty()) {
        return -1.0;
    }
    if (value.startsWith(QLatin1Char('['))) {
        return -1.0;
    }
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? parsed : -1.0;
}

quint64 bytesFrom(const QString &text) {
    const double value = numberFrom(text);
    if (value <= 0.0) {
        return 0;
    }
    return static_cast<quint64>(value);
}

}  // namespace

bool parseDrmFdinfo(const QByteArray &content, GpuEngineSample &out) {
    bool found = false;
    const QList<QByteArray> lines = content.split('\n');
    for (const QByteArray &line : lines) {
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        const QString key = QString::fromLatin1(line.left(colon)).trimmed();
        if (!key.startsWith(QStringLiteral("drm-"))) {
            continue;
        }
        QString value = QString::fromLatin1(line.mid(colon + 1)).trimmed();
        if (key == QStringLiteral("drm-driver")) {
            out.driver = value;
            found = true;
        } else if (key == QStringLiteral("drm-client-id")) {
            bool ok = false;
            const quint64 id = value.toULongLong(&ok);
            if (ok) {
                out.clientId = id;
            }
        } else if (key == QStringLiteral("drm-pdev")) {
            out.pciId = value;
        } else if (key.startsWith(QStringLiteral("drm-engine-capacity-"))) {
            continue;
        } else if (key.startsWith(QStringLiteral("drm-engine-"))) {
            const QString engine = key.mid(11);
            if (value.endsWith(QStringLiteral(" ns"))) {
                value.chop(3);
            }
            bool ok = false;
            const quint64 nanos = value.trimmed().toULongLong(&ok);
            if (ok && !engine.isEmpty()) {
                out.engineNanos.insert(engine, nanos);
                out.hasEngines = true;
                found = true;
            }
        } else if (key.startsWith(QStringLiteral("drm-total-"))
                   || key.startsWith(QStringLiteral("drm-memory-"))
                   || key == QStringLiteral("drm-resident-memory")) {
            const QString area = key.startsWith(QStringLiteral("drm-total-"))
                ? key.mid(10)
                : key.mid(11);
            bool ok = false;
            const quint64 bytes = value.toULongLong(&ok);
            if (!ok) {
                continue;
            }
            if (area.startsWith(QStringLiteral("local"))) {
                out.memLocal += bytes;
            } else {
                out.memSystem += bytes;
            }
            found = true;
        }
    }
    return found;
}

QList<GpuDevice> parseNvidiaSmiCsv(const QString &csv) {
    QList<GpuDevice> devices;
    const QStringList lines = csv.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList fields = line.split(QLatin1Char(','));
        if (fields.size() < 2) {
            continue;
        }
        GpuDevice device;
        device.vendor = QStringLiteral("nvidia");
        device.index = static_cast<int>(numberFrom(fields.at(0)));
        device.name = fields.at(1).trimmed();
        device.driver = QStringLiteral("nvidia");
        if (fields.size() > 2) {
            device.utilPercent = numberFrom(fields.at(2));
            if (device.utilPercent > 100.0) {
                device.utilPercent = 100.0;
            }
        }
        if (fields.size() > 3) {
            const double usedMiB = numberFrom(fields.at(3));
            if (usedMiB > 0.0) {
                device.memUsed = static_cast<quint64>(usedMiB * 1024.0 * 1024.0);
            }
        }
        if (fields.size() > 4) {
            const double totalMiB = numberFrom(fields.at(4));
            if (totalMiB > 0.0) {
                device.memTotal =
                    static_cast<quint64>(totalMiB * 1024.0 * 1024.0);
            }
        }
        if (fields.size() > 5) {
            device.tempC = numberFrom(fields.at(5));
        }
        if (fields.size() > 6) {
            device.powerW = numberFrom(fields.at(6));
        }
        if (fields.size() > 7) {
            const double clock = numberFrom(fields.at(7));
            if (clock > 0.0) {
                device.clockMhz = static_cast<int>(clock);
            }
        }
        devices.append(device);
    }
    return devices;
}

QList<GpuProcess> parseNvidiaSmiAppsCsv(const QString &csv) {
    QList<GpuProcess> processes;
    const QStringList lines = csv.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList fields = line.split(QLatin1Char(','));
        if (fields.size() < 3) {
            continue;
        }
        bool ok = false;
        const int pid = fields.at(1).trimmed().toInt(&ok);
        if (!ok || pid <= 0) {
            continue;
        }
        GpuProcess process;
        process.pid = pid;
        process.name = fields.at(2).trimmed();
        if (fields.size() > 3) {
            const double usedMiB = numberFrom(fields.at(3));
            if (usedMiB > 0.0) {
                process.memBytes =
                    static_cast<quint64>(usedMiB * 1024.0 * 1024.0);
            }
        }
        processes.append(process);
    }
    return processes;
}

bool parseIoregAccelerator(const QByteArray &text, GpuDevice &out) {
    const QString blob = QString::fromLatin1(text);
    if (blob.isEmpty()) {
        return false;
    }
    bool found = false;
    const auto valueFor = [&blob](const QString &key) -> QString {
        const QString needle = QStringLiteral("\"%1\"").arg(key);
        int at = blob.indexOf(needle);
        if (at < 0) {
            return QString();
        }
        at = blob.indexOf(QLatin1Char('='), at + needle.size());
        if (at < 0) {
            return QString();
        }
        int start = at + 1;
        while (start < blob.size() && blob.at(start) == QLatin1Char(' ')) {
            ++start;
        }
        int end = start;
        if (start < blob.size() && blob.at(start) == QLatin1Char('"')) {
            ++start;
            end = blob.indexOf(QLatin1Char('"'), start);
            if (end < 0) {
                return QString();
            }
            return blob.mid(start, end - start);
        }
        while (end < blob.size() && (blob.at(end).isLetterOrNumber()
                                     || blob.at(end) == QLatin1Char('.'))) {
            ++end;
        }
        return blob.mid(start, end - start);
    };

    const QString util = valueFor(QStringLiteral("Device Utilization %"));
    if (!util.isEmpty()) {
        out.utilPercent = numberFrom(util);
        found = true;
    } else {
        const QString core = valueFor(QStringLiteral("GPU Core Utilization"));
        if (!core.isEmpty()) {
            const double raw = numberFrom(core);
            if (raw >= 0.0) {
                out.utilPercent = raw <= 1.0 ? raw * 100.0 : raw;
                found = true;
            }
        }
    }
    const quint64 used = bytesFrom(valueFor(QStringLiteral("vramUsedBytes")));
    const quint64 free = bytesFrom(valueFor(QStringLiteral("vramFreeBytes")));
    if (used > 0 || free > 0) {
        out.memUsed = used;
        out.memTotal = used + free;
        found = true;
    } else {
        const quint64 inUse =
            bytesFrom(valueFor(QStringLiteral("In use system memory")));
        if (inUse > 0) {
            out.memUsed = inUse;
            found = true;
        }
    }
    const QString clock = valueFor(QStringLiteral("Core Clock(MHz)"));
    if (!clock.isEmpty()) {
        const double value = numberFrom(clock);
        if (value > 0.0) {
            out.clockMhz = static_cast<int>(value);
        }
    }
    const QString model = valueFor(QStringLiteral("model"));
    if (!model.isEmpty() && !model.startsWith(QStringLiteral("<"))) {
        out.name = model;
    }
    const QString vendorId = valueFor(QStringLiteral("vendor-id"));
    if (!vendorId.isEmpty() && vendorId.startsWith(QStringLiteral("<"))) {
        const QString hex = vendorId.mid(1, 4).rightJustified(4,
                                                             QLatin1Char('0'));
        out.vendor = gpuVendorFromPci(hex, QString());
    }
    return found;
}

QString gpuVendorFromPci(const QString &vendorHex, const QString &pciId) {
    QString vendor = vendorHex.trimmed().toLower();
    if (vendor.startsWith(QStringLiteral("0x"))) {
        vendor = vendor.mid(2);
    }
    if (vendor.size() >= 4) {
        vendor = vendor.left(4);
    }
    if (vendor == QStringLiteral("8086")) {
        return QStringLiteral("intel");
    }
    if (vendor == QStringLiteral("1002")) {
        return QStringLiteral("amd");
    }
    if (vendor == QStringLiteral("10de")) {
        return QStringLiteral("nvidia");
    }
    if (vendor == QStringLiteral("106b")) {
        return QStringLiteral("apple");
    }
    // ioreg prints vendor-id as raw data bytes, which reverses the PCI
    // byte pairs (8086 -> 8680, 1002 -> 0210, 10de -> de10, 106b -> 6b10).
    if (vendor == QStringLiteral("8680")) {
        return QStringLiteral("intel");
    }
    if (vendor == QStringLiteral("0210")) {
        return QStringLiteral("amd");
    }
    if (vendor == QStringLiteral("de10")) {
        return QStringLiteral("nvidia");
    }
    if (vendor == QStringLiteral("6b10")) {
        return QStringLiteral("apple");
    }
    Q_UNUSED(pciId);
    return QStringLiteral("unknown");
}

QString gpuVendorLabel(const QString &vendor) {
    if (vendor == QStringLiteral("intel")) {
        return QStringLiteral("Intel");
    }
    if (vendor == QStringLiteral("amd")) {
        return QStringLiteral("AMD");
    }
    if (vendor == QStringLiteral("nvidia")) {
        return QStringLiteral("NVIDIA");
    }
    if (vendor == QStringLiteral("apple")) {
        return QStringLiteral("Apple");
    }
    return QStringLiteral("GPU");
}

int gpuPidFromPdhInstance(const QString &instance) {
    const int at = instance.indexOf(QStringLiteral("pid_"));
    if (at < 0) {
        return 0;
    }
    int end = at + 4;
    while (end < instance.size() && instance.at(end).isDigit()) {
        ++end;
    }
    bool ok = false;
    const int pid = instance.mid(at + 4, end - at - 4).toInt(&ok);
    return ok ? pid : 0;
}

QHash<quint64, GpuClientBusy> gpuClientBusy(
    const QList<GpuEngineSample> &clients, qint64 windowMs,
    QHash<quint64, GpuClientPrior> &prior) {

    QHash<quint64, GpuClientBusy> busy;
    busy.reserve(clients.size());

    for (const GpuEngineSample &client : clients) {
        GpuClientBusy result;
        result.memBytes = client.memLocal > 0 ? client.memLocal
                                              : client.memSystem;
        const auto previous = prior.constFind(client.clientId);
        if (previous != prior.constEnd() && previous->seen && windowMs > 0) {
            double nanos = 0.0;
            QHash<QString, double> perEngine;
            for (auto it = client.engineNanos.constBegin();
                 it != client.engineNanos.constEnd(); ++it) {
                const auto old = previous->engines.constFind(it.key());
                const quint64 before = old == previous->engines.constEnd()
                    ? it.value()
                    : old.value();
                const quint64 delta = it.value() >= before ? it.value() - before
                                                          : 0;
                nanos += static_cast<double>(delta);
                if (delta > 0) {
                    perEngine.insert(it.key(), 0.0);
                }
            }
            const double percent =
                100.0 * nanos / (static_cast<double>(windowMs) * 1.0e6);
            result.busyPercent = std::min(100.0, percent);
            QStringList names;
            for (auto it = client.engineNanos.constBegin();
                 it != client.engineNanos.constEnd(); ++it) {
                const auto old = previous->engines.constFind(it.key());
                const quint64 before = old == previous->engines.constEnd()
                    ? it.value()
                    : old.value();
                const quint64 delta = it.value() >= before ? it.value() - before
                                                          : 0;
                if (delta == 0) {
                    continue;
                }
                const double enginePercent =
                    100.0 * static_cast<double>(delta)
                    / (static_cast<double>(windowMs) * 1.0e6);
                names.append(QStringLiteral("%1 %2%")
                                 .arg(it.key())
                                 .arg(enginePercent, 0, 'f', 0));
            }
            std::sort(names.begin(), names.end());
            result.engines = names.join(QStringLiteral(", "));
        }
        busy.insert(client.clientId, result);
    }

    if (clients.size() > kPriorCap) {
        prior.clear();
    } else {
        QSet<quint64> live;
        live.reserve(clients.size());
        for (const GpuEngineSample &client : clients) {
            live.insert(client.clientId);
        }
        for (auto it = prior.begin(); it != prior.end();) {
            if (!live.contains(it.key())) {
                it = prior.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const GpuEngineSample &client : clients) {
        GpuClientPrior entry;
        entry.engines = client.engineNanos;
        entry.seen = true;
        prior.insert(client.clientId, entry);
    }
    return busy;
}

QList<GpuProcess> gpuMergeClients(const QList<GpuEngineSample> &clients,
                                  const QHash<quint64, GpuClientBusy> &busy,
                                  int limit) {

    struct Agg {
        double busy = 0.0;
        bool busyKnown = false;
        quint64 mem = 0;
        QString name;
        QStringList engines;
    };
    QHash<int, Agg> byPid;
    for (const GpuEngineSample &client : clients) {
        const auto rate = busy.constFind(client.clientId);
        if (rate == busy.constEnd()) {
            continue;
        }
        Agg &agg = byPid[client.pid];
        if (agg.name.isEmpty()) {
            agg.name = client.name;
        }
        if (rate->busyPercent >= 0.0) {
            agg.busy += rate->busyPercent;
            agg.busyKnown = true;
        }
        agg.mem += rate->memBytes;
        if (!rate->engines.isEmpty()) {
            agg.engines.append(rate->engines);
        }
    }

    QList<GpuProcess> out;
    out.reserve(byPid.size());
    for (auto it = byPid.constBegin(); it != byPid.constEnd(); ++it) {
        if (it->mem == 0 && !it->busyKnown) {
            continue;
        }
        GpuProcess process;
        process.pid = it.key();
        process.name = it->name.isEmpty()
            ? QStringLiteral("pid %1").arg(it.key())
            : it->name;
        process.busyPercent = it->busyKnown ? std::min(100.0, it->busy) : -1.0;
        process.memBytes = it->mem;
        QStringList engines = it->engines;
        engines.removeDuplicates();
        process.engines = engines.join(QStringLiteral(", "));
        out.append(process);
    }
    std::sort(out.begin(), out.end(), [](const GpuProcess &a,
                                         const GpuProcess &b) {
        const double busyA = a.busyPercent < 0.0 ? 0.0 : a.busyPercent;
        const double busyB = b.busyPercent < 0.0 ? 0.0 : b.busyPercent;
        if (busyA != busyB) {
            return busyA > busyB;
        }
        if (a.memBytes != b.memBytes) {
            return a.memBytes > b.memBytes;
        }
        return a.pid < b.pid;
    });
    if (limit > 0 && out.size() > limit) {
        out.resize(limit);
    }
    return out;
}

double gpuClientTotalBusy(const QList<GpuProcess> &processes) {
    double total = 0.0;
    bool seen = false;
    for (const GpuProcess &process : processes) {
        if (process.busyPercent < 0.0) {
            continue;
        }
        total += process.busyPercent;
        seen = true;
    }
    return seen ? std::min(100.0, total) : -1.0;
}

QString formatGpuBytes(quint64 bytes) {
    if (bytes == 0) {
        return QStringLiteral("--");
    }
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mib >= 1024.0) {
        return QStringLiteral("%1 GB").arg(mib / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(mib, 0, 'f', 0);
}

QString gpuSummaryText(const GpuReading &reading, bool warming,
                       bool clientsAvailable, int processCount) {
    if (!reading.ok) {
        return reading.note.isEmpty()
            ? QStringLiteral("gpu: not detected on this machine")
            : reading.note;
    }
    QString text = QStringLiteral("gpu: %1 device%2 (%3)")
                       .arg(reading.devices.size())
                       .arg(reading.devices.size() == 1 ? QString()
                                                        : QStringLiteral("s"),
                            reading.vendor.isEmpty()
                                ? QStringLiteral("unknown")
                                : (reading.vendor == QStringLiteral("multiple")
                                       ? reading.vendor
                                       : gpuVendorLabel(reading.vendor)));
    if (warming) {
        text += QStringLiteral(" - warming up");
        return text;
    }
    if (clientsAvailable) {
        text += processCount > 0
            ? QStringLiteral(" - %1 process%2 using the gpu")
                  .arg(processCount)
                  .arg(processCount == 1 ? QString() : QStringLiteral("es"))
            : QStringLiteral(" - no process is using the gpu right now");
    } else if (!reading.note.isEmpty()) {
        text += QStringLiteral(" - %1").arg(reading.note);
    }
    return text;
}

QString gpuDeviceText(const GpuReading &reading) {
    if (reading.devices.isEmpty()) {
        return reading.ok ? QStringLiteral("no gpu device readable")
                          : QStringLiteral("--");
    }
    QStringList lines;
    for (const GpuDevice &device : reading.devices) {
        QString util = device.utilPercent < 0.0
            ? QStringLiteral("--")
            : QStringLiteral("%1%").arg(device.utilPercent, 0, 'f', 1);
        if (device.utilFromClients) {
            util += QStringLiteral(" (client)");
        }
        QString mem = formatGpuBytes(device.memUsed);
        if (device.memTotal > 0) {
            mem += QStringLiteral(" / %1").arg(formatGpuBytes(device.memTotal));
        }
        const QString temp = device.tempC < 0.0
            ? QStringLiteral("--")
            : QStringLiteral("%1 C").arg(device.tempC, 0, 'f', 0);
        const QString power = device.powerW < 0.0
            ? QStringLiteral("--")
            : QStringLiteral("%1 W").arg(device.powerW, 0, 'f', 1);
        const QString clock = device.clockMhz <= 0
            ? QStringLiteral("--")
            : QStringLiteral("%1 MHz").arg(device.clockMhz);
        QString line = QStringLiteral("%1 %2 %3  util %4  mem %5  %6  %7  %8")
                           .arg(device.index)
                           .arg(gpuVendorLabel(device.vendor))
                           .arg(device.name)
                           .arg(util)
                           .arg(mem)
                           .arg(temp)
                           .arg(power)
                           .arg(clock);
        if (!device.driver.isEmpty() || !device.pciId.isEmpty()) {
            line += QStringLiteral("  [%1 %2]")
                        .arg(device.driver, device.pciId);
        }
        lines.append(line);
    }
    return lines.join(QLatin1Char('\n'));
}

QString gpuProcessText(const QList<GpuProcess> &processes) {
    if (processes.isEmpty()) {
        return QStringLiteral("no process is using the gpu right now");
    }
    QStringList lines;
    for (const GpuProcess &process : processes) {
        const QString busy = process.busyPercent < 0.0
            ? QStringLiteral("warming")
            : QStringLiteral("%1%").arg(process.busyPercent, 0, 'f', 1);
        QString line = QStringLiteral("%1  pid %2  %3  %4")
                           .arg(process.name)
                           .arg(process.pid)
                           .arg(busy, formatGpuBytes(process.memBytes));
        if (!process.engines.isEmpty()) {
            line += QStringLiteral("  %1").arg(process.engines);
        }
        lines.append(line);
    }
    return lines.join(QLatin1Char('\n'));
}
