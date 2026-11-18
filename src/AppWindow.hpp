#pragma once

#include "ConfigManager.hpp"
#include "CpuIntelEngine.hpp"
#include "TrayIconRenderer.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <unknwn.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <cstdint>
#include <deque>
#include <vector>

namespace vint {

constexpr UINT WM_APP_SHOW_WINDOW = WM_APP + 1;
constexpr UINT WM_APP_TRAY_ICON   = WM_APP + 2;
constexpr UINT WM_APP_AMD_NOTIFY  = WM_APP + 3;

class AppWindow {
public:
    AppWindow(HINSTANCE hInst, bool startMinimized);
    ~AppWindow();

    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    bool Create();
    bool PreTranslateMessage(MSG* msg);
    HWND GetHwnd() const { return m_hwnd; }

private:
    enum class CoreFilterMode { All, PCores, ECores };
    enum class LeftTab { Sliders, Advanced, Targets, Compat };
    enum class PresetMode { Eco, Balanced, GamingOc, MaxIntelOc };
    enum class FadeMode { None, In, OutHide, OutMinimize, OutClose };

    struct CoreCardVisual {
        uint32_t coreIndex = 0;
        CoreKind kind = CoreKind::Standard;
        winrt::Windows::UI::Xaml::Controls::Border cardBorder{nullptr};
        winrt::Windows::UI::Xaml::Controls::TextBlock usageText{nullptr};
        winrt::Windows::UI::Xaml::Controls::TextBlock clockText{nullptr};
        winrt::Windows::UI::Xaml::Shapes::Polyline sparkLine{nullptr};
        winrt::Windows::UI::Xaml::Shapes::Polygon sparkFill{nullptr};
        std::deque<double> history;
    };

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate();
    void OnDestroy();
    void OnSize(UINT width, UINT height);
    void OnDpiChanged(UINT newDpi, const RECT* suggestedRect);
    void OnTimer(UINT_PTR timerId);
    void OnTrayMessage(LPARAM lParam);

    void ApplyDwmTheme();
    void ApplyAlwaysOnTop();
    void ResizeXamlIsland();

    void BindXamlControls();
    void LoadLogoIntoUi();
    void PopulateCpuStaticInfo();
    void PopulateCustomPowerPlanMenu();
    void TogglePowerPlanPopup(bool open);
    void PopulateRunningProcPopupMenu();
    void ToggleRunningProcPopup(bool open);
    void RebuildTargetExeListUi();
    void AddTargetExeRule(const std::wstring& exeName);
    void BrowseAndAddTargetExe();
    void PopulateFeatureCompatList();
    void BuildCoreCardVisuals();
    void RebuildCoreCardsGrid();
    void ApplyAmdBlockGuardIfNeeded();

    void HydrateSettingsAndTuningUi();
    void UpdateSliderLabelsAndPills();
    void SaveAndApplySettings();
    void ApplyQuickPreset(PresetMode preset);

    void SwitchLeftTab(LeftTab tab);
    void SwitchCoreFilter(CoreFilterMode filter);
    void SwitchToSettings(bool showSettings);
    void ShowMainWindow();
    void HideToTray();
    void MinimizeWindow();

    void StartWindowFade(FadeMode mode);
    void ApplyLayeredAlpha(BYTE alpha);
    void ClearLayeredStyle();
    void SyncScrollThumb(
        const winrt::Windows::UI::Xaml::Controls::ScrollViewer& sv,
        const winrt::Windows::UI::Xaml::Controls::Grid& track,
        const winrt::Windows::UI::Xaml::Controls::Border& thumb,
        const winrt::Windows::UI::Xaml::Media::TranslateTransform& transform);
    void AttachDragHandlers(const winrt::Windows::UI::Xaml::UIElement& element);

    void RefreshCpuTelemetry();
    void UpdateCoreCardVisual(CoreCardVisual& card, const CoreLiveSample& sample);
    void SetActionStatus(const std::wstring& text, bool isError);

    void ExecuteActivateVPowerPlan();
    void ExecuteApplyTuneProfile(bool ensureVPower);
    void ExecuteRestoreStock();
    void ExecuteSwitchSelectedPlan(const std::wstring& guidStr);
    void ExecuteApplyTargetsNow();

    void InitTrayIcon();
    void UpdateTrayIcon(const CpuLiveSnapshot& snap, bool forceIconRecreate);
    void RemoveTrayIcon();
    void ShowTrayBalloon(const wchar_t* title, const wchar_t* body, bool errorIcon);
    void ShowTrayContextMenu();

    HINSTANCE m_hInst = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_hwndXamlIsland = nullptr;
    bool m_startMinimized = false;
    bool m_isElevated = false;
    bool m_hydratingUi = false;
    bool m_trayAdded = false;
    bool m_closingForReal = false;
    bool m_showingSettings = false;
    bool m_amdPopupShown = false;
    bool m_powerPlanPopupOpen = false;
    bool m_runningProcPopupOpen = false;
    bool m_boostPausedByRestore = false;
    bool m_isDraggingWindow = false;
    POINT m_dragStartCursor{};
    POINT m_dragStartWin{};
    FadeMode m_fadeMode = FadeMode::None;
    double m_fadeProgress = 0.0;

    LeftTab m_leftTab = LeftTab::Sliders;
    CoreFilterMode m_coreFilter = CoreFilterMode::All;

    UINT m_dpi = 96;
    uint32_t m_sessionPeakMhz = 0;
    uint32_t m_lastTrayPercent = 999;
    std::wstring m_lastTrayStyle;
    HICON m_currentTrayIcon = nullptr;
    HICON m_appIconLarge = nullptr;
    HICON m_appIconSmall = nullptr;

    AppConfig m_config;
    CpuIntelEngine m_engine;
    CpuHardwareInfo m_cpuInfo;
    CpuLiveSnapshot m_lastSnap;
    TrayIconRenderer m_trayRenderer;
    std::vector<CoreCardVisual> m_coreCards;

    winrt::Windows::UI::Xaml::Hosting::WindowsXamlManager m_xamlManager{nullptr};
    winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource m_xamlSource{nullptr};
    winrt::Windows::UI::Xaml::FrameworkElement m_root{nullptr};

    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_gearSpinInSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_gearSpinOutSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_openSettingsSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_openMainSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_dashEntranceSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_liveDotPulseSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_leftTabSwitchSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_coreFilterSlideSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_planOpenSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_planCloseSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_runningOpenSb{nullptr};
    winrt::Windows::UI::Xaml::Media::Animation::Storyboard m_statusPopSb{nullptr};

    winrt::Windows::UI::Xaml::Controls::Grid m_titleBarDragArea{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_settingsHeaderDragArea{nullptr};
    winrt::Windows::UI::Xaml::Controls::Image m_logoImage{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_cpuBrandText{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_cpuSpecShortText{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_customPlanSelectBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_selectedPlanLabelText{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_powerPlanPopupHost{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_powerPlanBackdrop{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border m_powerPlanPopupBorder{nullptr};
    winrt::Windows::UI::Xaml::Controls::StackPanel m_powerPlanPopupList{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_runningProcPopupHost{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_runningProcBackdrop{nullptr};
    winrt::Windows::UI::Xaml::Controls::StackPanel m_runningProcPopupList{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_winMinimizeBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_winCloseBtn{nullptr};

    winrt::Windows::UI::Xaml::Controls::Grid m_mainView{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_settingsView{nullptr};

    winrt::Windows::UI::Xaml::Controls::TextBlock m_kpiClockText{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_kpiClockSubText{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_kpiPeakText{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_kpiPeakSubText{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_kpiLoadText{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_kpiLoadSubText{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_kpiTempText{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_kpiBoostSubText{nullptr};

    winrt::Windows::UI::Xaml::Controls::Button m_leftTabSlidersBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_leftTabAdvBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_leftTabTargetsBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_leftTabCheckBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglLiveApply{nullptr};

    winrt::Windows::UI::Xaml::Controls::Grid m_leftPageSliders{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_leftPageAdvanced{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_leftPageTargets{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_leftPageCompat{nullptr};

    winrt::Windows::UI::Xaml::Controls::Slider m_sliderUnpark{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_sldUnparkVal{nullptr};
    winrt::Windows::UI::Xaml::Controls::Slider m_sliderMinState{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_sldMinStateVal{nullptr};
    winrt::Windows::UI::Xaml::Controls::Slider m_sliderMaxState{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_sldMaxStateVal{nullptr};
    winrt::Windows::UI::Xaml::Controls::Slider m_sliderBoostPol{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_sldBoostPolVal{nullptr};
    winrt::Windows::UI::Xaml::Controls::Slider m_sliderEpp{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_sldEppVal{nullptr};
    winrt::Windows::UI::Xaml::Controls::Slider m_sliderFreqCap{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_sldFreqCapVal{nullptr};

    winrt::Windows::UI::Xaml::Controls::Button m_boostPrevBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_boostNextBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_boostValueText{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_schedPrevBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_schedNextBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_schedValueText{nullptr};

    winrt::Windows::UI::Xaml::Controls::Slider m_sliderIncThresh{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_sldIncThreshVal{nullptr};
    winrt::Windows::UI::Xaml::Controls::Slider m_sliderDecThresh{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_sldDecThreshVal{nullptr};
    winrt::Windows::UI::Xaml::Controls::Slider m_sliderHeteroFloor{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_sldHeteroFloorVal{nullptr};
    winrt::Windows::UI::Xaml::Controls::Slider m_sliderConcur{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_sldConcurVal{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglActiveCooling{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglOsDirectHwp{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglIdleDisable{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglOsThrottle{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglAdvHighRes{nullptr};

    winrt::Windows::UI::Xaml::Controls::Button m_browseExeBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_pickRunningExeBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_applyTargetsBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::StackPanel m_targetExeListPanel{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_targetExeEmptyHint{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_targetPrio0Btn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_targetPrio1Btn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_targetPrio2Btn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_targetPrio3Btn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_targetCore0Btn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_targetCore1Btn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_targetCore2Btn{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglTargetDeepBoost{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglTargetIfeo{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglFgBoost{nullptr};

    winrt::Windows::UI::Xaml::Controls::ScrollViewer m_compatScrollViewer{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_compatScrollTrack{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border m_compatScrollThumb{nullptr};
    winrt::Windows::UI::Xaml::Media::TranslateTransform m_compatScrollThumbTransform{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_compatSummaryText{nullptr};
    winrt::Windows::UI::Xaml::Controls::StackPanel m_compatListPanel{nullptr};

    winrt::Windows::UI::Xaml::Controls::Button m_presetEcoBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_presetBalancedBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_presetGamingBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_presetMaxOcBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_applyPresetsBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_restoreBtn{nullptr};

    winrt::Windows::UI::Xaml::Controls::Border m_amdBlockOverlay{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_amdBlockVendorText{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_amdBlockCpuNameText{nullptr};

    winrt::Windows::UI::Xaml::Controls::StackPanel m_coreLegendHybrid{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_filterAllBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_filterPCoreBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_filterECoreBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::ScrollViewer m_coreGridScrollViewer{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_coreGraphsContainer{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_coreScrollTrack{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border m_coreScrollThumb{nullptr};
    winrt::Windows::UI::Xaml::Media::TranslateTransform m_coreScrollThumbTransform{nullptr};

    winrt::Windows::UI::Xaml::Controls::Button m_settingsBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::TextBlock m_actionStatusText{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_minTrayBtn{nullptr};

    winrt::Windows::UI::Xaml::Controls::Button m_backBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglAutoStart{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglStartMinimized{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglMinToTray{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglCloseToTray{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_trayModeLogoBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_trayModePercentBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglAlwaysOnTop{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglNotifications{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglApplyOnStart{nullptr};
    winrt::Windows::UI::Xaml::Controls::ToggleSwitch m_tglHighResTimer{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_settingsElevateRow{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_settingsElevateBtn{nullptr};
    winrt::Windows::UI::Xaml::Controls::Button m_supportDevBtn{nullptr};
};

}
