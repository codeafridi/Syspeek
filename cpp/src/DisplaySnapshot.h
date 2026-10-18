#pragma once

#include "DiskStatus.h"
#include "SbomStatus.h"
#include "SecurityStatus.h"
#include "SloStatus.h"
#include "RuntimeStatus.h"
#include "ProfileStatus.h"
#include "HostInfoStatus.h"
#include "TcpStatus.h"
#include "FimStatus.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QtGlobal>

struct ProcessRow {
    int pid = 0;
    int depth = 0;
    int threads = 0;
    int fds = -1;
    double cpuPercent = 0.0;
    QString name;
    QString memory;
};

struct ConnectionRow {
    QString proto;
    QString local;
    QString remote;
    QString state;
};

struct InterfaceRow {
    QString name;
    unsigned long long rxBytes = 0;
    unsigned long long txBytes = 0;
    double rxPerSec = 0.0;
    double txPerSec = 0.0;
};

struct EndpointRow {
    QString proto;
    QString addr;
    quint16 port = 0;
    QString exposure;
    QString service;
    bool database = false;
};

struct DisplaySnapshot {
    QString appName = QStringLiteral("--");
    QString pid = QStringLiteral("--");
    QString cpu = QStringLiteral("--");
    QString memory = QStringLiteral("--");
    QString status = QStringLiteral("IDLE");
    bool cpuValid = false;
    double cpuPercent = 0.0;
    unsigned long long rssKb = 0;
    unsigned long long vmsKb = 0;
    unsigned long long swapKb = 0;
    unsigned long long sharedKb = 0;
    QList<ProcessRow> processes;
    QList<ConnectionRow> connections;
    QList<QString> listeningPorts;
    QList<EndpointRow> endpoints;
    QList<QString> dbConnections;
    QList<QString> instrumentation;
    QList<QPair<QString, int>> socketStateCounts;
    QList<InterfaceRow> interfaces;
    bool cgroupAvailable = false;
    QString cgroupVersion;
    QString cgroupPath;
    unsigned long long cgroupMemUsage = 0;
    long long cgroupMemLimit = -1;
    unsigned long long cgroupAnon = 0;
    unsigned long long cgroupFile = 0;
    unsigned long long cgroupKernel = 0;
    unsigned long long cgroupThrottled = 0;
    unsigned long long cgroupPeriods = 0;
    unsigned long long cgroupThrottledUsec = 0;
    bool hostAvailable = false;
    bool hostWarming = false;
    double hostCpuPercent = 0.0;
    unsigned long long hostMemTotalKb = 0;
    unsigned long long hostMemAvailableKb = 0;
    unsigned long long hostDiskTotalBytes = 0;
    unsigned long long hostDiskFreeBytes = 0;
    double hostNetRxPerSec = 0.0;
    double hostNetTxPerSec = 0.0;
    double hostLoad1 = 0.0;
    QList<DiskRow> disks;
    IoPressure ioPressure;
    bool ioPressureAvailable = false;
    bool disksAvailable = false;
    bool disksWarming = false;
    double diskIntervalMs = 0.0;
    TcpHealth tcpHealth;
    bool tcpHealthAvailable = false;
    SecurityPosture security;
    SbomReport sbom;
    SloReport slo;
    RuntimeInfo runtime;
    ProfileSnapshot profile;
    HostInfo hostInfo;
    FimSnapshot fim;
    int focusThreads = 0;
    int focusFds = -1;
    unsigned long long focusVolCtx = 0;
    unsigned long long focusInvolCtx = 0;
    double focusIoReadPerSec = 0.0;
    double focusIoWritePerSec = 0.0;
    QList<QString> focusOpenFiles;
    QList<QString> dependencies;
    quint64 sequence = 0;
};
