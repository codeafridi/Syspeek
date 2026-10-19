#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

struct HostCpuInfo {
    QString vendorId;
    QString modelName;
    int cores = 0;
    int logicalProcessors = 0;
    int packages = 1;
    double mhz = 0.0;
    QString cacheSize;
};

struct HostFsInfo {
    QString name;
    QString mountedOn;
    unsigned long long kbSize = 0;
    unsigned long long kbFree = 0;
    bool hasKbFree = false;
};

struct HostNetworkInfo {
    QString name;
    QString macAddress;
    QString ipv4;   // CIDR
    QString ipv6;   // CIDR
};

struct HostPlatformInfo {
    QString os;             // PRETTY_NAME
    QString hostname;
    QString kernelName;     // uname.sysname
    QString kernelRelease;  // uname.release
    QString kernelVersion;  // uname.version
    QString machine;        // uname.machine
};

struct HostInfo {
    bool available = false;
    HostPlatformInfo platform;
    HostCpuInfo cpu;
    unsigned long long memTotalKb = 0;
    unsigned long long swapTotalKb = 0;
    QString virtualization;
    QList<HostFsInfo> filesystems;
    QList<HostNetworkInfo> interfaces;
};

// Datadog gohai-style collectors.
HostCpuInfo parseCpuInfo(const QByteArray &cpuinfo);
QList<HostFsInfo> parseMounts(const QByteArray &mounts);
QString parseOsRelease(const QByteArray &osRelease);
HostInfo collectHostInfo();

#if defined(Q_OS_MACOS)
HostInfo collectHostInfoMac();
#endif

#if defined(Q_OS_WIN)
HostInfo collectHostInfoWin();
#endif

QString hostInfoSummaryText(const HostInfo &info);
QString hostInfoPlatformText(const HostInfo &info);
QString hostInfoCpuText(const HostInfo &info);
QString hostInfoFilesystemsText(const HostInfo &info);
QString hostInfoNetworkText(const HostInfo &info);
