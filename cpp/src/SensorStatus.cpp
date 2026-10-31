#include "SensorStatus.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {

QString readText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromLatin1(file.read(256)).trimmed();
}

bool readDouble(const QString &path, double *out) {
    const QString text = readText(path);
    if (text.isEmpty()) {
        return false;
    }
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok) {
        return false;
    }
    if (out != nullptr) {
        *out = value;
    }
    return true;
}

bool readInt(const QString &path, int *out) {
    const QString text = readText(path);
    if (text.isEmpty()) {
        return false;
    }
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok) {
        return false;
    }
    if (out != nullptr) {
        *out = value;
    }
    return true;
}

QString formatDuration(int minutes) {
    if (minutes < 0) {
        return QStringLiteral("--");
    }
    if (minutes < 60) {
        return QStringLiteral("%1m").arg(minutes);
    }
    return QStringLiteral("%1h %2m").arg(minutes / 60).arg(minutes % 60);
}

QString wh(double value) {
    if (value < 0.0) {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1 Wh").arg(value, 0, 'f', 1);
}

QString watts(double value) {
    if (value < 0.0) {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1 W").arg(value, 0, 'f', 2);
}

QString volts(double value) {
    if (value < 0.0) {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1 V").arg(value, 0, 'f', 2);
}

}  // namespace

SensorKind sensorKindFromKey(const QString &key) {
    const QString trimmed = key.trimmed();
    int end = 0;
    while (end < trimmed.size() && trimmed.at(end).isLetter()) {
        ++end;
    }
    const QString prefix = trimmed.left(end).toLower();
    if (prefix == QStringLiteral("temp") || prefix == QStringLiteral("thermal")) {
        return SensorKind::Temperature;
    }
    if (prefix == QStringLiteral("fan")) {
        return SensorKind::Fan;
    }
    if (prefix == QStringLiteral("power")) {
        return SensorKind::Power;
    }
    if (prefix == QStringLiteral("curr")) {
        return SensorKind::Current;
    }
    if (prefix == QStringLiteral("energy")) {
        return SensorKind::Energy;
    }
    if (prefix == QStringLiteral("in") || prefix == QStringLiteral("voltage")) {
        return SensorKind::Voltage;
    }
    return SensorKind::Other;
}

QString sensorKindName(SensorKind kind) {
    switch (kind) {
    case SensorKind::Temperature:
        return QStringLiteral("temp");
    case SensorKind::Fan:
        return QStringLiteral("fan");
    case SensorKind::Power:
        return QStringLiteral("power");
    case SensorKind::Current:
        return QStringLiteral("current");
    case SensorKind::Energy:
        return QStringLiteral("energy");
    case SensorKind::Voltage:
        return QStringLiteral("voltage");
    case SensorKind::Other:
        break;
    }
    return QStringLiteral("other");
}

QString sensorUnit(SensorKind kind) {
    switch (kind) {
    case SensorKind::Temperature:
        return QStringLiteral("\u00b0C");
    case SensorKind::Fan:
        return QStringLiteral("rpm");
    case SensorKind::Power:
        return QStringLiteral("W");
    case SensorKind::Current:
        return QStringLiteral("A");
    case SensorKind::Energy:
        return QStringLiteral("J");
    case SensorKind::Voltage:
        return QStringLiteral("V");
    case SensorKind::Other:
        break;
    }
    return QString();
}

double sensorScale(SensorKind kind) {
    switch (kind) {
    case SensorKind::Temperature:
        return 0.001;
    case SensorKind::Fan:
        return 1.0;
    case SensorKind::Power:
        return 0.000001;
    case SensorKind::Current:
        return 0.001;
    case SensorKind::Energy:
        return 0.000001;
    case SensorKind::Voltage:
        return 0.001;
    case SensorKind::Other:
        break;
    }
    return 1.0;
}

QString sensorValueText(const SensorReading &reading) {
    const QString unit = sensorUnit(reading.kind);
    int decimals = 2;
    switch (reading.kind) {
    case SensorKind::Temperature:
    case SensorKind::Energy:
        decimals = 1;
        break;
    case SensorKind::Fan:
        decimals = 0;
        break;
    default:
        break;
    }
    const QString number = QString::number(reading.value, 'f', decimals);
    return unit.isEmpty() ? number
                          : QStringLiteral("%1 %2").arg(number, unit);
}

// Firmware occasionally ships nonsense limits (an nvme drive reports
// temp*_max = 65261850 millidegrees, i.e. 65,261 °C). A threshold nobody
// can reach is not a threshold: treat it as absent so the UI does not
// print a limit that discredits every other number on the card.
bool sensorLimitPlausible(SensorKind kind, double value) {
    switch (kind) {
    case SensorKind::Temperature:
        return value > -100.0 && value < 200.0;
    case SensorKind::Fan:
        return value > 0.0 && value < 100000.0;
    case SensorKind::Power:
        return value > 0.0 && value < 10000.0;
    case SensorKind::Current:
        return value > 0.0 && value < 10000.0;
    case SensorKind::Energy:
        return value > 0.0 && value < 10000000.0;
    case SensorKind::Voltage:
        return value > 0.0 && value < 1000.0;
    case SensorKind::Other:
        break;
    }
    return true;
}

int sensorSeverity(const SensorReading &reading) {
    if (reading.hasCrit && reading.value >= reading.crit) {
        return 2;
    }
    if (reading.hasWarn && reading.value >= reading.warn) {
        return 1;
    }
    return 0;
}

QString sensorChipText(const SensorChip &chip) {
    if (chip.readings.isEmpty()) {
        return QStringLiteral("%1  (no readings)").arg(chip.name);
    }
    QStringList lines;
    lines.append(QStringLiteral("%1  (%2 readings)")
                     .arg(chip.name)
                     .arg(chip.readings.size()));
    for (const SensorReading &reading : chip.readings) {
        QString line = QStringLiteral("  %1  %2  %3")
                           .arg(reading.key, -9)
                           .arg(reading.label, -22)
                           .arg(sensorValueText(reading));
        if (reading.hasCrit) {
            line += QStringLiteral("   crit %1 %2")
                        .arg(reading.crit, 0, 'f', 1)
                        .arg(sensorUnit(reading.kind));
        } else if (reading.hasWarn) {
            line += QStringLiteral("   high %1 %2")
                        .arg(reading.warn, 0, 'f', 1)
                        .arg(sensorUnit(reading.kind));
        }
        lines.append(line);
    }
    return lines.join(QLatin1Char('\n'));
}

QString sensorsSummaryText(const SensorsSnapshot &snapshot) {
    if (!snapshot.available) {
        return snapshot.note.isEmpty()
                   ? QStringLiteral("sensors: unavailable on this OS")
                   : snapshot.note;
    }
    int temps = 0;
    int fans = 0;
    int power = 0;
    for (const SensorChip &chip : snapshot.chips) {
        for (const SensorReading &reading : chip.readings) {
            if (reading.kind == SensorKind::Temperature) {
                ++temps;
            } else if (reading.kind == SensorKind::Fan) {
                ++fans;
            } else if (reading.kind == SensorKind::Power) {
                ++power;
            }
        }
    }
    QStringList parts;
    parts.append(QStringLiteral("hwmon: %1 chips, %2 readings")
                     .arg(snapshot.chips.size())
                     .arg(temps + fans + power));
    parts.append(QStringLiteral("%1 temps, %2 fans, %3 power")
                     .arg(temps)
                     .arg(fans)
                     .arg(power));
    if (snapshot.hottestCelsius >= 0.0) {
        parts.append(QStringLiteral("hottest %1 \u00b0C (%2)")
                         .arg(snapshot.hottestCelsius, 0, 'f', 1)
                         .arg(snapshot.hottestLabel));
    }
    if (snapshot.maxFanRpm >= 0.0) {
        parts.append(QStringLiteral("fan max %1 rpm")
                         .arg(snapshot.maxFanRpm, 0, 'f', 0));
    }
    if (snapshot.totalPowerW >= 0.0) {
        parts.append(QStringLiteral("power %1").arg(watts(snapshot.totalPowerW)));
    }
    return parts.join(QStringLiteral(" - "));
}

QString sensorsThermalText(const SensorsSnapshot &snapshot) {
    if (!snapshot.available) {
        return snapshot.note.isEmpty()
                   ? QStringLiteral("thermal: unavailable on this OS")
                   : snapshot.note;
    }
    QStringList lines;
    for (const SensorChip &chip : snapshot.chips) {
        const SensorReading *hottest = nullptr;
        for (const SensorReading &reading : chip.readings) {
            if (reading.kind != SensorKind::Temperature) {
                continue;
            }
            if (hottest == nullptr || reading.value > hottest->value) {
                hottest = &reading;
            }
        }
        if (hottest == nullptr) {
            continue;
        }
        lines.append(QStringLiteral("%1  %2  %3")
                         .arg(chip.name, -12)
                         .arg(QStringLiteral("%1 %2")
                                  .arg(hottest->value, 0, 'f', 1)
                                  .arg(sensorUnit(SensorKind::Temperature)),
                              -10)
                         .arg(hottest->label));
    }
    for (const SensorChip &chip : snapshot.chips) {
        QStringList fans;
        for (const SensorReading &reading : chip.readings) {
            if (reading.kind == SensorKind::Fan) {
                fans.append(QStringLiteral("%1 %2 rpm")
                                .arg(reading.label)
                                .arg(reading.value, 0, 'f', 0));
            }
        }
        if (!fans.isEmpty()) {
            lines.append(QStringLiteral("%1  %2")
                             .arg(chip.name, -12)
                             .arg(fans.join(QStringLiteral(", "))));
        }
        QStringList power;
        for (const SensorReading &reading : chip.readings) {
            if (reading.kind == SensorKind::Power) {
                power.append(QStringLiteral("%1 %2")
                                 .arg(reading.label, sensorValueText(reading)));
            }
        }
        if (!power.isEmpty()) {
            lines.append(QStringLiteral("%1  %2")
                             .arg(chip.name, -12)
                             .arg(power.join(QStringLiteral(", "))));
        }
    }
    if (lines.isEmpty()) {
        return QStringLiteral("thermal: no temperature, fan or power "
                              "readings under /sys/class/hwmon");
    }
    return lines.join(QLatin1Char('\n'));
}

QString batteryStateText(BatteryState state) {
    switch (state) {
    case BatteryState::Charging:
        return QStringLiteral("charging");
    case BatteryState::Discharging:
        return QStringLiteral("discharging");
    case BatteryState::Full:
        return QStringLiteral("full");
    case BatteryState::NotCharging:
        return QStringLiteral("not charging");
    case BatteryState::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

int batteryHealthPercent(const BatteryReading &battery) {
    if (battery.energyFullWh <= 0.0 || battery.energyDesignWh <= 0.0) {
        return -1;
    }
    const double ratio = battery.energyFullWh / battery.energyDesignWh * 100.0;
    const int percent = static_cast<int>(std::lround(ratio));
    return percent < 0 ? -1 : percent;
}

QString batterySummaryText(const BatteryReading &battery) {
    QStringList parts;
    const QString who = QStringList{battery.manufacturer, battery.model}
                            .filter(QRegularExpression(QStringLiteral("\\S")))
                            .join(QLatin1Char(' '));
    QString head = battery.name;
    if (battery.capacityPercent >= 0) {
        head += QStringLiteral(" %1%").arg(battery.capacityPercent);
    }
    head += QStringLiteral(" (%1)").arg(batteryStateText(battery.state));
    if (!who.isEmpty()) {
        head += QStringLiteral(" - %1").arg(who);
    }
    if (!battery.technology.isEmpty()) {
        head += QStringLiteral(", %1").arg(battery.technology);
    }
    parts.append(head);
    if (battery.energyNowWh >= 0.0 || battery.energyFullWh >= 0.0) {
        parts.append(QStringLiteral("%1 / %2")
                         .arg(wh(battery.energyNowWh), wh(battery.energyFullWh)));
    }
    if (battery.energyDesignWh > 0.0) {
        parts.append(QStringLiteral("design %1").arg(wh(battery.energyDesignWh)));
    }
    return parts.join(QStringLiteral(" - "));
}

QString batteryDetailText(const BatteryReading &battery) {
    QStringList lines;
    lines.append(QStringLiteral("state      %1").arg(batteryStateText(battery.state)));
    if (battery.capacityPercent >= 0) {
        lines.append(QStringLiteral("charge     %1%").arg(battery.capacityPercent));
    }
    if (battery.energyNowWh >= 0.0 || battery.energyFullWh >= 0.0) {
        lines.append(QStringLiteral("energy     %1 now / %2 full")
                         .arg(wh(battery.energyNowWh), wh(battery.energyFullWh)));
    }
    if (battery.energyDesignWh > 0.0) {
        const int health = batteryHealthPercent(battery);
        if (health >= 0) {
            lines.append(QStringLiteral("design     %1 (health %2%)")
                             .arg(wh(battery.energyDesignWh))
                             .arg(health));
        } else {
            lines.append(QStringLiteral("design     %1")
                             .arg(wh(battery.energyDesignWh)));
        }
    }
    if (battery.powerW >= 0.0) {
        lines.append(QStringLiteral("power      %1").arg(watts(battery.powerW)));
    }
    if (battery.voltageV >= 0.0) {
        lines.append(QStringLiteral("voltage    %1").arg(volts(battery.voltageV)));
    }
    if (battery.temperatureC >= 0.0) {
        lines.append(QStringLiteral("temp       %1 \u00b0C")
                         .arg(battery.temperatureC, 0, 'f', 1));
    }
    if (battery.cycleCount >= 0) {
        lines.append(QStringLiteral("cycles     %1").arg(battery.cycleCount));
    }
    if (battery.timeToEmptyMin >= 0 || battery.timeToFullMin >= 0) {
        lines.append(QStringLiteral("runtime    %1 left, %2 to full")
                         .arg(formatDuration(battery.timeToEmptyMin),
                              formatDuration(battery.timeToFullMin)));
    }
    if (!battery.present) {
        lines.append(QStringLiteral("present    no"));
    }
    return lines.join(QLatin1Char('\n'));
}

QString adaptersText(const SensorsSnapshot &snapshot) {
    if (snapshot.adapters.isEmpty()) {
        return QStringLiteral("ac: no adapter reported");
    }
    QStringList parts;
    for (const AdapterReading &adapter : snapshot.adapters) {
        QString line = QStringLiteral("%1 %2")
                           .arg(adapter.name,
                                adapter.online ? QStringLiteral("online")
                                               : QStringLiteral("offline"));
        if (adapter.wattsW >= 0.0) {
            line += QStringLiteral(" (%1 W)").arg(adapter.wattsW, 0, 'f', 0);
        }
        parts.append(line);
    }
    return QStringLiteral("ac: %1").arg(parts.join(QStringLiteral(", ")));
}

void recomputeSensorTotals(SensorsSnapshot &snapshot) {
    snapshot.hottestCelsius = -1.0;
    snapshot.hottestLabel.clear();
    snapshot.hottestSeverity = 0;
    snapshot.maxFanRpm = -1.0;
    snapshot.maxFanLabel.clear();
    snapshot.fanCount = 0;
    snapshot.totalPowerW = -1.0;

    for (const SensorChip &chip : snapshot.chips) {
        for (const SensorReading &reading : chip.readings) {
            if (reading.kind == SensorKind::Temperature) {
                if (reading.value > snapshot.hottestCelsius) {
                    snapshot.hottestCelsius = reading.value;
                    snapshot.hottestLabel =
                        QStringLiteral("%1 %2").arg(chip.name, reading.label);
                    snapshot.hottestSeverity = sensorSeverity(reading);
                }
            } else if (reading.kind == SensorKind::Fan) {
                ++snapshot.fanCount;
                if (reading.value > snapshot.maxFanRpm) {
                    snapshot.maxFanRpm = reading.value;
                    snapshot.maxFanLabel =
                        QStringLiteral("%1 %2").arg(chip.name, reading.label);
                }
            } else if (reading.kind == SensorKind::Power) {
                if (snapshot.totalPowerW < 0.0) {
                    snapshot.totalPowerW = 0.0;
                }
                snapshot.totalPowerW += reading.value;
            }
        }
    }
    snapshot.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
}

SensorsSnapshot collectSensorsLinux(const QString &hwmonRoot,
                                    const QString &powerRoot) {
    SensorsSnapshot out;

    const QRegularExpression inputPattern(
        QStringLiteral("^(temp|fan|in|power|curr|energy|humidity|pressure)"
                       "([0-9]+)_input$"));

    const QStringList chipDirs =
        QDir(hwmonRoot).entryList(QStringList{QStringLiteral("hwmon*")},
                                  QDir::Dirs | QDir::NoDotAndDotDot,
                                  QDir::Name);
    for (const QString &dirName : chipDirs) {
        const QString dirPath = hwmonRoot + QLatin1Char('/') + dirName;
        SensorChip chip;
        chip.name = readText(dirPath + QStringLiteral("/name"));
        if (chip.name.isEmpty()) {
            chip.name = dirName;
        }

        const QStringList files =
            QDir(dirPath).entryList(QDir::Files, QDir::Name);
        for (const QString &file : files) {
            const QRegularExpressionMatch match = inputPattern.match(file);
            if (!match.hasMatch()) {
                continue;
            }
            const QString base = match.captured(1) + match.captured(2);
            SensorReading reading;
            reading.chip = chip.name;
            reading.key = base;
            reading.kind = sensorKindFromKey(base);
            const QString scale =
                dirPath + QLatin1Char('/') + base + QStringLiteral("_");
            double raw = 0.0;
            if (!readDouble(scale + QStringLiteral("input"), &raw)) {
                continue;
            }
            reading.value = raw * sensorScale(reading.kind);
            reading.label = readText(scale + QStringLiteral("label"));
            if (reading.label.isEmpty()) {
                reading.label = base;
            }
            double threshold = 0.0;
            if (readDouble(scale + QStringLiteral("crit"), &threshold)) {
                reading.crit = threshold * sensorScale(reading.kind);
                reading.hasCrit =
                    sensorLimitPlausible(reading.kind, reading.crit);
            }
            if (readDouble(scale + QStringLiteral("max"), &threshold)) {
                reading.warn = threshold * sensorScale(reading.kind);
                reading.hasWarn =
                    sensorLimitPlausible(reading.kind, reading.warn);
            }
            chip.readings.append(reading);
        }
        if (!chip.readings.isEmpty()) {
            const auto rank = [](SensorKind kind) {
                switch (kind) {
                case SensorKind::Temperature:
                    return 0;
                case SensorKind::Fan:
                    return 1;
                case SensorKind::Power:
                    return 2;
                case SensorKind::Current:
                    return 3;
                case SensorKind::Energy:
                    return 4;
                case SensorKind::Voltage:
                    return 5;
                case SensorKind::Other:
                    break;
                }
                return 6;
            };
            std::sort(chip.readings.begin(), chip.readings.end(),
                      [&rank](const SensorReading &a, const SensorReading &b) {
                          const int ra = rank(a.kind);
                          const int rb = rank(b.kind);
                          if (ra != rb) {
                              return ra < rb;
                          }
                          return a.key < b.key;
                      });
            out.chips.append(chip);
        }
    }

    const QStringList supplyDirs =
        QDir(powerRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                  QDir::Name);
    for (const QString &dirName : supplyDirs) {
        const QString dirPath = powerRoot + QLatin1Char('/') + dirName;
        const QString type = readText(dirPath + QStringLiteral("/type"));
        if (type == QStringLiteral("Battery")) {
            BatteryReading battery;
            battery.name = dirName;
            battery.model = readText(dirPath + QStringLiteral("/model_name"));
            battery.manufacturer =
                readText(dirPath + QStringLiteral("/manufacturer"));
            battery.technology =
                readText(dirPath + QStringLiteral("/technology"));
            const QString status = readText(dirPath + QStringLiteral("/status"));
            if (status == QStringLiteral("Charging")) {
                battery.state = BatteryState::Charging;
            } else if (status == QStringLiteral("Discharging")) {
                battery.state = BatteryState::Discharging;
            } else if (status == QStringLiteral("Full")) {
                battery.state = BatteryState::Full;
            } else if (status == QStringLiteral("Not charging")) {
                battery.state = BatteryState::NotCharging;
            }
            int present = 1;
            if (readInt(dirPath + QStringLiteral("/present"), &present)) {
                battery.present = present != 0;
            }
            readInt(dirPath + QStringLiteral("/capacity"),
                    &battery.capacityPercent);
            readInt(dirPath + QStringLiteral("/cycle_count"),
                    &battery.cycleCount);

            double energyNow = -1.0;
            double energyFull = -1.0;
            double energyDesign = -1.0;
            const bool hasEnergy = readDouble(
                dirPath + QStringLiteral("/energy_now"), &energyNow);
            readDouble(dirPath + QStringLiteral("/energy_full"), &energyFull);
            readDouble(dirPath + QStringLiteral("/energy_full_design"),
                       &energyDesign);

            double voltageMicro = -1.0;
            readDouble(dirPath + QStringLiteral("/voltage_now"), &voltageMicro);
            if (voltageMicro > 0.0) {
                battery.voltageV = voltageMicro / 1000000.0;
            }

            if (hasEnergy) {
                if (energyNow >= 0.0) {
                    battery.energyNowWh = energyNow / 1000000.0;
                }
                if (energyFull >= 0.0) {
                    battery.energyFullWh = energyFull / 1000000.0;
                }
                if (energyDesign >= 0.0) {
                    battery.energyDesignWh = energyDesign / 1000000.0;
                }
            } else if (battery.voltageV > 0.0) {
                double chargeNow = -1.0;
                double chargeFull = -1.0;
                double chargeDesign = -1.0;
                if (readDouble(dirPath + QStringLiteral("/charge_now"),
                               &chargeNow)
                    && chargeNow >= 0.0) {
                    battery.energyNowWh =
                        chargeNow / 1000000.0 * battery.voltageV;
                }
                if (readDouble(dirPath + QStringLiteral("/charge_full"),
                               &chargeFull)
                    && chargeFull >= 0.0) {
                    battery.energyFullWh =
                        chargeFull / 1000000.0 * battery.voltageV;
                }
                if (readDouble(dirPath + QStringLiteral("/charge_full_design"),
                               &chargeDesign)
                    && chargeDesign >= 0.0) {
                    battery.energyDesignWh =
                        chargeDesign / 1000000.0 * battery.voltageV;
                }
            }

            double powerMicro = -1.0;
            if (readDouble(dirPath + QStringLiteral("/power_now"), &powerMicro)
                && powerMicro >= 0.0) {
                battery.powerW = powerMicro / 1000000.0;
            } else {
                double currentMicro = -1.0;
                if (readDouble(dirPath + QStringLiteral("/current_now"),
                               &currentMicro)
                    && battery.voltageV > 0.0) {
                    battery.powerW =
                        std::fabs(currentMicro) / 1000000.0 * battery.voltageV;
                }
            }

            int minutes = -1;
            if (!readInt(dirPath + QStringLiteral("/time_to_empty_now"),
                         &minutes)) {
                minutes = -1;
            }
            if (minutes >= 0) {
                battery.timeToEmptyMin = minutes;
            } else if (battery.state == BatteryState::Discharging
                       && battery.energyNowWh >= 0.0 && battery.powerW > 0.0) {
                battery.timeToEmptyMin = static_cast<int>(
                    std::lround(battery.energyNowWh / battery.powerW * 60.0));
            }
            minutes = -1;
            if (!readInt(dirPath + QStringLiteral("/time_to_full_now"),
                         &minutes)) {
                minutes = -1;
            }
            if (minutes >= 0) {
                battery.timeToFullMin = minutes;
            } else if (battery.state == BatteryState::Charging
                       && battery.energyNowWh >= 0.0
                       && battery.energyFullWh > battery.energyNowWh
                       && battery.powerW > 0.0) {
                battery.timeToFullMin = static_cast<int>(std::lround(
                    (battery.energyFullWh - battery.energyNowWh)
                    / battery.powerW * 60.0));
            }

            out.batteries.append(battery);
        } else if (type == QStringLiteral("Mains")
                   || type == QStringLiteral("USB")
                   || type == QStringLiteral("USB_C")
                   || type == QStringLiteral("AC")
                   || type == QStringLiteral("Wireless")) {
            AdapterReading adapter;
            adapter.name = dirName;
            int online = 0;
            adapter.online =
                readInt(dirPath + QStringLiteral("/online"), &online)
                && online != 0;
            out.adapters.append(adapter);
        }
    }

    out.available = !out.chips.isEmpty() || !out.batteries.isEmpty()
                    || !out.adapters.isEmpty();
    if (!out.available) {
        out.note = QStringLiteral(
            "sensors: no hwmon or power_supply entries under /sys");
    }
    recomputeSensorTotals(out);
    return out;
}
