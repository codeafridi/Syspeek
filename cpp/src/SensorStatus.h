#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

enum class SensorKind {
    Temperature,
    Fan,
    Power,
    Current,
    Energy,
    Voltage,
    Other,
};

struct SensorReading {
    QString chip;
    QString key;
    QString label;
    SensorKind kind = SensorKind::Other;
    double value = 0.0;
    bool hasCrit = false;
    double crit = 0.0;
    bool hasWarn = false;
    double warn = 0.0;
};

struct SensorChip {
    QString name;
    QList<SensorReading> readings;
};

enum class BatteryState {
    Unknown,
    Charging,
    Discharging,
    Full,
    NotCharging,
};

struct BatteryReading {
    QString name;
    QString model;
    QString manufacturer;
    QString technology;
    BatteryState state = BatteryState::Unknown;
    int capacityPercent = -1;
    double energyNowWh = -1.0;
    double energyFullWh = -1.0;
    double energyDesignWh = -1.0;
    double powerW = -1.0;
    double voltageV = -1.0;
    int cycleCount = -1;
    int timeToEmptyMin = -1;
    int timeToFullMin = -1;
    double temperatureC = -1.0;
    bool present = true;
};

struct AdapterReading {
    QString name;
    bool online = false;
    double wattsW = -1.0;
};

struct SensorsSnapshot {
    bool available = false;
    QString note;
    QList<SensorChip> chips;
    QList<BatteryReading> batteries;
    QList<AdapterReading> adapters;
    double hottestCelsius = -1.0;
    QString hottestLabel;
    int hottestSeverity = 0;
    double maxFanRpm = -1.0;
    QString maxFanLabel;
    int fanCount = 0;
    double totalPowerW = -1.0;
    qint64 sampledAtMs = 0;
};

SensorsSnapshot collectSensorsLinux(
    const QString &hwmonRoot = QStringLiteral("/sys/class/hwmon"),
    const QString &powerRoot = QStringLiteral("/sys/class/power_supply"));

void recomputeSensorTotals(SensorsSnapshot &snapshot);

SensorKind sensorKindFromKey(const QString &key);
QString sensorKindName(SensorKind kind);
QString sensorUnit(SensorKind kind);
double sensorScale(SensorKind kind);
QString sensorValueText(const SensorReading &reading);
int sensorSeverity(const SensorReading &reading);
bool sensorLimitPlausible(SensorKind kind, double value);
QString sensorChipText(const SensorChip &chip);
QString sensorsSummaryText(const SensorsSnapshot &snapshot);
QString sensorsThermalText(const SensorsSnapshot &snapshot);
QString batteryStateText(BatteryState state);
QString batterySummaryText(const BatteryReading &battery);
QString batteryDetailText(const BatteryReading &battery);
int batteryHealthPercent(const BatteryReading &battery);
QString adaptersText(const SensorsSnapshot &snapshot);
