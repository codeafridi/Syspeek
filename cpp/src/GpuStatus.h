#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QtGlobal>

struct GpuDevice {
    int index = 0;
    QString vendor;
    QString name;
    QString driver;
    QString pciId;
    double utilPercent = -1.0;
    quint64 memUsed = 0;
    quint64 memTotal = 0;
    double tempC = -1.0;
    double powerW = -1.0;
    int clockMhz = -1;
    bool utilFromClients = false;
};

struct GpuProcess {
    int pid = 0;
    QString name;
    double busyPercent = -1.0;
    quint64 memBytes = 0;
    QString engines;
};

struct GpuEngineSample {
    int pid = 0;
    QString name;
    quint64 clientId = 0;
    QString driver;
    QString pciId;
    QHash<QString, quint64> engineNanos;
    quint64 memSystem = 0;
    quint64 memLocal = 0;
    bool hasEngines = false;
};

struct GpuReading {
    bool ok = false;
    QString vendor;
    QString note;
    QList<GpuDevice> devices;
    QList<GpuProcess> processes;
    bool processesComplete = true;
};

struct GpuClientPrior {
    QHash<QString, quint64> engines;
    bool seen = false;
};

struct GpuClientBusy {
    double busyPercent = -1.0;
    quint64 memBytes = 0;
    QString engines;
};

bool parseDrmFdinfo(const QByteArray &content, GpuEngineSample &out);
QList<GpuDevice> parseNvidiaSmiCsv(const QString &csv);
QList<GpuProcess> parseNvidiaSmiAppsCsv(const QString &csv);
bool parseIoregAccelerator(const QByteArray &text, GpuDevice &out);
QString gpuVendorFromPci(const QString &vendorHex, const QString &pciId);
QString gpuVendorLabel(const QString &vendor);
int gpuPidFromPdhInstance(const QString &instance);

QHash<quint64, GpuClientBusy> gpuClientBusy(
    const QList<GpuEngineSample> &clients, qint64 windowMs,
    QHash<quint64, GpuClientPrior> &prior);

QList<GpuProcess> gpuMergeClients(const QList<GpuEngineSample> &clients,
                                  const QHash<quint64, GpuClientBusy> &busy,
                                  int limit);

double gpuClientTotalBusy(const QList<GpuProcess> &processes);

QString formatGpuBytes(quint64 bytes);
QString gpuSummaryText(const GpuReading &reading, bool warming,
                       bool clientsAvailable, int processCount);
QString gpuDeviceText(const GpuReading &reading);
QString gpuProcessText(const QList<GpuProcess> &processes);
