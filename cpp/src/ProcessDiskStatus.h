#pragma once

#include <QHash>
#include <QList>
#include <QString>

struct ProcessDiskCounters {
    long long readBytes = -1;
    long long writeBytes = -1;
};

struct ProcessDiskRate {
    double readPerSec = -1.0;
    double writePerSec = -1.0;
};

struct ProcessDiskConsumer {
    int pid = 0;
    QString name;
    double readPerSec = -1.0;
    double writePerSec = -1.0;
};

QHash<int, ProcessDiskRate> processDiskRates(
    const QHash<int, ProcessDiskCounters> &counters, qint64 windowMs,
    QHash<int, ProcessDiskCounters> &prior);

QList<ProcessDiskConsumer> processDiskTopConsumers(
    const QHash<int, ProcessDiskRate> &rates,
    const QHash<int, QString> &names, int limit);

double processDiskTotal(const QHash<int, ProcessDiskRate> &rates, bool read);

QString processDiskSummaryText(int tracked, int unreadable,
                               double totalReadPerSec,
                               double totalWritePerSec);

QString processDiskConsumerText(const QList<ProcessDiskConsumer> &consumers);
