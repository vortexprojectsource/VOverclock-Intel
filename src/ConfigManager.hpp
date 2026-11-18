#pragma once

#include <string>
#include <vector>

namespace vint {

struct TargetExeRule {
    std::wstring exeName;
    bool enabled = true;
};

struct AppConfig {
    bool autoStart = false;
    bool startMinimized = false;
    bool minimizeToTray = true;
    bool closeToTray = true;
    bool alwaysOnTop = false;
    std::wstring trayStyle = L"logo";
    bool showNotifications = true;

    bool applyOnStartup = false;
    bool highResTimer = false;
    bool liveApply = false;

    int unparkCoresPercent = 100;
    int minStatePercent = 5;
    int maxStatePercent = 100;
    int turboBoostPolicy = 100;
    int boostMode = 4;
    int eppPercent = 0;
    int freqCapMhz = 0;

    int schedPolicy = 5;
    int incThreshold = 10;
    int decThreshold = 5;
    int heteroFloorPerf = 100;
    int concurrencyThreshold = 25;
    bool activeCooling = true;
    bool hwpAutonomous = true;
    bool disableIdleStates = false;
    bool disableOsThrottling = true;
    bool boostForegroundPCore = false;

    // Target EXE CPU optimizer settings (pure CPU/execution/I-O scheduling, zero RAM)
    int targetPriorityMode = 2;     // 0=Normal, 1=AboveNormal, 2=High, 3=Realtime
    int targetCoreMode = 2;         // 0=All Cores, 1=P-Cores Only, 2=Prefer P-Cores (CpuSets)
    bool targetDeepBoost = true;    // EcoQoS Off + Timer Throttle Off + Thread Ideal P-Core + Priority Boost + I/O High
    bool targetIfeoPersist = false; // HKLM IFEO PerfOptions (CpuPriorityClass + IoPriority at spawn)
    std::vector<TargetExeRule> targetExes;

    std::wstring originalSchemeGuid;
    std::wstring vpowerSchemeGuid;
};

class ConfigManager {
public:
    static std::wstring GetConfigFilePath();
    static AppConfig Load();
    static bool Save(const AppConfig& cfg);

    static bool IsAutoStartupEnabled();
    static bool SetAutoStartup(bool enabled, bool startMinimized);
};

}
