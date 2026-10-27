#include "MacSensors.h"

#ifdef Q_OS_MACOS

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>

#include <cmath>

namespace {

bool cfInt(CFTypeRef ref, qlonglong *out) {
    if (ref == nullptr || CFGetTypeID(ref) != CFNumberGetTypeID()) {
        return false;
    }
    qlonglong value = 0;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(ref), kCFNumberSInt64Type,
                          &value)) {
        return false;
    }
    if (out != nullptr) {
        *out = value;
    }
    return true;
}

bool cfDouble(CFTypeRef ref, double *out) {
    if (ref == nullptr || CFGetTypeID(ref) != CFNumberGetTypeID()) {
        return false;
    }
    double value = 0.0;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(ref), kCFNumberDoubleType,
                          &value)) {
        return false;
    }
    if (out != nullptr) {
        *out = value;
    }
    return true;
}

bool cfBool(CFTypeRef ref, bool *out) {
    if (ref == nullptr
        || CFGetTypeID(ref) != CFBooleanGetTypeID()) {
        return false;
    }
    if (out != nullptr) {
        *out = CFBooleanGetValue(static_cast<CFBooleanRef>(ref));
    }
    return true;
}

QString cfString(CFTypeRef ref) {
    if (ref == nullptr || CFGetTypeID(ref) != CFStringGetTypeID()) {
        return QString();
    }
    char buffer[256] = {};
    if (!CFStringGetCString(static_cast<CFStringRef>(ref), buffer,
                            sizeof(buffer), kCFStringEncodingUTF8)) {
        return QString();
    }
    return QString::fromUtf8(buffer);
}

CFTypeRef property(io_registry_entry_t service, const char *key) {
    CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, key,
                                                 kCFStringEncodingUTF8);
    if (name == nullptr) {
        return nullptr;
    }
    CFTypeRef value =
        IORegistryEntryCreateCFProperty(service, name, kCFAllocatorDefault, 0);
    CFRelease(name);
    return value;
}

qlonglong intProperty(io_registry_entry_t service, const char *key) {
    CFTypeRef value = property(service, key);
    qlonglong out = -1;
    if (!cfInt(value, &out)) {
        out = -1;
    }
    if (value != nullptr) {
        CFRelease(value);
    }
    return out;
}

double doubleProperty(io_registry_entry_t service, const char *key) {
    CFTypeRef value = property(service, key);
    double out = -1.0;
    if (!cfDouble(value, &out)) {
        double integer = -1.0;
        qlonglong whole = -1;
        if (cfInt(value, &whole)) {
            integer = static_cast<double>(whole);
        }
        out = integer;
    }
    if (value != nullptr) {
        CFRelease(value);
    }
    return out;
}

double batteryTemperature(io_registry_entry_t service) {
    const double direct = doubleProperty(service, "Temperature");
    if (direct > 0.0) {
        return direct / 100.0;
    }
    CFTypeRef pack = property(service, "BatteryData");
    if (pack != nullptr) {
        if (CFGetTypeID(pack) == CFDictionaryGetTypeID()) {
            CFTypeRef value = CFDictionaryGetValue(
                static_cast<CFDictionaryRef>(pack), CFSTR("Temperature"));
            double celsius = -1.0;
            if (!cfDouble(value, &celsius)) {
                qlonglong whole = -1;
                if (cfInt(value, &whole)) {
                    celsius = static_cast<double>(whole);
                }
            }
            CFRelease(pack);
            if (celsius > 0.0) {
                return celsius / 100.0;
            }
        } else {
            CFRelease(pack);
        }
    }
    return -1.0;
}

}  // namespace

namespace macsensors {

SensorsSnapshot collect() {
    SensorsSnapshot out;

    io_registry_entry_t service = IOServiceGetMatchingService(
        kIOMainPortDefault, IOServiceMatching("AppleSmartBattery"));

    CFTypeRef infoRef = IOPSCopyPowerSourcesInfo();
    CFArrayRef list = infoRef != nullptr ? IOPSCopyPowerSourcesList(infoRef)
                                         : nullptr;

    CFDictionaryRef chosen = nullptr;
    if (list != nullptr) {
        const CFIndex count = CFArrayGetCount(list);
        for (CFIndex i = 0; i < count; ++i) {
            CFTypeRef source = CFArrayGetValueAtIndex(list, i);
            CFDictionaryRef description =
                IOPSGetPowerSourceDescription(infoRef, source);
            if (description == nullptr) {
                continue;
            }
            const QString type = cfString(CFDictionaryGetValue(
                description, CFSTR(kIOPSTypeKey)));
            if (type == QLatin1String(kIOPSInternalBatteryType)) {
                chosen = description;
                break;
            }
            if (chosen == nullptr) {
                chosen = description;
            }
        }
    }

    if (chosen != nullptr || service != 0) {
        BatteryReading battery;
        battery.name = QStringLiteral("BAT0");
        if (chosen != nullptr) {
            const QString name = cfString(
                CFDictionaryGetValue(chosen, CFSTR(kIOPSNameKey)));
            if (!name.isEmpty()) {
                battery.name = name;
            }
        }

        const QString sourceState =
            chosen != nullptr
                ? cfString(CFDictionaryGetValue(
                      chosen, CFSTR(kIOPSPowerSourceStateKey)))
                : QString();
        bool charging = false;
        bool charged = false;
        if (chosen != nullptr) {
            cfBool(CFDictionaryGetValue(chosen, CFSTR(kIOPSIsChargingKey)),
                   &charging);
            cfBool(CFDictionaryGetValue(chosen, CFSTR(kIOPSIsChargedKey)),
                   &charged);
        }
        if (service != 0) {
            CFTypeRef value = property(service, "IsCharging");
            bool registryCharging = false;
            if (cfBool(value, &registryCharging)) {
                charging = registryCharging;
            }
            if (value != nullptr) {
                CFRelease(value);
            }
        }

        const bool onAc = sourceState == QLatin1String(kIOPSACPowerValue)
                          || sourceState == QLatin1String("AC Power");
        const bool onBattery =
            sourceState == QLatin1String(kIOPSBatteryPowerValue)
            || sourceState == QLatin1String("Battery Power");
        if (onBattery) {
            battery.state = BatteryState::Discharging;
        } else if (charging) {
            battery.state = BatteryState::Charging;
        } else if (charged) {
            battery.state = BatteryState::Full;
        } else if (onAc) {
            battery.state = BatteryState::NotCharging;
        }

        if (chosen != nullptr) {
            qlonglong level = -1;
            if (cfInt(CFDictionaryGetValue(chosen,
                                           CFSTR(kIOPSCurrentCapacityKey)),
                      &level)
                && level >= 0 && level <= 100) {
                battery.capacityPercent = static_cast<int>(level);
            }
            qlonglong minutes = -1;
            if (cfInt(CFDictionaryGetValue(chosen,
                                           CFSTR(kIOPSTimeToEmptyKey)),
                      &minutes)
                && minutes > 0) {
                battery.timeToEmptyMin = static_cast<int>(minutes);
            }
            minutes = -1;
            if (cfInt(CFDictionaryGetValue(chosen,
                                           CFSTR(kIOPSTimeToFullChargeKey)),
                      &minutes)
                && minutes > 0) {
                battery.timeToFullMin = static_cast<int>(minutes);
            }
        }

        double voltageMv = -1.0;
        qlonglong currentMa = 0;
        qlonglong designMah = -1;
        qlonglong fullMah = -1;
        qlonglong nowMah = -1;
        if (service != 0) {
            voltageMv = doubleProperty(service, "Voltage");
            currentMa = intProperty(service, "Amperage");
            designMah = intProperty(service, "DesignCapacity");
            const qlonglong cycles = intProperty(service, "CycleCount");
            if (cycles >= 0) {
                battery.cycleCount = static_cast<int>(cycles);
            }
            const qlonglong rawCurrent =
                intProperty(service, "AppleRawCurrentCapacity");
            nowMah = rawCurrent >= 0 ? rawCurrent
                                    : intProperty(service, "CurrentCapacity");
            const qlonglong rawMax = intProperty(service, "AppleRawMaxCapacity");
            const qlonglong nominal =
                intProperty(service, "NominalChargeCapacity");
            const qlonglong maxCapacity = intProperty(service, "MaxCapacity");
            if (rawMax > 100) {
                fullMah = rawMax;
            } else if (nominal > 100) {
                fullMah = nominal;
            } else if (maxCapacity > 100) {
                fullMah = maxCapacity;
            } else {
                fullMah = nominal >= 0 ? nominal : maxCapacity;
            }
            if (designMah <= 0) {
                CFTypeRef pack = property(service, "BatteryData");
                if (pack != nullptr) {
                    if (CFGetTypeID(pack) == CFDictionaryGetTypeID()) {
                        qlonglong nested = -1;
                        if (cfInt(CFDictionaryGetValue(
                                      static_cast<CFDictionaryRef>(pack),
                                      CFSTR("DesignCapacity")),
                                  &nested)
                            && nested > 0) {
                            designMah = nested;
                        }
                    }
                    CFRelease(pack);
                }
            }
            const double celsius = batteryTemperature(service);
            if (celsius > 0.0 && celsius < 150.0) {
                battery.temperatureC = celsius;
            }
        }

        if (voltageMv > 0.0) {
            battery.voltageV = voltageMv / 1000.0;
            const double volts = battery.voltageV;
            if (nowMah > 0) {
                battery.energyNowWh = nowMah * volts / 1000.0;
            }
            if (fullMah > 0) {
                battery.energyFullWh = fullMah * volts / 1000.0;
            }
            if (designMah > 0) {
                battery.energyDesignWh = designMah * volts / 1000.0;
            }
            if (currentMa != 0) {
                battery.powerW =
                    std::fabs(volts * static_cast<double>(currentMa) / 1000.0);
            }
        }

        out.batteries.append(battery);

        AdapterReading adapter;
        adapter.name = QStringLiteral("AC");
        adapter.online = onAc;
        CFTypeRef details = IOPSCopyExternalPowerAdapterDetails();
        if (details != nullptr) {
            if (CFGetTypeID(details) == CFDictionaryGetTypeID()) {
                qlonglong adapterWatts = -1;
                if (cfInt(CFDictionaryGetValue(
                              static_cast<CFDictionaryRef>(details),
                              CFSTR(kIOPSPowerAdapterWattsKey)),
                          &adapterWatts)
                    && adapterWatts > 0) {
                    adapter.wattsW = static_cast<double>(adapterWatts);
                }
            }
            CFRelease(details);
        }
        out.adapters.append(adapter);
    }

    if (list != nullptr) {
        CFRelease(list);
    }
    if (infoRef != nullptr) {
        CFRelease(infoRef);
    }
    if (service != 0) {
        IOObjectRelease(service);
    }

    out.available = !out.batteries.isEmpty();
    if (!out.available) {
        out.note = QStringLiteral(
            "battery: no AppleSmartBattery service (desktop Mac or VM)");
    }
    recomputeSensorTotals(out);
    return out;
}

}  // namespace macsensors

#endif
