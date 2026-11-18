#include "AppWindow.hpp"
#include "../resources/resource.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>

#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <map>
#include <string>

namespace winrt_wux  = winrt::Windows::UI::Xaml;
namespace winrt_ctrl = winrt::Windows::UI::Xaml::Controls;
namespace winrt_med  = winrt::Windows::UI::Xaml::Media;
namespace winrt_anim = winrt::Windows::UI::Xaml::Media::Animation;
namespace winrt_img  = winrt::Windows::UI::Xaml::Media::Imaging;
namespace winrt_shp  = winrt::Windows::UI::Xaml::Shapes;
namespace winrt_txt  = winrt::Windows::UI::Text;

namespace vint {

namespace {

constexpr wchar_t kWindowClassName[] = L"VOverclockIntel_V1_HostWindow";
constexpr int kClientDipWidth  = 1120;
constexpr int kClientDipHeight = 630;
constexpr size_t kSparkHistoryLen = 30;
constexpr float kSparkW = 240.0f;
constexpr float kSparkH = 34.0f;

constexpr UINT_PTR IDT_POLL_CPU     = 1001;
constexpr UINT_PTR IDT_WINDOW_FADE  = 1002;
constexpr UINT_PTR IDT_STATUS_RESET = 1003;
constexpr UINT_PTR IDT_LIVE_APPLY   = 1004;
constexpr UINT     kPollIntervalMs  = 500;
constexpr UINT     kFadeTimerMs     = 12;
constexpr UINT     kStatusResetMs   = 4500;
constexpr UINT     kLiveApplyMs     = 180;

constexpr UINT kTrayIconId       = 1;
constexpr UINT IDM_TRAY_SHOW     = 40001;
constexpr UINT IDM_TRAY_VPOWER   = 40002;
constexpr UINT IDM_TRAY_RESTORE  = 40003;
constexpr UINT IDM_TRAY_SETTINGS = 40004;
constexpr UINT IDM_TRAY_EXIT     = 40005;

constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmRoundCorner            = 2;
constexpr DWORD kDwmBorderColor            = 34;

constexpr std::array<int, 4> kBoostModes    = {0, 1, 2, 4};
constexpr std::array<int, 4> kSchedPolicies = {2, 1, 0, 5};

winrt::Windows::UI::Color ArgbColor(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return winrt::Windows::UI::ColorHelper::FromArgb(a, r, g, b);
}

winrt::Windows::UI::Color RgbColor(uint8_t r, uint8_t g, uint8_t b) {
    return ArgbColor(255, r, g, b);
}

std::wstring LoadUtf8ResourceAsWide(HINSTANCE hInst, UINT resId) {
    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return {};
    HGLOBAL hMem = LoadResource(hInst, hRes);
    if (!hMem) return {};
    const DWORD size = SizeofResource(hInst, hRes);
    const char* data = static_cast<const char*>(LockResource(hMem));
    if (!data || size == 0) return {};

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(size), nullptr, 0);
    if (wideLen <= 0) return {};
    std::wstring out(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(size), out.data(), wideLen);
    return out;
}

template <size_t N>
int StepOption(const std::array<int, N>& values, int current, int delta) {
    int idx = 0;
    for (size_t i = 0; i < N; ++i) {
        if (values[i] == current) {
            idx = static_cast<int>(i);
            break;
        }
    }
    const int next = (idx + delta + static_cast<int>(N)) % static_cast<int>(N);
    return values[next];
}

std::wstring FormatClockGhz(uint32_t mhz) {
    wchar_t buf[32]{};
    if (mhz >= 1000) {
        std::swprintf(buf, 32, L"%.2f GHz", static_cast<double>(mhz) / 1000.0);
    } else {
        std::swprintf(buf, 32, L"%u MHz", mhz);
    }
    return buf;
}

void SafeBeginSb(const winrt_anim::Storyboard& sb) noexcept {
    if (!sb) return;
    try {
        sb.Begin();
    } catch (...) {
    }
}

void SafeStopSb(const winrt_anim::Storyboard& sb) noexcept {
    if (!sb) return;
    try {
        sb.Stop();
    } catch (...) {
    }
}

}

AppWindow::AppWindow(HINSTANCE hInst, bool startMinimized)
    : m_hInst(hInst)
    , m_startMinimized(startMinimized)
    , m_isElevated(CpuIntelEngine::IsElevated()) {
    m_config = ConfigManager::Load();
    m_engine.Initialize();
    m_cpuInfo = m_engine.GetHardwareInfo();
}

AppWindow::~AppWindow() {
    if (m_appIconLarge) DestroyIcon(m_appIconLarge);
    if (m_appIconSmall) DestroyIcon(m_appIconSmall);
    if (m_currentTrayIcon) DestroyIcon(m_currentTrayIcon);
}

bool AppWindow::Create() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = m_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    wc.lpszClassName = kWindowClassName;

    m_dpi = GetDpiForSystem();
    m_appIconLarge = static_cast<HICON>(LoadImageW(
        m_hInst, MAKEINTRESOURCEW(IDI_VO_APP), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXICON, m_dpi),
        GetSystemMetricsForDpi(SM_CYICON, m_dpi),
        LR_DEFAULTCOLOR
    ));
    m_appIconSmall = static_cast<HICON>(LoadImageW(
        m_hInst, MAKEINTRESOURCEW(IDI_VO_APP), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXSMICON, m_dpi),
        GetSystemMetricsForDpi(SM_CYSMICON, m_dpi),
        LR_DEFAULTCOLOR
    ));
    wc.hIcon = m_appIconLarge;
    wc.hIconSm = m_appIconSmall;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD exStyle = WS_EX_APPWINDOW;

    const int winW = MulDiv(kClientDipWidth, m_dpi, 96);
    const int winH = MulDiv(kClientDipHeight, m_dpi, 96);

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int posX = workArea.left + ((workArea.right - workArea.left) - winW) / 2;
    const int posY = workArea.top + ((workArea.bottom - workArea.top) - winH) / 2;

    m_hwnd = CreateWindowExW(
        exStyle,
        kWindowClassName,
        L"VOverclock Intel Beta",
        style,
        posX, posY, winW, winH,
        nullptr, nullptr, m_hInst, this
    );
    if (!m_hwnd) {
        return false;
    }

    ApplyDwmTheme();
    ApplyAlwaysOnTop();

    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    const bool shouldHideOnLaunch = (m_startMinimized || m_config.startMinimized) && m_cpuInfo.isIntel;
    if (shouldHideOnLaunch) {
        ShowWindow(m_hwnd, SW_HIDE);
    } else {
        StartWindowFade(FadeMode::In);
    }

    if (!m_cpuInfo.isIntel) {
        ShowTrayBalloon(
            L"VOverclock Intel Beta - Blocked",
            L"AMD / Non-Intel CPU detected. All overclocking and VPower features are disabled.",
            true
        );
        PostMessageW(m_hwnd, WM_APP_AMD_NOTIFY, 0, 0);
    }

    return true;
}

bool AppWindow::PreTranslateMessage(MSG* msg) {
    if (m_xamlSource) {
        if (auto native2 = m_xamlSource.try_as<IDesktopWindowXamlSourceNative2>()) {
            BOOL processed = FALSE;
            if (SUCCEEDED(native2->PreTranslateMessage(msg, &processed)) && processed) {
                return true;
            }
        }
    }
    return false;
}

LRESULT CALLBACK AppWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<AppWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->WndProc(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AppWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        OnCreate();
        return 0;

    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            return 0;
        }
        break;

    case WM_NCACTIVATE:
        return TRUE;

    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(m_hwnd, &pt);
        if (pt.y >= 0 && pt.y < MulDiv(42, m_dpi, 96)) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_DPICHANGED:
        OnDpiChanged(HIWORD(wParam), reinterpret_cast<const RECT*>(lParam));
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            if (m_config.minimizeToTray) HideToTray();
            else MinimizeWindow();
            return 0;
        }
        if ((wParam & 0xFFF0) == SC_RESTORE && IsIconic(m_hwnd)) {
            DefWindowProcW(m_hwnd, msg, wParam, lParam);
            StartWindowFade(FadeMode::In);
            return 0;
        }
        break;

    case WM_CLOSE:
        if (!m_closingForReal && m_config.closeToTray) {
            HideToTray();
            return 0;
        }
        if (!m_closingForReal && IsWindowVisible(m_hwnd)) {
            StartWindowFade(FadeMode::OutClose);
            return 0;
        }
        DestroyWindow(m_hwnd);
        return 0;

    case WM_DESTROY:
        OnDestroy();
        PostQuitMessage(0);
        return 0;

    case WM_TIMER:
        OnTimer(static_cast<UINT_PTR>(wParam));
        return 0;

    case WM_APP_SHOW_WINDOW:
        ShowMainWindow();
        return 0;

    case WM_APP_TRAY_ICON:
        OnTrayMessage(lParam);
        return 0;

    case WM_APP_AMD_NOTIFY:
        if (!m_amdPopupShown && !m_cpuInfo.isIntel) {
            m_amdPopupShown = true;
            std::wstring msgBody = L"AMD / Non-Intel Processor Detected!\n\nDetected CPU: " +
                m_cpuInfo.brandName + L"\nVendor ID: " + m_cpuInfo.vendorId +
                L"\n\nVOverclock Intel Beta requires a GenuineIntel CPU. All tuning sliders, presets, and VPower Intel Beta features are blocked.";
            MessageBoxW(m_hwnd, msgBody.c_str(), L"VOverclock Intel Beta - Access Blocked", MB_OK | MB_ICONERROR);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TRAY_SHOW:
            if (IsWindowVisible(m_hwnd)) HideToTray();
            else ShowMainWindow();
            return 0;
        case IDM_TRAY_VPOWER:
            ExecuteActivateVPowerPlan();
            return 0;
        case IDM_TRAY_RESTORE:
            ExecuteRestoreStock();
            return 0;
        case IDM_TRAY_SETTINGS:
            ShowMainWindow();
            SwitchToSettings(true);
            return 0;
        case IDM_TRAY_EXIT:
            m_closingForReal = true;
            DestroyWindow(m_hwnd);
            return 0;
        }
        break;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void AppWindow::OnCreate() {
    m_dpi = GetDpiForWindow(m_hwnd);
    if (m_dpi == 0) m_dpi = 96;

    try {
        m_xamlManager = winrt_wux::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        m_xamlSource = winrt_wux::Hosting::DesktopWindowXamlSource();

        auto native = m_xamlSource.as<IDesktopWindowXamlSourceNative>();
        winrt::check_hresult(native->AttachToWindow(m_hwnd));
        winrt::check_hresult(native->get_WindowHandle(&m_hwndXamlIsland));

        const std::wstring xamlXml = LoadUtf8ResourceAsWide(m_hInst, IDR_XAML_LAYOUT);
        if (xamlXml.empty()) {
            throw winrt::hresult_error(E_FAIL, L"UI XAML resource missing.");
        }
        auto parsed = winrt_wux::Markup::XamlReader::Load(winrt::hstring(xamlXml));
        m_root = parsed.try_as<winrt_wux::FrameworkElement>();
        if (!m_root) {
            throw winrt::hresult_error(E_FAIL, L"Failed to load root XAML view.");
        }
        m_xamlSource.Content(m_root);
    } catch (winrt::hresult_error const& ex) {
        MessageBoxW(m_hwnd, ex.message().c_str(), L"VOverclock Intel Beta - UI Init Failed", MB_OK | MB_ICONERROR);
        m_closingForReal = true;
        PostQuitMessage(1);
        return;
    } catch (...) {
        MessageBoxW(m_hwnd, L"Unexpected error initializing UI runtime.", L"VOverclock Intel Beta", MB_OK | MB_ICONERROR);
        m_closingForReal = true;
        PostQuitMessage(1);
        return;
    }

    ResizeXamlIsland();
    BindXamlControls();
    LoadLogoIntoUi();
    PopulateCpuStaticInfo();
    PopulateFeatureCompatList();
    BuildCoreCardVisuals();
    RebuildCoreCardsGrid();

    if (m_cpuInfo.isIntel) {
        if (m_config.highResTimer) {
            m_engine.SetHighResTimer(true);
        }
        if (m_config.applyOnStartup) {
            m_engine.CreateOrActivateVPowerPlan(m_config, true);
        } else {
            m_engine.SyncConfigFromActivePlan(m_config);
        }
    }

    HydrateSettingsAndTuningUi();
    PopulateCustomPowerPlanMenu();
    ApplyAmdBlockGuardIfNeeded();

    SafeBeginSb(m_liveDotPulseSb);
    if (!m_startMinimized) SafeBeginSb(m_dashEntranceSb);

    InitTrayIcon();
    RefreshCpuTelemetry();
    SetTimer(m_hwnd, IDT_POLL_CPU, kPollIntervalMs, nullptr);
}

void AppWindow::OnDestroy() {
    KillTimer(m_hwnd, IDT_POLL_CPU);
    KillTimer(m_hwnd, IDT_WINDOW_FADE);
    KillTimer(m_hwnd, IDT_STATUS_RESET);
    KillTimer(m_hwnd, IDT_LIVE_APPLY);
    m_engine.SetHighResTimer(false);
    RemoveTrayIcon();

    if (m_xamlSource) {
        m_xamlSource.Close();
        m_xamlSource = nullptr;
    }
    if (m_xamlManager) {
        m_xamlManager.Close();
        m_xamlManager = nullptr;
    }
}

void AppWindow::OnSize(UINT, UINT) {
    ResizeXamlIsland();
}

void AppWindow::OnDpiChanged(UINT newDpi, const RECT* suggestedRect) {
    m_dpi = newDpi;
    if (suggestedRect) {
        SetWindowPos(m_hwnd, nullptr,
                     suggestedRect->left, suggestedRect->top,
                     suggestedRect->right - suggestedRect->left,
                     suggestedRect->bottom - suggestedRect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ResizeXamlIsland();
    UpdateTrayIcon(m_lastSnap, true);
}

void AppWindow::OnTimer(UINT_PTR timerId) {
    if (timerId == IDT_POLL_CPU) {
        RefreshCpuTelemetry();
    } else if (timerId == IDT_LIVE_APPLY) {
        KillTimer(m_hwnd, IDT_LIVE_APPLY);
        if (m_cpuInfo.isIntel && m_config.liveApply) {
            ExecuteApplyTuneProfile(true);
        }
    } else if (timerId == IDT_WINDOW_FADE) {
        if (m_fadeMode == FadeMode::In) {
            m_fadeProgress = std::min(1.0, m_fadeProgress + 0.085);
            const double eased = 1.0 - std::pow(1.0 - m_fadeProgress, 3.0);
            ApplyLayeredAlpha(static_cast<BYTE>(std::lround(eased * 255.0)));
            if (m_fadeProgress >= 1.0) {
                KillTimer(m_hwnd, IDT_WINDOW_FADE);
                m_fadeMode = FadeMode::None;
                ClearLayeredStyle();
            }
        } else if (m_fadeMode == FadeMode::OutHide ||
                   m_fadeMode == FadeMode::OutMinimize ||
                   m_fadeMode == FadeMode::OutClose) {
            m_fadeProgress = std::max(0.0, m_fadeProgress - 0.10);
            const double eased = m_fadeProgress * m_fadeProgress;
            ApplyLayeredAlpha(static_cast<BYTE>(std::lround(eased * 255.0)));
            if (m_fadeProgress <= 0.0) {
                KillTimer(m_hwnd, IDT_WINDOW_FADE);
                const FadeMode doneMode = m_fadeMode;
                m_fadeMode = FadeMode::None;
                if (doneMode == FadeMode::OutHide) {
                    ShowWindow(m_hwnd, SW_HIDE);
                    ClearLayeredStyle();
                } else if (doneMode == FadeMode::OutMinimize) {
                    ShowWindow(m_hwnd, SW_MINIMIZE);
                    ClearLayeredStyle();
                } else if (doneMode == FadeMode::OutClose) {
                    m_closingForReal = true;
                    DestroyWindow(m_hwnd);
                }
            }
        } else {
            KillTimer(m_hwnd, IDT_WINDOW_FADE);
            ClearLayeredStyle();
        }
    } else if (timerId == IDT_STATUS_RESET) {
        KillTimer(m_hwnd, IDT_STATUS_RESET);
        if (m_actionStatusText) {
            m_actionStatusText.Text(L"");
        }
    }
}

void AppWindow::OnTrayMessage(LPARAM lParam) {
    const UINT mouseMsg = LOWORD(lParam);
    if (mouseMsg == WM_LBUTTONUP || mouseMsg == WM_LBUTTONDBLCLK) {
        if (IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd)) HideToTray();
        else ShowMainWindow();
    } else if (mouseMsg == WM_RBUTTONUP || mouseMsg == WM_CONTEXTMENU) {
        ShowTrayContextMenu();
    }
}

void AppWindow::ApplyDwmTheme() {
    DwmSetWindowAttribute(m_hwnd, kDwmWindowCornerPreference, &kDwmRoundCorner, sizeof(kDwmRoundCorner));
    COLORREF border = RGB(203, 213, 225);
    DwmSetWindowAttribute(m_hwnd, kDwmBorderColor, &border, sizeof(border));
    MARGINS shadowMargins{0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(m_hwnd, &shadowMargins);
}

void AppWindow::ApplyAlwaysOnTop() {
    SetWindowPos(m_hwnd,
                 m_config.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void AppWindow::ResizeXamlIsland() {
    if (!m_hwndXamlIsland) return;
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    SetWindowPos(m_hwndXamlIsland, nullptr,
                 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_SHOWWINDOW);
}

void AppWindow::BindXamlControls() {
    auto find = [&](const wchar_t* name) {
        return m_root.FindName(winrt::hstring(name));
    };

    auto resources = m_root.Resources();
    auto findSb = [&](const wchar_t* name) -> winrt_anim::Storyboard {
        if (auto obj = find(name)) {
            return obj.try_as<winrt_anim::Storyboard>();
        }
        if (resources && resources.HasKey(winrt::box_value(name))) {
            return resources.Lookup(winrt::box_value(name)).try_as<winrt_anim::Storyboard>();
        }
        return nullptr;
    };

    m_gearSpinInSb      = findSb(L"GearSpinInStoryboard");
    m_gearSpinOutSb     = findSb(L"GearSpinOutStoryboard");
    m_openSettingsSb    = findSb(L"OpenSettingsStoryboard");
    m_openMainSb        = findSb(L"OpenMainStoryboard");
    m_dashEntranceSb    = findSb(L"DashboardEntranceStoryboard");
    m_liveDotPulseSb    = findSb(L"LiveDotPulseStoryboard");
    m_leftTabSwitchSb   = findSb(L"LeftTabSwitchStoryboard");
    m_coreFilterSlideSb = findSb(L"CoreFilterSlideStoryboard");
    m_planOpenSb        = findSb(L"PowerPlanOpenStoryboard");
    m_planCloseSb       = findSb(L"PowerPlanCloseStoryboard");
    m_runningOpenSb     = findSb(L"RunningProcOpenStoryboard");
    m_statusPopSb       = findSb(L"StatusPopStoryboard");

    m_titleBarDragArea       = find(L"TitleBarDragArea").try_as<winrt_ctrl::Grid>();
    m_settingsHeaderDragArea = find(L"SettingsHeaderDragArea").try_as<winrt_ctrl::Grid>();
    m_logoImage              = find(L"LogoImage").try_as<winrt_ctrl::Image>();
    m_cpuBrandText           = find(L"CpuBrandText").try_as<winrt_ctrl::TextBlock>();
    m_cpuSpecShortText       = find(L"CpuSpecShortText").try_as<winrt_ctrl::TextBlock>();
    m_customPlanSelectBtn    = find(L"CustomPlanSelectBtn").try_as<winrt_ctrl::Button>();
    m_selectedPlanLabelText  = find(L"SelectedPlanLabelText").try_as<winrt_ctrl::TextBlock>();
    m_powerPlanPopupHost     = find(L"PowerPlanPopupHost").try_as<winrt_ctrl::Grid>();
    m_powerPlanBackdrop      = find(L"PowerPlanBackdrop").try_as<winrt_ctrl::Grid>();
    m_powerPlanPopupBorder   = find(L"PowerPlanPopupBorder").try_as<winrt_ctrl::Border>();
    m_powerPlanPopupList     = find(L"PowerPlanPopupList").try_as<winrt_ctrl::StackPanel>();
    m_runningProcPopupHost   = find(L"RunningProcPopupHost").try_as<winrt_ctrl::Grid>();
    m_runningProcBackdrop    = find(L"RunningProcBackdrop").try_as<winrt_ctrl::Grid>();
    m_runningProcPopupList   = find(L"RunningProcPopupList").try_as<winrt_ctrl::StackPanel>();
    m_winMinimizeBtn         = find(L"WinMinimizeButton").try_as<winrt_ctrl::Button>();
    m_winCloseBtn            = find(L"WinCloseButton").try_as<winrt_ctrl::Button>();

    m_mainView     = find(L"MainView").try_as<winrt_ctrl::Grid>();
    m_settingsView = find(L"SettingsView").try_as<winrt_ctrl::Grid>();

    m_kpiClockText    = find(L"KpiClockText").try_as<winrt_ctrl::TextBlock>();
    m_kpiClockSubText = find(L"KpiClockSubText").try_as<winrt_ctrl::TextBlock>();
    m_kpiPeakText     = find(L"KpiPeakText").try_as<winrt_ctrl::TextBlock>();
    m_kpiPeakSubText  = find(L"KpiPeakSubText").try_as<winrt_ctrl::TextBlock>();
    m_kpiLoadText     = find(L"KpiLoadText").try_as<winrt_ctrl::TextBlock>();
    m_kpiLoadSubText  = find(L"KpiLoadSubText").try_as<winrt_ctrl::TextBlock>();
    m_kpiTempText     = find(L"KpiTempText").try_as<winrt_ctrl::TextBlock>();
    m_kpiBoostSubText = find(L"KpiBoostSubText").try_as<winrt_ctrl::TextBlock>();

    m_leftTabSlidersBtn = find(L"LeftTabSlidersBtn").try_as<winrt_ctrl::Button>();
    m_leftTabAdvBtn     = find(L"LeftTabAdvBtn").try_as<winrt_ctrl::Button>();
    m_leftTabTargetsBtn = find(L"LeftTabTargetsBtn").try_as<winrt_ctrl::Button>();
    m_leftTabCheckBtn   = find(L"LeftTabCheckBtn").try_as<winrt_ctrl::Button>();
    m_tglLiveApply      = find(L"ToggleLiveApply").try_as<winrt_ctrl::ToggleSwitch>();

    m_leftPageSliders  = find(L"LeftPageSliders").try_as<winrt_ctrl::Grid>();
    m_leftPageAdvanced = find(L"LeftPageAdvanced").try_as<winrt_ctrl::Grid>();
    m_leftPageTargets  = find(L"LeftPageTargets").try_as<winrt_ctrl::Grid>();
    m_leftPageCompat   = find(L"LeftPageCompat").try_as<winrt_ctrl::Grid>();

    m_sliderUnpark   = find(L"SliderUnpark").try_as<winrt_ctrl::Slider>();
    m_sldUnparkVal   = find(L"SldUnparkVal").try_as<winrt_ctrl::TextBlock>();
    m_sliderMinState = find(L"SliderMinState").try_as<winrt_ctrl::Slider>();
    m_sldMinStateVal = find(L"SldMinStateVal").try_as<winrt_ctrl::TextBlock>();
    m_sliderMaxState = find(L"SliderMaxState").try_as<winrt_ctrl::Slider>();
    m_sldMaxStateVal = find(L"SldMaxStateVal").try_as<winrt_ctrl::TextBlock>();
    m_sliderBoostPol = find(L"SliderBoostPol").try_as<winrt_ctrl::Slider>();
    m_sldBoostPolVal = find(L"SldBoostPolVal").try_as<winrt_ctrl::TextBlock>();
    m_sliderEpp      = find(L"SliderEpp").try_as<winrt_ctrl::Slider>();
    m_sldEppVal      = find(L"SldEppVal").try_as<winrt_ctrl::TextBlock>();
    m_sliderFreqCap  = find(L"SliderFreqCap").try_as<winrt_ctrl::Slider>();
    m_sldFreqCapVal  = find(L"SldFreqCapVal").try_as<winrt_ctrl::TextBlock>();

    m_boostPrevBtn   = find(L"BoostPrevBtn").try_as<winrt_ctrl::Button>();
    m_boostNextBtn   = find(L"BoostNextBtn").try_as<winrt_ctrl::Button>();
    m_boostValueText = find(L"BoostValueText").try_as<winrt_ctrl::TextBlock>();
    m_schedPrevBtn   = find(L"SchedPrevBtn").try_as<winrt_ctrl::Button>();
    m_schedNextBtn   = find(L"SchedNextBtn").try_as<winrt_ctrl::Button>();
    m_schedValueText = find(L"SchedValueText").try_as<winrt_ctrl::TextBlock>();

    m_sliderIncThresh    = find(L"SliderIncThresh").try_as<winrt_ctrl::Slider>();
    m_sldIncThreshVal    = find(L"SldIncThreshVal").try_as<winrt_ctrl::TextBlock>();
    m_sliderDecThresh    = find(L"SliderDecThresh").try_as<winrt_ctrl::Slider>();
    m_sldDecThreshVal    = find(L"SldDecThreshVal").try_as<winrt_ctrl::TextBlock>();
    m_sliderHeteroFloor  = find(L"SliderHeteroFloor").try_as<winrt_ctrl::Slider>();
    m_sldHeteroFloorVal  = find(L"SldHeteroFloorVal").try_as<winrt_ctrl::TextBlock>();
    m_sliderConcur       = find(L"SliderConcur").try_as<winrt_ctrl::Slider>();
    m_sldConcurVal       = find(L"SldConcurVal").try_as<winrt_ctrl::TextBlock>();
    m_tglActiveCooling   = find(L"ToggleActiveCooling").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglOsDirectHwp     = find(L"ToggleOsDirectHwp").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglIdleDisable     = find(L"ToggleIdleDisable").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglOsThrottle      = find(L"ToggleOsThrottle").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglAdvHighRes      = find(L"ToggleAdvHighRes").try_as<winrt_ctrl::ToggleSwitch>();

    m_browseExeBtn       = find(L"BrowseExeBtn").try_as<winrt_ctrl::Button>();
    m_pickRunningExeBtn  = find(L"PickRunningExeBtn").try_as<winrt_ctrl::Button>();
    m_applyTargetsBtn    = find(L"ApplyTargetsBtn").try_as<winrt_ctrl::Button>();
    m_targetExeListPanel = find(L"TargetExeListPanel").try_as<winrt_ctrl::StackPanel>();
    m_targetExeEmptyHint = find(L"TargetExeEmptyHint").try_as<winrt_ctrl::TextBlock>();
    m_targetPrio0Btn     = find(L"TargetPrio0Btn").try_as<winrt_ctrl::Button>();
    m_targetPrio1Btn     = find(L"TargetPrio1Btn").try_as<winrt_ctrl::Button>();
    m_targetPrio2Btn     = find(L"TargetPrio2Btn").try_as<winrt_ctrl::Button>();
    m_targetPrio3Btn     = find(L"TargetPrio3Btn").try_as<winrt_ctrl::Button>();
    m_targetCore0Btn     = find(L"TargetCore0Btn").try_as<winrt_ctrl::Button>();
    m_targetCore1Btn     = find(L"TargetCore1Btn").try_as<winrt_ctrl::Button>();
    m_targetCore2Btn     = find(L"TargetCore2Btn").try_as<winrt_ctrl::Button>();
    m_tglTargetDeepBoost = find(L"ToggleTargetDeepBoost").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglTargetIfeo      = find(L"ToggleTargetIfeo").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglFgBoost         = find(L"ToggleFgBoost").try_as<winrt_ctrl::ToggleSwitch>();

    m_compatScrollViewer       = find(L"CompatScrollViewer").try_as<winrt_ctrl::ScrollViewer>();
    m_compatScrollTrack        = find(L"CompatScrollTrack").try_as<winrt_ctrl::Grid>();
    m_compatScrollThumb        = find(L"CompatScrollThumb").try_as<winrt_ctrl::Border>();
    m_compatScrollThumbTransform = find(L"CompatScrollThumbTransform").try_as<winrt_med::TranslateTransform>();
    m_compatSummaryText        = find(L"CompatSummaryText").try_as<winrt_ctrl::TextBlock>();
    m_compatListPanel          = find(L"CompatListPanel").try_as<winrt_ctrl::StackPanel>();

    m_presetEcoBtn       = find(L"PresetEcoBtn").try_as<winrt_ctrl::Button>();
    m_presetBalancedBtn  = find(L"PresetBalancedBtn").try_as<winrt_ctrl::Button>();
    m_presetGamingBtn    = find(L"PresetGamingBtn").try_as<winrt_ctrl::Button>();
    m_presetMaxOcBtn     = find(L"PresetMaxOcBtn").try_as<winrt_ctrl::Button>();
    m_applyPresetsBtn    = find(L"ApplyPresetsBtn").try_as<winrt_ctrl::Button>();
    m_restoreBtn         = find(L"RestoreBtn").try_as<winrt_ctrl::Button>();

    m_amdBlockOverlay     = find(L"AmdBlockOverlay").try_as<winrt_ctrl::Border>();
    m_amdBlockVendorText  = find(L"AmdBlockVendorText").try_as<winrt_ctrl::TextBlock>();
    m_amdBlockCpuNameText = find(L"AmdBlockCpuNameText").try_as<winrt_ctrl::TextBlock>();

    m_coreLegendHybrid         = find(L"CoreLegendHybrid").try_as<winrt_ctrl::StackPanel>();
    m_filterAllBtn             = find(L"FilterAllBtn").try_as<winrt_ctrl::Button>();
    m_filterPCoreBtn           = find(L"FilterPCoreBtn").try_as<winrt_ctrl::Button>();
    m_filterECoreBtn           = find(L"FilterECoreBtn").try_as<winrt_ctrl::Button>();
    m_coreGridScrollViewer     = find(L"CoreGridScrollViewer").try_as<winrt_ctrl::ScrollViewer>();
    m_coreGraphsContainer      = find(L"CoreGraphsContainer").try_as<winrt_ctrl::Grid>();
    m_coreScrollTrack          = find(L"CoreScrollTrack").try_as<winrt_ctrl::Grid>();
    m_coreScrollThumb          = find(L"CoreScrollThumb").try_as<winrt_ctrl::Border>();
    m_coreScrollThumbTransform = find(L"CoreScrollThumbTransform").try_as<winrt_med::TranslateTransform>();

    m_settingsBtn      = find(L"SettingsButton").try_as<winrt_ctrl::Button>();
    m_actionStatusText = find(L"ActionStatusText").try_as<winrt_ctrl::TextBlock>();
    m_minTrayBtn       = find(L"MinimizeTrayButton").try_as<winrt_ctrl::Button>();

    m_backBtn              = find(L"BackButton").try_as<winrt_ctrl::Button>();
    m_tglAutoStart         = find(L"ToggleAutoStart").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglStartMinimized    = find(L"ToggleStartMinimized").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglMinToTray         = find(L"ToggleMinimizeToTray").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglCloseToTray       = find(L"ToggleCloseToTray").try_as<winrt_ctrl::ToggleSwitch>();
    m_trayModeLogoBtn      = find(L"TrayModeLogoBtn").try_as<winrt_ctrl::Button>();
    m_trayModePercentBtn   = find(L"TrayModePercentBtn").try_as<winrt_ctrl::Button>();
    m_tglAlwaysOnTop       = find(L"ToggleAlwaysOnTop").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglNotifications     = find(L"ToggleNotifications").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglApplyOnStart      = find(L"ToggleApplyOnStart").try_as<winrt_ctrl::ToggleSwitch>();
    m_tglHighResTimer      = find(L"ToggleHighResTimer").try_as<winrt_ctrl::ToggleSwitch>();
    m_settingsElevateRow   = find(L"SettingsElevateRow").try_as<winrt_ctrl::Grid>();
    m_settingsElevateBtn   = find(L"SettingsElevateButton").try_as<winrt_ctrl::Button>();
    m_supportDevBtn        = find(L"SupportDevButton").try_as<winrt_ctrl::Button>();

    AttachDragHandlers(m_titleBarDragArea);
    AttachDragHandlers(m_settingsHeaderDragArea);

    if (m_winMinimizeBtn) {
        m_winMinimizeBtn.Click([this](auto&&, auto&&) {
            if (m_config.minimizeToTray) HideToTray();
            else MinimizeWindow();
        });
    }
    if (m_winCloseBtn) {
        m_winCloseBtn.Click([this](auto&&, auto&&) {
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        });
    }

    if (m_settingsElevateRow) {
        m_settingsElevateRow.Visibility(m_isElevated ? winrt_wux::Visibility::Collapsed : winrt_wux::Visibility::Visible);
    }
    if (m_settingsElevateBtn) {
        m_settingsElevateBtn.Click([this](auto&&, auto&&) {
            if (m_isElevated) return;
            if (CpuIntelEngine::RestartElevated(false)) {
                m_closingForReal = true;
                DestroyWindow(m_hwnd);
            }
        });
    }

    if (m_supportDevBtn) {
        m_supportDevBtn.Click([](auto&&, auto&&) {
            ShellExecuteW(nullptr, L"open", L"https://sociabuzz.com/idnvinnmalay", nullptr, nullptr, SW_SHOWNORMAL);
        });
    }

    if (m_coreGridScrollViewer) {
        m_coreGridScrollViewer.ViewChanged([this](auto&&, auto&&) {
            SyncScrollThumb(m_coreGridScrollViewer, m_coreScrollTrack, m_coreScrollThumb, m_coreScrollThumbTransform);
        });
        m_coreGridScrollViewer.SizeChanged([this](auto&&, auto&&) {
            SyncScrollThumb(m_coreGridScrollViewer, m_coreScrollTrack, m_coreScrollThumb, m_coreScrollThumbTransform);
        });
    }
    if (m_coreScrollTrack) {
        m_coreScrollTrack.SizeChanged([this](auto&&, auto&&) {
            SyncScrollThumb(m_coreGridScrollViewer, m_coreScrollTrack, m_coreScrollThumb, m_coreScrollThumbTransform);
        });
    }

    if (m_compatScrollViewer) {
        m_compatScrollViewer.ViewChanged([this](auto&&, auto&&) {
            SyncScrollThumb(m_compatScrollViewer, m_compatScrollTrack, m_compatScrollThumb, m_compatScrollThumbTransform);
        });
        m_compatScrollViewer.SizeChanged([this](auto&&, auto&&) {
            SyncScrollThumb(m_compatScrollViewer, m_compatScrollTrack, m_compatScrollThumb, m_compatScrollThumbTransform);
        });
    }

    if (m_leftTabSlidersBtn) m_leftTabSlidersBtn.Click([this](auto&&, auto&&) { SwitchLeftTab(LeftTab::Sliders); });
    if (m_leftTabAdvBtn)     m_leftTabAdvBtn.Click([this](auto&&, auto&&) { SwitchLeftTab(LeftTab::Advanced); });
    if (m_leftTabTargetsBtn) m_leftTabTargetsBtn.Click([this](auto&&, auto&&) { SwitchLeftTab(LeftTab::Targets); });
    if (m_leftTabCheckBtn)   m_leftTabCheckBtn.Click([this](auto&&, auto&&) { SwitchLeftTab(LeftTab::Compat); });

    if (m_filterAllBtn)   m_filterAllBtn.Click([this](auto&&, auto&&) { SwitchCoreFilter(CoreFilterMode::All); });
    if (m_filterPCoreBtn) m_filterPCoreBtn.Click([this](auto&&, auto&&) { SwitchCoreFilter(CoreFilterMode::PCores); });
    if (m_filterECoreBtn) m_filterECoreBtn.Click([this](auto&&, auto&&) { SwitchCoreFilter(CoreFilterMode::ECores); });

    if (m_root) {
        m_root.KeyDown([this](auto&&, winrt_wux::Input::KeyRoutedEventArgs const& args) {
            if (args.Key() == winrt::Windows::System::VirtualKey::Escape) {
                if (m_powerPlanPopupOpen) {
                    TogglePowerPlanPopup(false);
                    args.Handled(true);
                } else if (m_runningProcPopupOpen) {
                    ToggleRunningProcPopup(false);
                    args.Handled(true);
                }
            }
        });
    }

    if (m_customPlanSelectBtn) {
        m_customPlanSelectBtn.Click([this](auto&&, auto&&) {
            if (m_runningProcPopupOpen) ToggleRunningProcPopup(false);
            TogglePowerPlanPopup(!m_powerPlanPopupOpen);
        });
    }
    if (m_powerPlanBackdrop) {
        m_powerPlanBackdrop.PointerPressed([this](auto&&, winrt_wux::Input::PointerRoutedEventArgs const& args) {
            args.Handled(true);
            TogglePowerPlanPopup(false);
        });
    }
    if (m_runningProcBackdrop) {
        m_runningProcBackdrop.PointerPressed([this](auto&&, winrt_wux::Input::PointerRoutedEventArgs const& args) {
            args.Handled(true);
            ToggleRunningProcPopup(false);
        });
    }
    if (m_planCloseSb) {
        m_planCloseSb.Completed([this](auto&&, auto&&) {
            if (!m_powerPlanPopupOpen && m_powerPlanPopupHost) {
                m_powerPlanPopupHost.Visibility(winrt_wux::Visibility::Collapsed);
            }
        });
    }

    if (m_applyPresetsBtn) m_applyPresetsBtn.Click([this](auto&&, auto&&) { ExecuteApplyTuneProfile(true); });
    if (m_restoreBtn)      m_restoreBtn.Click([this](auto&&, auto&&) { ExecuteRestoreStock(); });

    if (m_browseExeBtn)      m_browseExeBtn.Click([this](auto&&, auto&&) { BrowseAndAddTargetExe(); });
    if (m_pickRunningExeBtn) m_pickRunningExeBtn.Click([this](auto&&, auto&&) { ToggleRunningProcPopup(!m_runningProcPopupOpen); });
    if (m_applyTargetsBtn)   m_applyTargetsBtn.Click([this](auto&&, auto&&) { ExecuteApplyTargetsNow(); });

    auto setPrioMode = [this](int mode) {
        m_config.targetPriorityMode = mode;
        UpdateSliderLabelsAndPills();
        ConfigManager::Save(m_config);
        m_engine.InvalidateProcessBoostCache();
        if (m_cpuInfo.isIntel) ExecuteApplyTargetsNow();
    };
    if (m_targetPrio0Btn) m_targetPrio0Btn.Click([setPrioMode](auto&&, auto&&) { setPrioMode(0); });
    if (m_targetPrio1Btn) m_targetPrio1Btn.Click([setPrioMode](auto&&, auto&&) { setPrioMode(1); });
    if (m_targetPrio2Btn) m_targetPrio2Btn.Click([setPrioMode](auto&&, auto&&) { setPrioMode(2); });
    if (m_targetPrio3Btn) m_targetPrio3Btn.Click([setPrioMode](auto&&, auto&&) { setPrioMode(3); });

    auto setCoreMode = [this](int mode) {
        m_config.targetCoreMode = m_cpuInfo.isHybrid ? mode : 0;
        UpdateSliderLabelsAndPills();
        ConfigManager::Save(m_config);
        m_engine.InvalidateProcessBoostCache();
        if (m_cpuInfo.isIntel) ExecuteApplyTargetsNow();
    };
    if (m_targetCore0Btn) m_targetCore0Btn.Click([setCoreMode](auto&&, auto&&) { setCoreMode(0); });
    if (m_targetCore1Btn) m_targetCore1Btn.Click([setCoreMode](auto&&, auto&&) { setCoreMode(1); });
    if (m_targetCore2Btn) m_targetCore2Btn.Click([setCoreMode](auto&&, auto&&) { setCoreMode(2); });

    auto onTargetToggle = [this](auto&&, auto&&) {
        if (m_hydratingUi) return;
        if (m_tglTargetDeepBoost) m_config.targetDeepBoost      = m_tglTargetDeepBoost.IsOn();
        if (m_tglTargetIfeo)      m_config.targetIfeoPersist    = m_tglTargetIfeo.IsOn();
        if (m_tglFgBoost)         m_config.boostForegroundPCore = m_tglFgBoost.IsOn();
        ConfigManager::Save(m_config);
        m_engine.InvalidateProcessBoostCache();
        if (m_cpuInfo.isIntel) {
            ExecuteApplyTargetsNow();
        }
    };
    if (m_tglTargetDeepBoost) m_tglTargetDeepBoost.Toggled(onTargetToggle);
    if (m_tglTargetIfeo)      m_tglTargetIfeo.Toggled(onTargetToggle);
    if (m_tglFgBoost)         m_tglFgBoost.Toggled(onTargetToggle);

    if (m_presetEcoBtn)      m_presetEcoBtn.Click([this](auto&&, auto&&) { ApplyQuickPreset(PresetMode::Eco); });
    if (m_presetBalancedBtn) m_presetBalancedBtn.Click([this](auto&&, auto&&) { ApplyQuickPreset(PresetMode::Balanced); });
    if (m_presetGamingBtn)   m_presetGamingBtn.Click([this](auto&&, auto&&) { ApplyQuickPreset(PresetMode::GamingOc); });
    if (m_presetMaxOcBtn)    m_presetMaxOcBtn.Click([this](auto&&, auto&&) { ApplyQuickPreset(PresetMode::MaxIntelOc); });

    if (m_boostPrevBtn) {
        m_boostPrevBtn.Click([this](auto&&, auto&&) {
            m_config.boostMode = StepOption(kBoostModes, m_config.boostMode, -1);
            UpdateSliderLabelsAndPills();
            if (m_config.liveApply) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        });
    }
    if (m_boostNextBtn) {
        m_boostNextBtn.Click([this](auto&&, auto&&) {
            m_config.boostMode = StepOption(kBoostModes, m_config.boostMode, +1);
            UpdateSliderLabelsAndPills();
            if (m_config.liveApply) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        });
    }

    if (m_schedPrevBtn) {
        m_schedPrevBtn.Click([this](auto&&, auto&&) {
            m_config.schedPolicy = StepOption(kSchedPolicies, m_config.schedPolicy, -1);
            UpdateSliderLabelsAndPills();
            if (m_config.liveApply) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        });
    }
    if (m_schedNextBtn) {
        m_schedNextBtn.Click([this](auto&&, auto&&) {
            m_config.schedPolicy = StepOption(kSchedPolicies, m_config.schedPolicy, +1);
            UpdateSliderLabelsAndPills();
            if (m_config.liveApply) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        });
    }

    auto onSliderChanged = [this]() {
        if (m_hydratingUi) return;
        if (m_sliderUnpark)   m_config.unparkCoresPercent = static_cast<int>(std::lround(m_sliderUnpark.Value()));
        if (m_sliderMinState) m_config.minStatePercent    = static_cast<int>(std::lround(m_sliderMinState.Value()));
        if (m_sliderMaxState) m_config.maxStatePercent    = static_cast<int>(std::lround(m_sliderMaxState.Value()));
        if (m_config.minStatePercent > m_config.maxStatePercent) {
            m_config.minStatePercent = m_config.maxStatePercent;
            m_hydratingUi = true;
            if (m_sliderMinState) m_sliderMinState.Value(static_cast<double>(m_config.minStatePercent));
            m_hydratingUi = false;
        }
        if (m_sliderBoostPol) m_config.turboBoostPolicy = static_cast<int>(std::lround(m_sliderBoostPol.Value()));
        if (m_sliderEpp)      m_config.eppPercent       = static_cast<int>(std::lround(m_sliderEpp.Value()));
        if (m_sliderFreqCap) {
            int rawFreq = static_cast<int>(std::lround(m_sliderFreqCap.Value()));
            m_config.freqCapMhz = (rawFreq < 1000) ? 0 : rawFreq;
        }

        if (m_sliderIncThresh)   m_config.incThreshold         = static_cast<int>(std::lround(m_sliderIncThresh.Value()));
        if (m_sliderDecThresh)   m_config.decThreshold         = static_cast<int>(std::lround(m_sliderDecThresh.Value()));
        if (m_sliderHeteroFloor) m_config.heteroFloorPerf      = static_cast<int>(std::lround(m_sliderHeteroFloor.Value()));
        if (m_sliderConcur)      m_config.concurrencyThreshold = static_cast<int>(std::lround(m_sliderConcur.Value()));

        UpdateSliderLabelsAndPills();
        if (m_config.liveApply && m_cpuInfo.isIntel) {
            SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        }
    };

    if (m_sliderUnpark)      m_sliderUnpark.ValueChanged([onSliderChanged](auto&&, auto&&) { onSliderChanged(); });
    if (m_sliderMinState)    m_sliderMinState.ValueChanged([onSliderChanged](auto&&, auto&&) { onSliderChanged(); });
    if (m_sliderMaxState)    m_sliderMaxState.ValueChanged([onSliderChanged](auto&&, auto&&) { onSliderChanged(); });
    if (m_sliderBoostPol)    m_sliderBoostPol.ValueChanged([onSliderChanged](auto&&, auto&&) { onSliderChanged(); });
    if (m_sliderEpp)         m_sliderEpp.ValueChanged([onSliderChanged](auto&&, auto&&) { onSliderChanged(); });
    if (m_sliderFreqCap)     m_sliderFreqCap.ValueChanged([onSliderChanged](auto&&, auto&&) { onSliderChanged(); });
    if (m_sliderIncThresh)   m_sliderIncThresh.ValueChanged([onSliderChanged](auto&&, auto&&) { onSliderChanged(); });
    if (m_sliderDecThresh)   m_sliderDecThresh.ValueChanged([onSliderChanged](auto&&, auto&&) { onSliderChanged(); });
    if (m_sliderHeteroFloor) m_sliderHeteroFloor.ValueChanged([onSliderChanged](auto&&, auto&&) { onSliderChanged(); });
    if (m_sliderConcur)      m_sliderConcur.ValueChanged([onSliderChanged](auto&&, auto&&) { onSliderChanged(); });

    if (m_tglLiveApply) {
        m_tglLiveApply.Toggled([this](auto&&, auto&&) {
            if (m_hydratingUi) return;
            m_config.liveApply = m_tglLiveApply.IsOn();
            ConfigManager::Save(m_config);
        });
    }

    auto onAdvToggle = [this](auto&&, auto&&) {
        if (m_hydratingUi) return;
        if (m_tglActiveCooling) m_config.activeCooling       = m_tglActiveCooling.IsOn();
        if (m_tglOsDirectHwp)   m_config.hwpAutonomous       = !m_tglOsDirectHwp.IsOn();
        if (m_tglIdleDisable)   m_config.disableIdleStates   = m_tglIdleDisable.IsOn();
        if (m_tglOsThrottle)    m_config.disableOsThrottling = m_tglOsThrottle.IsOn();
        ConfigManager::Save(m_config);
        if (m_config.liveApply && m_cpuInfo.isIntel) {
            SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        }
    };
    if (m_tglActiveCooling) m_tglActiveCooling.Toggled(onAdvToggle);
    if (m_tglOsDirectHwp)   m_tglOsDirectHwp.Toggled(onAdvToggle);
    if (m_tglIdleDisable)   m_tglIdleDisable.Toggled(onAdvToggle);
    if (m_tglOsThrottle)    m_tglOsThrottle.Toggled(onAdvToggle);

    if (m_tglAdvHighRes) {
        m_tglAdvHighRes.Toggled([this](auto&&, auto&&) {
            if (m_hydratingUi) return;
            m_config.highResTimer = m_tglAdvHighRes.IsOn();
            m_hydratingUi = true;
            if (m_tglHighResTimer) m_tglHighResTimer.IsOn(m_config.highResTimer);
            m_hydratingUi = false;
            SaveAndApplySettings();
        });
    }

    if (m_settingsBtn) {
        m_settingsBtn.Click([this](auto&&, auto&&) { SwitchToSettings(true); });
        m_settingsBtn.PointerEntered([this](auto&&, auto&&) { SafeBeginSb(m_gearSpinInSb); });
        m_settingsBtn.PointerExited([this](auto&&, auto&&) { SafeBeginSb(m_gearSpinOutSb); });
    }
    if (m_backBtn)    m_backBtn.Click([this](auto&&, auto&&) { SwitchToSettings(false); });
    if (m_minTrayBtn) m_minTrayBtn.Click([this](auto&&, auto&&) { HideToTray(); });

    auto onSettingToggle = [this](auto&&, auto&&) {
        if (!m_hydratingUi) SaveAndApplySettings();
    };
    if (m_tglAutoStart)      m_tglAutoStart.Toggled(onSettingToggle);
    if (m_tglStartMinimized) m_tglStartMinimized.Toggled(onSettingToggle);
    if (m_tglMinToTray)      m_tglMinToTray.Toggled(onSettingToggle);
    if (m_tglCloseToTray)    m_tglCloseToTray.Toggled(onSettingToggle);
    if (m_tglAlwaysOnTop)    m_tglAlwaysOnTop.Toggled(onSettingToggle);
    if (m_tglNotifications)  m_tglNotifications.Toggled(onSettingToggle);
    if (m_tglApplyOnStart)   m_tglApplyOnStart.Toggled(onSettingToggle);
    if (m_tglHighResTimer)   m_tglHighResTimer.Toggled(onSettingToggle);

    if (m_trayModeLogoBtn) {
        m_trayModeLogoBtn.Click([this](auto&&, auto&&) {
            m_config.trayStyle = L"logo";
            UpdateSliderLabelsAndPills();
            SaveAndApplySettings();
        });
    }
    if (m_trayModePercentBtn) {
        m_trayModePercentBtn.Click([this](auto&&, auto&&) {
            m_config.trayStyle = L"percent";
            UpdateSliderLabelsAndPills();
            SaveAndApplySettings();
        });
    }
}

void AppWindow::AttachDragHandlers(const winrt_wux::UIElement& element) {
    if (!element) return;

    element.PointerPressed([this, element](auto&&, winrt_wux::Input::PointerRoutedEventArgs const& args) {
        auto curNode = args.OriginalSource().try_as<winrt_wux::DependencyObject>();
        while (curNode) {
            if (curNode.try_as<winrt_ctrl::Primitives::ButtonBase>() ||
                curNode.try_as<winrt_ctrl::Primitives::Thumb>() ||
                curNode.try_as<winrt_ctrl::Slider>() ||
                curNode.try_as<winrt_ctrl::ComboBox>() ||
                curNode.try_as<winrt_ctrl::ComboBoxItem>() ||
                curNode.try_as<winrt_ctrl::ToggleSwitch>()) {
                return;
            }
            curNode = winrt_med::VisualTreeHelper::GetParent(curNode);
        }

        if (args.GetCurrentPoint(element).Properties().IsLeftButtonPressed()) {
            RECT winRc{};
            if (GetWindowRect(m_hwnd, &winRc) && GetCursorPos(&m_dragStartCursor)) {
                m_dragStartWin.x = winRc.left;
                m_dragStartWin.y = winRc.top;
                m_isDraggingWindow = true;
                element.CapturePointer(args.Pointer());
                args.Handled(true);
            }
        }
    });

    element.PointerMoved([this, element](auto&&, winrt_wux::Input::PointerRoutedEventArgs const& args) {
        if (!m_isDraggingWindow) return;
        const bool mouseDown = (GetKeyState(VK_LBUTTON) & 0x8000) != 0 ||
                               args.GetCurrentPoint(element).Properties().IsLeftButtonPressed();
        if (!mouseDown) {
            m_isDraggingWindow = false;
            element.ReleasePointerCaptures();
            return;
        }
        POINT cur{};
        if (GetCursorPos(&cur)) {
            const int newX = m_dragStartWin.x + (cur.x - m_dragStartCursor.x);
            const int newY = m_dragStartWin.y + (cur.y - m_dragStartCursor.y);
            SetWindowPos(m_hwnd, nullptr, newX, newY, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        args.Handled(true);
    });

    auto stopDrag = [this, element](auto&&, winrt_wux::Input::PointerRoutedEventArgs const& args) {
        if (m_isDraggingWindow) {
            m_isDraggingWindow = false;
            element.ReleasePointerCaptures();
            args.Handled(true);
        }
    };
    element.PointerReleased(stopDrag);
    element.PointerCanceled(stopDrag);
    element.PointerCaptureLost(stopDrag);
}

void AppWindow::LoadLogoIntoUi() {
    auto pngBytes = TrayIconRenderer::LoadLogoPngBytes(m_hInst);
    if (!pngBytes.empty() && m_logoImage) {
        auto loadPngAsync = [](AppWindow* self, std::vector<uint8_t> bytes) -> winrt::fire_and_forget {
            HWND hwnd = self->m_hwnd;
            winrt::apartment_context uiThread;
            try {
                winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
                winrt::Windows::Storage::Streams::DataWriter writer(stream.GetOutputStreamAt(0));
                writer.WriteBytes(bytes);
                co_await writer.StoreAsync();
                writer.DetachStream();
                stream.Seek(0);

                co_await uiThread;
                if (!IsWindow(hwnd) || !self->m_logoImage) co_return;

                winrt_img::BitmapImage bmp;
                bmp.DecodePixelHeight(48);
                self->m_logoImage.Source(bmp);
                co_await bmp.SetSourceAsync(stream);
            } catch (...) {
            }
        };
        loadPngAsync(this, std::move(pngBytes));
    }
}

void AppWindow::PopulateCpuStaticInfo() {
    if (m_cpuBrandText) {
        m_cpuBrandText.Text(winrt::hstring(m_cpuInfo.brandName));
    }
    if (m_cpuSpecShortText) {
        wchar_t spec[160]{};
        const double baseGhz = static_cast<double>(m_cpuInfo.baseMhz) / 1000.0;
        const double maxGhz  = static_cast<double>(m_cpuInfo.maxTurboMhz) / 1000.0;
        if (m_cpuInfo.isHybrid && maxGhz > baseGhz) {
            std::swprintf(spec, 160, L"%uP + %uE · %u Threads · Base %.2f · Max %.2f GHz",
                          m_cpuInfo.pCoreCount, m_cpuInfo.eCoreCount, m_cpuInfo.logicalThreads, baseGhz, maxGhz);
        } else if (m_cpuInfo.isHybrid) {
            std::swprintf(spec, 160, L"%uP + %uE · %u Threads · Base %.2f GHz",
                          m_cpuInfo.pCoreCount, m_cpuInfo.eCoreCount, m_cpuInfo.logicalThreads, baseGhz);
        } else {
            std::swprintf(spec, 160, L"%u Cores / %u Threads · Base %.2f GHz",
                          m_cpuInfo.physicalCores, m_cpuInfo.logicalThreads, baseGhz);
        }
        m_cpuSpecShortText.Text(winrt::hstring(spec));
    }

    if (m_filterAllBtn && m_filterPCoreBtn && m_filterECoreBtn) {
        wchar_t bAll[32]{}, bP[32]{}, bE[32]{};
        std::swprintf(bAll, 32, L"All (%u)", m_cpuInfo.physicalCores);
        std::swprintf(bP, 32, L"P-Core (%u)", m_cpuInfo.pCoreCount);
        std::swprintf(bE, 32, L"E-Core (%u)", m_cpuInfo.eCoreCount);
        m_filterAllBtn.Content(winrt::box_value(winrt::hstring(bAll)));
        m_filterPCoreBtn.Content(winrt::box_value(winrt::hstring(bP)));
        m_filterECoreBtn.Content(winrt::box_value(winrt::hstring(bE)));

        const bool showSplit = m_cpuInfo.isHybrid && m_cpuInfo.eCoreCount > 0;
        m_filterPCoreBtn.Visibility(showSplit ? winrt_wux::Visibility::Visible : winrt_wux::Visibility::Collapsed);
        m_filterECoreBtn.Visibility(showSplit ? winrt_wux::Visibility::Visible : winrt_wux::Visibility::Collapsed);
        if (m_coreLegendHybrid) {
            m_coreLegendHybrid.Visibility(showSplit ? winrt_wux::Visibility::Visible : winrt_wux::Visibility::Collapsed);
        }
    }
}

void AppWindow::PopulateCustomPowerPlanMenu() {
    const auto plans = m_engine.ListAllPowerPlans();
    std::wstring activeName = plans.empty() ? L"No Power Plan" : plans.front().name;
    for (const auto& p : plans) {
        if (p.isActive) {
            activeName = p.name;
            break;
        }
    }

    if (m_selectedPlanLabelText) {
        m_selectedPlanLabelText.Text(winrt::hstring(activeName));
    }

    if (!m_powerPlanPopupList) return;
    m_powerPlanPopupList.Children().Clear();

    winrt_wux::Style itemStyle{nullptr};
    if (auto res = m_root.Resources()) {
        if (res.HasKey(winrt::box_value(L"PlanPopupItemButtonStyle"))) {
            itemStyle = res.Lookup(winrt::box_value(L"PlanPopupItemButtonStyle")).try_as<winrt_wux::Style>();
        }
    }

    for (const auto& p : plans) {
        const bool isActive = p.isActive;

        winrt_ctrl::Button row;
        if (itemStyle) {
            row.Style(itemStyle);
        }
        row.Background(winrt_med::SolidColorBrush(isActive ? RgbColor(239, 246, 255) : RgbColor(255, 255, 255)));
        row.BorderBrush(winrt_med::SolidColorBrush(isActive ? RgbColor(147, 197, 253) : ArgbColor(0, 255, 255, 255)));
        row.BorderThickness(winrt_wux::ThicknessHelper::FromUniformLength(1.0));
        row.CornerRadius(winrt_wux::CornerRadiusHelper::FromUniformRadius(6.0));
        row.Padding(winrt_wux::ThicknessHelper::FromLengths(10, 7, 10, 7));
        row.HorizontalAlignment(winrt_wux::HorizontalAlignment::Stretch);
        row.HorizontalContentAlignment(winrt_wux::HorizontalAlignment::Stretch);
        winrt::Windows::UI::Xaml::Automation::AutomationProperties::SetName(row, winrt::hstring(p.name));

        winrt_ctrl::Grid rowGrid;
        winrt_ctrl::ColumnDefinition colName;
        colName.Width(winrt_wux::GridLengthHelper::FromValueAndType(1.0, winrt_wux::GridUnitType::Star));
        winrt_ctrl::ColumnDefinition colBadge;
        colBadge.Width(winrt_wux::GridLengthHelper::Auto());
        rowGrid.ColumnDefinitions().Append(colName);
        rowGrid.ColumnDefinitions().Append(colBadge);

        winrt_ctrl::StackPanel leftPart;
        leftPart.Orientation(winrt_ctrl::Orientation::Horizontal);
        leftPart.Spacing(8.0);
        leftPart.VerticalAlignment(winrt_wux::VerticalAlignment::Center);

        winrt_shp::Ellipse dot;
        dot.Width(7.0);
        dot.Height(7.0);
        dot.VerticalAlignment(winrt_wux::VerticalAlignment::Center);
        dot.Fill(winrt_med::SolidColorBrush(
            isActive ? RgbColor(37, 99, 235) : RgbColor(203, 213, 225)));
        leftPart.Children().Append(dot);

        winrt_ctrl::TextBlock nameTxt;
        nameTxt.Text(winrt::hstring(p.name));
        nameTxt.FontSize(12.0);
        nameTxt.FontWeight(isActive ? winrt_txt::FontWeights::Bold() : winrt_txt::FontWeights::SemiBold());
        nameTxt.Foreground(winrt_med::SolidColorBrush(isActive ? RgbColor(29, 78, 216) : RgbColor(15, 23, 42)));
        nameTxt.TextTrimming(winrt_wux::TextTrimming::CharacterEllipsis);
        nameTxt.VerticalAlignment(winrt_wux::VerticalAlignment::Center);
        leftPart.Children().Append(nameTxt);

        winrt_ctrl::Grid::SetColumn(leftPart, 0);
        rowGrid.Children().Append(leftPart);

        if (isActive) {
            winrt_ctrl::Border tagPill;
            tagPill.Background(winrt_med::SolidColorBrush(RgbColor(37, 99, 235)));
            tagPill.CornerRadius(winrt_wux::CornerRadiusHelper::FromUniformRadius(4.0));
            tagPill.Padding(winrt_wux::ThicknessHelper::FromLengths(6, 0, 6, 0));
            tagPill.Height(17.0);
            tagPill.VerticalAlignment(winrt_wux::VerticalAlignment::Center);
            tagPill.Margin(winrt_wux::ThicknessHelper::FromLengths(8, 0, 0, 0));

            winrt_ctrl::TextBlock tagTxt;
            tagTxt.Text(L"ACTIVE");
            tagTxt.FontSize(9.0);
            tagTxt.FontWeight(winrt_txt::FontWeights::Bold());
            tagTxt.Foreground(winrt_med::SolidColorBrush(RgbColor(255, 255, 255)));
            tagTxt.HorizontalAlignment(winrt_wux::HorizontalAlignment::Center);
            tagTxt.VerticalAlignment(winrt_wux::VerticalAlignment::Center);
            tagTxt.TextLineBounds(winrt_wux::TextLineBounds::Tight);
            tagPill.Child(tagTxt);

            winrt_ctrl::Grid::SetColumn(tagPill, 1);
            rowGrid.Children().Append(tagPill);
        }

        row.Content(rowGrid);

        const std::wstring guidCopy = p.guidStr;
        row.Click([this, guidCopy](auto&&, auto&&) {
            const std::wstring guidLocal = guidCopy;
            TogglePowerPlanPopup(false);
            ExecuteSwitchSelectedPlan(guidLocal);
        });

        m_powerPlanPopupList.Children().Append(row);
    }
}

void AppWindow::TogglePowerPlanPopup(bool open) {
    if (open && !m_cpuInfo.isIntel) return;
    if (!m_powerPlanPopupHost) return;
    if (open == m_powerPlanPopupOpen) return;
    m_powerPlanPopupOpen = open;

    if (open) {
        try {
            PopulateCustomPowerPlanMenu();
        } catch (...) {
        }
        m_powerPlanPopupHost.Visibility(winrt_wux::Visibility::Visible);
        SafeStopSb(m_planCloseSb);
        SafeBeginSb(m_planOpenSb);
    } else {
        SafeStopSb(m_planOpenSb);
        if (m_planCloseSb) {
            SafeBeginSb(m_planCloseSb);
        } else {
            m_powerPlanPopupHost.Visibility(winrt_wux::Visibility::Collapsed);
        }
    }
}

void AppWindow::PopulateRunningProcPopupMenu() {
    if (!m_runningProcPopupList) return;
    m_runningProcPopupList.Children().Clear();

    winrt_wux::Style itemStyle{nullptr};
    if (auto res = m_root.Resources()) {
        if (res.HasKey(winrt::box_value(L"PlanPopupItemButtonStyle"))) {
            itemStyle = res.Lookup(winrt::box_value(L"PlanPopupItemButtonStyle")).try_as<winrt_wux::Style>();
        }
    }

    const auto procs = m_engine.ListRunningProcesses();
    if (procs.empty()) {
        winrt_ctrl::TextBlock emptyTxt;
        emptyTxt.Text(L"No user processes found");
        emptyTxt.FontSize(11.5);
        emptyTxt.Foreground(winrt_med::SolidColorBrush(RgbColor(100, 116, 139)));
        emptyTxt.Margin(winrt_wux::ThicknessHelper::FromUniformLength(8.0));
        m_runningProcPopupList.Children().Append(emptyTxt);
        return;
    }

    for (const auto& p : procs) {
        winrt_ctrl::Button row;
        if (itemStyle) row.Style(itemStyle);
        row.Background(winrt_med::SolidColorBrush(RgbColor(255, 255, 255)));
        row.Padding(winrt_wux::ThicknessHelper::FromLengths(9, 5, 9, 5));
        row.HorizontalAlignment(winrt_wux::HorizontalAlignment::Stretch);
        row.HorizontalContentAlignment(winrt_wux::HorizontalAlignment::Stretch);
        winrt::Windows::UI::Xaml::Automation::AutomationProperties::SetName(row, winrt::hstring(p.exeName));

        winrt_ctrl::Grid rowGrid;
        winrt_ctrl::ColumnDefinition c0;
        c0.Width(winrt_wux::GridLengthHelper::FromValueAndType(1.0, winrt_wux::GridUnitType::Star));
        winrt_ctrl::ColumnDefinition c1;
        c1.Width(winrt_wux::GridLengthHelper::Auto());
        rowGrid.ColumnDefinitions().Append(c0);
        rowGrid.ColumnDefinitions().Append(c1);

        winrt_ctrl::StackPanel leftPart;
        leftPart.Orientation(winrt_ctrl::Orientation::Horizontal);
        leftPart.Spacing(7.0);
        leftPart.VerticalAlignment(winrt_wux::VerticalAlignment::Center);

        winrt_shp::Ellipse dot;
        dot.Width(6.5);
        dot.Height(6.5);
        dot.Fill(winrt_med::SolidColorBrush(p.isForeground ? RgbColor(37, 99, 235) : RgbColor(203, 213, 225)));
        leftPart.Children().Append(dot);

        winrt_ctrl::TextBlock nameTxt;
        nameTxt.Text(winrt::hstring(p.exeName));
        nameTxt.FontSize(11.5);
        nameTxt.FontWeight(winrt_txt::FontWeights::SemiBold());
        nameTxt.Foreground(winrt_med::SolidColorBrush(RgbColor(9, 9, 11)));
        nameTxt.TextTrimming(winrt_wux::TextTrimming::CharacterEllipsis);
        leftPart.Children().Append(nameTxt);
        winrt_ctrl::Grid::SetColumn(leftPart, 0);
        rowGrid.Children().Append(leftPart);

        if (p.instanceCount > 1) {
            wchar_t cntBuf[16]{};
            std::swprintf(cntBuf, 16, L"x%u", p.instanceCount);
            winrt_ctrl::TextBlock cntTxt;
            cntTxt.Text(winrt::hstring(cntBuf));
            cntTxt.FontSize(10.0);
            cntTxt.FontWeight(winrt_txt::FontWeights::SemiBold());
            cntTxt.Foreground(winrt_med::SolidColorBrush(RgbColor(100, 116, 139)));
            cntTxt.VerticalAlignment(winrt_wux::VerticalAlignment::Center);
            winrt_ctrl::Grid::SetColumn(cntTxt, 1);
            rowGrid.Children().Append(cntTxt);
        }

        row.Content(rowGrid);
        const std::wstring exeCopy = p.exeName;
        row.Click([this, exeCopy](auto&&, auto&&) {
            const std::wstring exeLocal = exeCopy;
            ToggleRunningProcPopup(false);
            AddTargetExeRule(exeLocal);
        });
        m_runningProcPopupList.Children().Append(row);
    }
}

void AppWindow::ToggleRunningProcPopup(bool open) {
    if (open && !m_cpuInfo.isIntel) return;
    if (!m_runningProcPopupHost) return;
    if (open == m_runningProcPopupOpen) return;
    m_runningProcPopupOpen = open;

    if (open) {
        if (m_powerPlanPopupOpen) TogglePowerPlanPopup(false);
        PopulateRunningProcPopupMenu();
        m_runningProcPopupHost.Visibility(winrt_wux::Visibility::Visible);
        SafeBeginSb(m_runningOpenSb);
    } else {
        m_runningProcPopupHost.Visibility(winrt_wux::Visibility::Collapsed);
    }
}

void AppWindow::AddTargetExeRule(const std::wstring& exeName) {
    if (exeName.empty() || !m_cpuInfo.isIntel) return;
    std::wstring clean = exeName;
    const size_t sl = clean.find_last_of(L"\\/");
    if (sl != std::wstring::npos) clean = clean.substr(sl + 1);
    if (clean.empty()) return;

    for (auto& r : m_config.targetExes) {
        if (_wcsicmp(r.exeName.c_str(), clean.c_str()) == 0) {
            r.enabled = true;
            ConfigManager::Save(m_config);
            m_engine.InvalidateProcessBoostCache();
            ExecuteApplyTargetsNow();
            RebuildTargetExeListUi();
            return;
        }
    }

    TargetExeRule rule{};
    rule.exeName = clean;
    rule.enabled = true;
    m_config.targetExes.push_back(std::move(rule));
    ConfigManager::Save(m_config);
    m_engine.InvalidateProcessBoostCache();
    ExecuteApplyTargetsNow();
    RebuildTargetExeListUi();
}

void AppWindow::BrowseAndAddTargetExe() {
    if (!m_cpuInfo.isIntel) return;
    wchar_t fileBuf[1024]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Executables (*.exe)\0*.exe\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = 1024;
    ofn.lpstrTitle = L"Select Application (.exe) to Boost";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        AddTargetExeRule(fileBuf);
    }
}

void AppWindow::RebuildTargetExeListUi() {
    if (!m_targetExeListPanel) return;
    m_targetExeListPanel.Children().Clear();

    if (m_targetExeEmptyHint) {
        m_targetExeEmptyHint.Visibility(m_config.targetExes.empty()
            ? winrt_wux::Visibility::Visible
            : winrt_wux::Visibility::Collapsed);
    }
    if (m_config.targetExes.empty()) return;

    std::map<std::wstring, uint32_t> liveCounts;
    for (const auto& p : m_engine.ListRunningProcesses()) {
        std::wstring k = p.exeName;
        for (auto& c : k) c = static_cast<wchar_t>(towlower(c));
        liveCounts[k] = p.instanceCount;
    }

    for (size_t i = 0; i < m_config.targetExes.size(); ++i) {
        const auto& rule = m_config.targetExes[i];
        std::wstring k = rule.exeName;
        for (auto& c : k) c = static_cast<wchar_t>(towlower(c));
        const auto itCnt = liveCounts.find(k);
        const uint32_t runCnt = (itCnt != liveCounts.end()) ? itCnt->second : 0;

        winrt_ctrl::Border rowCard;
        rowCard.Background(winrt_med::SolidColorBrush(RgbColor(255, 255, 255)));
        rowCard.BorderBrush(winrt_med::SolidColorBrush(rule.enabled && runCnt > 0 ? RgbColor(191, 219, 254) : RgbColor(226, 232, 240)));
        rowCard.BorderThickness(winrt_wux::ThicknessHelper::FromUniformLength(1.0));
        rowCard.CornerRadius(winrt_wux::CornerRadiusHelper::FromUniformRadius(6.0));
        rowCard.Padding(winrt_wux::ThicknessHelper::FromLengths(9, 4, 6, 4));

        winrt_ctrl::Grid rowGrid;
        winrt_ctrl::ColumnDefinition cName;
        cName.Width(winrt_wux::GridLengthHelper::FromValueAndType(1.0, winrt_wux::GridUnitType::Star));
        winrt_ctrl::ColumnDefinition cStatus;
        cStatus.Width(winrt_wux::GridLengthHelper::Auto());
        winrt_ctrl::ColumnDefinition cToggle;
        cToggle.Width(winrt_wux::GridLengthHelper::Auto());
        winrt_ctrl::ColumnDefinition cDel;
        cDel.Width(winrt_wux::GridLengthHelper::Auto());
        rowGrid.ColumnDefinitions().Append(cName);
        rowGrid.ColumnDefinitions().Append(cStatus);
        rowGrid.ColumnDefinitions().Append(cToggle);
        rowGrid.ColumnDefinitions().Append(cDel);

        winrt_ctrl::TextBlock nameTxt;
        nameTxt.Text(winrt::hstring(rule.exeName));
        nameTxt.FontSize(11.5);
        nameTxt.FontWeight(winrt_txt::FontWeights::SemiBold());
        nameTxt.Foreground(winrt_med::SolidColorBrush(rule.enabled ? RgbColor(9, 9, 11) : RgbColor(148, 163, 184)));
        nameTxt.VerticalAlignment(winrt_wux::VerticalAlignment::Center);
        nameTxt.TextTrimming(winrt_wux::TextTrimming::CharacterEllipsis);
        winrt_ctrl::Grid::SetColumn(nameTxt, 0);
        rowGrid.Children().Append(nameTxt);

        winrt_ctrl::Border badge;
        badge.Background(winrt_med::SolidColorBrush(runCnt > 0 ? RgbColor(239, 246, 255) : RgbColor(248, 250, 252)));
        badge.CornerRadius(winrt_wux::CornerRadiusHelper::FromUniformRadius(4.0));
        badge.Padding(winrt_wux::ThicknessHelper::FromLengths(6, 2, 6, 2));
        badge.Margin(winrt_wux::ThicknessHelper::FromLengths(6, 0, 8, 0));
        badge.VerticalAlignment(winrt_wux::VerticalAlignment::Center);

        wchar_t bBuf[24]{};
        if (runCnt > 1) std::swprintf(bBuf, 24, L"RUNNING x%u", runCnt);
        else if (runCnt == 1) wcscpy_s(bBuf, L"RUNNING");
        else wcscpy_s(bBuf, L"IDLE");

        winrt_ctrl::TextBlock badgeTxt;
        badgeTxt.Text(winrt::hstring(bBuf));
        badgeTxt.FontSize(9.0);
        badgeTxt.FontWeight(winrt_txt::FontWeights::Bold());
        badgeTxt.Foreground(winrt_med::SolidColorBrush(runCnt > 0 ? RgbColor(37, 99, 235) : RgbColor(148, 163, 184)));
        badge.Child(badgeTxt);
        winrt_ctrl::Grid::SetColumn(badge, 1);
        rowGrid.Children().Append(badge);

        winrt_ctrl::ToggleSwitch tgl;
        tgl.OnContent(winrt::box_value(L""));
        tgl.OffContent(winrt::box_value(L""));
        tgl.IsOn(rule.enabled);
        tgl.Margin(winrt_wux::ThicknessHelper::FromLengths(0, 0, 6, 0));
        winrt::Windows::UI::Xaml::Automation::AutomationProperties::SetAutomationId(
            tgl, winrt::hstring(L"TargetToggle_" + rule.exeName));
        const std::wstring exeCopy = rule.exeName;
        tgl.Toggled([this, exeCopy, tgl](auto&&, auto&&) {
            const std::wstring exeLocal = exeCopy;
            const bool en = tgl.IsOn();
            for (auto& r : m_config.targetExes) {
                if (_wcsicmp(r.exeName.c_str(), exeLocal.c_str()) == 0) {
                    r.enabled = en;
                    break;
                }
            }
            ConfigManager::Save(m_config);
            m_engine.InvalidateProcessBoostCache();
            if (!en) {
                m_engine.ClearTargetExeRuleEffect(exeLocal);
                SetActionStatus(L"Restored process defaults: " + exeLocal, false);
            } else {
                ExecuteApplyTargetsNow();
            }
        });
        winrt_ctrl::Grid::SetColumn(tgl, 2);
        rowGrid.Children().Append(tgl);

        winrt_ctrl::Button delBtn;
        delBtn.Content(winrt::box_value(L"×"));
        delBtn.Width(22);
        delBtn.Height(22);
        delBtn.FontSize(13.0);
        delBtn.Padding(winrt_wux::ThicknessHelper::FromUniformLength(0));
        delBtn.Foreground(winrt_med::SolidColorBrush(RgbColor(100, 116, 139)));
        delBtn.Background(winrt_med::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
        delBtn.BorderThickness(winrt_wux::ThicknessHelper::FromUniformLength(0));
        delBtn.Click([this, exeCopy](auto&&, auto&&) {
            const std::wstring exeLocal = exeCopy;
            m_engine.ClearTargetExeRuleEffect(exeLocal);
            auto& vec = m_config.targetExes;
            vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const TargetExeRule& r) {
                return _wcsicmp(r.exeName.c_str(), exeLocal.c_str()) == 0;
            }), vec.end());
            ConfigManager::Save(m_config);
            RebuildTargetExeListUi();
            SetActionStatus(L"Removed target: " + exeLocal, false);
        });
        winrt_ctrl::Grid::SetColumn(delBtn, 3);
        rowGrid.Children().Append(delBtn);

        rowCard.Child(rowGrid);
        m_targetExeListPanel.Children().Append(rowCard);
    }
}

void AppWindow::PopulateFeatureCompatList() {
    if (!m_compatListPanel) return;
    m_compatListPanel.Children().Clear();

    int readyCount = 0;
    const int totalCount = static_cast<int>(m_cpuInfo.featureChecks.size());

    for (const auto& item : m_cpuInfo.featureChecks) {
        if (item.level == SupportLevel::Supported || item.level == SupportLevel::ConditionalAdmin) {
            ++readyCount;
        }

        winrt_ctrl::Grid row;
        winrt_ctrl::ColumnDefinition c0;
        c0.Width(winrt_wux::GridLengthHelper::FromValueAndType(1.0, winrt_wux::GridUnitType::Star));
        winrt_ctrl::ColumnDefinition c1;
        c1.Width(winrt_wux::GridLengthHelper::Auto());
        row.ColumnDefinitions().Append(c0);
        row.ColumnDefinitions().Append(c1);
        row.Padding(winrt_wux::ThicknessHelper::FromLengths(8, 5, 8, 5));

        winrt_ctrl::Border rowCard;
        rowCard.Background(winrt_med::SolidColorBrush(RgbColor(248, 250, 252)));
        rowCard.BorderBrush(winrt_med::SolidColorBrush(RgbColor(226, 232, 240)));
        rowCard.BorderThickness(winrt_wux::ThicknessHelper::FromUniformLength(1.0));
        rowCard.CornerRadius(winrt_wux::CornerRadiusHelper::FromUniformRadius(6.0));

        winrt_ctrl::StackPanel left;
        left.Spacing(1.0);
        winrt_ctrl::TextBlock title;
        title.Text(winrt::hstring(item.name));
        title.FontSize(11.0);
        title.FontWeight(winrt_txt::FontWeights::SemiBold());
        title.Foreground(winrt_med::SolidColorBrush(RgbColor(9, 9, 11)));

        winrt_ctrl::TextBlock desc;
        desc.Text(winrt::hstring(item.detail));
        desc.FontSize(9.5);
        desc.Foreground(winrt_med::SolidColorBrush(RgbColor(71, 85, 105)));
        desc.TextTrimming(winrt_wux::TextTrimming::CharacterEllipsis);

        left.Children().Append(title);
        left.Children().Append(desc);
        winrt_ctrl::Grid::SetColumn(left, 0);

        winrt_ctrl::Border badge;
        badge.CornerRadius(winrt_wux::CornerRadiusHelper::FromUniformRadius(4.0));
        badge.Padding(winrt_wux::ThicknessHelper::FromLengths(6, 2, 6, 2));
        badge.VerticalAlignment(winrt_wux::VerticalAlignment::Center);
        badge.Margin(winrt_wux::ThicknessHelper::FromLengths(6, 0, 0, 0));

        winrt::Windows::UI::Color bg = RgbColor(239, 246, 255);
        winrt::Windows::UI::Color fg = RgbColor(29, 78, 216);
        if (item.level == SupportLevel::ConditionalAdmin) {
            bg = RgbColor(254, 243, 199); fg = RgbColor(180, 83, 9);
        } else if (item.level == SupportLevel::LockedOrLimited) {
            bg = RgbColor(241, 245, 249); fg = RgbColor(15, 23, 42);
        } else if (item.level == SupportLevel::Unsupported) {
            bg = RgbColor(241, 245, 249); fg = RgbColor(100, 116, 139);
        } else if (item.level == SupportLevel::Blocked) {
            bg = RgbColor(254, 226, 226); fg = RgbColor(185, 28, 28);
        }
        badge.Background(winrt_med::SolidColorBrush(bg));

        winrt_ctrl::TextBlock badgeTxt;
        badgeTxt.Text(winrt::hstring(item.badgeText));
        badgeTxt.FontSize(9.0);
        badgeTxt.FontWeight(winrt_txt::FontWeights::Bold());
        badgeTxt.Foreground(winrt_med::SolidColorBrush(fg));
        badge.Child(badgeTxt);
        winrt_ctrl::Grid::SetColumn(badge, 1);

        row.Children().Append(left);
        row.Children().Append(badge);
        rowCard.Child(row);
        m_compatListPanel.Children().Append(rowCard);
    }

    if (m_compatSummaryText) {
        wchar_t sum[48]{};
        std::swprintf(sum, 48, L"%d/%d Ready", readyCount, totalCount);
        m_compatSummaryText.Text(winrt::hstring(sum));
        m_compatSummaryText.Foreground(winrt_med::SolidColorBrush(
            m_cpuInfo.isIntel ? RgbColor(37, 99, 235) : RgbColor(220, 38, 38)
        ));
    }

    if (m_leftTabCheckBtn) {
        m_leftTabCheckBtn.Content(winrt::box_value(winrt::hstring(L"Support")));
    }

    const bool turboOk  = m_cpuInfo.isIntel && m_cpuInfo.hasTurboBoost;
    const bool hwpOk    = m_cpuInfo.isIntel && m_cpuInfo.hasHwp;
    const bool eppOk    = hwpOk && m_cpuInfo.hasHwpEpp;
    const bool hybridOk = m_cpuInfo.isIntel && m_cpuInfo.isHybrid;

    if (m_boostPrevBtn)      m_boostPrevBtn.IsEnabled(turboOk);
    if (m_boostNextBtn)      m_boostNextBtn.IsEnabled(turboOk);
    if (m_sliderBoostPol)    m_sliderBoostPol.IsEnabled(turboOk);
    if (m_sliderEpp)         m_sliderEpp.IsEnabled(eppOk);
    if (m_tglOsDirectHwp)    m_tglOsDirectHwp.IsEnabled(hwpOk);
    if (m_schedPrevBtn)      m_schedPrevBtn.IsEnabled(hybridOk);
    if (m_schedNextBtn)      m_schedNextBtn.IsEnabled(hybridOk);
    if (m_sliderHeteroFloor) m_sliderHeteroFloor.IsEnabled(hybridOk);
    if (m_targetCore1Btn)    m_targetCore1Btn.IsEnabled(hybridOk);
    if (m_targetCore2Btn)    m_targetCore2Btn.IsEnabled(hybridOk);
}

void AppWindow::BuildCoreCardVisuals() {
    m_coreCards.clear();

    for (const auto& ct : m_cpuInfo.cores) {
        CoreCardVisual visual{};
        visual.coreIndex = ct.coreIndex;
        visual.kind = ct.kind;
        visual.history.assign(kSparkHistoryLen, 0.0);

        const bool isPCore = (ct.kind == CoreKind::PCore);
        const bool isECore = (ct.kind == CoreKind::ECore);

        winrt::Windows::UI::Color accentColor = isECore ? RgbColor(9, 9, 11) : RgbColor(37, 99, 235);
        winrt::Windows::UI::Color pillBg = isECore ? RgbColor(9, 9, 11) : RgbColor(239, 246, 255);
        winrt::Windows::UI::Color pillFg = isECore ? RgbColor(255, 255, 255) : RgbColor(29, 78, 216);
        winrt::Windows::UI::Color pillBorder = isECore ? RgbColor(9, 9, 11) : RgbColor(191, 219, 254);
        winrt::Windows::UI::Color fillColor = isECore ? ArgbColor(26, 15, 23, 42) : ArgbColor(34, 37, 99, 235);
        winrt::Windows::UI::Color cardBorderClr = isPCore ? RgbColor(219, 234, 254) : RgbColor(226, 232, 240);

        winrt_ctrl::Border card;
        card.Background(winrt_med::SolidColorBrush(RgbColor(255, 255, 255)));
        card.BorderBrush(winrt_med::SolidColorBrush(cardBorderClr));
        card.BorderThickness(winrt_wux::ThicknessHelper::FromUniformLength(1.0));
        card.CornerRadius(winrt_wux::CornerRadiusHelper::FromUniformRadius(7.0));
        card.Padding(winrt_wux::ThicknessHelper::FromLengths(9, 6, 9, 6));

        winrt_ctrl::StackPanel rootStack;
        rootStack.Spacing(4.0);

        winrt_ctrl::Grid hdrGrid;
        winrt_ctrl::StackPanel leftHdr;
        leftHdr.Orientation(winrt_ctrl::Orientation::Horizontal);
        leftHdr.Spacing(5.0);
        leftHdr.VerticalAlignment(winrt_wux::VerticalAlignment::Center);

        wchar_t labelBuf[16]{};
        if (isPCore) std::swprintf(labelBuf, 16, L"P%u", ct.typeLocalIndex);
        else if (isECore) std::swprintf(labelBuf, 16, L"E%u", ct.typeLocalIndex);
        else std::swprintf(labelBuf, 16, L"C%u", ct.coreIndex);

        winrt_ctrl::Border badge;
        badge.Background(winrt_med::SolidColorBrush(pillBg));
        badge.BorderBrush(winrt_med::SolidColorBrush(pillBorder));
        badge.BorderThickness(winrt_wux::ThicknessHelper::FromUniformLength(1.0));
        badge.CornerRadius(winrt_wux::CornerRadiusHelper::FromUniformRadius(4.0));
        badge.Padding(winrt_wux::ThicknessHelper::FromLengths(6, 0, 6, 0));
        badge.Height(18.0);
        badge.MinWidth(26.0);
        badge.VerticalAlignment(winrt_wux::VerticalAlignment::Center);

        winrt_ctrl::TextBlock badgeTxt;
        badgeTxt.Text(winrt::hstring(labelBuf));
        badgeTxt.FontSize(9.5);
        badgeTxt.FontWeight(winrt_txt::FontWeights::Bold());
        badgeTxt.Foreground(winrt_med::SolidColorBrush(pillFg));
        badgeTxt.HorizontalAlignment(winrt_wux::HorizontalAlignment::Center);
        badgeTxt.VerticalAlignment(winrt_wux::VerticalAlignment::Center);
        badgeTxt.TextLineBounds(winrt_wux::TextLineBounds::Tight);
        badge.Child(badgeTxt);
        leftHdr.Children().Append(badge);

        winrt_ctrl::StackPanel rightHdr;
        rightHdr.Orientation(winrt_ctrl::Orientation::Horizontal);
        rightHdr.Spacing(6.0);
        rightHdr.HorizontalAlignment(winrt_wux::HorizontalAlignment::Right);
        rightHdr.VerticalAlignment(winrt_wux::VerticalAlignment::Center);

        winrt_ctrl::TextBlock clkTxt;
        clkTxt.Text(L"0.00 GHz");
        clkTxt.FontSize(10.0);
        clkTxt.FontWeight(winrt_txt::FontWeights::SemiBold());
        clkTxt.Foreground(winrt_med::SolidColorBrush(RgbColor(71, 85, 105)));
        clkTxt.VerticalAlignment(winrt_wux::VerticalAlignment::Center);

        winrt_ctrl::TextBlock pctTxt;
        pctTxt.Text(L"0%");
        pctTxt.FontSize(11.5);
        pctTxt.FontWeight(winrt_txt::FontWeights::Bold());
        pctTxt.Foreground(winrt_med::SolidColorBrush(accentColor));
        pctTxt.VerticalAlignment(winrt_wux::VerticalAlignment::Center);

        rightHdr.Children().Append(clkTxt);
        rightHdr.Children().Append(pctTxt);

        hdrGrid.Children().Append(leftHdr);
        hdrGrid.Children().Append(rightHdr);

        winrt_ctrl::Border graphBox;
        graphBox.Background(winrt_med::SolidColorBrush(RgbColor(248, 250, 252)));
        graphBox.BorderBrush(winrt_med::SolidColorBrush(RgbColor(226, 232, 240)));
        graphBox.BorderThickness(winrt_wux::ThicknessHelper::FromUniformLength(1.0));
        graphBox.CornerRadius(winrt_wux::CornerRadiusHelper::FromUniformRadius(5.0));
        graphBox.Height(34.0);

        winrt_ctrl::Viewbox vb;
        vb.Stretch(winrt_med::Stretch::Fill);

        winrt_ctrl::Canvas canvas;
        canvas.Width(kSparkW);
        canvas.Height(kSparkH);

        for (int g = 1; g <= 3; ++g) {
            winrt_shp::Line gridLine;
            const double gy = (kSparkH * g) / 4.0;
            gridLine.X1(0.0);
            gridLine.X2(kSparkW);
            gridLine.Y1(gy);
            gridLine.Y2(gy);
            gridLine.Stroke(winrt_med::SolidColorBrush(RgbColor(226, 232, 240)));
            gridLine.StrokeThickness(0.8);
            canvas.Children().Append(gridLine);
        }

        winrt_shp::Polygon fillPoly;
        fillPoly.Fill(winrt_med::SolidColorBrush(fillColor));

        winrt_shp::Polyline linePoly;
        linePoly.Stroke(winrt_med::SolidColorBrush(accentColor));
        linePoly.StrokeThickness(1.8);
        linePoly.StrokeLineJoin(winrt_med::PenLineJoin::Round);

        canvas.Children().Append(fillPoly);
        canvas.Children().Append(linePoly);

        vb.Child(canvas);
        graphBox.Child(vb);

        rootStack.Children().Append(hdrGrid);
        rootStack.Children().Append(graphBox);
        card.Child(rootStack);

        card.PointerEntered([card, isECore](auto&&, auto&&) {
            card.BorderBrush(winrt_med::SolidColorBrush(isECore ? RgbColor(15, 23, 42) : RgbColor(37, 99, 235)));
            card.Background(winrt_med::SolidColorBrush(isECore ? RgbColor(248, 250, 252) : RgbColor(245, 249, 255)));
        });
        card.PointerExited([card, cardBorderClr](auto&&, auto&&) {
            card.BorderBrush(winrt_med::SolidColorBrush(cardBorderClr));
            card.Background(winrt_med::SolidColorBrush(RgbColor(255, 255, 255)));
        });

        visual.cardBorder = card;
        visual.usageText = pctTxt;
        visual.clockText = clkTxt;
        visual.sparkLine = linePoly;
        visual.sparkFill = fillPoly;
        m_coreCards.push_back(std::move(visual));
    }
}

void AppWindow::RebuildCoreCardsGrid() {
    if (!m_coreGraphsContainer) return;

    m_coreGraphsContainer.Children().Clear();
    m_coreGraphsContainer.RowDefinitions().Clear();
    m_coreGraphsContainer.ColumnDefinitions().Clear();

    winrt_ctrl::ColumnDefinition c0;
    c0.Width(winrt_wux::GridLengthHelper::FromValueAndType(1.0, winrt_wux::GridUnitType::Star));
    winrt_ctrl::ColumnDefinition cGap;
    cGap.Width(winrt_wux::GridLengthHelper::FromPixels(8.0));
    winrt_ctrl::ColumnDefinition c1;
    c1.Width(winrt_wux::GridLengthHelper::FromValueAndType(1.0, winrt_wux::GridUnitType::Star));
    m_coreGraphsContainer.ColumnDefinitions().Append(c0);
    m_coreGraphsContainer.ColumnDefinitions().Append(cGap);
    m_coreGraphsContainer.ColumnDefinitions().Append(c1);

    std::vector<CoreCardVisual*> visible;
    for (auto& card : m_coreCards) {
        if (m_coreFilter == CoreFilterMode::PCores && card.kind != CoreKind::PCore) continue;
        if (m_coreFilter == CoreFilterMode::ECores && card.kind != CoreKind::ECore) continue;
        visible.push_back(&card);
    }

    const int rowCount = (static_cast<int>(visible.size()) + 1) / 2;
    for (int r = 0; r < rowCount; ++r) {
        winrt_ctrl::RowDefinition rd;
        rd.Height(winrt_wux::GridLengthHelper::Auto());
        m_coreGraphsContainer.RowDefinitions().Append(rd);
    }

    for (size_t idx = 0; idx < visible.size(); ++idx) {
        const int r = static_cast<int>(idx / 2);
        const int col = (idx % 2 == 0) ? 0 : 2;
        auto& border = visible[idx]->cardBorder;
        border.Margin(winrt_wux::ThicknessHelper::FromLengths(0, 0, 0, 6));
        winrt_ctrl::Grid::SetRow(border, r);
        winrt_ctrl::Grid::SetColumn(border, col);
        m_coreGraphsContainer.Children().Append(border);
    }

    SyncScrollThumb(m_coreGridScrollViewer, m_coreScrollTrack, m_coreScrollThumb, m_coreScrollThumbTransform);
}

void AppWindow::ApplyAmdBlockGuardIfNeeded() {
    if (m_cpuInfo.isIntel) {
        if (m_amdBlockOverlay) m_amdBlockOverlay.Visibility(winrt_wux::Visibility::Collapsed);
        return;
    }

    if (m_amdBlockVendorText) {
        wchar_t buf[128]{};
        std::swprintf(buf, 128, L"Vendor ID: %s (GenuineIntel Required)", m_cpuInfo.vendorId.c_str());
        m_amdBlockVendorText.Text(winrt::hstring(buf));
    }
    if (m_amdBlockCpuNameText) {
        std::wstring line = L"Detected Processor: " + m_cpuInfo.brandName;
        m_amdBlockCpuNameText.Text(winrt::hstring(line));
    }
    if (m_amdBlockOverlay) {
        m_amdBlockOverlay.Visibility(winrt_wux::Visibility::Visible);
    }

    if (m_customPlanSelectBtn) m_customPlanSelectBtn.IsEnabled(false);
    if (m_powerPlanPopupHost) m_powerPlanPopupHost.Visibility(winrt_wux::Visibility::Collapsed);
    if (m_runningProcPopupHost) m_runningProcPopupHost.Visibility(winrt_wux::Visibility::Collapsed);
    if (m_applyPresetsBtn) m_applyPresetsBtn.IsEnabled(false);
    if (m_restoreBtn) m_restoreBtn.IsEnabled(false);
    if (m_presetEcoBtn) m_presetEcoBtn.IsEnabled(false);
    if (m_presetBalancedBtn) m_presetBalancedBtn.IsEnabled(false);
    if (m_presetGamingBtn) m_presetGamingBtn.IsEnabled(false);
    if (m_presetMaxOcBtn) m_presetMaxOcBtn.IsEnabled(false);
    if (m_tglLiveApply) m_tglLiveApply.IsEnabled(false);
    if (m_tglApplyOnStart) m_tglApplyOnStart.IsEnabled(false);
    if (m_tglHighResTimer) m_tglHighResTimer.IsEnabled(false);
    if (m_tglAdvHighRes) m_tglAdvHighRes.IsEnabled(false);
    if (m_tglOsDirectHwp) m_tglOsDirectHwp.IsEnabled(false);
    if (m_tglOsThrottle) m_tglOsThrottle.IsEnabled(false);
    if (m_browseExeBtn) m_browseExeBtn.IsEnabled(false);
    if (m_pickRunningExeBtn) m_pickRunningExeBtn.IsEnabled(false);
    if (m_applyTargetsBtn) m_applyTargetsBtn.IsEnabled(false);
    if (m_targetPrio0Btn) m_targetPrio0Btn.IsEnabled(false);
    if (m_targetPrio1Btn) m_targetPrio1Btn.IsEnabled(false);
    if (m_targetPrio2Btn) m_targetPrio2Btn.IsEnabled(false);
    if (m_targetPrio3Btn) m_targetPrio3Btn.IsEnabled(false);
    if (m_targetCore0Btn) m_targetCore0Btn.IsEnabled(false);
    if (m_targetCore1Btn) m_targetCore1Btn.IsEnabled(false);
    if (m_targetCore2Btn) m_targetCore2Btn.IsEnabled(false);
    if (m_tglTargetDeepBoost) m_tglTargetDeepBoost.IsEnabled(false);
    if (m_tglTargetIfeo) m_tglTargetIfeo.IsEnabled(false);
    if (m_tglFgBoost) m_tglFgBoost.IsEnabled(false);
    SetActionStatus(L"Blocked: Non-Intel / AMD CPU Detected", true);
}

void AppWindow::HydrateSettingsAndTuningUi() {
    m_hydratingUi = true;

    if (m_tglAutoStart)      m_tglAutoStart.IsOn(m_config.autoStart);
    if (m_tglStartMinimized) m_tglStartMinimized.IsOn(m_config.startMinimized);
    if (m_tglMinToTray)      m_tglMinToTray.IsOn(m_config.minimizeToTray);
    if (m_tglCloseToTray)    m_tglCloseToTray.IsOn(m_config.closeToTray);
    if (m_tglAlwaysOnTop)    m_tglAlwaysOnTop.IsOn(m_config.alwaysOnTop);
    if (m_tglNotifications)  m_tglNotifications.IsOn(m_config.showNotifications);
    if (m_tglApplyOnStart)   m_tglApplyOnStart.IsOn(m_config.applyOnStartup);
    if (m_tglHighResTimer)   m_tglHighResTimer.IsOn(m_config.highResTimer);
    if (m_tglAdvHighRes)     m_tglAdvHighRes.IsOn(m_config.highResTimer);
    if (m_tglLiveApply)      m_tglLiveApply.IsOn(m_config.liveApply);

    if (m_sliderUnpark)   m_sliderUnpark.Value(std::clamp(static_cast<double>(m_config.unparkCoresPercent), 0.0, 100.0));
    if (m_sliderMinState) m_sliderMinState.Value(std::clamp(static_cast<double>(m_config.minStatePercent), 0.0, 100.0));
    if (m_sliderMaxState) m_sliderMaxState.Value(std::clamp(static_cast<double>(m_config.maxStatePercent), 0.0, 100.0));
    if (m_sliderBoostPol) m_sliderBoostPol.Value(std::clamp(static_cast<double>(m_config.turboBoostPolicy), 0.0, 100.0));
    if (m_sliderEpp)      m_sliderEpp.Value(std::clamp(static_cast<double>(m_config.eppPercent), 0.0, 100.0));
    if (m_sliderFreqCap)  m_sliderFreqCap.Value(std::clamp(static_cast<double>(m_config.freqCapMhz), 0.0, 6600.0));

    if (m_sliderIncThresh)   m_sliderIncThresh.Value(std::clamp(static_cast<double>(m_config.incThreshold), 0.0, 100.0));
    if (m_sliderDecThresh)   m_sliderDecThresh.Value(std::clamp(static_cast<double>(m_config.decThreshold), 0.0, 100.0));
    if (m_sliderHeteroFloor) m_sliderHeteroFloor.Value(std::clamp(static_cast<double>(m_config.heteroFloorPerf), 0.0, 100.0));
    if (m_sliderConcur)      m_sliderConcur.Value(std::clamp(static_cast<double>(m_config.concurrencyThreshold), 0.0, 100.0));

    if (m_tglActiveCooling)   m_tglActiveCooling.IsOn(m_config.activeCooling);
    if (m_tglOsDirectHwp)     m_tglOsDirectHwp.IsOn(!m_config.hwpAutonomous);
    if (m_tglIdleDisable)     m_tglIdleDisable.IsOn(m_config.disableIdleStates);
    if (m_tglOsThrottle)      m_tglOsThrottle.IsOn(m_config.disableOsThrottling);
    if (m_tglTargetDeepBoost) m_tglTargetDeepBoost.IsOn(m_config.targetDeepBoost);
    if (m_tglTargetIfeo)      m_tglTargetIfeo.IsOn(m_config.targetIfeoPersist);
    if (m_tglFgBoost)         m_tglFgBoost.IsOn(m_config.boostForegroundPCore);

    RebuildTargetExeListUi();
    UpdateSliderLabelsAndPills();
    m_hydratingUi = false;
}

void AppWindow::UpdateSliderLabelsAndPills() {
    auto activeBg   = winrt_med::SolidColorBrush(RgbColor(37, 99, 235));
    auto activeFg   = winrt_med::SolidColorBrush(RgbColor(255, 255, 255));
    auto inactiveBg = winrt_med::SolidColorBrush(winrt::Windows::UI::Colors::Transparent());
    auto inactiveFg = winrt_med::SolidColorBrush(RgbColor(71, 85, 105));

    const bool isPercentTray = (m_config.trayStyle == L"percent");
    if (m_trayModeLogoBtn && m_trayModePercentBtn) {
        m_trayModeLogoBtn.Background(isPercentTray ? inactiveBg : activeBg);
        m_trayModeLogoBtn.Foreground(isPercentTray ? inactiveFg : activeFg);
        m_trayModePercentBtn.Background(isPercentTray ? activeBg : inactiveBg);
        m_trayModePercentBtn.Foreground(isPercentTray ? activeFg : inactiveFg);
    }

    if (m_leftTabSlidersBtn && m_leftTabAdvBtn && m_leftTabTargetsBtn && m_leftTabCheckBtn) {
        m_leftTabSlidersBtn.Background(m_leftTab == LeftTab::Sliders ? activeBg : inactiveBg);
        m_leftTabSlidersBtn.Foreground(m_leftTab == LeftTab::Sliders ? activeFg : inactiveFg);
        m_leftTabAdvBtn.Background(m_leftTab == LeftTab::Advanced ? activeBg : inactiveBg);
        m_leftTabAdvBtn.Foreground(m_leftTab == LeftTab::Advanced ? activeFg : inactiveFg);
        m_leftTabTargetsBtn.Background(m_leftTab == LeftTab::Targets ? activeBg : inactiveBg);
        m_leftTabTargetsBtn.Foreground(m_leftTab == LeftTab::Targets ? activeFg : inactiveFg);
        m_leftTabCheckBtn.Background(m_leftTab == LeftTab::Compat ? activeBg : inactiveBg);
        m_leftTabCheckBtn.Foreground(m_leftTab == LeftTab::Compat ? activeFg : inactiveFg);
    }

    auto setTargetPill = [&](const winrt_ctrl::Button& b, bool on) {
        if (!b) return;
        b.Background(on ? activeBg : inactiveBg);
        b.Foreground(on ? activeFg : inactiveFg);
    };
    setTargetPill(m_targetPrio0Btn, m_config.targetPriorityMode == 0);
    setTargetPill(m_targetPrio1Btn, m_config.targetPriorityMode == 1);
    setTargetPill(m_targetPrio2Btn, m_config.targetPriorityMode == 2);
    setTargetPill(m_targetPrio3Btn, m_config.targetPriorityMode == 3);
    const int uiCoreMode = m_cpuInfo.isHybrid ? m_config.targetCoreMode : 0;
    setTargetPill(m_targetCore0Btn, uiCoreMode == 0);
    setTargetPill(m_targetCore1Btn, m_cpuInfo.isHybrid && uiCoreMode == 1);
    setTargetPill(m_targetCore2Btn, m_cpuInfo.isHybrid && uiCoreMode == 2);

    if (m_filterAllBtn && m_filterPCoreBtn && m_filterECoreBtn) {
        m_filterAllBtn.Background(m_coreFilter == CoreFilterMode::All ? activeBg : inactiveBg);
        m_filterAllBtn.Foreground(m_coreFilter == CoreFilterMode::All ? activeFg : inactiveFg);
        m_filterPCoreBtn.Background(m_coreFilter == CoreFilterMode::PCores ? activeBg : inactiveBg);
        m_filterPCoreBtn.Foreground(m_coreFilter == CoreFilterMode::PCores ? activeFg : inactiveFg);
        m_filterECoreBtn.Background(m_coreFilter == CoreFilterMode::ECores ? activeBg : inactiveBg);
        m_filterECoreBtn.Foreground(m_coreFilter == CoreFilterMode::ECores ? activeFg : inactiveFg);
    }

    wchar_t buf[32]{};
    if (m_sldUnparkVal) {
        std::swprintf(buf, 32, m_config.unparkCoresPercent >= 100 ? L"100%% Unpark" : L"%d%%", m_config.unparkCoresPercent);
        m_sldUnparkVal.Text(winrt::hstring(buf));
    }
    if (m_sldMinStateVal) {
        std::swprintf(buf, 32, L"%d%%", m_config.minStatePercent);
        m_sldMinStateVal.Text(winrt::hstring(buf));
    }
    if (m_sldMaxStateVal) {
        std::swprintf(buf, 32, L"%d%%", m_config.maxStatePercent);
        m_sldMaxStateVal.Text(winrt::hstring(buf));
    }
    if (m_sldBoostPolVal) {
        std::swprintf(buf, 32, L"%d%%", m_config.turboBoostPolicy);
        m_sldBoostPolVal.Text(winrt::hstring(buf));
    }
    if (m_sldEppVal) {
        std::swprintf(buf, 32, m_config.eppPercent == 0 ? L"0%% Max" : L"%d%%", m_config.eppPercent);
        m_sldEppVal.Text(winrt::hstring(buf));
    }
    if (m_sldFreqCapVal) {
        if (m_config.freqCapMhz <= 0) m_sldFreqCapVal.Text(L"Uncapped");
        else {
            std::swprintf(buf, 32, L"%d MHz", m_config.freqCapMhz);
            m_sldFreqCapVal.Text(winrt::hstring(buf));
        }
    }
    if (m_sldIncThreshVal) {
        std::swprintf(buf, 32, L"%d%%", m_config.incThreshold);
        m_sldIncThreshVal.Text(winrt::hstring(buf));
    }
    if (m_sldDecThreshVal) {
        std::swprintf(buf, 32, L"%d%%", m_config.decThreshold);
        m_sldDecThreshVal.Text(winrt::hstring(buf));
    }
    if (m_sldHeteroFloorVal) {
        std::swprintf(buf, 32, L"%d%%", m_config.heteroFloorPerf);
        m_sldHeteroFloorVal.Text(winrt::hstring(buf));
    }
    if (m_sldConcurVal) {
        std::swprintf(buf, 32, L"%d%%", m_config.concurrencyThreshold);
        m_sldConcurVal.Text(winrt::hstring(buf));
    }

    if (m_boostValueText) {
        m_boostValueText.Text(winrt::hstring(CpuIntelEngine::BoostModeToString(static_cast<uint32_t>(m_config.boostMode))));
    }
    if (m_schedValueText) {
        m_schedValueText.Text(winrt::hstring(CpuIntelEngine::SchedPolicyToString(static_cast<uint32_t>(m_config.schedPolicy))));
    }

    const bool isEco = (m_config.boostMode == 0 && m_config.eppPercent == 85 && m_config.maxStatePercent == 75);
    const bool isBalanced = (m_config.unparkCoresPercent == 100 && m_config.minStatePercent == 5 &&
                             m_config.turboBoostPolicy == 80 && m_config.eppPercent == 45);
    const bool isPerf = (m_config.unparkCoresPercent == 100 && m_config.minStatePercent == 5 &&
                         m_config.turboBoostPolicy == 100 && m_config.eppPercent == 10 && m_config.boostMode == 4);
    const bool isBrutal = (m_config.unparkCoresPercent == 100 && m_config.maxStatePercent == 100 &&
                           m_config.turboBoostPolicy == 100 && m_config.eppPercent == 0 &&
                           m_config.boostMode == 4 && m_config.disableOsThrottling);

    auto stylePreset = [&](const winrt_ctrl::Button& btn, bool selected) {
        if (!btn) return;
        btn.Background(winrt_med::SolidColorBrush(selected ? RgbColor(239, 246, 255) : RgbColor(255, 255, 255)));
        btn.BorderBrush(winrt_med::SolidColorBrush(selected ? RgbColor(37, 99, 235) : RgbColor(226, 232, 240)));
        btn.Foreground(winrt_med::SolidColorBrush(selected ? RgbColor(37, 99, 235) : RgbColor(9, 9, 11)));
    };
    stylePreset(m_presetEcoBtn, isEco);
    stylePreset(m_presetBalancedBtn, isBalanced);
    stylePreset(m_presetGamingBtn, isPerf);
    stylePreset(m_presetMaxOcBtn, isBrutal);
}

void AppWindow::ApplyQuickPreset(PresetMode preset) {
    if (!m_cpuInfo.isIntel) {
        SetActionStatus(L"Blocked: Non-Intel / AMD CPU", true);
        return;
    }

    switch (preset) {
    case PresetMode::Eco:
        m_config.unparkCoresPercent = 50;
        m_config.minStatePercent = 5;
        m_config.maxStatePercent = 75;
        m_config.turboBoostPolicy = 30;
        m_config.boostMode = 0;
        m_config.eppPercent = 85;
        m_config.freqCapMhz = 0;
        m_config.schedPolicy = 5;
        m_config.incThreshold = 60;
        m_config.decThreshold = 35;
        m_config.heteroFloorPerf = 20;
        m_config.concurrencyThreshold = 80;
        m_config.activeCooling = false;
        m_config.hwpAutonomous = true;
        m_config.disableIdleStates = false;
        m_config.disableOsThrottling = false;
        m_config.boostForegroundPCore = false;
        m_config.highResTimer = false;
        break;
    case PresetMode::Balanced:
        m_config.unparkCoresPercent = 100;
        m_config.minStatePercent = 5;
        m_config.maxStatePercent = 100;
        m_config.turboBoostPolicy = 80;
        m_config.boostMode = 4;
        m_config.eppPercent = 45;
        m_config.freqCapMhz = 0;
        m_config.schedPolicy = 5;
        m_config.incThreshold = 30;
        m_config.decThreshold = 15;
        m_config.heteroFloorPerf = 50;
        m_config.concurrencyThreshold = 50;
        m_config.activeCooling = true;
        m_config.hwpAutonomous = true;
        m_config.disableIdleStates = false;
        m_config.disableOsThrottling = false;
        m_config.boostForegroundPCore = false;
        break;
    case PresetMode::GamingOc:
        m_config.unparkCoresPercent = 100;
        m_config.minStatePercent = 5;
        m_config.maxStatePercent = 100;
        m_config.turboBoostPolicy = 100;
        m_config.boostMode = 4;
        m_config.eppPercent = 10;
        m_config.freqCapMhz = 0;
        m_config.schedPolicy = 5;
        m_config.incThreshold = 10;
        m_config.decThreshold = 5;
        m_config.heteroFloorPerf = 90;
        m_config.concurrencyThreshold = 25;
        m_config.activeCooling = true;
        m_config.hwpAutonomous = true;
        m_config.disableIdleStates = false;
        m_config.disableOsThrottling = true;
        m_config.boostForegroundPCore = true;
        m_config.highResTimer = true;
        break;
    case PresetMode::MaxIntelOc:
        m_config.unparkCoresPercent = 100;
        m_config.minStatePercent = 5;
        m_config.maxStatePercent = 100;
        m_config.turboBoostPolicy = 100;
        m_config.boostMode = 4;
        m_config.eppPercent = 0;
        m_config.freqCapMhz = 0;
        m_config.schedPolicy = 5;
        m_config.incThreshold = 5;
        m_config.decThreshold = 2;
        m_config.heteroFloorPerf = 100;
        m_config.concurrencyThreshold = 15;
        m_config.activeCooling = true;
        m_config.hwpAutonomous = true;
        m_config.disableIdleStates = false;
        m_config.disableOsThrottling = true;
        m_config.boostForegroundPCore = true;
        m_config.highResTimer = true;
        break;
    }

    HydrateSettingsAndTuningUi();
    SetActionStatus(L"Preset selected — click Apply Presets Settings", false);
}

void AppWindow::SaveAndApplySettings() {
    if (m_tglAutoStart)      m_config.autoStart         = m_tglAutoStart.IsOn();
    if (m_tglStartMinimized) m_config.startMinimized    = m_tglStartMinimized.IsOn();
    if (m_tglMinToTray)      m_config.minimizeToTray    = m_tglMinToTray.IsOn();
    if (m_tglCloseToTray)    m_config.closeToTray       = m_tglCloseToTray.IsOn();
    if (m_tglAlwaysOnTop)    m_config.alwaysOnTop       = m_tglAlwaysOnTop.IsOn();
    if (m_tglNotifications)  m_config.showNotifications = m_tglNotifications.IsOn();
    if (m_tglApplyOnStart)   m_config.applyOnStartup    = m_tglApplyOnStart.IsOn();
    if (m_tglHighResTimer)   m_config.highResTimer      = m_tglHighResTimer.IsOn();

    m_hydratingUi = true;
    if (m_tglAdvHighRes) m_tglAdvHighRes.IsOn(m_config.highResTimer);
    m_hydratingUi = false;

    ConfigManager::SetAutoStartup(m_config.autoStart, m_config.startMinimized);
    ConfigManager::Save(m_config);
    ApplyAlwaysOnTop();
    if (m_cpuInfo.isIntel) {
        m_engine.SetHighResTimer(m_config.highResTimer);
    }
    UpdateTrayIcon(m_lastSnap, true);
}

void AppWindow::SwitchLeftTab(LeftTab tab) {
    m_leftTab = tab;
    UpdateSliderLabelsAndPills();

    if (m_leftPageSliders) {
        m_leftPageSliders.Visibility(tab == LeftTab::Sliders ? winrt_wux::Visibility::Visible : winrt_wux::Visibility::Collapsed);
    }
    if (m_leftPageAdvanced) {
        m_leftPageAdvanced.Visibility(tab == LeftTab::Advanced ? winrt_wux::Visibility::Visible : winrt_wux::Visibility::Collapsed);
    }
    if (m_leftPageTargets) {
        m_leftPageTargets.Visibility(tab == LeftTab::Targets ? winrt_wux::Visibility::Visible : winrt_wux::Visibility::Collapsed);
    }
    if (m_leftPageCompat) {
        m_leftPageCompat.Visibility(tab == LeftTab::Compat ? winrt_wux::Visibility::Visible : winrt_wux::Visibility::Collapsed);
    }
    if (tab == LeftTab::Targets) {
        RebuildTargetExeListUi();
    } else if (m_runningProcPopupOpen) {
        ToggleRunningProcPopup(false);
    }
    SyncScrollThumb(m_compatScrollViewer, m_compatScrollTrack, m_compatScrollThumb, m_compatScrollThumbTransform);
    SafeBeginSb(m_leftTabSwitchSb);
}

void AppWindow::SwitchCoreFilter(CoreFilterMode filter) {
    m_coreFilter = filter;
    UpdateSliderLabelsAndPills();
    RebuildCoreCardsGrid();
    SafeBeginSb(m_coreFilterSlideSb);
}

void AppWindow::SwitchToSettings(bool showSettings) {
    if (m_showingSettings == showSettings) return;
    m_showingSettings = showSettings;
    TogglePowerPlanPopup(false);
    ToggleRunningProcPopup(false);

    if (showSettings) {
        if (m_mainView)     m_mainView.Visibility(winrt_wux::Visibility::Collapsed);
        if (m_settingsView) m_settingsView.Visibility(winrt_wux::Visibility::Visible);
        SafeBeginSb(m_openSettingsSb);
    } else {
        if (m_settingsView) m_settingsView.Visibility(winrt_wux::Visibility::Collapsed);
        if (m_mainView)     m_mainView.Visibility(winrt_wux::Visibility::Visible);
        SafeBeginSb(m_openMainSb);
    }
}

void AppWindow::StartWindowFade(FadeMode mode) {
    m_fadeMode = mode;
    if (mode == FadeMode::In) {
        const bool wasHidden = !IsWindowVisible(m_hwnd) || IsIconic(m_hwnd);
        if (wasHidden) {
            m_fadeProgress = 0.0;
            ApplyLayeredAlpha(0);
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
        }
        SetTimer(m_hwnd, IDT_WINDOW_FADE, kFadeTimerMs, nullptr);
    } else if (mode == FadeMode::OutHide || mode == FadeMode::OutMinimize || mode == FadeMode::OutClose) {
        if (!IsWindowVisible(m_hwnd)) {
            m_fadeMode = FadeMode::None;
            if (mode == FadeMode::OutClose) {
                m_closingForReal = true;
                DestroyWindow(m_hwnd);
            }
            return;
        }
        if (m_fadeProgress <= 0.0 || m_fadeProgress > 1.0) {
            m_fadeProgress = 1.0;
        }
        ApplyLayeredAlpha(static_cast<BYTE>(std::lround(m_fadeProgress * 255.0)));
        SetTimer(m_hwnd, IDT_WINDOW_FADE, kFadeTimerMs, nullptr);
    }
}

void AppWindow::ApplyLayeredAlpha(BYTE alpha) {
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_LAYERED) == 0) {
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    }
    SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA);
}

void AppWindow::ClearLayeredStyle() {
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_LAYERED) != 0) {
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    }
}

void AppWindow::ShowMainWindow() {
    StartWindowFade(FadeMode::In);
    if (!m_showingSettings) {
        SafeBeginSb(m_dashEntranceSb);
    }
}
void AppWindow::HideToTray() { StartWindowFade(FadeMode::OutHide); }
void AppWindow::MinimizeWindow() { StartWindowFade(FadeMode::OutMinimize); }

void AppWindow::SyncScrollThumb(
    const winrt_ctrl::ScrollViewer& sv,
    const winrt_ctrl::Grid& track,
    const winrt_ctrl::Border& thumb,
    const winrt_med::TranslateTransform& transform) {
    if (!sv || !track || !thumb || !transform) return;

    const double extent = sv.ExtentHeight();
    const double viewport = sv.ViewportHeight();
    const double trackH = track.ActualHeight();
    if (extent <= viewport || viewport <= 0.0 || trackH <= 0.0) {
        thumb.Visibility(winrt_wux::Visibility::Collapsed);
        return;
    }

    thumb.Visibility(winrt_wux::Visibility::Visible);
    const double thumbH = std::max(28.0, trackH * (viewport / extent));
    thumb.Height(thumbH);

    const double maxOffset = extent - viewport;
    const double ratio = (maxOffset > 0.0) ? std::clamp(sv.VerticalOffset() / maxOffset, 0.0, 1.0) : 0.0;
    transform.Y(ratio * (trackH - thumbH));
}

void AppWindow::RefreshCpuTelemetry() {
    CpuLiveSnapshot snap = m_engine.SampleLiveSnapshot();
    if (m_cpuInfo.isIntel && !m_boostPausedByRestore) {
        snap.activeBoostedProcess = m_engine.TickProcessBoosts(m_config, snap.activeTargetCount);
    }
    m_sessionPeakMhz = std::max(m_sessionPeakMhz, snap.peakCoreMhz);
    m_lastSnap = snap;

    if (m_kpiClockText) {
        m_kpiClockText.Text(winrt::hstring(FormatClockGhz(snap.packageMhz)));
    }
    if (m_kpiClockSubText) {
        wchar_t cbuf[64]{};
        if (m_cpuInfo.isHybrid) {
            std::swprintf(cbuf, 64, L"P %.2f · E %.2f",
                          static_cast<double>(snap.pCoreAvgMhz) / 1000.0,
                          static_cast<double>(snap.eCoreAvgMhz) / 1000.0);
        } else {
            std::swprintf(cbuf, 64, L"Base %.2f GHz",
                          static_cast<double>(m_cpuInfo.baseMhz) / 1000.0);
        }
        m_kpiClockSubText.Text(winrt::hstring(cbuf));
    }

    if (m_kpiPeakText && m_kpiPeakSubText) {
        m_kpiPeakText.Text(winrt::hstring(FormatClockGhz(snap.peakCoreMhz)));
        wchar_t psub[64]{};
        const double sessGhz = static_cast<double>(m_sessionPeakMhz) / 1000.0;
        if (m_cpuInfo.maxTurboMhz > 0) {
            std::swprintf(psub, 64, L"Max %.2f · Cap %.2f",
                          sessGhz, static_cast<double>(m_cpuInfo.maxTurboMhz) / 1000.0);
        } else {
            std::swprintf(psub, 64, L"Session Max %.2f GHz", sessGhz);
        }
        m_kpiPeakSubText.Text(winrt::hstring(psub));
    }

    if (m_kpiLoadText) {
        wchar_t buf[16]{};
        std::swprintf(buf, 16, L"%.0f%%", snap.totalUsagePercent);
        m_kpiLoadText.Text(winrt::hstring(buf));
    }
    if (m_kpiLoadSubText) {
        wchar_t buf[48]{};
        if (m_cpuInfo.isHybrid) {
            std::swprintf(buf, 48, L"P %.0f%% · E %.0f%%", snap.pCoreAvgUsage, snap.eCoreAvgUsage);
        } else {
            std::swprintf(buf, 48, L"%u Threads", m_cpuInfo.logicalThreads);
        }
        m_kpiLoadSubText.Text(winrt::hstring(buf));
    }

    if (m_kpiTempText) {
        if (snap.tempCelsius >= 0.0) {
            wchar_t tbuf[16]{};
            std::swprintf(tbuf, 16, L"%.0f°C", snap.tempCelsius);
            m_kpiTempText.Text(winrt::hstring(tbuf));
            m_kpiTempText.Foreground(winrt_med::SolidColorBrush(
                snap.tempCelsius >= 85.0 ? RgbColor(220, 38, 38) : RgbColor(37, 99, 235)
            ));
        } else {
            m_kpiTempText.Text(L"N/A");
            m_kpiTempText.Foreground(winrt_med::SolidColorBrush(RgbColor(71, 85, 105)));
        }
    }

    if (m_kpiBoostSubText) {
        wchar_t bbuf[64]{};
        if (!snap.activeBoostedProcess.empty()) {
            _snwprintf_s(bbuf, ARRAYSIZE(bbuf), _TRUNCATE, L"EPP %u%% · %s", snap.curEpp, snap.activeBoostedProcess.c_str());
        } else {
            _snwprintf_s(bbuf, ARRAYSIZE(bbuf), _TRUNCATE, L"EPP %u%% · %s", snap.curEpp,
                         CpuIntelEngine::BoostModeToString(snap.curBoostMode));
        }
        m_kpiBoostSubText.Text(winrt::hstring(bbuf));
    }

    if (m_selectedPlanLabelText && !snap.activePlanName.empty()) {
        m_selectedPlanLabelText.Text(winrt::hstring(snap.activePlanName));
    }

    const size_t n = std::min(m_coreCards.size(), snap.cores.size());
    for (size_t i = 0; i < n; ++i) {
        UpdateCoreCardVisual(m_coreCards[i], snap.cores[i]);
    }

    UpdateTrayIcon(snap, false);
}

void AppWindow::UpdateCoreCardVisual(CoreCardVisual& card, const CoreLiveSample& sample) {
    if (card.usageText) {
        wchar_t pbuf[16]{};
        std::swprintf(pbuf, 16, L"%.0f%%", sample.usagePercent);
        card.usageText.Text(winrt::hstring(pbuf));
    }
    if (card.clockText) {
        card.clockText.Text(winrt::hstring(FormatClockGhz(sample.currentMhz)));
    }

    card.history.push_back(std::clamp(sample.usagePercent, 0.0, 100.0));
    while (card.history.size() > kSparkHistoryLen) {
        card.history.pop_front();
    }

    if (card.sparkLine && card.sparkFill && card.history.size() >= 2) {
        winrt_med::PointCollection linePts;
        winrt_med::PointCollection fillPts;
        const size_t count = card.history.size();
        const float stepX = kSparkW / static_cast<float>(count - 1);

        fillPts.Append(winrt::Windows::Foundation::Point{0.0f, kSparkH});
        for (size_t i = 0; i < count; ++i) {
            const float x = static_cast<float>(i) * stepX;
            const float ratio = static_cast<float>(card.history[i] / 100.0);
            const float y = std::clamp(kSparkH - (ratio * (kSparkH - 3.0f)) - 1.0f, 1.0f, kSparkH);
            linePts.Append(winrt::Windows::Foundation::Point{x, y});
            fillPts.Append(winrt::Windows::Foundation::Point{x, y});
        }
        fillPts.Append(winrt::Windows::Foundation::Point{kSparkW, kSparkH});

        card.sparkLine.Points(linePts);
        card.sparkFill.Points(fillPts);
    }
}

void AppWindow::SetActionStatus(const std::wstring& text, bool isError) {
    if (!m_actionStatusText) return;
    m_actionStatusText.Text(winrt::hstring(text));
    m_actionStatusText.Foreground(winrt_med::SolidColorBrush(
        isError ? RgbColor(220, 38, 38) : RgbColor(37, 99, 235)
    ));
    SafeBeginSb(m_statusPopSb);
    SetTimer(m_hwnd, IDT_STATUS_RESET, kStatusResetMs, nullptr);
}

void AppWindow::ExecuteSwitchSelectedPlan(const std::wstring& guidStr) {
    if (!m_cpuInfo.isIntel || guidStr.empty()) return;
    KillTimer(m_hwnd, IDT_LIVE_APPLY);
    const TuneApplyResult res = m_engine.SwitchPowerPlan(guidStr, m_config);
    m_hydratingUi = true;
    HydrateSettingsAndTuningUi();
    m_hydratingUi = false;
    PopulateCustomPowerPlanMenu();
    SetActionStatus(res.message, !res.success);
    RefreshCpuTelemetry();
}

void AppWindow::ExecuteActivateVPowerPlan() {
    if (!m_cpuInfo.isIntel) {
        SetActionStatus(L"Blocked: Non-Intel / AMD CPU", true);
        return;
    }
    m_boostPausedByRestore = false;
    const TuneApplyResult res = m_engine.CreateOrActivateVPowerPlan(m_config, true);
    if (res.success) {
        ConfigManager::Save(m_config);
    }
    HydrateSettingsAndTuningUi();
    PopulateCustomPowerPlanMenu();
    SetActionStatus(res.message, !res.success);
    if (res.success && m_config.showNotifications && !IsWindowVisible(m_hwnd)) {
        ShowTrayBalloon(L"VPower Intel Beta", res.message.c_str(), false);
    }
    RefreshCpuTelemetry();
}

void AppWindow::ExecuteApplyTuneProfile(bool ensureVPower) {
    if (!m_cpuInfo.isIntel) {
        SetActionStatus(L"Blocked: Non-Intel / AMD CPU", true);
        return;
    }
    m_boostPausedByRestore = false;
    const TuneApplyResult res = m_engine.ApplyTuneProfile(m_config, ensureVPower);
    if (res.success) {
        if (!m_config.targetExes.empty() || m_config.boostForegroundPCore) {
            m_engine.ApplyTargetExeRulesNow(m_config);
        }
        ConfigManager::Save(m_config);
    }
    HydrateSettingsAndTuningUi();
    PopulateCustomPowerPlanMenu();
    SetActionStatus(res.message, !res.success);
    if (res.success && m_config.showNotifications && !IsWindowVisible(m_hwnd)) {
        ShowTrayBalloon(L"VOverclock Intel Beta", res.message.c_str(), false);
    }
    RefreshCpuTelemetry();
}

void AppWindow::ExecuteRestoreStock() {
    if (!m_cpuInfo.isIntel) {
        SetActionStatus(L"Blocked: Non-Intel / AMD CPU", true);
        return;
    }
    KillTimer(m_hwnd, IDT_LIVE_APPLY);
    m_boostPausedByRestore = true;
    const TuneApplyResult res = m_engine.RestoreStockPlan(m_config);
    if (res.success) {
        m_sessionPeakMhz = 0;
        ConfigManager::Save(m_config);
    }
    HydrateSettingsAndTuningUi();
    PopulateCustomPowerPlanMenu();
    SetActionStatus(res.message, !res.success);
    RefreshCpuTelemetry();
}

void AppWindow::ExecuteApplyTargetsNow() {
    if (!m_cpuInfo.isIntel) {
        SetActionStatus(L"Blocked: Non-Intel / AMD CPU", true);
        return;
    }
    m_boostPausedByRestore = false;
    const TuneApplyResult res = m_engine.ApplyTargetExeRulesNow(m_config);
    SetActionStatus(res.message, !res.success);
    RefreshCpuTelemetry();
}

void AppWindow::InitTrayIcon() {
    m_lastSnap = m_engine.SampleLiveSnapshot();

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY_ICON;

    const uint32_t pct = static_cast<uint32_t>(std::lround(m_lastSnap.totalUsagePercent));
    if (m_config.trayStyle == L"percent") {
        m_currentTrayIcon = m_trayRenderer.CreateCpuPercentIcon(pct, !m_cpuInfo.isIntel, m_dpi);
    } else {
        m_currentTrayIcon = m_trayRenderer.CreateLogoIcon(m_hInst, m_dpi);
    }
    nid.hIcon = m_currentTrayIcon ? m_currentTrayIcon : m_appIconSmall;
    wcscpy_s(nid.szTip, L"VOverclock Intel Beta");

    m_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    m_lastTrayPercent = pct;
    m_lastTrayStyle = m_config.trayStyle;
}

void AppWindow::UpdateTrayIcon(const CpuLiveSnapshot& snap, bool forceIconRecreate) {
    if (!m_trayAdded) return;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_TIP;

    const uint32_t pct = static_cast<uint32_t>(std::lround(snap.totalUsagePercent));
    if (!m_cpuInfo.isIntel) {
        wcsncpy_s(nid.szTip, L"VOverclock Intel Beta - AMD CPU Blocked", _TRUNCATE);
    } else {
        _snwprintf_s(nid.szTip, ARRAYSIZE(nid.szTip), _TRUNCATE,
                     L"VOverclock Intel - %u%% | %.2f GHz | %s",
                     pct, static_cast<double>(snap.packageMhz) / 1000.0,
                     snap.activePlanName.c_str());
    }

    const bool styleChanged = (m_lastTrayStyle != m_config.trayStyle);
    const bool percentChanged = (m_config.trayStyle == L"percent" && m_lastTrayPercent != pct);

    if (forceIconRecreate || styleChanged || percentChanged) {
        HICON nextIcon = (m_config.trayStyle == L"percent")
            ? m_trayRenderer.CreateCpuPercentIcon(pct, !m_cpuInfo.isIntel, m_dpi)
            : m_trayRenderer.CreateLogoIcon(m_hInst, m_dpi);

        if (nextIcon) {
            if (m_currentTrayIcon) DestroyIcon(m_currentTrayIcon);
            m_currentTrayIcon = nextIcon;
            nid.uFlags |= NIF_ICON;
            nid.hIcon = m_currentTrayIcon;
        }
        m_lastTrayPercent = pct;
        m_lastTrayStyle = m_config.trayStyle;
    }

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void AppWindow::RemoveTrayIcon() {
    if (!m_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_trayAdded = false;
}

void AppWindow::ShowTrayBalloon(const wchar_t* title, const wchar_t* body, bool errorIcon) {
    if (!m_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = (errorIcon ? NIIF_ERROR : NIIF_INFO) | NIIF_NOSOUND;
    wcsncpy_s(nid.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(nid.szInfo, body, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void AppWindow::ShowTrayContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    const bool visible = IsWindowVisible(m_hwnd) != FALSE;
    const UINT intelFlags = m_cpuInfo.isIntel ? MF_STRING : (MF_STRING | MF_GRAYED);

    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, visible ? L"Hide to Tray" : L"Show VOverclock Intel");
    AppendMenuW(hMenu, intelFlags, IDM_TRAY_VPOWER, L"Activate VPower Intel Beta");
    AppendMenuW(hMenu, intelFlags, IDM_TRAY_RESTORE, L"Restore Stock Power Plan");
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SETTINGS, L"Settings");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

}
