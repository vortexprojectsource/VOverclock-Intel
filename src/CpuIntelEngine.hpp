#pragma once

#include "ConfigManager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace vint {

enum class CoreKind {
    PCore,
    ECore,
    Standard
};

enum class SupportLevel {
    Supported,
    ConditionalAdmin,
    LockedOrLimited,
    Unsupported,
    Blocked
};

struct FeatureCheckItem {
    std::wstring id;
    std::wstring name;
    std::wstring detail;
    SupportLevel level = SupportLevel::Unsupported;
    std::wstring badgeText;
};

struct PowerPlanEntry {
    std::wstring guidStr;
    std::wstring name;
    bool isActive = false;
    bool isVPower = false;
};

struct RunningProcessEntry {
    std::wstring exeName;
    uint32_t instanceCount = 1;
    bool isForeground = false;
};

struct CoreTopology {
    uint32_t coreIndex = 0;
    uint32_t typeLocalIndex = 0;
    CoreKind kind = CoreKind::Standard;
    uint8_t efficiencyClass = 0;
    bool hasSmt = false;
    std::vector<std::pair<WORD, uint32_t>> logicalProcessors;
};

struct CpuHardwareInfo {
    std::wstring vendorId;
    std::wstring brandName;
    bool isIntel = false;
    bool isAmd = false;
    bool isHybrid = false;
    bool isUnlockedSku = false;

    uint32_t family = 0;
    uint32_t model = 0;
    uint32_t stepping = 0;
    uint32_t baseMhz = 0;
    uint32_t maxTurboMhz = 0;
    uint32_t busMhz = 0;

    uint32_t physicalCores = 0;
    uint32_t logicalThreads = 0;
    uint32_t pCoreCount = 0;
    uint32_t eCoreCount = 0;

    uint32_t l1CacheKB = 0;
    uint32_t l2CacheKB = 0;
    uint32_t l3CacheKB = 0;

    bool hasThermalSensor = false;
    bool hasTurboBoost = false;
    bool hasTurboMax3 = false;
    bool hasHwp = false;
    bool hasHwpEpp = false;
    bool hasPkgThermal = false;
    bool hasAvx2 = false;
    bool hasVtX = false;
    bool hasAes = false;
    bool hasAcpiThermal = false;

    std::vector<CoreTopology> cores;
    std::vector<FeatureCheckItem> featureChecks;
};

struct CoreLiveSample {
    uint32_t coreIndex = 0;
    uint32_t typeLocalIndex = 0;
    CoreKind kind = CoreKind::Standard;
    uint32_t threadCount = 1;
    double usagePercent = 0.0;
    uint32_t currentMhz = 0;
    uint32_t baseMhz = 0;
};

struct CpuLiveSnapshot {
    bool isIntel = false;
    bool isAmdBlocked = false;
    bool onAcPower = true;

    double totalUsagePercent = 0.0;
    double pCoreAvgUsage = 0.0;
    double eCoreAvgUsage = 0.0;

    uint32_t packageMhz = 0;
    uint32_t pCoreAvgMhz = 0;
    uint32_t eCoreAvgMhz = 0;
    uint32_t peakCoreMhz = 0;
    double tempCelsius = -1.0;

    uint32_t processCount = 0;
    uint32_t threadCount = 0;

    std::wstring activePlanName;
    std::wstring activePlanGuid;
    bool isVPowerActive = false;
    bool vpowerPlanExists = false;

    uint32_t curBoostMode = 1;
    uint32_t curTurboBoostPol = 100;
    uint32_t curEpp = 50;
    uint32_t curMinState = 5;
    uint32_t curMaxState = 100;
    uint32_t curMinCores = 100;
    uint32_t curSchedPolicy = 5;
    uint32_t curFreqCapMhz = 0;
    bool curIdleDisabled = false;
    bool highResTimerActive = false;
    std::wstring activeBoostedProcess;
    uint32_t activeTargetCount = 0;

    std::vector<CoreLiveSample> cores;
};

struct TuneApplyResult {
    bool success = false;
    bool blockedNonIntel = false;
    bool accessDenied = false;
    std::wstring schemeGuid;
    std::wstring message;
};

class CpuIntelEngine {
public:
    CpuIntelEngine();
    ~CpuIntelEngine();

    CpuIntelEngine(const CpuIntelEngine&) = delete;
    CpuIntelEngine& operator=(const CpuIntelEngine&) = delete;

    void Initialize();
    const CpuHardwareInfo& GetHardwareInfo() const { return m_info; }

    CpuLiveSnapshot SampleLiveSnapshot();

    std::vector<PowerPlanEntry> ListAllPowerPlans() const;
    std::vector<RunningProcessEntry> ListRunningProcesses() const;
    bool SyncConfigFromActivePlan(AppConfig& cfg) const;
    TuneApplyResult SwitchPowerPlan(const std::wstring& guidStr, AppConfig& cfg);
    TuneApplyResult CreateOrActivateVPowerPlan(AppConfig& cfg, bool applyMaxIntelPreset = true);
    TuneApplyResult ApplyTuneProfile(AppConfig& cfg, bool ensureVPowerScheme);
    TuneApplyResult RestoreStockPlan(AppConfig& cfg);
    bool SetHighResTimer(bool enable);
    void ApplyOsBrutalTweaks(bool enable) const;

    std::wstring TickProcessBoosts(const AppConfig& cfg, uint32_t& outBoostedTargetCount, bool forceReapply = false);
    TuneApplyResult ApplyTargetExeRulesNow(const AppConfig& cfg);
    void ClearTargetExeRuleEffect(const std::wstring& exeName);
    void RestoreAllBoostedProcesses(const AppConfig& cfg);
    void InvalidateProcessBoostCache();

    static bool IsElevated();
    static bool RestartElevated(bool startMinimized);
    static const wchar_t* BoostModeToString(uint32_t mode);
    static const wchar_t* SchedPolicyToString(uint32_t policy);
    static bool EvaluateVendorIsIntel(const std::wstring& vendorId, const std::wstring& brandName);
    static bool DetectUnlockedIntelSku(const std::wstring& brandName);

private:
    struct Impl;
    Impl* m_impl = nullptr;
    CpuHardwareInfo m_info;

    void DetectHardwareAndTopology();
    void BuildFeatureCompatibilityMatrix();
    double QueryAcpiThermalC();
};

}
