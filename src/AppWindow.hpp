#pragma once

#include "ConfigManager.hpp"
#include "CpuIntelEngine.hpp"
#include "TrayIconRenderer.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <cstdint>
#include <deque>
#include <string>
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

    enum class ControlId {
        None = 0,
        ModalBackdrop,
        WinMinimize,
        WinClose,
        PowerPlanBtn,
        PowerPlanItem,
        LeftTabTuning,
        LeftTabPowerSet,
        LeftTabAppsTweak,
        LeftTabSupport,
        ToggleLiveApply,
        SliderUnpark,
        SliderMinState,
        SliderMaxState,
        SliderBoostPol,
        SliderEpp,
        SliderFreqCap,
        BoostPrev,
        BoostNext,
        SchedPrev,
        SchedNext,
        SliderIncThresh,
        SliderDecThresh,
        SliderHeteroFloor,
        SliderConcur,
        ToggleActiveCooling,
        ToggleOsDirectHwp,
        ToggleIdleDisable,
        ToggleOsThrottle,
        ToggleAdvHighRes,
        BrowseExeBtn,
        RunningAppsBtn,
        RunningProcItem,
        ApplyAppsBtn,
        TargetRuleToggle,
        TargetRuleRemove,
        TargetPrio0,
        TargetPrio1,
        TargetPrio2,
        TargetPrio3,
        TargetCore0,
        TargetCore1,
        TargetCore2,
        ToggleTargetDeepBoost,
        ToggleTargetIfeo,
        ToggleFgBoost,
        CompatScrollThumb,
        PresetEco,
        PresetBalanced,
        PresetPerf,
        PresetMaxOc,
        ApplyPresetsBtn,
        RestoreBtn,
        FilterAll,
        FilterPCores,
        FilterECores,
        CoreScrollThumb,
        SettingsBtn,
        MinimizeTrayBtn,
        SettingsBackBtn,
        ToggleAutoStart,
        ToggleStartMinimized,
        ToggleMinToTray,
        ToggleCloseToTray,
        TrayModeLogo,
        TrayModePercent,
        ToggleAlwaysOnTop,
        ToggleNotifications,
        ToggleApplyOnStart,
        ToggleHighResTimer,
        SettingsElevateBtn,
        SupportDevBtn
    };

    struct HitControl {
        ControlId id = ControlId::None;
        D2D1_RECT_F rect{};
        bool enabled = true;
        bool isSlider = false;
        double sliderMin = 0.0;
        double sliderMax = 100.0;
        double sliderStep = 1.0;
        int argInt = 0;
        std::wstring argStr;
    };

    struct CoreHistory {
        uint32_t coreIndex = 0;
        CoreKind kind = CoreKind::Standard;
        uint32_t typeLocalIndex = 0;
        std::deque<double> history;
        double currentUsage = 0.0;
        uint32_t currentMhz = 0;
    };

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate();
    void OnDestroy();
    void OnSize(UINT width, UINT height);
    void OnDpiChanged(UINT newDpi, const RECT* suggestedRect);
    void OnTimer(UINT_PTR timerId);
    void OnTrayMessage(LPARAM lParam);
    void OnPaint();
    void OnPrintClient(HDC hdc);
    void OnMouseMove(int px, int py);
    void OnMouseLeave();
    void OnLButtonDown(int px, int py);
    void OnLButtonUp(int px, int py);
    void OnMouseWheel(int px, int py, short delta);
    void OnKeyDown(WPARAM vk);

    void ApplyDwmTheme();
    void ApplyAlwaysOnTop();
    void InvalidateView();

    bool EnsureD2DFactoryResources();
    void EnsureDibSurface();
    void DiscardD2DDeviceResources();
    void LoadLogoBitmap();
    void RenderToMemoryDc();

    void BuildCoreHistoryList();
    void PopulatePowerPlansCache();
    void RefreshCpuTelemetry();
    void AddTargetExeRule(const std::wstring& exeName);
    void BrowseAndAddTargetExe();
    void ApplyQuickPreset(PresetMode preset);
    void SaveAndApplySettings();
    void SetActionStatus(const std::wstring& text, bool isError);

    void ExecuteActivateVPowerPlan();
    void ExecuteApplyTuneProfile(bool ensureVPower);
    void ExecuteRestoreStock();
    void ExecuteSwitchSelectedPlan(const std::wstring& guidStr);
    void ExecuteApplyTargetsNow();

    void TogglePowerPlanPopup(bool open);
    void ToggleRunningProcPopup(bool open);
    void SwitchLeftTab(LeftTab tab);
    void SwitchCoreFilter(CoreFilterMode filter);
    void SwitchToSettings(bool showSettings);
    void ShowMainWindow();
    void HideToTray();
    void MinimizeWindow();
    void StartWindowFade(FadeMode mode);
    void ApplyLayeredAlpha(BYTE alpha);
    void ClearLayeredStyle();

    void InitTrayIcon();
    void UpdateTrayIcon(const CpuLiveSnapshot& snap, bool forceIconRecreate);
    void RemoveTrayIcon();
    void ShowTrayBalloon(const wchar_t* title, const wchar_t* body, bool errorIcon);
    void ShowTrayContextMenu();

    D2D1_POINT_2F PtToDip(int px, int py) const noexcept;
    const HitControl* HitTestDip(D2D1_POINT_2F pt) const;
    bool IsControlHovered(ControlId id, int argInt = 0, const std::wstring& argStr = L"") const;
    bool IsControlPressed(ControlId id, int argInt = 0, const std::wstring& argStr = L"") const;
    void ApplySliderFromDipX(const HitControl& ctrl, float dipX);
    void ActivateControl(const HitControl& ctrl);
    void RegisterHit(ControlId id, const D2D1_RECT_F& rc, bool enabled = true, int argInt = 0, std::wstring argStr = L"");

    // Painting helpers
    void DrawTitleBar();
    void DrawMainView();
    void DrawTopControlStrip();
    void DrawKpiStrip();
    void DrawLeftPane();
    void DrawTabTuning(float innerLeft, float innerRight, float topY, float bottomY);
    void DrawTabPowerSet(float innerLeft, float innerRight, float topY, float bottomY);
    void DrawTabAppsTweak(float innerLeft, float innerRight, float topY, float bottomY);
    void DrawTabSupport(float innerLeft, float innerRight, float topY, float bottomY);
    void DrawPresetFooter(float innerLeft, float innerRight, float topY);
    void DrawRightCoresPane();
    void DrawAmdOverlay();
    void DrawBottomBar();
    void DrawPopups();
    void DrawSettingsView();

    void DrawTextStrict(const std::wstring& text,
                        IDWriteTextFormat* fmt,
                        const D2D1_RECT_F& rc,
                        D2D1_COLOR_F color,
                        DWRITE_TEXT_ALIGNMENT hAlign = DWRITE_TEXT_ALIGNMENT_LEADING,
                        DWRITE_PARAGRAPH_ALIGNMENT vAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    float MeasureTextWidth(const std::wstring& text, IDWriteTextFormat* fmt);
    void DrawRoundedBox(const D2D1_RECT_F& rc, float radius, D2D1_COLOR_F fill, D2D1_COLOR_F border, float borderW = 1.0f);
    void DrawButton(ControlId id,
                    const D2D1_RECT_F& rc,
                    const std::wstring& text,
                    bool primaryBlue,
                    bool selectedBlueTint,
                    bool enabled,
                    float fontSize = 11.0f,
                    bool bold = true,
                    float radius = 6.0f,
                    int argInt = 0,
                    std::wstring argStr = L"");
    void DrawToggleSwitch(ControlId id,
                          float rightX,
                          float centerY,
                          bool isOn,
                          bool enabled,
                          int argInt = 0,
                          std::wstring argStr = L"");
    void DrawLabeledSlider(ControlId id,
                           float leftX,
                           float rightX,
                           float topY,
                           const std::wstring& label,
                           const std::wstring& valText,
                           double val,
                           double minVal,
                           double maxVal,
                           double stepVal,
                           bool enabled);
    void DrawScrollbar(ControlId thumbId,
                       const D2D1_RECT_F& trackRc,
                       float contentH,
                       float viewH,
                       float scrollOffset);

    HINSTANCE m_hInst = nullptr;
    HWND m_hwnd = nullptr;
    bool m_startMinimized = false;
    bool m_isElevated = false;
    bool m_trayAdded = false;
    bool m_closingForReal = false;
    bool m_showingSettings = false;
    bool m_amdPopupShown = false;
    bool m_powerPlanPopupOpen = false;
    bool m_runningProcPopupOpen = false;
    bool m_boostPausedByRestore = false;
    bool m_mouseTracking = false;

    FadeMode m_fadeMode = FadeMode::None;
    double m_fadeProgress = 0.0;
    float m_gearAngle = 0.0f;
    float m_chevronAngle = 0.0f;

    LeftTab m_leftTab = LeftTab::Sliders;
    CoreFilterMode m_coreFilter = CoreFilterMode::All;

    UINT m_dpi = 96;
    uint32_t m_sessionPeakMhz = 0;
    uint32_t m_lastTrayPercent = 999;
    std::wstring m_lastTrayStyle;
    HICON m_currentTrayIcon = nullptr;
    HICON m_appIconLarge = nullptr;
    HICON m_appIconSmall = nullptr;

    AppConfig m_config{};
    CpuHardwareInfo m_cpuInfo{};
    CpuLiveSnapshot m_lastSnap{};
    CpuIntelEngine m_engine;
    TrayIconRenderer m_trayRenderer;
    std::vector<PowerPlanEntry> m_cachedPlans;
    std::vector<RunningProcessEntry> m_cachedProcs;
    std::vector<CoreHistory> m_coreHistories;

    std::wstring m_actionStatusText;
    bool m_actionStatusError = false;

    // Scroll offsets & geometry metrics
    float m_coreScroll = 0.0f;
    float m_coreMaxScroll = 0.0f;
    float m_compatScroll = 0.0f;
    float m_compatMaxScroll = 0.0f;
    float m_targetScroll = 0.0f;
    float m_targetMaxScroll = 0.0f;
    float m_planPopupScroll = 0.0f;
    float m_planPopupMaxScroll = 0.0f;
    float m_procPopupScroll = 0.0f;
    float m_procPopupMaxScroll = 0.0f;

    D2D1_RECT_F m_coreViewportRc{};
    D2D1_RECT_F m_coreTrackRc{};
    D2D1_RECT_F m_compatViewportRc{};
    D2D1_RECT_F m_compatTrackRc{};
    D2D1_RECT_F m_targetViewportRc{};
    D2D1_RECT_F m_planPopupRc{};
    D2D1_RECT_F m_procPopupRc{};

    // Interactive state
    std::vector<HitControl> m_hitControls;
    ControlId m_hoverId = ControlId::None;
    int m_hoverArgInt = 0;
    std::wstring m_hoverArgStr;
    ControlId m_pressId = ControlId::None;
    int m_pressArgInt = 0;
    std::wstring m_pressArgStr;
    ControlId m_draggingSliderId = ControlId::None;
    ControlId m_draggingScrollId = ControlId::None;
    float m_scrollDragStartY = 0.0f;
    float m_scrollDragStartOffset = 0.0f;

    // Decoded logo PBGRA buffer (device-independent)
    uint32_t m_logoW = 0;
    uint32_t m_logoH = 0;
    std::vector<uint8_t> m_logoPbgra;

    // Offscreen 32-bit DIB surface + Direct2D / DirectWrite resources
    HDC m_memDc = nullptr;
    HBITMAP m_dibBmp = nullptr;
    HBITMAP m_oldBmp = nullptr;
    int m_dibW = 0;
    int m_dibH = 0;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_rt;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brush;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> m_roundStroke;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_logoBitmap;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtReg9;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtReg10;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtReg11;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtReg125;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtSemi10;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtSemi11;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtSemi12;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBold9;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBold105;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBold115;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBold135;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBold145;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fmtBold18;
};

} // namespace vint
