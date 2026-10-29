#include "MacGpu.h"

#include <QProcess>
#include <QStringList>

namespace macgpu {

GpuReading collect() {
    GpuReading reading;
    QProcess process;
    process.start(QStringLiteral("/usr/sbin/ioreg"),
                  {QStringLiteral("-r"), QStringLiteral("-c"),
                   QStringLiteral("IOAccelerator"), QStringLiteral("-d"),
                   QStringLiteral("1"), QStringLiteral("-w"),
                   QStringLiteral("0")});
    if (!process.waitForFinished(2000) || process.exitCode() != 0) {
        reading.note = QStringLiteral(
            "gpu: IOAccelerator registry query failed");
        return reading;
    }
    const QByteArray raw = process.readAllStandardOutput();
    if (raw.isEmpty()) {
        reading.note = QStringLiteral(
            "gpu: no IOAccelerator service in the ioregistry");
        return reading;
    }

    QStringList chunks =
        QString::fromLatin1(raw).split(QStringLiteral("\n+-o "));
    if (!chunks.isEmpty()) {
        chunks.removeFirst();
    }
    int index = 0;
    for (const QString &chunk : chunks) {
        if (!chunk.contains(QStringLiteral("PerformanceStatistics"))) {
            continue;
        }
        GpuDevice device;
        if (!parseIoregAccelerator(chunk.toLatin1(), device)) {
            continue;
        }
        if (device.name.isEmpty()) {
            const QString head = chunk.left(chunk.indexOf(QLatin1Char('<')));
            const QString token = head.split(QLatin1Char(' '),
                                             Qt::SkipEmptyParts).value(0);
            device.name = token.isEmpty() ? QStringLiteral("Apple GPU") : token;
        }
        if ((device.vendor.isEmpty()
             || device.vendor == QStringLiteral("unknown"))
            && (chunk.contains(QStringLiteral("AGXAccelerator"))
                || device.name.startsWith(QStringLiteral("Apple")))) {
            device.vendor = QStringLiteral("apple");
        }
        if (device.driver.isEmpty()) {
            device.driver = QStringLiteral("IOAccelerator");
        }
        device.index = index++;
        reading.devices.append(device);
    }

    reading.ok = !reading.devices.isEmpty();
    if (!reading.ok) {
        if (reading.note.isEmpty()) {
            reading.note = QStringLiteral(
                "gpu: IOAccelerator exposes no PerformanceStatistics");
        }
        return reading;
    }
    reading.vendor = reading.devices.at(0).vendor;
    for (const GpuDevice &device : reading.devices) {
        if (device.vendor != reading.vendor) {
            reading.vendor = QStringLiteral("multiple");
            break;
        }
    }
    if (reading.vendor == QStringLiteral("unknown")) {
        reading.vendor = QStringLiteral("apple");
    }
    return reading;
}

}  // namespace macgpu
