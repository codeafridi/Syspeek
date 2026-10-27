cmake_minimum_required(VERSION 3.16)

project(syspeek
    VERSION 0.8.9
    DESCRIPTION "Syspeek HUD (Qt 6 Widgets)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()

add_library(syspeek_core STATIC
    src/HudWindow.cpp
    src/HudWindow.h
    src/DashboardPanel.cpp
    src/DashboardPanel.h
    src/DisplaySnapshot.h
    src/ProcessIdentity.h
    src/LinuxProcess.cpp
    src/LinuxProcess.h
    src/FocusedWindowProvider.h
    src/GnomePidFileProvider.cpp
    src/GnomePidFileProvider.h
    src/FocusSampler.cpp
    src/FocusSampler.h
    src/CpuSampler.cpp
    src/CpuSampler.h
    src/AlertEngine.cpp
    src/AlertEngine.h
    src/AlertWebhook.cpp
    src/AlertWebhook.h
    src/HostSampler.cpp
    src/HostSampler.h
    src/IoRateTracker.cpp
    src/IoRateTracker.h
    src/NetRateTracker.cpp
    src/NetRateTracker.h
    src/ServicesStatus.cpp
    src/ServicesStatus.h
    src/ServicesSource.cpp
    src/ServicesSource.h
    src/SyntheticStatus.cpp
    src/SyntheticStatus.h
    src/SyntheticSource.cpp
    src/SyntheticSource.h
    src/JournalStatus.cpp
    src/JournalStatus.h
    src/JournalSource.cpp
    src/JournalSource.h
    src/MemoryFormat.cpp
    src/MemoryFormat.h
    src/MetricStore.cpp
    src/MetricStore.h
    src/Sparkline.cpp
    src/TimeseriesChart.cpp
    src/Sparkline.h
    src/ProcessGrouper.cpp
    src/ProcessGrouper.h
    src/GroupCache.cpp
    src/GroupCache.h
    src/SamplerWorker.cpp
    src/SamplerWorker.h
    src/HealthEvaluator.cpp
    src/HealthEvaluator.h
    src/NullFocusProvider.cpp
    src/NullFocusProvider.h
    src/X11FocusProvider.cpp
    src/X11FocusProvider.h
    src/HyprctlFocusProvider.cpp
    src/HyprctlFocusProvider.h
    src/SwaymsgFocusProvider.cpp
    src/SwaymsgFocusProvider.h
    src/NiriFocusProvider.cpp
    src/NiriFocusProvider.h
    src/FocusProviderFactory.cpp
    src/FocusProviderFactory.h
    src/CachingFocusProvider.cpp
    src/CachingFocusProvider.h
    src/KdeFocusState.cpp
    src/KdeFocusState.h
    src/KdeFocusProvider.cpp
    src/KdeFocusProvider.h
    src/KdeFocusService.cpp
    src/KdeFocusService.h
    src/DockerStatus.cpp
    src/DockerStatus.h
    src/DockerSource.cpp
    src/DockerSource.h
    src/DockerEngineClient.cpp
    src/DockerEngineClient.h
    src/Config.cpp
    src/Config.h
    src/CostModel.cpp
    src/CostModel.h
    src/DiskSampler.cpp
    src/DiskSampler.h
    src/DiskStatus.cpp
    src/DiskStatus.h
    src/TcpSampler.cpp
    src/TcpSampler.h
    src/TcpStatus.cpp
    src/TcpStatus.h
    src/SecurityStatus.cpp
    src/SecurityStatus.h
    src/LogParser.cpp
    src/LogParser.h
    src/TraceSource.cpp
    src/TraceSource.h
    src/TraceStatus.cpp
    src/TraceStatus.h
    src/SbomSource.cpp
    src/SbomSource.h
    src/SbomStatus.cpp
    src/SbomStatus.h
    src/PathSource.cpp
    src/PathSource.h
    src/PathStatus.cpp
    src/PathStatus.h
    src/StatsdSource.cpp
    src/StatsdSource.h
    src/StatsdStatus.cpp
    src/StatsdStatus.h
    src/SensitiveStatus.cpp
    src/SensitiveStatus.h
    src/SloStatus.cpp
    src/SloStatus.h
    src/DeprecationStatus.cpp
    src/DeprecationStatus.h
    src/ThreatStatus.cpp
    src/ThreatStatus.h
    src/PgClient.cpp
    src/PgClient.h
    src/DbmStatus.cpp
    src/DbmStatus.h
    src/DbmSource.cpp
    src/DbmSource.h
    src/PrometheusStatus.cpp
    src/PrometheusStatus.h
    src/MetricsServer.cpp
    src/MetricsServer.h
    src/MetricsEndpoint.cpp
    src/MetricsEndpoint.h
    src/WatchdogStatus.cpp
    src/WatchdogStatus.h
    src/TlsSource.cpp
    src/TlsSource.h
    src/TlsStatus.cpp
    src/TlsStatus.h
    src/RuntimeStatus.cpp
    src/RuntimeStatus.h
    src/IncidentStatus.cpp
    src/IncidentStatus.h
    src/ProfileStatus.cpp
    src/ProfileStatus.h
    src/HostInfoStatus.cpp
    src/HostInfoStatus.h
    src/FimStatus.cpp
    src/FimStatus.h
    src/FimSource.cpp
    src/FimSource.h
    src/DumpReport.cpp
    src/DumpReport.h
    src/AppLog.cpp
    src/AppLog.h
    src/ProbeRun.cpp
    src/ProbeRun.h
    src/MetricsExport.cpp
    src/MetricsExport.h
    src/ExportRun.cpp
    src/ExportRun.h
    src/ProcessDiskStatus.cpp
    src/ProcessDiskStatus.h
    src/GpuStatus.cpp
    src/GpuStatus.h
    src/HistoryLog.cpp
    src/HistoryLog.h
    src/AlertSilence.cpp
    src/AlertSilence.h
    src/MonitorBands.cpp
    src/MonitorBands.h
    src/ProcessNetStatus.cpp
    src/ProcessNetStatus.h
    src/ProcessActions.cpp
    src/ProcessActions.h
    src/KubeStatus.cpp
    src/KubeStatus.h
    src/KubeSource.cpp
    src/KubeSource.h
    src/platform/SystemBackend.h
    src/platform/PlatformFactory.cpp
    src/platform/PlatformFactory.h
    src/platform/PortCatalog.cpp
    src/platform/PortCatalog.h
    src/platform/null/NullBackend.cpp
    src/platform/null/NullBackend.h)

if(UNIX AND NOT APPLE)
    target_sources(syspeek_core PRIVATE
        src/platform/linux/LinuxBackend.cpp
        src/platform/linux/LinuxBackend.h
        src/platform/linux/LinuxGpu.cpp
        src/platform/linux/LinuxGpu.h)
elseif(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    target_sources(syspeek_core PRIVATE
        src/platform/mac/MacBackend.cpp
        src/platform/mac/MacBackend.h
        src/platform/mac/MacGpu.cpp
        src/platform/mac/MacGpu.h
        src/platform/mac/MacFocusProvider.mm
        src/platform/mac/MacFocusProvider.h
        src/platform/mac/MacLogSource.mm
        src/platform/mac/MacLogSource.h
        src/platform/mac/MacOverlay.mm
        src/platform/mac/MacOverlay.h
        src/platform/mac/MacFimStream.mm
        src/platform/mac/MacFimStream.h)
elseif(WIN32)
    find_package(Qt6 QUIET COMPONENTS DBus)
    target_sources(syspeek_core PRIVATE
        src/platform/win/WindowsBackend.cpp
        src/platform/win/WindowsBackend.h
        src/platform/win/WinFocusProvider.cpp
        src/platform/win/WinFocusProvider.h
        src/platform/win/WinLogSource.cpp
        src/platform/win/WinLogSource.h)
    if(TARGET Qt6::DBus)
        target_link_libraries(syspeek_core PUBLIC Qt6::DBus)
    endif()
    target_link_libraries(syspeek_core PUBLIC
        iphlpapi ws2_32 psapi user32 advapi32 pdh wevtapi)
    target_compile_definitions(syspeek_core PUBLIC
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS)
endif()

target_include_directories(syspeek_core PUBLIC src)
target_compile_definitions(syspeek_core PUBLIC
    "SYSPEEK_APP_VERSION=\"${PROJECT_VERSION}\"")
target_link_libraries(syspeek_core PUBLIC Qt6::Widgets Qt6::Network)
if(APPLE)
    target_link_libraries(syspeek_core PUBLIC
        "-framework Cocoa" "-framework CoreFoundation" "-framework IOKit"
        "-framework OSLog" "-framework CoreServices")
    target_compile_options(syspeek_core PUBLIC $<$<COMPILE_LANGUAGE:OBJCXX>:-Wno-deprecated-declarations>)
elseif(UNIX)
    target_link_libraries(syspeek_core PUBLIC Qt6::DBus)
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_link_libraries(syspeek_core PUBLIC X11::X11)
        target_compile_definitions(syspeek_core PUBLIC SYSPEEK_X11=1)
    else()
        message(WARNING
            "X11 dev headers not found; native X11 focus provider disabled")
    endif()
endif()
target_compile_options(syspeek_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)

add_executable(syspeek src/main.cpp)
target_link_libraries(syspeek PRIVATE syspeek_core)
if(WIN32)
    target_sources(syspeek PRIVATE
        packaging/windows/syspeek.rc)
    # GUI subsystem: launching the HUD must never host it in a console
    # window (shipped GUI tools are subsystem 2 - e.g. Everything.exe;
    # console attachment for verb output is opt-in, see main.cpp).
    set_target_properties(syspeek PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE)
    set_target_properties(syspeek PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Syspeek"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.syspeek.hud"
        MACOSX_BUNDLE_EXECUTABLE_NAME "syspeek"
        MACOSX_BUNDLE_INFO_STRING "Syspeek ${PROJECT_VERSION} - local system HUD"
        MACOSX_BUNDLE_LONG_VERSION_STRING "syspeek ${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in")
endif()

enable_testing()

add_executable(syspeek_tests tests/test_hud_buttons.cpp)
target_link_libraries(syspeek_tests PRIVATE syspeek_core)

add_executable(syspeek_focus_tests tests/test_focus.cpp)
target_link_libraries(syspeek_focus_tests PRIVATE syspeek_core)

add_executable(syspeek_cpu_tests tests/test_cpu.cpp)
target_link_libraries(syspeek_cpu_tests PRIVATE syspeek_core)

add_executable(syspeek_memory_tests tests/test_memory.cpp)
target_link_libraries(syspeek_memory_tests PRIVATE syspeek_core)

add_executable(syspeek_group_tests tests/test_group.cpp)
target_link_libraries(syspeek_group_tests PRIVATE syspeek_core)

add_executable(syspeek_sampler_tests tests/test_sampler.cpp)
target_link_libraries(syspeek_sampler_tests PRIVATE syspeek_core)

add_executable(syspeek_health_tests tests/test_health.cpp)
target_link_libraries(syspeek_health_tests PRIVATE syspeek_core)

add_executable(syspeek_provider_tests tests/test_providers.cpp)
target_link_libraries(syspeek_provider_tests PRIVATE syspeek_core)

add_executable(syspeek_docker_tests tests/test_docker.cpp)
target_link_libraries(syspeek_docker_tests PRIVATE syspeek_core)

add_executable(syspeek_config_tests tests/test_config.cpp)
target_link_libraries(syspeek_config_tests PRIVATE syspeek_core)

add_executable(syspeek_kube_tests tests/test_kube.cpp)
target_link_libraries(syspeek_kube_tests PRIVATE syspeek_core)

add_executable(syspeek_adapter_tests tests/test_adapters.cpp)
target_link_libraries(syspeek_adapter_tests PRIVATE syspeek_core)

add_executable(syspeek_metrics_tests tests/test_metrics.cpp)
target_link_libraries(syspeek_metrics_tests PRIVATE syspeek_core)

add_executable(syspeek_screenshot EXCLUDE_FROM_ALL tools/screenshot.cpp)
target_link_libraries(syspeek_screenshot PRIVATE syspeek_core)

add_executable(syspeek_backend_tests tests/test_backend.cpp)
target_link_libraries(syspeek_backend_tests PRIVATE syspeek_core)

add_test(NAME hud_buttons COMMAND syspeek_tests)
add_test(NAME focus COMMAND syspeek_focus_tests)
add_test(NAME cpu COMMAND syspeek_cpu_tests)
add_test(NAME memory COMMAND syspeek_memory_tests)
add_test(NAME group COMMAND syspeek_group_tests)
add_test(NAME sampler COMMAND syspeek_sampler_tests)
add_test(NAME health COMMAND syspeek_health_tests)
add_test(NAME providers COMMAND syspeek_provider_tests)
add_test(NAME docker COMMAND syspeek_docker_tests)
add_test(NAME config COMMAND syspeek_config_tests)
add_test(NAME kube COMMAND syspeek_kube_tests)
add_test(NAME adapters COMMAND syspeek_adapter_tests)
add_test(NAME metrics COMMAND syspeek_metrics_tests)
add_test(NAME backend COMMAND syspeek_backend_tests)

set_tests_properties(hud_buttons focus cpu memory group sampler health providers docker config kube adapters metrics backend PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
