#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

struct UnitRow {
    QString name;
    QString load;
    QString active;
    QString sub;
    QString description;
};

struct ServiceChange {
    qint64 atMs = 0;
    QString unit;
    QString summary;
};

struct ServicesSnapshot {
    bool available = false;
    QString errorMessage;
    QList<UnitRow> units;
    QList<ServiceChange> changes;
    int runningCount = 0;
    int failedCount = 0;
    qint64 sampledAtMs = 0;
};

// One SystemControlManager record (dwCurrentState/dwWin32ExitCode from
// SERVICE_STATUS_PROCESS; the dwCurrentState values mirror winsvc.h).
struct ServiceRecord {
    QString name;
    QString displayName;
    int state = 0;
    unsigned long exitCode = 0;
    int processId = 0;
};

bool parseSystemctlUnits(const QByteArray &text, QList<UnitRow> &out);
bool parseLaunchctlList(const QByteArray &text, QList<UnitRow> &out);
bool serviceRecordsToUnits(const QList<ServiceRecord> &records, QList<UnitRow> &out);
int servicesFailedCount(const QList<UnitRow> &units);
int servicesRunningCount(const QList<UnitRow> &units);

QString servicesSummaryText(const ServicesSnapshot &snapshot);
QString servicesDetailText(const ServicesSnapshot &snapshot);
QList<ServiceChange> diffServices(const QList<UnitRow> &prev,
                                  const QList<UnitRow> &cur, qint64 nowMs);
QString servicesChangesText(const QList<ServiceChange> &changes);
