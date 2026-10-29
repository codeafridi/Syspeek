#include "WindowsSensors.h"

#ifdef Q_OS_WIN

#include <windows.h>

#include <setupapi.h>
#include <winioctl.h>

#include <cmath>

#if defined(__has_include)
#if __has_include(<poclass.h>)
#define SYSPEEK_HAVE_POCLASS 1
#endif
#endif

#ifdef SYSPEEK_HAVE_POCLASS
#include <initguid.h>
#include <poclass.h>
#else
typedef enum {
    BatteryInformation = 0,
    BatteryGranularityInformation,
    BatteryTemperature,
    BatteryEstimatedTime,
    BatteryDeviceName,
    BatteryManufactureDate,
    BatteryManufactureName,
    BatteryUniqueID,
    BatterySerialNumber
} BATTERY_QUERY_INFORMATION_LEVEL;

typedef struct _BATTERY_QUERY_INFORMATION {
    ULONG BatteryTag;
    BATTERY_QUERY_INFORMATION_LEVEL InformationLevel;
    ULONG AtRate;
} BATTERY_QUERY_INFORMATION, *PBATTERY_QUERY_INFORMATION;

typedef struct _BATTERY_INFORMATION {
    ULONG Capabilities;
    UCHAR Technology;
    UCHAR Reserved[3];
    UCHAR Chemistry[4];
    ULONG DesignedCapacity;
    ULONG FullChargedCapacity;
    ULONG DefaultAlert1;
    ULONG DefaultAlert2;
    ULONG CriticalBias;
    ULONG CycleCount;
} BATTERY_INFORMATION, *PBATTERY_INFORMATION;

typedef struct _BATTERY_WAIT_STATUS {
    ULONG BatteryTag;
    ULONG Timeout;
    ULONG PowerState;
    ULONG LowCapacity;
    ULONG HighCapacity;
} BATTERY_WAIT_STATUS, *PBATTERY_WAIT_STATUS;

typedef struct _BATTERY_STATUS {
    ULONG PowerState;
    ULONG Capacity;
    ULONG Voltage;
    LONG Rate;
} BATTERY_STATUS, *PBATTERY_STATUS;

#define BATTERY_UNKNOWN_CAPACITY 0xFFFFFFFF
#define BATTERY_UNKNOWN_VOLTAGE 0xFFFFFFFF
#define BATTERY_UNKNOWN_RATE 0x80000000
#define BATTERY_UNKNOWN_TIME 0xFFFFFFFF
#define BATTERY_SYSTEM_BATTERY 0x80000000
#define BATTERY_CAPACITY_RELATIVE 0x40000000
#define BATTERY_POWER_ON_LINE 0x00000001
#define BATTERY_DISCHARGING 0x00000002
#define BATTERY_CHARGING 0x00000004
#define BATTERY_CRITICAL 0x00000008
#define MAX_BATTERY_STRING_SIZE 128

#define FILE_DEVICE_BATTERY 0x00000029

#define IOCTL_BATTERY_QUERY_TAG                                            \
    CTL_CODE(FILE_DEVICE_BATTERY, 0x10, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_BATTERY_QUERY_INFORMATION                                    \
    CTL_CODE(FILE_DEVICE_BATTERY, 0x11, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_BATTERY_QUERY_STATUS                                         \
    CTL_CODE(FILE_DEVICE_BATTERY, 0x13, METHOD_BUFFERED, FILE_READ_ACCESS)

static const GUID GUID_DEVICE_BATTERY = {
    0x72631e54,
    0x78A4,
    0x11d0,
    {0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a}};
#endif

namespace {

QString batteryString(HANDLE device, ULONG tag,
                      BATTERY_QUERY_INFORMATION_LEVEL level) {
    BATTERY_QUERY_INFORMATION query = {};
    query.BatteryTag = tag;
    query.InformationLevel = level;
    wchar_t buffer[MAX_BATTERY_STRING_SIZE] = {};
    DWORD returned = 0;
    if (!DeviceIoControl(device, IOCTL_BATTERY_QUERY_INFORMATION, &query,
                         sizeof(query), buffer, sizeof(buffer), &returned,
                         nullptr)) {
        return QString();
    }
    buffer[(MAX_BATTERY_STRING_SIZE / sizeof(wchar_t)) - 1] = L'\0';
    return QString::fromWCharArray(buffer).trimmed();
}

QString deviceRegistryString(HDEVINFO devices, PSP_DEVINFO_DATA deviceInfo,
                             DWORD property) {
    if (deviceInfo == nullptr) {
        return QString();
    }
    wchar_t buffer[512] = {};
    DWORD type = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(devices, deviceInfo, property,
                                           &type,
                                           reinterpret_cast<PBYTE>(buffer),
                                           sizeof(buffer), nullptr)) {
        return QString();
    }
    return QString::fromWCharArray(buffer).trimmed();
}

}  // namespace

namespace winsensors {

SensorsSnapshot collect() {
    SensorsSnapshot out;
    HDEVINFO devices = SetupDiGetClassDevsW(
        &GUID_DEVICE_BATTERY, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        out.note = QStringLiteral(
            "battery: SetupDiGetClassDevs(GUID_DEVICE_BATTERY) failed");
        return out;
    }

    // The interface set resolves device paths; a separate device information
    // set is what carries the registry description/manufacturer properties
    // (SetupDiGetDeviceRegistryPropertyW takes SP_DEVINFO_DATA, never
    // SP_DEVICE_INTERFACE_DATA).
    const HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(
        &GUID_DEVICE_BATTERY, nullptr, nullptr, DIGCF_PRESENT);

    int denied = 0;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData = {};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &GUID_DEVICE_BATTERY,
                                         index, &interfaceData)) {
            break;
        }

        SP_DEVINFO_DATA deviceInfo = {};
        deviceInfo.cbSize = sizeof(deviceInfo);
        const bool haveDeviceInfo =
            deviceInfoSet != INVALID_HANDLE_VALUE
            && SetupDiEnumDeviceInfo(deviceInfoSet, index, &deviceInfo);

        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0,
                                         &needed, nullptr);
        if (needed == 0) {
            continue;
        }
        QByteArray detail(static_cast<int>(needed) + 8, 0);
        const quintptr raw = reinterpret_cast<quintptr>(detail.data());
        const quintptr aligned = (raw + 7) & ~static_cast<quintptr>(7);
        auto *detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(
            aligned);
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData,
                                              detailData, needed, &needed,
                                              nullptr)) {
            continue;
        }

        const wchar_t *path = detailData->DevicePath;
        HANDLE device = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
        if (device == INVALID_HANDLE_VALUE) {
            ++denied;
            continue;
        }

        DWORD wait = 0;
        DWORD returned = 0;
        ULONG tag = 0;
        if (!DeviceIoControl(device, IOCTL_BATTERY_QUERY_TAG, &wait,
                             sizeof(wait), &tag, sizeof(tag), &returned,
                             nullptr)
            || tag == 0) {
            CloseHandle(device);
            continue;
        }

        BATTERY_QUERY_INFORMATION query = {};
        query.BatteryTag = tag;
        query.InformationLevel = BatteryInformation;
        BATTERY_INFORMATION info = {};
        if (!DeviceIoControl(device, IOCTL_BATTERY_QUERY_INFORMATION, &query,
                             sizeof(query), &info, sizeof(info), &returned,
                             nullptr)) {
            CloseHandle(device);
            continue;
        }
        if ((info.Capabilities & BATTERY_SYSTEM_BATTERY) == 0) {
            CloseHandle(device);
            continue;
        }

        BatteryReading battery;
        battery.name = batteryString(device, tag, BatteryDeviceName);
        if (battery.name.isEmpty()) {
            battery.name = deviceRegistryString(
                deviceInfoSet, haveDeviceInfo ? &deviceInfo : nullptr,
                SPDRP_DEVICEDESC);
        }
        if (battery.name.isEmpty()) {
            battery.name = QStringLiteral("battery%1").arg(index);
        }
        battery.manufacturer =
            batteryString(device, tag, BatteryManufactureName);
        if (battery.manufacturer.isEmpty()) {
            battery.manufacturer = deviceRegistryString(
                deviceInfoSet, haveDeviceInfo ? &deviceInfo : nullptr,
                SPDRP_MFG);
        }
        battery.technology =
            QString::fromLatin1(reinterpret_cast<const char *>(info.Chemistry),
                                static_cast<int>(sizeof(info.Chemistry)))
                .trimmed();

        BATTERY_WAIT_STATUS waitStatus = {};
        waitStatus.BatteryTag = tag;
        BATTERY_STATUS status = {};
        if (DeviceIoControl(device, IOCTL_BATTERY_QUERY_STATUS, &waitStatus,
                            sizeof(waitStatus), &status, sizeof(status),
                            &returned, nullptr)) {
            const bool relative =
                (info.Capabilities & BATTERY_CAPACITY_RELATIVE) != 0;
            const bool fullKnown =
                info.FullChargedCapacity != BATTERY_UNKNOWN_CAPACITY
                && info.FullChargedCapacity > 0;
            const bool nowKnown = status.Capacity != BATTERY_UNKNOWN_CAPACITY;
            if (fullKnown && nowKnown) {
                battery.capacityPercent = static_cast<int>(
                    std::lround(static_cast<double>(status.Capacity) * 100.0
                                / static_cast<double>(info.FullChargedCapacity)));
            }
            if (!relative) {
                if (nowKnown) {
                    battery.energyNowWh = status.Capacity / 1000.0;
                }
                if (fullKnown) {
                    battery.energyFullWh =
                        info.FullChargedCapacity / 1000.0;
                }
                if (info.DesignedCapacity != BATTERY_UNKNOWN_CAPACITY
                    && info.DesignedCapacity > 0) {
                    battery.energyDesignWh = info.DesignedCapacity / 1000.0;
                }
            }
            if (status.Voltage != BATTERY_UNKNOWN_VOLTAGE
                && status.Voltage > 0) {
                battery.voltageV = status.Voltage / 1000.0;
            }
            if (static_cast<ULONG>(status.Rate) != BATTERY_UNKNOWN_RATE) {
                battery.powerW =
                    std::fabs(static_cast<double>(status.Rate) / 1000.0);
            }

            if ((status.PowerState & BATTERY_CHARGING) != 0) {
                battery.state = BatteryState::Charging;
            } else if ((status.PowerState & BATTERY_DISCHARGING) != 0) {
                battery.state = BatteryState::Discharging;
            } else if ((status.PowerState & BATTERY_POWER_ON_LINE) != 0) {
                battery.state =
                    battery.capacityPercent >= 100 ? BatteryState::Full
                                                   : BatteryState::NotCharging;
            }

            AdapterReading adapter;
            adapter.name = QStringLiteral("AC");
            adapter.online = (status.PowerState & BATTERY_POWER_ON_LINE) != 0;
            out.adapters.append(adapter);
        }

        if (info.CycleCount != 0) {
            battery.cycleCount = static_cast<int>(info.CycleCount);
        }

        query.InformationLevel = BatteryEstimatedTime;
        ULONG seconds = 0;
        if (DeviceIoControl(device, IOCTL_BATTERY_QUERY_INFORMATION, &query,
                            sizeof(query), &seconds, sizeof(seconds),
                            &returned, nullptr)
            && seconds != BATTERY_UNKNOWN_TIME) {
            const int minutes = static_cast<int>((seconds + 30) / 60);
            if (battery.state == BatteryState::Charging) {
                battery.timeToFullMin = minutes;
            } else {
                battery.timeToEmptyMin = minutes;
            }
        }

        query.InformationLevel = BatteryTemperature;
        ULONG tenthKelvin = 0;
        if (DeviceIoControl(device, IOCTL_BATTERY_QUERY_INFORMATION, &query,
                            sizeof(query), &tenthKelvin, sizeof(tenthKelvin),
                            &returned, nullptr)
            && tenthKelvin != 0) {
            const double celsius =
                static_cast<double>(tenthKelvin) / 10.0 - 273.15;
            if (celsius > -50.0 && celsius < 150.0) {
                battery.temperatureC = celsius;
            }
        }

        out.batteries.append(battery);
        CloseHandle(device);
    }

    if (deviceInfoSet != INVALID_HANDLE_VALUE) {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
    }
    SetupDiDestroyDeviceInfoList(devices);

    out.available = !out.batteries.isEmpty();
    if (!out.available) {
        if (denied > 0) {
            out.note = QStringLiteral(
                "battery: %1 device(s) present but access denied - run with "
                "battery device read access to read them").arg(denied);
        } else {
            out.note = QStringLiteral(
                "battery: no system battery reported (desktop or VM)");
        }
    }
    recomputeSensorTotals(out);
    return out;
}

}  // namespace winsensors

#endif
