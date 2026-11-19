#include "AppWindow.hpp"
#include "VectorIcons.hpp"
#include "Win32Compat.hpp"
#include "../resources/resource.h"

#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwctype>

using Microsoft::WRL::ComPtr;

namespace vint {

namespace {

constexpr wchar_t kWindowClassName[] = L"VOverclockIntel_V1_HostWindow";
constexpr float kClientDipWidth  = 1120.0f;
constexpr float kClientDipHeight = 630.0f;
constexpr size_t kSparkHistoryLen = 30;

constexpr UINT_PTR IDT_POLL_CPU     = 1001;
constexpr UINT_PTR IDT_WINDOW_FADE  = 1002;
constexpr UINT_PTR IDT_STATUS_RESET = 1003;
constexpr UINT_PTR IDT_LIVE_APPLY   = 1004;
constexpr UINT_PTR IDT_UI_ANIM      = 1005;

constexpr UINT kPollIntervalMs  = 500;
constexpr UINT kFadeTimerMs     = 12;
constexpr UINT kStatusResetMs   = 4500;
constexpr UINT kLiveApplyMs     = 180;
constexpr UINT kUiAnimMs        = 16;

constexpr UINT kTrayIconId       = 100;
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

D2D1_COLOR_F D2dColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept {
    return D2D1_COLOR_F{
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f,
        static_cast<float>(a) / 255.0f
    };
}

// Color tokens matching AppLayout.xaml light high-contrast palette
const D2D1_COLOR_F kClrWhite       = D2dColor(255, 255, 255);
const D2D1_COLOR_F kClrBlue        = D2dColor(37, 99, 235);
const D2D1_COLOR_F kClrBlueHover   = D2dColor(29, 78, 216);
const D2D1_COLOR_F kClrBluePress   = D2dColor(30, 64, 175);
const D2D1_COLOR_F kClrBlueTint    = D2dColor(239, 246, 255);
const D2D1_COLOR_F kClrBlueBorder  = D2dColor(191, 219, 254);
const D2D1_COLOR_F kClrInk         = D2dColor(9, 9, 11);
const D2D1_COLOR_F kClrSlateDark   = D2dColor(15, 23, 42);
const D2D1_COLOR_F kClrSlate600    = D2dColor(71, 85, 105);
const D2D1_COLOR_F kClrSlate500    = D2dColor(100, 116, 139);
const D2D1_COLOR_F kClrSlate400    = D2dColor(148, 163, 184);
const D2D1_COLOR_F kClrSlate300    = D2dColor(203, 213, 225);
const D2D1_COLOR_F kClrBorder      = D2dColor(226, 232, 240);
const D2D1_COLOR_F kClrDivider     = D2dColor(241, 245, 249);
const D2D1_COLOR_F kClrSurface     = D2dColor(248, 250, 252);
const D2D1_COLOR_F kClrRed         = D2dColor(220, 38, 38);
const D2D1_COLOR_F kClrRedTint     = D2dColor(254, 242, 242);
const D2D1_COLOR_F kClrRedHover    = D2dColor(254, 226, 226);
const D2D1_COLOR_F kClrRedDark     = D2dColor(153, 27, 27);
const D2D1_COLOR_F kClrRedBorder   = D2dColor(239, 68, 68);
const D2D1_COLOR_F kClrAmberTint   = D2dColor(254, 243, 199);
const D2D1_COLOR_F kClrAmberText   = D2dColor(180, 83, 9);

bool PointInRectF(const D2D1_RECT_F& r, D2D1_POINT_2F p) noexcept {
    return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
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

void ApplyEllipsisNoWrap(IDWriteFactory* dw, IDWriteTextFormat* fmt) {
    if (!dw || !fmt) return;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<IDWriteInlineObject> sign;
    if (SUCCEEDED(dw->CreateEllipsisTrimmingSign(fmt, &sign))) {
        DWRITE_TRIMMING trim{};
        trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        fmt->SetTrimming(&trim, sign.Get());
    }
}

} // namespace

AppWindow::AppWindow(HINSTANCE hInst, bool startMinimized)
    : m_hInst(hInst)
    , m_startMinimized(startMinimized)
    , m_isElevated(CpuIntelEngine::IsElevated()) {
    m_config = ConfigManager::Load();
    m_engine.Initialize();
    m_cpuInfo = m_engine.GetHardwareInfo();
}

AppWindow::~AppWindow() {
    DiscardD2DDeviceResources();
    if (m_dibBmp) {
        if (m_memDc && m_oldBmp) SelectObject(m_memDc, m_oldBmp);
        DeleteObject(m_dibBmp);
        m_dibBmp = nullptr;
    }
    if (m_memDc) {
        DeleteDC(m_memDc);
        m_memDc = nullptr;
    }
    if (m_appIconLarge) DestroyIcon(m_appIconLarge);
    if (m_appIconSmall) DestroyIcon(m_appIconSmall);
    if (m_currentTrayIcon) DestroyIcon(m_currentTrayIcon);
}

bool AppWindow::Create() {
    if (!EnsureD2DFactoryResources()) {
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = m_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClassName;

    m_dpi = compat::GetDpiForSystemCompat();
    m_appIconLarge = static_cast<HICON>(LoadImageW(
        m_hInst, MAKEINTRESOURCEW(IDI_VO_APP), IMAGE_ICON,
        compat::GetSystemMetricsForDpiCompat(SM_CXICON, m_dpi),
        compat::GetSystemMetricsForDpiCompat(SM_CYICON, m_dpi),
        LR_DEFAULTCOLOR
    ));
    m_appIconSmall = static_cast<HICON>(LoadImageW(
        m_hInst, MAKEINTRESOURCEW(IDI_VO_APP), IMAGE_ICON,
        compat::GetSystemMetricsForDpiCompat(SM_CXSMICON, m_dpi),
        compat::GetSystemMetricsForDpiCompat(SM_CYSMICON, m_dpi),
        LR_DEFAULTCOLOR
    ));
    wc.hIcon = m_appIconLarge;
    wc.hIconSm = m_appIconSmall;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD exStyle = WS_EX_APPWINDOW;

    const int winW = compat::ScaleDip(kClientDipWidth, m_dpi);
    const int winH = compat::ScaleDip(kClientDipHeight, m_dpi);

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int posX = workArea.left + ((workArea.right - workArea.left) - winW) / 2;
    const int posY = workArea.top + ((workArea.bottom - workArea.top) - winH) / 2;

    m_hwnd = CreateWindowExW(
        exStyle,
        kWindowClassName,
        L"VOverclock Intel Beta Phase 2",
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
            L"VOverclock Intel Beta Phase 2 - Blocked",
            L"AMD / Non-Intel CPU detected. All overclocking and VPower features are disabled.",
            true
        );
        PostMessageW(m_hwnd, WM_APP_AMD_NOTIFY, 0, 0);
    }

    return true;
}

bool AppWindow::PreTranslateMessage(MSG* msg) {
    if (msg && msg->message == WM_KEYDOWN && msg->wParam == VK_ESCAPE) {
        if (m_powerPlanPopupOpen || m_runningProcPopupOpen) {
            OnKeyDown(msg->wParam);
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK AppWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<AppWindow*>(cs->lpCreateParams);
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
        if (wParam == TRUE) return 0;
        break;

    case WM_NCACTIVATE:
        return TRUE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_PRINTCLIENT:
        OnPrintClient(reinterpret_cast<HDC>(wParam));
        return 0;

    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(m_hwnd, &pt);
        const D2D1_POINT_2F dip = PtToDip(pt.x, pt.y);
        if (dip.y >= 0.0f && dip.y < 42.0f) {
            const D2D1_RECT_F minRc{1050.0f, 7.0f, 1078.0f, 35.0f};
            const D2D1_RECT_F closeRc{1082.0f, 7.0f, 1110.0f, 35.0f};
            if (!PointInRectF(minRc, dip) && !PointInRectF(closeRc, dip)) {
                return HTCAPTION;
            }
        }
        return HTCLIENT;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            const bool hand = (m_hoverId != ControlId::None && m_hoverId != ControlId::ModalBackdrop);
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_MOUSELEAVE:
        OnMouseLeave();
        return 0;

    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(m_hwnd, &pt);
        OnMouseWheel(pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    }

    case WM_KEYDOWN:
        OnKeyDown(wParam);
        return 0;

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
                L"\n\nVOverclock Intel Beta Phase 2 requires a GenuineIntel CPU. All tuning sliders, presets, and VPower Intel Beta features are blocked.";
            MessageBoxW(m_hwnd, msgBody.c_str(), L"VOverclock Intel Beta Phase 2 - Access Blocked", MB_OK | MB_ICONERROR);
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
    m_dpi = compat::GetDpiForWindowCompat(m_hwnd);
    if (m_dpi == 0) m_dpi = 96;

    LoadLogoBitmap();
    BuildCoreHistoryList();

    if (m_cpuInfo.isIntel) {
        if (m_config.highResTimer) {
            m_engine.SetHighResTimer(true);
        }
        if (m_config.applyOnStartup) {
            m_engine.CreateOrActivateVPowerPlan(m_config, true);
        } else {
            m_engine.SyncConfigFromActivePlan(m_config);
        }
    } else {
        SetActionStatus(L"Blocked: Non-Intel / AMD CPU Detected", true);
    }

    PopulatePowerPlansCache();
    InitTrayIcon();
    RefreshCpuTelemetry();
    SetTimer(m_hwnd, IDT_POLL_CPU, kPollIntervalMs, nullptr);
    EnsureDibSurface();
    InvalidateView();
}

void AppWindow::OnDestroy() {
    KillTimer(m_hwnd, IDT_POLL_CPU);
    KillTimer(m_hwnd, IDT_WINDOW_FADE);
    KillTimer(m_hwnd, IDT_STATUS_RESET);
    KillTimer(m_hwnd, IDT_LIVE_APPLY);
    KillTimer(m_hwnd, IDT_UI_ANIM);
    m_engine.SetHighResTimer(false);
    RemoveTrayIcon();
}

void AppWindow::OnSize(UINT, UINT) {
    DiscardD2DDeviceResources();
    EnsureDibSurface();
    InvalidateView();
}

void AppWindow::OnDpiChanged(UINT newDpi, const RECT* suggestedRect) {
    if (newDpi > 0) m_dpi = newDpi;
    DiscardD2DDeviceResources();
    if (suggestedRect) {
        const int winW = compat::ScaleDip(kClientDipWidth, m_dpi);
        const int winH = compat::ScaleDip(kClientDipHeight, m_dpi);
        SetWindowPos(m_hwnd, nullptr,
                     suggestedRect->left, suggestedRect->top,
                     winW, winH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    EnsureDibSurface();
    UpdateTrayIcon(m_lastSnap, true);
    InvalidateView();
}

void AppWindow::OnTimer(UINT_PTR timerId) {
    if (timerId == IDT_POLL_CPU) {
        RefreshCpuTelemetry();
    } else if (timerId == IDT_LIVE_APPLY) {
        KillTimer(m_hwnd, IDT_LIVE_APPLY);
        if (m_cpuInfo.isIntel && m_config.liveApply) {
            ExecuteApplyTuneProfile(true);
        }
    } else if (timerId == IDT_UI_ANIM) {
        const float gearTarget = (!m_showingSettings && IsControlHovered(ControlId::SettingsBtn)) ? 45.0f : 0.0f;
        const float chevTarget = m_powerPlanPopupOpen ? 180.0f : 0.0f;
        m_gearAngle += (gearTarget - m_gearAngle) * 0.28f;
        m_chevronAngle += (chevTarget - m_chevronAngle) * 0.30f;

        bool keepRunning = false;
        if (std::fabs(gearTarget - m_gearAngle) < 0.5f) m_gearAngle = gearTarget;
        else keepRunning = true;
        if (std::fabs(chevTarget - m_chevronAngle) < 0.5f) m_chevronAngle = chevTarget;
        else keepRunning = true;

        if (!keepRunning) {
            KillTimer(m_hwnd, IDT_UI_ANIM);
        }
        InvalidateView();
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
        if (!m_actionStatusText.empty()) {
            m_actionStatusText.clear();
            InvalidateView();
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

void AppWindow::InvalidateView() {
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

bool AppWindow::EnsureD2DFactoryResources() {
    if (!m_d2dFactory) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf()))) {
            return false;
        }
    }
    if (!m_dwFactory) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(m_dwFactory.GetAddressOf())))) {
            return false;
        }
    }
    if (!m_roundStroke && m_d2dFactory) {
        D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND,
            10.0f,
            D2D1_DASH_STYLE_SOLID,
            0.0f
        );
        m_d2dFactory->CreateStrokeStyle(&sp, nullptr, 0, m_roundStroke.GetAddressOf());
    }

    auto makeFmt = [&](ComPtr<IDWriteTextFormat>& out, float size, DWRITE_FONT_WEIGHT weight) -> bool {
        if (out) return true;
        if (FAILED(m_dwFactory->CreateTextFormat(
                L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                size, L"en-US", out.GetAddressOf()))) {
            return false;
        }
        ApplyEllipsisNoWrap(m_dwFactory.Get(), out.Get());
        return true;
    };

    return makeFmt(m_fmtReg9,    9.2f,  DWRITE_FONT_WEIGHT_REGULAR) &&
           makeFmt(m_fmtReg10,   10.0f, DWRITE_FONT_WEIGHT_REGULAR) &&
           makeFmt(m_fmtReg11,   11.0f, DWRITE_FONT_WEIGHT_REGULAR) &&
           makeFmt(m_fmtReg125,  12.5f, DWRITE_FONT_WEIGHT_REGULAR) &&
           makeFmt(m_fmtSemi10,  10.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           makeFmt(m_fmtSemi11,  11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           makeFmt(m_fmtSemi12,  11.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           makeFmt(m_fmtBold9,   9.0f,  DWRITE_FONT_WEIGHT_BOLD) &&
           makeFmt(m_fmtBold105, 10.5f, DWRITE_FONT_WEIGHT_BOLD) &&
           makeFmt(m_fmtBold115, 11.5f, DWRITE_FONT_WEIGHT_BOLD) &&
           makeFmt(m_fmtBold135, 13.5f, DWRITE_FONT_WEIGHT_BOLD) &&
           makeFmt(m_fmtBold145, 14.5f, DWRITE_FONT_WEIGHT_BOLD) &&
           makeFmt(m_fmtBold18,  18.0f, DWRITE_FONT_WEIGHT_BOLD);
}

void AppWindow::EnsureDibSurface() {
    if (!m_hwnd) return;
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int w = std::max(1, static_cast<int>(rc.right - rc.left));
    const int h = std::max(1, static_cast<int>(rc.bottom - rc.top));
    if (m_dibBmp && w == m_dibW && h == m_dibH) {
        return;
    }

    if (m_dibBmp) {
        if (m_memDc && m_oldBmp) SelectObject(m_memDc, m_oldBmp);
        DeleteObject(m_dibBmp);
        m_dibBmp = nullptr;
        m_oldBmp = nullptr;
    }
    if (!m_memDc) {
        m_memDc = CreateCompatibleDC(nullptr);
    }
    if (!m_memDc) return;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    m_dibBmp = CreateDIBSection(m_memDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (m_dibBmp) {
        m_oldBmp = static_cast<HBITMAP>(SelectObject(m_memDc, m_dibBmp));
        m_dibW = w;
        m_dibH = h;
    }
}

void AppWindow::DiscardD2DDeviceResources() {
    m_logoBitmap.Reset();
    m_brush.Reset();
    m_rt.Reset();
}

void AppWindow::LoadLogoBitmap() {
    if (!m_logoPbgra.empty()) return;
    const auto pngBytes = TrayIconRenderer::LoadLogoPngBytes(m_hInst);
    if (!pngBytes.empty()) {
        compat::DecodePngToPbgra(pngBytes.data(), pngBytes.size(), m_logoW, m_logoH, m_logoPbgra);
    }
}

void AppWindow::RenderToMemoryDc() {
    if (!EnsureD2DFactoryResources()) return;
    EnsureDibSurface();
    if (!m_memDc || !m_dibBmp) return;

    if (!m_rt) {
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            static_cast<float>(m_dpi),
            static_cast<float>(m_dpi)
        );
        if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, m_rt.GetAddressOf()))) {
            return;
        }
        m_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    }
    if (!m_brush && FAILED(m_rt->CreateSolidColorBrush(kClrInk, m_brush.GetAddressOf()))) {
        return;
    }
    if (!m_logoBitmap && !m_logoPbgra.empty() && m_logoW > 0 && m_logoH > 0) {
        const D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        m_rt->CreateBitmap(D2D1::SizeU(m_logoW, m_logoH), m_logoPbgra.data(),
                           m_logoW * 4, &bp, m_logoBitmap.GetAddressOf());
    }

    const RECT bindRc{0, 0, m_dibW, m_dibH};
    if (FAILED(m_rt->BindDC(m_memDc, &bindRc))) {
        DiscardD2DDeviceResources();
        return;
    }

    m_hitControls.clear();

    m_rt->BeginDraw();
    m_rt->Clear(kClrWhite);

    DrawTitleBar();
    if (m_showingSettings) {
        DrawSettingsView();
    } else {
        DrawMainView();
    }

    const HRESULT endHr = m_rt->EndDraw();
    if (endHr == D2DERR_RECREATE_TARGET) {
        DiscardD2DDeviceResources();
    }
}

void AppWindow::OnPaint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(m_hwnd, &ps);
    RenderToMemoryDc();
    if (hdc && m_memDc && m_dibBmp) {
        BitBlt(hdc, 0, 0, m_dibW, m_dibH, m_memDc, 0, 0, SRCCOPY);
    }
    EndPaint(m_hwnd, &ps);
}

void AppWindow::OnPrintClient(HDC hdc) {
    RenderToMemoryDc();
    if (hdc && m_memDc && m_dibBmp) {
        BitBlt(hdc, 0, 0, m_dibW, m_dibH, m_memDc, 0, 0, SRCCOPY);
    }
}

D2D1_POINT_2F AppWindow::PtToDip(int px, int py) const noexcept {
    const float scale = 96.0f / static_cast<float>(m_dpi > 0 ? m_dpi : 96);
    return D2D1::Point2F(static_cast<float>(px) * scale, static_cast<float>(py) * scale);
}

const AppWindow::HitControl* AppWindow::HitTestDip(D2D1_POINT_2F pt) const {
    for (auto it = m_hitControls.rbegin(); it != m_hitControls.rend(); ++it) {
        if (PointInRectF(it->rect, pt)) {
            return it->enabled ? &(*it) : nullptr;
        }
    }
    return nullptr;
}

bool AppWindow::IsControlHovered(ControlId id, int argInt, const std::wstring& argStr) const {
    return m_hoverId == id && m_hoverArgInt == argInt && m_hoverArgStr == argStr;
}

bool AppWindow::IsControlPressed(ControlId id, int argInt, const std::wstring& argStr) const {
    return m_pressId == id && m_pressArgInt == argInt && m_pressArgStr == argStr;
}

void AppWindow::RegisterHit(ControlId id, const D2D1_RECT_F& rc, bool enabled, int argInt, std::wstring argStr) {
    HitControl hc{};
    hc.id = id;
    hc.rect = rc;
    hc.enabled = enabled;
    hc.argInt = argInt;
    hc.argStr = std::move(argStr);
    m_hitControls.push_back(std::move(hc));
}

void AppWindow::OnMouseMove(int px, int py) {
    if (!m_mouseTracking) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hwnd;
        TrackMouseEvent(&tme);
        m_mouseTracking = true;
    }

    const D2D1_POINT_2F dip = PtToDip(px, py);

    if (m_draggingSliderId != ControlId::None) {
        for (const auto& hc : m_hitControls) {
            if (hc.id == m_draggingSliderId && hc.isSlider) {
                ApplySliderFromDipX(hc, dip.x);
                break;
            }
        }
        return;
    }

    if (m_draggingScrollId != ControlId::None) {
        const float dy = dip.y - m_scrollDragStartY;
        if (m_draggingScrollId == ControlId::CoreScrollThumb && m_coreMaxScroll > 0.0f) {
            const float trackH = std::max(40.0f, m_coreTrackRc.bottom - m_coreTrackRc.top);
            const float thumbH = std::max(28.0f, trackH * (m_coreViewportRc.bottom - m_coreViewportRc.top) /
                                                  ((m_coreViewportRc.bottom - m_coreViewportRc.top) + m_coreMaxScroll));
            const float travel = std::max(1.0f, trackH - thumbH);
            m_coreScroll = std::clamp(m_scrollDragStartOffset + (dy / travel) * m_coreMaxScroll, 0.0f, m_coreMaxScroll);
            InvalidateView();
        } else if (m_draggingScrollId == ControlId::CompatScrollThumb && m_compatMaxScroll > 0.0f) {
            const float trackH = std::max(40.0f, m_compatTrackRc.bottom - m_compatTrackRc.top);
            const float thumbH = std::max(28.0f, trackH * (m_compatViewportRc.bottom - m_compatViewportRc.top) /
                                                  ((m_compatViewportRc.bottom - m_compatViewportRc.top) + m_compatMaxScroll));
            const float travel = std::max(1.0f, trackH - thumbH);
            m_compatScroll = std::clamp(m_scrollDragStartOffset + (dy / travel) * m_compatMaxScroll, 0.0f, m_compatMaxScroll);
            InvalidateView();
        }
        return;
    }

    const HitControl* hit = HitTestDip(dip);
    const ControlId nextId = hit ? hit->id : ControlId::None;
    const int nextArgInt = hit ? hit->argInt : 0;
    const std::wstring nextArgStr = hit ? hit->argStr : L"";

    if (nextId != m_hoverId || nextArgInt != m_hoverArgInt || nextArgStr != m_hoverArgStr) {
        const bool gearHoverChanged =
            (m_hoverId == ControlId::SettingsBtn) != (nextId == ControlId::SettingsBtn);
        m_hoverId = nextId;
        m_hoverArgInt = nextArgInt;
        m_hoverArgStr = nextArgStr;
        if (gearHoverChanged && !m_showingSettings) {
            SetTimer(m_hwnd, IDT_UI_ANIM, kUiAnimMs, nullptr);
        }
        InvalidateView();
    }
}

void AppWindow::OnMouseLeave() {
    m_mouseTracking = false;
    if (m_hoverId != ControlId::None) {
        const bool wasGear = (m_hoverId == ControlId::SettingsBtn);
        m_hoverId = ControlId::None;
        m_hoverArgInt = 0;
        m_hoverArgStr.clear();
        if (wasGear && !m_showingSettings) {
            SetTimer(m_hwnd, IDT_UI_ANIM, kUiAnimMs, nullptr);
        }
        InvalidateView();
    }
}

void AppWindow::OnLButtonDown(int px, int py) {
    SetFocus(m_hwnd);
    const D2D1_POINT_2F dip = PtToDip(px, py);

    const HitControl* hit = HitTestDip(dip);
    if (!hit) {
        // Support dragging window via empty Settings subheader band
        if (m_showingSettings && dip.y >= 43.0f && dip.y <= 88.0f && dip.x > 60.0f) {
            ReleaseCapture();
            SendMessageW(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return;
    }

    m_pressId = hit->id;
    m_pressArgInt = hit->argInt;
    m_pressArgStr = hit->argStr;

    if (hit->isSlider) {
        SetCapture(m_hwnd);
        m_draggingSliderId = hit->id;
        ApplySliderFromDipX(*hit, dip.x);
        return;
    }

    if (hit->id == ControlId::CoreScrollThumb) {
        SetCapture(m_hwnd);
        m_draggingScrollId = ControlId::CoreScrollThumb;
        m_scrollDragStartY = dip.y;
        m_scrollDragStartOffset = m_coreScroll;
        InvalidateView();
        return;
    }

    if (hit->id == ControlId::CompatScrollThumb) {
        SetCapture(m_hwnd);
        m_draggingScrollId = ControlId::CompatScrollThumb;
        m_scrollDragStartY = dip.y;
        m_scrollDragStartOffset = m_compatScroll;
        InvalidateView();
        return;
    }

    InvalidateView();
}

void AppWindow::OnLButtonUp(int px, int py) {
    const D2D1_POINT_2F dip = PtToDip(px, py);

    if (m_draggingSliderId != ControlId::None || m_draggingScrollId != ControlId::None) {
        ReleaseCapture();
        const bool wasSlider = (m_draggingSliderId != ControlId::None);
        m_draggingSliderId = ControlId::None;
        m_draggingScrollId = ControlId::None;
        m_pressId = ControlId::None;
        m_pressArgInt = 0;
        m_pressArgStr.clear();
        if (wasSlider) {
            ConfigManager::Save(m_config);
        }
        InvalidateView();
        return;
    }

    const ControlId pressedId = m_pressId;
    const int pressedArgInt = m_pressArgInt;
    const std::wstring pressedArgStr = m_pressArgStr;
    m_pressId = ControlId::None;
    m_pressArgInt = 0;
    m_pressArgStr.clear();

    const HitControl* hit = HitTestDip(dip);
    if (hit && pressedId == hit->id && pressedArgInt == hit->argInt && pressedArgStr == hit->argStr) {
        ActivateControl(*hit);
    }
    InvalidateView();
}

void AppWindow::OnMouseWheel(int px, int py, short delta) {
    const D2D1_POINT_2F dip = PtToDip(px, py);
    const float step = -static_cast<float>(delta) / 120.0f * 36.0f;

    if (m_powerPlanPopupOpen && PointInRectF(m_planPopupRc, dip)) {
        m_planPopupScroll = std::clamp(m_planPopupScroll + step, 0.0f, m_planPopupMaxScroll);
        InvalidateView();
        return;
    }
    if (m_runningProcPopupOpen && PointInRectF(m_procPopupRc, dip)) {
        m_procPopupScroll = std::clamp(m_procPopupScroll + step, 0.0f, m_procPopupMaxScroll);
        InvalidateView();
        return;
    }
    if (m_showingSettings || !m_cpuInfo.isIntel) {
        return;
    }

    if (PointInRectF(m_coreViewportRc, dip) && m_coreMaxScroll > 0.0f) {
        m_coreScroll = std::clamp(m_coreScroll + step, 0.0f, m_coreMaxScroll);
        InvalidateView();
    } else if (m_leftTab == LeftTab::Compat && PointInRectF(m_compatViewportRc, dip) && m_compatMaxScroll > 0.0f) {
        m_compatScroll = std::clamp(m_compatScroll + step, 0.0f, m_compatMaxScroll);
        InvalidateView();
    } else if (m_leftTab == LeftTab::Targets && PointInRectF(m_targetViewportRc, dip) && m_targetMaxScroll > 0.0f) {
        m_targetScroll = std::clamp(m_targetScroll + step, 0.0f, m_targetMaxScroll);
        InvalidateView();
    }
}

void AppWindow::OnKeyDown(WPARAM vk) {
    if (vk == VK_ESCAPE) {
        if (m_powerPlanPopupOpen) TogglePowerPlanPopup(false);
        else if (m_runningProcPopupOpen) ToggleRunningProcPopup(false);
    }
}

void AppWindow::ApplySliderFromDipX(const HitControl& ctrl, float dipX) {
    if (!m_cpuInfo.isIntel) return;
    const float trackLeft = ctrl.rect.left + 7.0f;
    const float trackRight = ctrl.rect.right - 7.0f;
    const float span = std::max(1.0f, trackRight - trackLeft);
    const double t = std::clamp(static_cast<double>(dipX - trackLeft) / static_cast<double>(span), 0.0, 1.0);
    double raw = ctrl.sliderMin + t * (ctrl.sliderMax - ctrl.sliderMin);
    if (ctrl.sliderStep > 0.0) {
        raw = ctrl.sliderMin + std::round((raw - ctrl.sliderMin) / ctrl.sliderStep) * ctrl.sliderStep;
    }
    raw = std::clamp(raw, ctrl.sliderMin, ctrl.sliderMax);
    const int val = static_cast<int>(std::lround(raw));

    switch (ctrl.id) {
    case ControlId::SliderUnpark:
        m_config.unparkCoresPercent = val;
        break;
    case ControlId::SliderMinState:
        m_config.minStatePercent = std::min(val, m_config.maxStatePercent);
        break;
    case ControlId::SliderMaxState:
        m_config.maxStatePercent = val;
        if (m_config.minStatePercent > m_config.maxStatePercent) {
            m_config.minStatePercent = m_config.maxStatePercent;
        }
        break;
    case ControlId::SliderBoostPol:
        m_config.turboBoostPolicy = val;
        break;
    case ControlId::SliderEpp:
        m_config.eppPercent = val;
        break;
    case ControlId::SliderFreqCap:
        m_config.freqCapMhz = (val < 1000) ? 0 : val;
        break;
    case ControlId::SliderIncThresh:
        m_config.incThreshold = val;
        break;
    case ControlId::SliderDecThresh:
        m_config.decThreshold = val;
        break;
    case ControlId::SliderHeteroFloor:
        m_config.heteroFloorPerf = val;
        break;
    case ControlId::SliderConcur:
        m_config.concurrencyThreshold = val;
        break;
    default:
        return;
    }

    if (m_config.liveApply && m_cpuInfo.isIntel) {
        SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
    }
    InvalidateView();
}

void AppWindow::ActivateControl(const HitControl& ctrl) {
    switch (ctrl.id) {
    case ControlId::ModalBackdrop:
        if (m_powerPlanPopupOpen) TogglePowerPlanPopup(false);
        if (m_runningProcPopupOpen) ToggleRunningProcPopup(false);
        break;
    case ControlId::WinMinimize:
        if (m_config.minimizeToTray) HideToTray();
        else MinimizeWindow();
        break;
    case ControlId::WinClose:
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        break;
    case ControlId::PowerPlanBtn:
        if (m_runningProcPopupOpen) ToggleRunningProcPopup(false);
        TogglePowerPlanPopup(!m_powerPlanPopupOpen);
        break;
    case ControlId::PowerPlanItem:
        TogglePowerPlanPopup(false);
        ExecuteSwitchSelectedPlan(ctrl.argStr);
        break;
    case ControlId::LeftTabTuning:    SwitchLeftTab(LeftTab::Sliders); break;
    case ControlId::LeftTabPowerSet:  SwitchLeftTab(LeftTab::Advanced); break;
    case ControlId::LeftTabAppsTweak: SwitchLeftTab(LeftTab::Targets); break;
    case ControlId::LeftTabSupport:   SwitchLeftTab(LeftTab::Compat); break;
    case ControlId::ToggleLiveApply:
        m_config.liveApply = !m_config.liveApply;
        ConfigManager::Save(m_config);
        break;
    case ControlId::BoostPrev:
        m_config.boostMode = StepOption(kBoostModes, m_config.boostMode, -1);
        ConfigManager::Save(m_config);
        if (m_config.liveApply) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        break;
    case ControlId::BoostNext:
        m_config.boostMode = StepOption(kBoostModes, m_config.boostMode, +1);
        ConfigManager::Save(m_config);
        if (m_config.liveApply) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        break;
    case ControlId::SchedPrev:
        m_config.schedPolicy = StepOption(kSchedPolicies, m_config.schedPolicy, -1);
        ConfigManager::Save(m_config);
        if (m_config.liveApply) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        break;
    case ControlId::SchedNext:
        m_config.schedPolicy = StepOption(kSchedPolicies, m_config.schedPolicy, +1);
        ConfigManager::Save(m_config);
        if (m_config.liveApply) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        break;
    case ControlId::ToggleActiveCooling:
        m_config.activeCooling = !m_config.activeCooling;
        ConfigManager::Save(m_config);
        if (m_config.liveApply && m_cpuInfo.isIntel) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        break;
    case ControlId::ToggleOsDirectHwp:
        m_config.hwpAutonomous = !m_config.hwpAutonomous;
        ConfigManager::Save(m_config);
        if (m_config.liveApply && m_cpuInfo.isIntel) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        break;
    case ControlId::ToggleIdleDisable:
        m_config.disableIdleStates = !m_config.disableIdleStates;
        ConfigManager::Save(m_config);
        if (m_config.liveApply && m_cpuInfo.isIntel) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        break;
    case ControlId::ToggleOsThrottle:
        m_config.disableOsThrottling = !m_config.disableOsThrottling;
        ConfigManager::Save(m_config);
        if (m_config.liveApply && m_cpuInfo.isIntel) SetTimer(m_hwnd, IDT_LIVE_APPLY, kLiveApplyMs, nullptr);
        break;
    case ControlId::ToggleAdvHighRes:
    case ControlId::ToggleHighResTimer:
        m_config.highResTimer = !m_config.highResTimer;
        SaveAndApplySettings();
        break;
    case ControlId::BrowseExeBtn:
        BrowseAndAddTargetExe();
        break;
    case ControlId::RunningAppsBtn:
        ToggleRunningProcPopup(!m_runningProcPopupOpen);
        break;
    case ControlId::RunningProcItem:
        ToggleRunningProcPopup(false);
        AddTargetExeRule(ctrl.argStr);
        break;
    case ControlId::ApplyAppsBtn:
        ExecuteApplyTargetsNow();
        break;
    case ControlId::TargetRuleToggle:
        if (ctrl.argInt >= 0 && static_cast<size_t>(ctrl.argInt) < m_config.targetExes.size()) {
            auto& rule = m_config.targetExes[static_cast<size_t>(ctrl.argInt)];
            rule.enabled = !rule.enabled;
            const std::wstring exeLocal = rule.exeName;
            ConfigManager::Save(m_config);
            m_engine.InvalidateProcessBoostCache();
            if (!rule.enabled) {
                m_engine.ClearTargetExeRuleEffect(exeLocal);
                SetActionStatus(L"Restored process defaults: " + exeLocal, false);
            } else {
                ExecuteApplyTargetsNow();
            }
        }
        break;
    case ControlId::TargetRuleRemove:
        if (ctrl.argInt >= 0 && static_cast<size_t>(ctrl.argInt) < m_config.targetExes.size()) {
            const std::wstring exeLocal = m_config.targetExes[static_cast<size_t>(ctrl.argInt)].exeName;
            m_engine.ClearTargetExeRuleEffect(exeLocal);
            m_config.targetExes.erase(m_config.targetExes.begin() + ctrl.argInt);
            ConfigManager::Save(m_config);
            SetActionStatus(L"Removed target: " + exeLocal, false);
        }
        break;
    case ControlId::TargetPrio0:
    case ControlId::TargetPrio1:
    case ControlId::TargetPrio2:
    case ControlId::TargetPrio3:
        m_config.targetPriorityMode = ctrl.argInt;
        ConfigManager::Save(m_config);
        m_engine.InvalidateProcessBoostCache();
        if (m_cpuInfo.isIntel) ExecuteApplyTargetsNow();
        break;
    case ControlId::TargetCore0:
    case ControlId::TargetCore1:
    case ControlId::TargetCore2:
        m_config.targetCoreMode = m_cpuInfo.isHybrid ? ctrl.argInt : 0;
        ConfigManager::Save(m_config);
        m_engine.InvalidateProcessBoostCache();
        if (m_cpuInfo.isIntel) ExecuteApplyTargetsNow();
        break;
    case ControlId::ToggleTargetDeepBoost:
        m_config.targetDeepBoost = !m_config.targetDeepBoost;
        ConfigManager::Save(m_config);
        m_engine.InvalidateProcessBoostCache();
        if (m_cpuInfo.isIntel) ExecuteApplyTargetsNow();
        break;
    case ControlId::ToggleTargetIfeo:
        m_config.targetIfeoPersist = !m_config.targetIfeoPersist;
        ConfigManager::Save(m_config);
        m_engine.InvalidateProcessBoostCache();
        if (m_cpuInfo.isIntel) ExecuteApplyTargetsNow();
        break;
    case ControlId::ToggleFgBoost:
        m_config.boostForegroundPCore = !m_config.boostForegroundPCore;
        ConfigManager::Save(m_config);
        m_engine.InvalidateProcessBoostCache();
        if (m_cpuInfo.isIntel) ExecuteApplyTargetsNow();
        break;
    case ControlId::PresetEco:      ApplyQuickPreset(PresetMode::Eco); break;
    case ControlId::PresetBalanced: ApplyQuickPreset(PresetMode::Balanced); break;
    case ControlId::PresetPerf:     ApplyQuickPreset(PresetMode::GamingOc); break;
    case ControlId::PresetMaxOc:    ApplyQuickPreset(PresetMode::MaxIntelOc); break;
    case ControlId::ApplyPresetsBtn: ExecuteApplyTuneProfile(true); break;
    case ControlId::RestoreBtn:      ExecuteRestoreStock(); break;
    case ControlId::FilterAll:    SwitchCoreFilter(CoreFilterMode::All); break;
    case ControlId::FilterPCores: SwitchCoreFilter(CoreFilterMode::PCores); break;
    case ControlId::FilterECores: SwitchCoreFilter(CoreFilterMode::ECores); break;
    case ControlId::SettingsBtn:     SwitchToSettings(true); break;
    case ControlId::MinimizeTrayBtn: HideToTray(); break;
    case ControlId::SettingsBackBtn: SwitchToSettings(false); break;
    case ControlId::ToggleAutoStart:
        m_config.autoStart = !m_config.autoStart;
        SaveAndApplySettings();
        break;
    case ControlId::ToggleStartMinimized:
        m_config.startMinimized = !m_config.startMinimized;
        SaveAndApplySettings();
        break;
    case ControlId::ToggleMinToTray:
        m_config.minimizeToTray = !m_config.minimizeToTray;
        SaveAndApplySettings();
        break;
    case ControlId::ToggleCloseToTray:
        m_config.closeToTray = !m_config.closeToTray;
        SaveAndApplySettings();
        break;
    case ControlId::TrayModeLogo:
        m_config.trayStyle = L"logo";
        SaveAndApplySettings();
        break;
    case ControlId::TrayModePercent:
        m_config.trayStyle = L"percent";
        SaveAndApplySettings();
        break;
    case ControlId::ToggleAlwaysOnTop:
        m_config.alwaysOnTop = !m_config.alwaysOnTop;
        SaveAndApplySettings();
        break;
    case ControlId::ToggleNotifications:
        m_config.showNotifications = !m_config.showNotifications;
        SaveAndApplySettings();
        break;
    case ControlId::ToggleApplyOnStart:
        m_config.applyOnStartup = !m_config.applyOnStartup;
        SaveAndApplySettings();
        break;
    case ControlId::SettingsElevateBtn:
        if (!m_isElevated && CpuIntelEngine::RestartElevated(false)) {
            m_closingForReal = true;
            DestroyWindow(m_hwnd);
        }
        break;
    case ControlId::SupportDevBtn:
        ShellExecuteW(nullptr, L"open", L"https://sociabuzz.com/idnvinnmalay", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    default:
        break;
    }
}

void AppWindow::BuildCoreHistoryList() {
    m_coreHistories.clear();
    for (const auto& ct : m_cpuInfo.cores) {
        CoreHistory ch{};
        ch.coreIndex = ct.coreIndex;
        ch.kind = ct.kind;
        ch.typeLocalIndex = ct.typeLocalIndex;
        ch.history.assign(kSparkHistoryLen, 0.0);
        m_coreHistories.push_back(std::move(ch));
    }
}

void AppWindow::PopulatePowerPlansCache() {
    m_cachedPlans = m_engine.ListAllPowerPlans();
}

void AppWindow::RefreshCpuTelemetry() {
    CpuLiveSnapshot snap = m_engine.SampleLiveSnapshot();
    if (m_cpuInfo.isIntel && !m_boostPausedByRestore) {
        snap.activeBoostedProcess = m_engine.TickProcessBoosts(m_config, snap.activeTargetCount);
    }
    if (m_leftTab == LeftTab::Targets && !m_showingSettings) {
        m_cachedProcs = m_engine.ListRunningProcesses();
    }
    m_sessionPeakMhz = std::max(m_sessionPeakMhz, snap.peakCoreMhz);
    m_lastSnap = snap;

    const size_t n = std::min(m_coreHistories.size(), snap.cores.size());
    for (size_t i = 0; i < n; ++i) {
        auto& ch = m_coreHistories[i];
        ch.currentUsage = snap.cores[i].usagePercent;
        ch.currentMhz = snap.cores[i].currentMhz;
        ch.history.push_back(std::clamp(snap.cores[i].usagePercent, 0.0, 100.0));
        while (ch.history.size() > kSparkHistoryLen) {
            ch.history.pop_front();
        }
    }

    UpdateTrayIcon(snap, false);
    if (!m_showingSettings && IsWindowVisible(m_hwnd)) {
        InvalidateView();
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

    SetActionStatus(L"Preset selected — click Apply Presets Settings", false);
}

void AppWindow::SaveAndApplySettings() {
    ConfigManager::SetAutoStartup(m_config.autoStart, m_config.startMinimized);
    ConfigManager::Save(m_config);
    ApplyAlwaysOnTop();
    if (m_cpuInfo.isIntel) {
        m_engine.SetHighResTimer(m_config.highResTimer);
    }
    UpdateTrayIcon(m_lastSnap, true);
    InvalidateView();
}

void AppWindow::SetActionStatus(const std::wstring& text, bool isError) {
    m_actionStatusText = text;
    m_actionStatusError = isError;
    if (m_hwnd) {
        SetTimer(m_hwnd, IDT_STATUS_RESET, kStatusResetMs, nullptr);
    }
    InvalidateView();
}

void AppWindow::ExecuteSwitchSelectedPlan(const std::wstring& guidStr) {
    if (!m_cpuInfo.isIntel || guidStr.empty()) return;
    KillTimer(m_hwnd, IDT_LIVE_APPLY);
    const TuneApplyResult res = m_engine.SwitchPowerPlan(guidStr, m_config);
    PopulatePowerPlansCache();
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
    PopulatePowerPlansCache();
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
    PopulatePowerPlansCache();
    SetActionStatus(res.message, !res.success);
    if (res.success && m_config.showNotifications && !IsWindowVisible(m_hwnd)) {
        ShowTrayBalloon(L"VOverclock Intel Beta Phase 2", res.message.c_str(), false);
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
    PopulatePowerPlansCache();
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

void AppWindow::TogglePowerPlanPopup(bool open) {
    if (open && !m_cpuInfo.isIntel) return;
    if (open == m_powerPlanPopupOpen) return;
    m_powerPlanPopupOpen = open;
    if (open) {
        if (m_runningProcPopupOpen) ToggleRunningProcPopup(false);
        PopulatePowerPlansCache();
        m_planPopupScroll = 0.0f;
    }
    SetTimer(m_hwnd, IDT_UI_ANIM, kUiAnimMs, nullptr);
    InvalidateView();
}

void AppWindow::ToggleRunningProcPopup(bool open) {
    if (open && !m_cpuInfo.isIntel) return;
    if (open == m_runningProcPopupOpen) return;
    m_runningProcPopupOpen = open;
    if (open) {
        if (m_powerPlanPopupOpen) TogglePowerPlanPopup(false);
        m_cachedProcs = m_engine.ListRunningProcesses();
        m_procPopupScroll = 0.0f;
    }
    InvalidateView();
}

void AppWindow::SwitchLeftTab(LeftTab tab) {
    m_leftTab = tab;
    if (tab == LeftTab::Targets) {
        m_targetScroll = 0.0f;
        m_cachedProcs = m_engine.ListRunningProcesses();
    }
    if (tab == LeftTab::Compat)  m_compatScroll = 0.0f;
    InvalidateView();
}

void AppWindow::SwitchCoreFilter(CoreFilterMode filter) {
    m_coreFilter = filter;
    m_coreScroll = 0.0f;
    InvalidateView();
}

void AppWindow::SwitchToSettings(bool showSettings) {
    if (m_showingSettings == showSettings) return;
    TogglePowerPlanPopup(false);
    ToggleRunningProcPopup(false);
    m_showingSettings = showSettings;
    if (showSettings) {
        KillTimer(m_hwnd, IDT_UI_ANIM);
        m_gearAngle = 0.0f;
    }
    InvalidateView();
}

void AppWindow::StartWindowFade(FadeMode mode) {
    if (!m_hwnd) return;
    m_fadeMode = mode;

    LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_LAYERED)) {
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    }

    if (mode == FadeMode::In) {
        m_fadeProgress = 0.0;
        ApplyLayeredAlpha(0);
        if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
    } else {
        m_fadeProgress = 1.0;
    }

    SetTimer(m_hwnd, IDT_WINDOW_FADE, kFadeTimerMs, nullptr);
}

void AppWindow::ApplyLayeredAlpha(BYTE alpha) {
    SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA);
}

void AppWindow::ClearLayeredStyle() {
    LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED) {
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
    }
}

void AppWindow::ShowMainWindow() {
    StartWindowFade(FadeMode::In);
}

void AppWindow::HideToTray() { StartWindowFade(FadeMode::OutHide); }
void AppWindow::MinimizeWindow() { StartWindowFade(FadeMode::OutMinimize); }

// --- Direct2D / DirectWrite Drawing Routines (1120 x 630 DIP unified coordinate system) ---

void AppWindow::DrawTextStrict(const std::wstring& text,
                               IDWriteTextFormat* fmt,
                               const D2D1_RECT_F& rc,
                               D2D1_COLOR_F color,
                               DWRITE_TEXT_ALIGNMENT hAlign,
                               DWRITE_PARAGRAPH_ALIGNMENT vAlign) {
    if (!m_rt || !m_brush || !fmt || text.empty()) return;
    fmt->SetTextAlignment(hAlign);
    fmt->SetParagraphAlignment(vAlign);
    m_brush->SetColor(color);
    m_rt->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt, rc, m_brush.Get());
}

float AppWindow::MeasureTextWidth(const std::wstring& text, IDWriteTextFormat* fmt) {
    if (!m_dwFactory || !fmt || text.empty()) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_dwFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                             fmt, 2000.0f, 100.0f, layout.GetAddressOf()))) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS tm{};
    if (FAILED(layout->GetMetrics(&tm))) return 0.0f;
    return tm.widthIncludingTrailingWhitespace;
}

void AppWindow::DrawRoundedBox(const D2D1_RECT_F& rc,
                               float radius,
                               D2D1_COLOR_F fill,
                               D2D1_COLOR_F border,
                               float borderW) {
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rc, radius, radius);
    if (fill.a > 0.0f) {
        m_brush->SetColor(fill);
        m_rt->FillRoundedRectangle(rr, m_brush.Get());
    }
    if (border.a > 0.0f && borderW > 0.0f) {
        m_brush->SetColor(border);
        m_rt->DrawRoundedRectangle(rr, m_brush.Get(), borderW);
    }
}

void AppWindow::DrawButton(ControlId id,
                           const D2D1_RECT_F& rc,
                           const std::wstring& text,
                           bool primaryBlue,
                           bool selectedBlueTint,
                           bool enabled,
                           float fontSize,
                           bool bold,
                           float radius,
                           int argInt,
                           std::wstring argStr) {
    RegisterHit(id, rc, enabled, argInt, argStr);
    const bool hov = enabled && IsControlHovered(id, argInt, argStr);
    const bool prs = enabled && IsControlPressed(id, argInt, argStr);

    D2D1_COLOR_F fill = kClrWhite;
    D2D1_COLOR_F border = kClrSlate300;
    D2D1_COLOR_F fg = kClrInk;

    if (primaryBlue) {
        fill = !enabled ? kClrSlate300 : (prs ? kClrBluePress : (hov ? kClrBlueHover : kClrBlue));
        border = fill;
        fg = kClrWhite;
    } else if (selectedBlueTint) {
        fill = prs ? D2dColor(219, 234, 254) : (hov ? D2dColor(232, 242, 255) : kClrBlueTint);
        border = kClrBlue;
        fg = kClrBlue;
    } else {
        fill = prs ? kClrDivider : (hov ? kClrSurface : kClrWhite);
        border = hov ? kClrSlate400 : kClrBorder;
        fg = enabled ? kClrInk : kClrSlate400;
    }

    DrawRoundedBox(rc, radius, fill, border, 1.0f);

    IDWriteTextFormat* fmt = m_fmtSemi11.Get();
    if (fontSize <= 10.2f) fmt = bold ? m_fmtBold9.Get() : m_fmtSemi10.Get();
    else if (fontSize <= 10.8f) fmt = bold ? m_fmtBold105.Get() : m_fmtSemi11.Get();
    else fmt = bold ? m_fmtBold115.Get() : m_fmtSemi11.Get();

    DrawTextStrict(text, fmt, rc, fg, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void AppWindow::DrawToggleSwitch(ControlId id,
                                 float rightX,
                                 float centerY,
                                 bool isOn,
                                 bool enabled,
                                 int argInt,
                                 std::wstring argStr) {
    const D2D1_RECT_F rc{rightX - 40.0f, centerY - 11.0f, rightX, centerY + 11.0f};
    RegisterHit(id, rc, enabled, argInt, argStr);

    const bool hov = enabled && IsControlHovered(id, argInt, argStr);
    const bool prs = enabled && IsControlPressed(id, argInt, argStr);

    D2D1_COLOR_F trackBg = isOn
        ? (enabled ? (prs ? kClrBluePress : (hov ? kClrBlueHover : kClrBlue)) : D2dColor(147, 197, 253))
        : (enabled ? (hov ? kClrSlate300 : kClrBorder) : kClrDivider);
    D2D1_COLOR_F trackBorder = isOn ? trackBg : (enabled ? kClrSlate300 : kClrBorder);

    DrawRoundedBox(rc, 11.0f, trackBg, trackBorder, 1.0f);

    const float knobX = isOn ? (rc.right - 11.0f) : (rc.left + 11.0f);
    const float knobR = isOn ? 8.0f : 7.0f;
    m_brush->SetColor(isOn ? kClrWhite : (enabled ? kClrInk : kClrSlate400));
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, centerY), knobR, knobR), m_brush.Get());
}

void AppWindow::DrawLabeledSlider(ControlId id,
                                  float leftX,
                                  float rightX,
                                  float topY,
                                  const std::wstring& label,
                                  const std::wstring& valText,
                                  double val,
                                  double minVal,
                                  double maxVal,
                                  double stepVal,
                                  bool enabled) {
    const D2D1_RECT_F labelRc{leftX, topY, rightX - 96.0f, topY + 14.0f};
    const D2D1_RECT_F valRc{rightX - 96.0f, topY, rightX, topY + 14.0f};
    DrawTextStrict(label, m_fmtSemi11.Get(), labelRc, enabled ? kClrInk : kClrSlate400);
    DrawTextStrict(valText, m_fmtBold105.Get(), valRc, enabled ? kClrBlue : kClrSlate400,
                   DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const D2D1_RECT_F sliderRc{leftX, topY + 14.0f, rightX, topY + 32.0f};
    HitControl hc{};
    hc.id = id;
    hc.rect = sliderRc;
    hc.enabled = enabled;
    hc.isSlider = true;
    hc.sliderMin = minVal;
    hc.sliderMax = maxVal;
    hc.sliderStep = stepVal;
    m_hitControls.push_back(hc);

    const float tLeft = leftX + 7.0f;
    const float tRight = rightX - 7.0f;
    const float cy = topY + 23.0f;
    const double span = std::max(1.0, maxVal - minVal);
    const double ratio = std::clamp((val - minVal) / span, 0.0, 1.0);
    const float thumbX = tLeft + static_cast<float>(ratio) * (tRight - tLeft);

    DrawRoundedBox(D2D1::RectF(tLeft, cy - 2.0f, tRight, cy + 2.0f), 2.0f,
                   kClrBorder, D2dColor(0, 0, 0, 0), 0.0f);
    if (thumbX > tLeft + 0.5f) {
        DrawRoundedBox(D2D1::RectF(tLeft, cy - 2.0f, thumbX, cy + 2.0f), 2.0f,
                       enabled ? kClrBlue : kClrSlate300, D2dColor(0, 0, 0, 0), 0.0f);
    }

    const bool hov = enabled && (IsControlHovered(id) || m_draggingSliderId == id);
    const float r = hov ? 7.6f : 7.0f;
    m_brush->SetColor(kClrWhite);
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy), r, r), m_brush.Get());
    m_brush->SetColor(enabled ? (hov ? kClrBlueHover : kClrBlue) : kClrSlate400);
    m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy), r, r), m_brush.Get(), 2.2f);
}

void AppWindow::DrawScrollbar(ControlId thumbId,
                              const D2D1_RECT_F& trackRc,
                              float contentH,
                              float viewH,
                              float scrollOffset) {
    if (contentH <= viewH + 1.0f || viewH <= 0.0f) return;
    const float trackH = trackRc.bottom - trackRc.top;
    const float thumbH = std::max(28.0f, trackH * (viewH / contentH));
    const float maxScroll = std::max(1.0f, contentH - viewH);
    const float maxThumbOffset = std::max(0.0f, trackH - thumbH);
    const float thumbY = trackRc.top + std::clamp(scrollOffset / maxScroll, 0.0f, 1.0f) * maxThumbOffset;

    const D2D1_RECT_F thumbRc{trackRc.left, thumbY, trackRc.right, thumbY + thumbH};
    const D2D1_RECT_F hitRc{trackRc.left - 4.0f, trackRc.top, trackRc.right + 4.0f, trackRc.bottom};
    RegisterHit(thumbId, hitRc, true);

    const bool active = IsControlHovered(thumbId) || (m_draggingScrollId == thumbId);
    DrawRoundedBox(thumbRc, 2.0f, active ? kClrSlate400 : kClrSlate300, D2dColor(0, 0, 0, 0), 0.0f);
}

void AppWindow::DrawTitleBar() {
    float titleX = 14.0f;
    if (m_logoBitmap && m_logoW > 0 && m_logoH > 0) {
        const float aspect = static_cast<float>(m_logoW) / static_cast<float>(m_logoH);
        const float drawH = 22.0f;
        const float drawW = std::min(40.0f, drawH * aspect);
        const float actualH = drawW / aspect;
        const float top = 10.0f + (22.0f - actualH) * 0.5f;
        m_rt->DrawBitmap(m_logoBitmap.Get(), D2D1::RectF(14.0f, top, 14.0f + drawW, top + actualH));
        titleX = 14.0f + drawW + 8.0f;
    }

    DrawTextStrict(L"VOverclock Intel", m_fmtBold135.Get(),
                   D2D1::RectF(titleX, 0.0f, 500.0f, 42.0f), kClrInk);

    // Minimize & Close caption buttons (top right)
    const D2D1_RECT_F minRc{1050.0f, 7.0f, 1078.0f, 35.0f};
    RegisterHit(ControlId::WinMinimize, minRc, true);
    if (IsControlHovered(ControlId::WinMinimize) || IsControlPressed(ControlId::WinMinimize)) {
        DrawRoundedBox(minRc, 6.0f,
                       IsControlPressed(ControlId::WinMinimize) ? kClrBorder : kClrDivider,
                       D2dColor(0, 0, 0, 0), 0.0f);
    }
    m_brush->SetColor(kClrSlateDark);
    icons::DrawIcon(m_rt.Get(), m_d2dFactory.Get(), m_brush.Get(), m_roundStroke.Get(),
                    icons::IconKind::Minimize, D2D1::RectF(1059.0f, 16.0f, 1069.0f, 26.0f), 1.6f);

    const D2D1_RECT_F closeRc{1082.0f, 7.0f, 1110.0f, 35.0f};
    RegisterHit(ControlId::WinClose, closeRc, true);
    const bool closeHov = IsControlHovered(ControlId::WinClose);
    const bool closePrs = IsControlPressed(ControlId::WinClose);
    if (closeHov || closePrs) {
        DrawRoundedBox(closeRc, 6.0f,
                       closePrs ? D2dColor(254, 202, 202) : kClrRedHover,
                       D2dColor(0, 0, 0, 0), 0.0f);
    }
    m_brush->SetColor((closeHov || closePrs) ? kClrRed : kClrSlateDark);
    icons::DrawIcon(m_rt.Get(), m_d2dFactory.Get(), m_brush.Get(), m_roundStroke.Get(),
                    icons::IconKind::Close, D2D1::RectF(1091.0f, 16.0f, 1101.0f, 26.0f), 1.6f);

    m_brush->SetColor(kClrBorder);
    m_rt->DrawLine(D2D1::Point2F(0.0f, 42.5f), D2D1::Point2F(kClientDipWidth, 42.5f), m_brush.Get(), 1.0f);
}

void AppWindow::DrawMainView() {
    DrawTopControlStrip();
    DrawKpiStrip();
    DrawLeftPane();
    DrawRightCoresPane();
    DrawAmdOverlay();
    DrawBottomBar();
    DrawPopups();
}

void AppWindow::DrawTopControlStrip() {
    DrawTextStrict(m_cpuInfo.brandName, m_fmtBold145.Get(),
                   D2D1::RectF(16.0f, 52.0f, 770.0f, 72.0f), kClrInk);

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
    DrawTextStrict(spec, m_fmtReg11.Get(), D2D1::RectF(16.0f, 72.0f, 770.0f, 88.0f), kClrSlate500);

    DrawTextStrict(L"Power Plan", m_fmtSemi11.Get(),
                   D2D1::RectF(785.0f, 56.0f, 876.0f, 84.0f), kClrSlate500,
                   DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const D2D1_RECT_F planBtnRc{884.0f, 56.0f, 1104.0f, 84.0f};
    const bool planEnabled = m_cpuInfo.isIntel;
    RegisterHit(ControlId::PowerPlanBtn, planBtnRc, planEnabled);
    const bool hov = planEnabled && (IsControlHovered(ControlId::PowerPlanBtn) || m_powerPlanPopupOpen);

    DrawRoundedBox(planBtnRc, 6.0f,
                   hov ? kClrBlueTint : kClrWhite,
                   hov ? kClrBlue : kClrSlate300, 1.0f);

    std::wstring activePlan = !m_lastSnap.activePlanName.empty() ? m_lastSnap.activePlanName : L"Balanced";
    DrawTextStrict(activePlan, m_fmtSemi11.Get(),
                   D2D1::RectF(planBtnRc.left + 10.0f, planBtnRc.top, planBtnRc.right - 26.0f, planBtnRc.bottom),
                   planEnabled ? kClrInk : kClrSlate400);

    m_brush->SetColor(kClrSlate500);
    icons::DrawIcon(m_rt.Get(), m_d2dFactory.Get(), m_brush.Get(), m_roundStroke.Get(),
                    icons::IconKind::ChevronDown,
                    D2D1::RectF(planBtnRc.right - 21.0f, 65.0f, planBtnRc.right - 11.0f, 75.0f),
                    1.8f, m_chevronAngle);
}

void AppWindow::DrawKpiStrip() {
    const D2D1_RECT_F stripRc{16.0f, 97.0f, 1104.0f, 155.0f};
    DrawRoundedBox(stripRc, 10.0f, kClrWhite, kClrBorder, 1.0f);

    const float colW = (1080.0f - 3.0f) / 4.0f;
    for (int i = 1; i <= 3; ++i) {
        const float lx = 20.0f + colW * i + (i - 1) + 0.5f;
        m_brush->SetColor(kClrDivider);
        m_rt->DrawLine(D2D1::Point2F(lx, 105.0f), D2D1::Point2F(lx, 147.0f), m_brush.Get(), 1.0f);
    }

    auto drawCol = [&](int idx, const wchar_t* title, const std::wstring& mainVal, D2D1_COLOR_F mainClr,
                       const std::wstring& subVal, float subWidth) {
        const float left = 20.0f + idx * (colW + 1.0f) + 12.0f;
        const float right = 20.0f + (idx + 1) * colW + idx - 12.0f;
        DrawTextStrict(title, m_fmtBold9.Get(), D2D1::RectF(left, 104.0f, right, 117.0f), kClrSlate500);
        DrawTextStrict(mainVal, m_fmtBold18.Get(), D2D1::RectF(left, 118.0f, right - subWidth - 4.0f, 146.0f), mainClr);
        DrawTextStrict(subVal, m_fmtSemi10.Get(), D2D1::RectF(right - subWidth, 126.0f, right, 144.0f), kClrSlate600,
                       DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    wchar_t cSub[64]{};
    if (m_cpuInfo.isHybrid) {
        std::swprintf(cSub, 64, L"P %.2f · E %.2f",
                      static_cast<double>(m_lastSnap.pCoreAvgMhz) / 1000.0,
                      static_cast<double>(m_lastSnap.eCoreAvgMhz) / 1000.0);
    } else {
        std::swprintf(cSub, 64, L"Base %.2f GHz", static_cast<double>(m_cpuInfo.baseMhz) / 1000.0);
    }
    drawCol(0, L"AVG CLOCK", FormatClockGhz(m_lastSnap.packageMhz), kClrBlue, cSub, 110.0f);

    wchar_t pSub[64]{};
    const double sessGhz = static_cast<double>(m_sessionPeakMhz) / 1000.0;
    if (m_cpuInfo.maxTurboMhz > 0) {
        std::swprintf(pSub, 64, L"Max %.2f · Cap %.2f", sessGhz, static_cast<double>(m_cpuInfo.maxTurboMhz) / 1000.0);
    } else {
        std::swprintf(pSub, 64, L"Session %.2f GHz", sessGhz);
    }
    drawCol(1, L"TURBO PEAK", FormatClockGhz(m_lastSnap.peakCoreMhz), kClrInk, pSub, 118.0f);

    wchar_t lPct[16]{}, lSub[48]{};
    std::swprintf(lPct, 16, L"%.0f%%", m_lastSnap.totalUsagePercent);
    if (m_cpuInfo.isHybrid) {
        std::swprintf(lSub, 48, L"P %.0f%% · E %.0f%%", m_lastSnap.pCoreAvgUsage, m_lastSnap.eCoreAvgUsage);
    } else {
        std::swprintf(lSub, 48, L"%u Threads", m_cpuInfo.logicalThreads);
    }
    drawCol(2, L"CPU LOAD", lPct, kClrInk, lSub, 115.0f);

    std::wstring tStr = L"N/A";
    D2D1_COLOR_F tClr = kClrSlate600;
    if (m_lastSnap.tempCelsius >= 0.0) {
        wchar_t tBuf[16]{};
        std::swprintf(tBuf, 16, L"%.0f°C", m_lastSnap.tempCelsius);
        tStr = tBuf;
        tClr = (m_lastSnap.tempCelsius >= 85.0) ? kClrRed : kClrBlue;
    }
    wchar_t bSub[64]{};
    if (!m_lastSnap.activeBoostedProcess.empty()) {
        _snwprintf_s(bSub, ARRAYSIZE(bSub), _TRUNCATE, L"EPP %u%% · %s", m_lastSnap.curEpp,
                     m_lastSnap.activeBoostedProcess.c_str());
    } else {
        _snwprintf_s(bSub, ARRAYSIZE(bSub), _TRUNCATE, L"EPP %u%% · %s", m_lastSnap.curEpp,
                     CpuIntelEngine::BoostModeToString(m_lastSnap.curBoostMode));
    }
    drawCol(3, L"THERMAL · EPP", tStr, tClr, bSub, 152.0f);
}

void AppWindow::DrawLeftPane() {
    const D2D1_RECT_F paneRc{16.0f, 165.0f, 520.0f, 586.0f};
    DrawRoundedBox(paneRc, 10.0f, kClrWhite, kClrBorder, 1.0f);

    const float iLeft = 30.0f;
    const float iRight = 506.0f;

    // 4-tab pill bar
    const D2D1_RECT_F tabStripRc{iLeft, 175.0f, 366.0f, 201.0f};
    DrawRoundedBox(tabStripRc, 7.0f, kClrSurface, kClrBorder, 1.0f);

    struct TabDef { LeftTab tab; ControlId id; const wchar_t* label; float w; };
    const TabDef tabs[4] = {
        {LeftTab::Sliders,  ControlId::LeftTabTuning,    L"Tuning",     72.0f},
        {LeftTab::Advanced, ControlId::LeftTabPowerSet,  L"Power Set",  84.0f},
        {LeftTab::Targets,  ControlId::LeftTabAppsTweak, L"Apps Tweak", 92.0f},
        {LeftTab::Compat,   ControlId::LeftTabSupport,   L"Support",    74.0f}
    };
    float tx = tabStripRc.left + 3.0f;
    for (const auto& t : tabs) {
        const D2D1_RECT_F tr{tx, 177.5f, tx + t.w, 198.5f};
        RegisterHit(t.id, tr, true);
        const bool sel = (m_leftTab == t.tab);
        const bool hov = !sel && IsControlHovered(t.id);
        if (sel || hov) {
            DrawRoundedBox(tr, 5.0f, sel ? kClrBlue : kClrBlueTint, D2dColor(0, 0, 0, 0), 0.0f);
        }
        DrawTextStrict(t.label, m_fmtSemi11.Get(), tr, sel ? kClrWhite : kClrSlate600,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        tx += t.w + 2.0f;
    }

    // Live apply toggle on right
    DrawTextStrict(L"Live", m_fmtSemi11.Get(), D2D1::RectF(424.0f, 175.0f, 460.0f, 201.0f),
                   kClrSlate500, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawToggleSwitch(ControlId::ToggleLiveApply, iRight, 188.0f, m_config.liveApply, m_cpuInfo.isIntel);

    const float contentTop = 208.0f;
    const float contentBottom = 474.0f;
    const float footerTop = 479.0f;

    switch (m_leftTab) {
    case LeftTab::Sliders:  DrawTabTuning(iLeft, iRight, contentTop, contentBottom); break;
    case LeftTab::Advanced: DrawTabPowerSet(iLeft, iRight, contentTop, contentBottom); break;
    case LeftTab::Targets:  DrawTabAppsTweak(iLeft, iRight, contentTop, contentBottom); break;
    case LeftTab::Compat:   DrawTabSupport(iLeft, iRight, contentTop, contentBottom); break;
    }

    DrawPresetFooter(iLeft, iRight, footerTop);
}

void AppWindow::DrawTabTuning(float innerLeft, float innerRight, float topY, float) {
    const bool turboOk  = m_cpuInfo.isIntel && m_cpuInfo.hasTurboBoost;
    const bool eppOk    = m_cpuInfo.isIntel && m_cpuInfo.hasHwp && m_cpuInfo.hasHwpEpp;
    const bool hybridOk = m_cpuInfo.isIntel && m_cpuInfo.isHybrid;

    wchar_t vUnpark[32]{}, vMin[16]{}, vMax[16]{}, vBoost[16]{}, vEpp[24]{}, vFreq[24]{};
    if (m_config.unparkCoresPercent >= 100) wcscpy_s(vUnpark, L"100% Unpark");
    else std::swprintf(vUnpark, 32, L"%d%%", m_config.unparkCoresPercent);
    std::swprintf(vMin, 16, L"%d%%", m_config.minStatePercent);
    std::swprintf(vMax, 16, L"%d%%", m_config.maxStatePercent);
    std::swprintf(vBoost, 16, L"%d%%", m_config.turboBoostPolicy);
    if (m_config.eppPercent == 0) wcscpy_s(vEpp, L"0% Max");
    else std::swprintf(vEpp, 24, L"%d%%", m_config.eppPercent);
    if (m_config.freqCapMhz <= 0) wcscpy_s(vFreq, L"Uncapped");
    else std::swprintf(vFreq, 24, L"%d MHz", m_config.freqCapMhz);

    struct SliderSpec {
        ControlId id;
        const wchar_t* title;
        const wchar_t* valStr;
        double val, mn, mx, step;
        bool en;
    };
    const SliderSpec sliders[6] = {
        {ControlId::SliderUnpark,   L"Core Parking",       vUnpark, static_cast<double>(m_config.unparkCoresPercent), 0, 100, 5, m_cpuInfo.isIntel},
        {ControlId::SliderMinState, L"Minimum CPU State",  vMin,    static_cast<double>(m_config.minStatePercent),    5, 100, 5, m_cpuInfo.isIntel},
        {ControlId::SliderMaxState, L"Maximum CPU State",  vMax,    static_cast<double>(m_config.maxStatePercent),   20, 100, 5, m_cpuInfo.isIntel},
        {ControlId::SliderBoostPol, L"Turbo Boost Policy", vBoost,  static_cast<double>(m_config.turboBoostPolicy),  0, 100, 5, turboOk},
        {ControlId::SliderEpp,      L"Speed Shift EPP",    vEpp,    static_cast<double>(m_config.eppPercent),        0, 100, 5, eppOk},
        {ControlId::SliderFreqCap,  L"Frequency Limit",    vFreq,   static_cast<double>(m_config.freqCapMhz),        0, 6600, 200, m_cpuInfo.isIntel}
    };

    float y = topY;
    for (int i = 0; i < 6; ++i) {
        const auto& s = sliders[i];
        DrawLabeledSlider(s.id, innerLeft, innerRight, y, s.title, s.valStr, s.val, s.mn, s.mx, s.step, s.en);
        y += 33.5f;
        if (i < 5) {
            m_brush->SetColor(kClrDivider);
            m_rt->DrawLine(D2D1::Point2F(innerLeft, y), D2D1::Point2F(innerRight, y), m_brush.Get(), 1.0f);
            y += 3.5f;
        }
    }

    y += 5.0f;
    m_brush->SetColor(kClrDivider);
    m_rt->DrawLine(D2D1::Point2F(innerLeft, y), D2D1::Point2F(innerRight, y), m_brush.Get(), 1.0f);
    y += 7.0f;

    auto drawStepper = [&](float sx, float w, const wchar_t* label, const wchar_t* valueText,
                           ControlId prevId, ControlId nextId, bool enabled) {
        DrawTextStrict(label, m_fmtSemi10.Get(), D2D1::RectF(sx, y, sx + 74.0f, y + 26.0f), kClrSlate500);
        const D2D1_RECT_F pillRc{sx + w - 134.0f, y, sx + w, y + 26.0f};
        DrawRoundedBox(pillRc, 6.0f, kClrSurface, kClrBorder, 1.0f);

        const D2D1_RECT_F prevRc{pillRc.left + 3.0f, y + 3.0f, pillRc.left + 23.0f, y + 23.0f};
        RegisterHit(prevId, prevRc, enabled);
        if (enabled && IsControlHovered(prevId)) DrawRoundedBox(prevRc, 4.0f, kClrDivider, D2dColor(0, 0, 0, 0), 0.0f);
        m_brush->SetColor(enabled ? kClrInk : kClrSlate400);
        icons::DrawIcon(m_rt.Get(), m_d2dFactory.Get(), m_brush.Get(), m_roundStroke.Get(),
                        icons::IconKind::ChevronLeft,
                        D2D1::RectF(prevRc.left + 5.0f, prevRc.top + 5.0f, prevRc.right - 5.0f, prevRc.bottom - 5.0f), 1.8f);

        DrawTextStrict(valueText, m_fmtBold105.Get(),
                       D2D1::RectF(prevRc.right + 2.0f, y, pillRc.right - 25.0f, y + 26.0f),
                       enabled ? kClrBlue : kClrSlate400,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const D2D1_RECT_F nextRc{pillRc.right - 23.0f, y + 3.0f, pillRc.right - 3.0f, y + 23.0f};
        RegisterHit(nextId, nextRc, enabled);
        if (enabled && IsControlHovered(nextId)) DrawRoundedBox(nextRc, 4.0f, kClrDivider, D2dColor(0, 0, 0, 0), 0.0f);
        m_brush->SetColor(enabled ? kClrInk : kClrSlate400);
        icons::DrawIcon(m_rt.Get(), m_d2dFactory.Get(), m_brush.Get(), m_roundStroke.Get(),
                        icons::IconKind::ChevronRight,
                        D2D1::RectF(nextRc.left + 5.0f, nextRc.top + 5.0f, nextRc.right - 5.0f, nextRc.bottom - 5.0f), 1.8f);
    };

    const float halfW = (innerRight - innerLeft - 12.0f) * 0.5f;
    drawStepper(innerLeft, halfW, L"Boost",
                CpuIntelEngine::BoostModeToString(static_cast<uint32_t>(m_config.boostMode)),
                ControlId::BoostPrev, ControlId::BoostNext, turboOk);
    drawStepper(innerLeft + halfW + 12.0f, halfW, L"Scheduler",
                CpuIntelEngine::SchedPolicyToString(static_cast<uint32_t>(m_config.schedPolicy)),
                ControlId::SchedPrev, ControlId::SchedNext, hybridOk);
}

void AppWindow::DrawTabPowerSet(float innerLeft, float innerRight, float topY, float) {
    const bool hwpOk    = m_cpuInfo.isIntel && m_cpuInfo.hasHwp;
    const bool hybridOk = m_cpuInfo.isIntel && m_cpuInfo.isHybrid;

    wchar_t vInc[16]{}, vDec[16]{}, vHet[16]{}, vCon[16]{};
    std::swprintf(vInc, 16, L"%d%%", m_config.incThreshold);
    std::swprintf(vDec, 16, L"%d%%", m_config.decThreshold);
    std::swprintf(vHet, 16, L"%d%%", m_config.heteroFloorPerf);
    std::swprintf(vCon, 16, L"%d%%", m_config.concurrencyThreshold);

    struct SliderSpec {
        ControlId id;
        const wchar_t* title;
        const wchar_t* valStr;
        int val, mn, step;
        bool en;
    };
    const SliderSpec sliders[4] = {
        {ControlId::SliderIncThresh,   L"Boost Ramp Threshold",  vInc, m_config.incThreshold,         5, 5, m_cpuInfo.isIntel},
        {ControlId::SliderDecThresh,   L"Boost Hold Threshold",  vDec, m_config.decThreshold,         2, 1, m_cpuInfo.isIntel},
        {ControlId::SliderHeteroFloor, L"P-Core Hetero Floor",   vHet, m_config.heteroFloorPerf,      0, 5, hybridOk},
        {ControlId::SliderConcur,      L"Parking Concurrency",   vCon, m_config.concurrencyThreshold, 5, 5, m_cpuInfo.isIntel}
    };

    float y = topY;
    for (int i = 0; i < 4; ++i) {
        const auto& s = sliders[i];
        DrawLabeledSlider(s.id, innerLeft, innerRight, y, s.title, s.valStr,
                          static_cast<double>(s.val), static_cast<double>(s.mn), 100.0,
                          static_cast<double>(s.step), s.en);
        y += 31.5f;
        if (i < 3) {
            m_brush->SetColor(kClrDivider);
            m_rt->DrawLine(D2D1::Point2F(innerLeft, y), D2D1::Point2F(innerRight, y), m_brush.Get(), 1.0f);
            y += 2.0f;
        }
    }

    y += 4.0f;
    m_brush->SetColor(kClrBorder);
    m_rt->DrawLine(D2D1::Point2F(innerLeft, y), D2D1::Point2F(innerRight, y), m_brush.Get(), 1.0f);
    y += 4.0f;

    struct ToggleRow {
        ControlId id;
        const wchar_t* label;
        bool val;
        bool en;
    };
    const ToggleRow rows[5] = {
        {ControlId::ToggleActiveCooling, L"Active Cooling",          m_config.activeCooling,       m_cpuInfo.isIntel},
        {ControlId::ToggleOsDirectHwp,   L"OS Direct Clock Control", !m_config.hwpAutonomous,      hwpOk},
        {ControlId::ToggleIdleDisable,   L"Disable Idle C-States",   m_config.disableIdleStates,   m_cpuInfo.isIntel},
        {ControlId::ToggleOsThrottle,    L"OS Scheduler Tweaks",     m_config.disableOsThrottling, m_cpuInfo.isIntel},
        {ControlId::ToggleAdvHighRes,    L"0.5ms Timer Resolution",  m_config.highResTimer,        m_cpuInfo.isIntel}
    };

    for (int i = 0; i < 5; ++i) {
        const auto& r = rows[i];
        const float midY = y + 12.0f;
        DrawTextStrict(r.label, m_fmtReg11.Get(),
                       D2D1::RectF(innerLeft, y, innerRight - 50.0f, y + 24.0f),
                       r.en ? kClrInk : kClrSlate400);
        DrawToggleSwitch(r.id, innerRight, midY, r.val, r.en);
        y += 24.0f;
        if (i < 4) {
            m_brush->SetColor(kClrDivider);
            m_rt->DrawLine(D2D1::Point2F(innerLeft, y), D2D1::Point2F(innerRight, y), m_brush.Get(), 1.0f);
            y += 1.0f;
        }
    }
}

void AppWindow::DrawTabAppsTweak(float innerLeft, float innerRight, float topY, float) {
    float y = topY;
    DrawButton(ControlId::BrowseExeBtn, D2D1::RectF(innerLeft, y, innerLeft + 86.0f, y + 25.0f),
               L"Browse .exe", false, false, m_cpuInfo.isIntel, 10.5f, true, 6.0f);

    const D2D1_RECT_F runBtnRc{innerLeft + 92.0f, y, innerLeft + 194.0f, y + 25.0f};
    RegisterHit(ControlId::RunningAppsBtn, runBtnRc, m_cpuInfo.isIntel);
    DrawRoundedBox(runBtnRc, 6.0f,
                   IsControlHovered(ControlId::RunningAppsBtn) ? kClrBlueTint : kClrWhite,
                   IsControlHovered(ControlId::RunningAppsBtn) ? kClrBlue : kClrSlate300, 1.0f);
    DrawTextStrict(L"Running Apps", m_fmtBold105.Get(), runBtnRc,
                   m_cpuInfo.isIntel ? kClrBlue : kClrSlate400,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    DrawButton(ControlId::ApplyAppsBtn, D2D1::RectF(innerRight - 88.0f, y, innerRight, y + 25.0f),
               L"Apply Apps", true, false, m_cpuInfo.isIntel, 10.5f, true, 6.0f);

    y += 31.0f;
    const D2D1_RECT_F listBoxRc{innerLeft, y, innerRight, y + 100.0f};
    DrawRoundedBox(listBoxRc, 7.0f, kClrSurface, kClrBorder, 1.0f);

    m_targetViewportRc = D2D1::RectF(listBoxRc.left + 6.0f, listBoxRc.top + 6.0f,
                                     listBoxRc.right - 6.0f, listBoxRc.bottom - 6.0f);
    if (m_config.targetExes.empty()) {
        m_targetMaxScroll = 0.0f;
        m_targetScroll = 0.0f;
        DrawTextStrict(L"No apps added", m_fmtReg11.Get(), m_targetViewportRc, kClrSlate400,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    } else {
        const float viewH = m_targetViewportRc.bottom - m_targetViewportRc.top;
        const float rowH = 29.0f;
        const float gap = 4.0f;
        const float totalH = static_cast<float>(m_config.targetExes.size()) * (rowH + gap) - gap;
        m_targetMaxScroll = std::max(0.0f, totalH - viewH);
        m_targetScroll = std::clamp(m_targetScroll, 0.0f, m_targetMaxScroll);

        m_rt->PushAxisAlignedClip(m_targetViewportRc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float ry = m_targetViewportRc.top - m_targetScroll;
        for (size_t i = 0; i < m_config.targetExes.size(); ++i) {
            const auto& rule = m_config.targetExes[i];
            if (ry + rowH >= m_targetViewportRc.top && ry <= m_targetViewportRc.bottom) {
                uint32_t runCnt = 0;
                for (const auto& p : m_cachedProcs) {
                    if (_wcsicmp(p.exeName.c_str(), rule.exeName.c_str()) == 0) {
                        runCnt = p.instanceCount;
                        break;
                    }
                }
                const D2D1_RECT_F cardRc{m_targetViewportRc.left, ry, m_targetViewportRc.right, ry + rowH};
                DrawRoundedBox(cardRc, 6.0f, kClrWhite,
                               (rule.enabled && runCnt > 0) ? kClrBlueBorder : kClrBorder, 1.0f);
                DrawTextStrict(rule.exeName, m_fmtSemi11.Get(),
                               D2D1::RectF(cardRc.left + 9.0f, ry, cardRc.right - 154.0f, ry + rowH),
                               rule.enabled ? kClrInk : kClrSlate400);

                wchar_t bBuf[24]{};
                if (runCnt > 1) std::swprintf(bBuf, 24, L"RUNNING x%u", runCnt);
                else if (runCnt == 1) wcscpy_s(bBuf, L"RUNNING");
                else wcscpy_s(bBuf, L"IDLE");

                const D2D1_RECT_F badgeRc{cardRc.right - 148.0f, ry + 6.0f, cardRc.right - 76.0f, ry + 23.0f};
                DrawRoundedBox(badgeRc, 4.0f, runCnt > 0 ? kClrBlueTint : kClrSurface, D2dColor(0, 0, 0, 0), 0.0f);
                DrawTextStrict(bBuf, m_fmtBold9.Get(), badgeRc, runCnt > 0 ? kClrBlue : kClrSlate400,
                               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                DrawToggleSwitch(ControlId::TargetRuleToggle, cardRc.right - 29.0f, ry + rowH * 0.5f,
                                 rule.enabled, m_cpuInfo.isIntel, static_cast<int>(i));

                const D2D1_RECT_F delRc{cardRc.right - 24.0f, ry + 3.5f, cardRc.right - 2.0f, ry + 25.5f};
                RegisterHit(ControlId::TargetRuleRemove, delRc, m_cpuInfo.isIntel, static_cast<int>(i));
                if (IsControlHovered(ControlId::TargetRuleRemove, static_cast<int>(i))) {
                    DrawRoundedBox(delRc, 4.0f, kClrRedHover, D2dColor(0, 0, 0, 0), 0.0f);
                }
                DrawTextStrict(L"×", m_fmtSemi12.Get(), delRc,
                               IsControlHovered(ControlId::TargetRuleRemove, static_cast<int>(i)) ? kClrRed : kClrSlate500,
                               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            ry += rowH + gap;
        }
        m_rt->PopAxisAlignedClip();
    }

    y += 106.0f;
    // Priority row
    DrawTextStrict(L"Priority", m_fmtSemi11.Get(), D2D1::RectF(innerLeft, y, innerLeft + 92.0f, y + 25.0f), kClrInk);
    const D2D1_RECT_F prioBox{innerRight - 248.0f, y, innerRight, y + 25.0f};
    DrawRoundedBox(prioBox, 7.0f, kClrSurface, kClrBorder, 1.0f);
    struct PillItem { ControlId id; const wchar_t* label; int val; float w; };
    const PillItem prioPills[4] = {
        {ControlId::TargetPrio0, L"Normal",   0, 56.0f},
        {ControlId::TargetPrio1, L"Above",    1, 56.0f},
        {ControlId::TargetPrio2, L"High",     2, 52.0f},
        {ControlId::TargetPrio3, L"Realtime", 3, 72.0f}
    };
    float px = prioBox.left + 3.0f;
    for (const auto& p : prioPills) {
        const D2D1_RECT_F r{px, y + 2.5f, px + p.w, y + 22.5f};
        RegisterHit(p.id, r, m_cpuInfo.isIntel, p.val);
        const bool sel = (m_config.targetPriorityMode == p.val);
        const bool hov = m_cpuInfo.isIntel && !sel && IsControlHovered(p.id, p.val);
        if (sel || hov) DrawRoundedBox(r, 5.0f, sel ? kClrBlue : kClrBlueTint, D2dColor(0, 0, 0, 0), 0.0f);
        DrawTextStrict(p.label, m_fmtSemi10.Get(), r, sel ? kClrWhite : (m_cpuInfo.isIntel ? kClrSlate600 : kClrSlate400),
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        px += p.w + 2.0f;
    }

    y += 28.0f;
    m_brush->SetColor(kClrDivider);
    m_rt->DrawLine(D2D1::Point2F(innerLeft, y), D2D1::Point2F(innerRight, y), m_brush.Get(), 1.0f);
    y += 3.0f;

    // Core routing row
    DrawTextStrict(L"Core Routing", m_fmtSemi11.Get(), D2D1::RectF(innerLeft, y, innerLeft + 110.0f, y + 25.0f), kClrInk);
    const D2D1_RECT_F coreBox{innerRight - 196.0f, y, innerRight, y + 25.0f};
    DrawRoundedBox(coreBox, 7.0f, kClrSurface, kClrBorder, 1.0f);
    const bool hybridOk = m_cpuInfo.isIntel && m_cpuInfo.isHybrid;
    const int uiCoreMode = hybridOk ? m_config.targetCoreMode : 0;
    const PillItem corePills[3] = {
        {ControlId::TargetCore0, L"All",      0, 48.0f},
        {ControlId::TargetCore1, L"P-Core",   1, 66.0f},
        {ControlId::TargetCore2, L"Prefer P", 2, 72.0f}
    };
    px = coreBox.left + 3.0f;
    for (int i = 0; i < 3; ++i) {
        const auto& c = corePills[i];
        const bool en = (i == 0) ? m_cpuInfo.isIntel : hybridOk;
        const D2D1_RECT_F r{px, y + 2.5f, px + c.w, y + 22.5f};
        RegisterHit(c.id, r, en, c.val);
        const bool sel = (uiCoreMode == c.val);
        const bool hov = en && !sel && IsControlHovered(c.id, c.val);
        if (sel || hov) DrawRoundedBox(r, 5.0f, sel ? kClrBlue : kClrBlueTint, D2dColor(0, 0, 0, 0), 0.0f);
        DrawTextStrict(c.label, m_fmtSemi10.Get(), r, sel ? kClrWhite : (en ? kClrSlate600 : kClrSlate400),
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        px += c.w + 2.0f;
    }

    y += 28.0f;
    const struct { ControlId id; const wchar_t* lbl; bool on; } optRows[3] = {
        {ControlId::ToggleTargetDeepBoost, L"Disable EcoQoS & High I/O Priority", m_config.targetDeepBoost},
        {ControlId::ToggleTargetIfeo,      L"Save Launch Policy (Registry)",      m_config.targetIfeoPersist},
        {ControlId::ToggleFgBoost,         L"Boost Active Foreground Window",     m_config.boostForegroundPCore}
    };
    for (int i = 0; i < 3; ++i) {
        m_brush->SetColor(kClrDivider);
        m_rt->DrawLine(D2D1::Point2F(innerLeft, y), D2D1::Point2F(innerRight, y), m_brush.Get(), 1.0f);
        y += 1.0f;
        DrawTextStrict(optRows[i].lbl, m_fmtReg11.Get(),
                       D2D1::RectF(innerLeft, y, innerRight - 48.0f, y + 24.0f),
                       m_cpuInfo.isIntel ? kClrInk : kClrSlate400);
        DrawToggleSwitch(optRows[i].id, innerRight, y + 12.0f, optRows[i].on, m_cpuInfo.isIntel);
        y += 24.0f;
    }
}

void AppWindow::DrawTabSupport(float innerLeft, float innerRight, float topY, float bottomY) {
    int readyCount = 0;
    const int totalCount = static_cast<int>(m_cpuInfo.featureChecks.size());
    for (const auto& item : m_cpuInfo.featureChecks) {
        if (item.level == SupportLevel::Supported || item.level == SupportLevel::ConditionalAdmin) {
            ++readyCount;
        }
    }

    DrawTextStrict(L"HARDWARE SUPPORT", m_fmtBold105.Get(),
                   D2D1::RectF(innerLeft, topY, innerRight - 100.0f, topY + 18.0f), kClrSlateDark);
    wchar_t sum[48]{};
    std::swprintf(sum, 48, L"%d/%d Ready", readyCount, totalCount);
    DrawTextStrict(sum, m_fmtBold105.Get(),
                   D2D1::RectF(innerRight - 110.0f, topY, innerRight, topY + 18.0f),
                   m_cpuInfo.isIntel ? kClrBlue : kClrRed,
                   DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_compatViewportRc = D2D1::RectF(innerLeft, topY + 22.0f, innerRight - 8.0f, bottomY);
    m_compatTrackRc    = D2D1::RectF(innerRight - 4.0f, topY + 22.0f, innerRight, bottomY);

    const float rowH = 38.0f;
    const float gap = 4.0f;
    const float totalH = m_cpuInfo.featureChecks.empty()
        ? 0.0f
        : static_cast<float>(m_cpuInfo.featureChecks.size()) * (rowH + gap) - gap;
    const float viewH = m_compatViewportRc.bottom - m_compatViewportRc.top;
    m_compatMaxScroll = std::max(0.0f, totalH - viewH);
    m_compatScroll = std::clamp(m_compatScroll, 0.0f, m_compatMaxScroll);

    m_rt->PushAxisAlignedClip(m_compatViewportRc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float ry = m_compatViewportRc.top - m_compatScroll;
    for (const auto& item : m_cpuInfo.featureChecks) {
        if (ry + rowH >= m_compatViewportRc.top && ry <= m_compatViewportRc.bottom) {
            const D2D1_RECT_F cardRc{m_compatViewportRc.left, ry, m_compatViewportRc.right, ry + rowH};
            DrawRoundedBox(cardRc, 6.0f, kClrSurface, kClrBorder, 1.0f);

            DrawTextStrict(item.name, m_fmtSemi11.Get(),
                           D2D1::RectF(cardRc.left + 8.0f, ry + 5.0f, cardRc.right - 96.0f, ry + 20.0f), kClrInk);
            DrawTextStrict(item.detail, m_fmtReg9.Get(),
                           D2D1::RectF(cardRc.left + 8.0f, ry + 20.0f, cardRc.right - 96.0f, ry + 34.0f), kClrSlate600);

            D2D1_COLOR_F bg = kClrBlueTint;
            D2D1_COLOR_F fg = kClrBlueHover;
            if (item.level == SupportLevel::ConditionalAdmin) {
                bg = kClrAmberTint; fg = kClrAmberText;
            } else if (item.level == SupportLevel::LockedOrLimited) {
                bg = kClrDivider; fg = kClrSlateDark;
            } else if (item.level == SupportLevel::Unsupported) {
                bg = kClrDivider; fg = kClrSlate500;
            } else if (item.level == SupportLevel::Blocked) {
                bg = kClrRedHover; fg = kClrRedDark;
            }

            const D2D1_RECT_F badgeRc{cardRc.right - 90.0f, ry + 10.0f, cardRc.right - 8.0f, ry + 28.0f};
            DrawRoundedBox(badgeRc, 4.0f, bg, D2dColor(0, 0, 0, 0), 0.0f);
            DrawTextStrict(item.badgeText, m_fmtBold9.Get(), badgeRc, fg,
                           DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        ry += rowH + gap;
    }
    m_rt->PopAxisAlignedClip();

    DrawScrollbar(ControlId::CompatScrollThumb, m_compatTrackRc, totalH, viewH, m_compatScroll);
}

void AppWindow::DrawPresetFooter(float innerLeft, float innerRight, float topY) {
    m_brush->SetColor(kClrDivider);
    m_rt->DrawLine(D2D1::Point2F(innerLeft, topY), D2D1::Point2F(innerRight, topY), m_brush.Get(), 1.0f);

    const bool isEco = (m_config.boostMode == 0 && m_config.eppPercent == 85 && m_config.maxStatePercent == 75);
    const bool isBalanced = (m_config.unparkCoresPercent == 100 && m_config.minStatePercent == 5 &&
                             m_config.turboBoostPolicy == 80 && m_config.eppPercent == 45);
    const bool isPerf = (m_config.unparkCoresPercent == 100 && m_config.minStatePercent == 5 &&
                         m_config.turboBoostPolicy == 100 && m_config.eppPercent == 10 && m_config.boostMode == 4);
    const bool isBrutal = (m_config.unparkCoresPercent == 100 && m_config.maxStatePercent == 100 &&
                           m_config.turboBoostPolicy == 100 && m_config.eppPercent == 0 &&
                           m_config.boostMode == 4 && m_config.disableOsThrottling);

    const float pY = topY + 6.0f;
    const float btnW = (innerRight - innerLeft - 18.0f) * 0.25f;
    const struct { ControlId id; const wchar_t* label; bool sel; bool bold; } presets[4] = {
        {ControlId::PresetEco,      L"Eco",         isEco,      false},
        {ControlId::PresetBalanced, L"Balanced",    isBalanced, false},
        {ControlId::PresetPerf,     L"Performance", isPerf,     false},
        {ControlId::PresetMaxOc,    L"Max OC",      isBrutal,   true}
    };
    float px = innerLeft;
    for (const auto& p : presets) {
        DrawButton(p.id, D2D1::RectF(px, pY, px + btnW, pY + 24.0f),
                   p.label, false, p.sel, m_cpuInfo.isIntel, 10.5f, p.bold, 6.0f);
        px += btnW + 6.0f;
    }

    DrawButton(ControlId::ApplyPresetsBtn, D2D1::RectF(innerLeft, pY + 29.0f, innerRight, pY + 59.0f),
               L"Apply Presets Settings", true, false, m_cpuInfo.isIntel, 11.5f, true, 6.0f);
    DrawButton(ControlId::RestoreBtn, D2D1::RectF(innerLeft, pY + 64.0f, innerRight, pY + 90.0f),
               L"Restore", false, false, m_cpuInfo.isIntel, 11.0f, true, 6.0f);
}

void AppWindow::DrawRightCoresPane() {
    const D2D1_RECT_F paneRc{532.0f, 165.0f, 1104.0f, 586.0f};
    DrawRoundedBox(paneRc, 10.0f, kClrWhite, kClrBorder, 1.0f);

    const float iLeft = 545.0f;
    const float iRight = 1091.0f;

    // Header dot + CORES + optional hybrid legend
    m_brush->SetColor(kClrBlue);
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(iLeft + 3.5f, 187.5f), 3.5f, 3.5f), m_brush.Get());
    DrawTextStrict(L"CORES", m_fmtBold105.Get(), D2D1::RectF(iLeft + 14.0f, 175.0f, iLeft + 74.0f, 200.0f), kClrInk);

    const bool showSplit = m_cpuInfo.isHybrid && m_cpuInfo.eCoreCount > 0;
    if (showSplit) {
        m_brush->SetColor(kClrBlue);
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(iLeft + 79.0f, 187.5f), 3.0f, 3.0f), m_brush.Get());
        DrawTextStrict(L"P-Core", m_fmtSemi10.Get(), D2D1::RectF(iLeft + 86.0f, 175.0f, iLeft + 128.0f, 200.0f), kClrBlue);
        m_brush->SetColor(kClrInk);
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(iLeft + 135.0f, 187.5f), 3.0f, 3.0f), m_brush.Get());
        DrawTextStrict(L"E-Core", m_fmtSemi10.Get(), D2D1::RectF(iLeft + 142.0f, 175.0f, iLeft + 188.0f, 200.0f), kClrInk);
    }

    // Filter pill bar on right
    wchar_t bAll[32]{}, bP[32]{}, bE[32]{};
    std::swprintf(bAll, 32, L"All (%u)", m_cpuInfo.physicalCores);
    std::swprintf(bP, 32, L"P-Core (%u)", m_cpuInfo.pCoreCount);
    std::swprintf(bE, 32, L"E-Core (%u)", m_cpuInfo.eCoreCount);

    const float pillStripW = showSplit ? 238.0f : 72.0f;
    const D2D1_RECT_F filterBox{iRight - pillStripW, 175.0f, iRight, 200.0f};
    DrawRoundedBox(filterBox, 7.0f, kClrSurface, kClrBorder, 1.0f);

    float fx = filterBox.left + 3.0f;
    auto drawFilterPill = [&](ControlId id, CoreFilterMode mode, const wchar_t* label, float w) {
        const D2D1_RECT_F fr{fx, filterBox.top + 2.5f, fx + w, filterBox.bottom - 2.5f};
        RegisterHit(id, fr, true);
        const bool sel = (m_coreFilter == mode);
        const bool hov = !sel && IsControlHovered(id);
        if (sel || hov) DrawRoundedBox(fr, 5.0f, sel ? kClrBlue : kClrBlueTint, D2dColor(0, 0, 0, 0), 0.0f);
        DrawTextStrict(label, m_fmtSemi10.Get(), fr, sel ? kClrWhite : kClrSlate600,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fx += w + 2.0f;
    };
    drawFilterPill(ControlId::FilterAll, CoreFilterMode::All, bAll, 66.0f);
    if (showSplit) {
        drawFilterPill(ControlId::FilterPCores, CoreFilterMode::PCores, bP, 81.0f);
        drawFilterPill(ControlId::FilterECores, CoreFilterMode::ECores, bE, 81.0f);
    }

    m_coreViewportRc = D2D1::RectF(iLeft, 206.0f, iRight - 9.0f, 576.0f);
    m_coreTrackRc    = D2D1::RectF(iRight - 4.0f, 206.0f, iRight, 576.0f);

    std::vector<const CoreHistory*> visible;
    for (const auto& ch : m_coreHistories) {
        if (m_coreFilter == CoreFilterMode::PCores && ch.kind != CoreKind::PCore) continue;
        if (m_coreFilter == CoreFilterMode::ECores && ch.kind != CoreKind::ECore) continue;
        visible.push_back(&ch);
    }

    const int rowCount = (static_cast<int>(visible.size()) + 1) / 2;
    const float cardH = 66.0f;
    const float rowGap = 6.0f;
    const float totalH = (rowCount > 0) ? (static_cast<float>(rowCount) * (cardH + rowGap) - rowGap) : 0.0f;
    const float viewH = m_coreViewportRc.bottom - m_coreViewportRc.top;
    m_coreMaxScroll = std::max(0.0f, totalH - viewH);
    m_coreScroll = std::clamp(m_coreScroll, 0.0f, m_coreMaxScroll);

    const float colW = (m_coreViewportRc.right - m_coreViewportRc.left - 8.0f) * 0.5f;

    m_rt->PushAxisAlignedClip(m_coreViewportRc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t idx = 0; idx < visible.size(); ++idx) {
        const auto* ch = visible[idx];
        const int r = static_cast<int>(idx / 2);
        const int col = static_cast<int>(idx % 2);
        const float cx = m_coreViewportRc.left + col * (colW + 8.0f);
        const float cy = m_coreViewportRc.top - m_coreScroll + r * (cardH + rowGap);
        if (cy + cardH < m_coreViewportRc.top || cy > m_coreViewportRc.bottom) continue;

        const bool isECore = (ch->kind == CoreKind::ECore);
        const bool isPCore = (ch->kind == CoreKind::PCore);
        const D2D1_COLOR_F accent   = isECore ? kClrInk : kClrBlue;
        const D2D1_COLOR_F pillBg   = isECore ? kClrInk : kClrBlueTint;
        const D2D1_COLOR_F pillFg   = isECore ? kClrWhite : kClrBlueHover;
        const D2D1_COLOR_F pillBrd  = isECore ? kClrInk : kClrBlueBorder;
        const D2D1_COLOR_F fillClr  = isECore ? D2dColor(15, 23, 42, 26) : D2dColor(37, 99, 235, 34);
        const D2D1_COLOR_F cardBrd  = isPCore ? D2dColor(219, 234, 254) : kClrBorder;

        const D2D1_RECT_F cardRc{cx, cy, cx + colW, cy + cardH};
        DrawRoundedBox(cardRc, 7.0f, kClrWhite, cardBrd, 1.0f);

        wchar_t lbl[16]{};
        if (isPCore) std::swprintf(lbl, 16, L"P%u", ch->typeLocalIndex);
        else if (isECore) std::swprintf(lbl, 16, L"E%u", ch->typeLocalIndex);
        else std::swprintf(lbl, 16, L"C%u", ch->coreIndex);

        const D2D1_RECT_F badgeRc{cx + 9.0f, cy + 6.0f, cx + 38.0f, cy + 23.0f};
        DrawRoundedBox(badgeRc, 4.0f, pillBg, pillBrd, 1.0f);
        DrawTextStrict(lbl, m_fmtBold9.Get(), badgeRc, pillFg,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        wchar_t pctBuf[16]{};
        std::swprintf(pctBuf, 16, L"%.0f%%", ch->currentUsage);
        DrawTextStrict(FormatClockGhz(ch->currentMhz), m_fmtSemi10.Get(),
                       D2D1::RectF(cx + 46.0f, cy + 6.0f, cx + colW - 46.0f, cy + 23.0f), kClrSlate600,
                       DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DrawTextStrict(pctBuf, m_fmtBold115.Get(),
                       D2D1::RectF(cx + colW - 42.0f, cy + 5.0f, cx + colW - 9.0f, cy + 23.0f), accent,
                       DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const D2D1_RECT_F boxRc{cx + 9.0f, cy + 27.0f, cx + colW - 9.0f, cy + 60.0f};
        DrawRoundedBox(boxRc, 5.0f, kClrSurface, kClrBorder, 1.0f);

        const float gw = boxRc.right - boxRc.left;
        const float gh = boxRc.bottom - boxRc.top;
        m_rt->PushAxisAlignedClip(D2D1::RectF(boxRc.left + 1.0f, boxRc.top + 1.0f,
                                              boxRc.right - 1.0f, boxRc.bottom - 1.0f),
                                  D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        m_brush->SetColor(kClrBorder);
        for (int g = 1; g <= 3; ++g) {
            const float gy = boxRc.top + (gh * static_cast<float>(g)) / 4.0f;
            m_rt->DrawLine(D2D1::Point2F(boxRc.left, gy), D2D1::Point2F(boxRc.right, gy), m_brush.Get(), 0.8f);
        }

        if (ch->history.size() >= 2 && m_d2dFactory) {
            const size_t cnt = ch->history.size();
            const float dx = gw / static_cast<float>(cnt - 1);
            std::vector<D2D1_POINT_2F> pts;
            pts.reserve(cnt);
            for (size_t i = 0; i < cnt; ++i) {
                const float x = boxRc.left + static_cast<float>(i) * dx;
                const float y = boxRc.top + gh - (static_cast<float>(std::clamp(ch->history[i], 0.0, 100.0)) / 100.0f) * (gh - 3.0f) - 1.0f;
                pts.push_back(D2D1::Point2F(x, y));
            }

            ComPtr<ID2D1PathGeometry> fillGeom;
            if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&fillGeom))) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(fillGeom->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(pts.front().x, boxRc.bottom), D2D1_FIGURE_BEGIN_FILLED);
                    for (const auto& pt : pts) sink->AddLine(pt);
                    sink->AddLine(D2D1::Point2F(pts.back().x, boxRc.bottom));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    m_brush->SetColor(fillClr);
                    m_rt->FillGeometry(fillGeom.Get(), m_brush.Get());
                }
            }

            ComPtr<ID2D1PathGeometry> lineGeom;
            if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&lineGeom))) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(lineGeom->Open(&sink))) {
                    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLines(pts.data() + 1, static_cast<UINT32>(pts.size() - 1));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->Close();
                    m_brush->SetColor(accent);
                    m_rt->DrawGeometry(lineGeom.Get(), m_brush.Get(), 1.8f, m_roundStroke.Get());
                }
            }
        }
        m_rt->PopAxisAlignedClip();
    }
    m_rt->PopAxisAlignedClip();

    DrawScrollbar(ControlId::CoreScrollThumb, m_coreTrackRc, totalH, viewH, m_coreScroll);
}

void AppWindow::DrawAmdOverlay() {
    if (m_cpuInfo.isIntel) return;

    const D2D1_RECT_F overlayRc{16.0f, 165.0f, 1104.0f, 586.0f};
    RegisterHit(ControlId::None, overlayRc, false);
    DrawRoundedBox(overlayRc, 10.0f, D2dColor(255, 255, 255, 246), D2dColor(0, 0, 0, 0), 0.0f);

    const D2D1_RECT_F cardRc{290.0f, 270.0f, 830.0f, 476.0f};
    DrawRoundedBox(cardRc, 10.0f, kClrRedTint, kClrRedBorder, 1.5f);

    const D2D1_RECT_F badgeRc{314.0f, 294.0f, 348.0f, 328.0f};
    DrawRoundedBox(badgeRc, 8.0f, kClrRed, D2dColor(0, 0, 0, 0), 0.0f);
    m_brush->SetColor(kClrWhite);
    icons::DrawIcon(m_rt.Get(), m_d2dFactory.Get(), m_brush.Get(), m_roundStroke.Get(),
                    icons::IconKind::ShieldAlert, D2D1::RectF(322.0f, 302.0f, 340.0f, 320.0f), 1.9f);

    DrawTextStrict(L"NON-INTEL / AMD CPU DETECTED — ACCESS BLOCKED", m_fmtBold145.Get(),
                   D2D1::RectF(358.0f, 292.0f, 808.0f, 312.0f), kClrRedDark);

    wchar_t vendorBuf[128]{};
    std::swprintf(vendorBuf, 128, L"Vendor ID: %s (GenuineIntel Required)", m_cpuInfo.vendorId.c_str());
    DrawTextStrict(vendorBuf, m_fmtSemi11.Get(),
                   D2D1::RectF(358.0f, 312.0f, 808.0f, 330.0f), D2dColor(185, 28, 28));

    m_fmtSemi11->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    DrawTextStrict(L"VOverclock Intel Beta Phase 2 only permits GenuineIntel processors. All sliders, VPower Intel Beta power plan creation, core tuning, and target optimization have been locked.",
                   m_fmtSemi11.Get(), D2D1::RectF(314.0f, 342.0f, 806.0f, 404.0f), kClrInk,
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    m_fmtSemi11->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    const D2D1_RECT_F cpuBoxRc{314.0f, 412.0f, 806.0f, 452.0f};
    DrawRoundedBox(cpuBoxRc, 6.0f, kClrWhite, D2dColor(254, 202, 202), 1.0f);
    DrawTextStrict(L"Detected Processor: " + m_cpuInfo.brandName, m_fmtSemi11.Get(),
                   D2D1::RectF(324.0f, 412.0f, 796.0f, 452.0f), kClrRedDark);
}

void AppWindow::DrawBottomBar() {
    const D2D1_RECT_F gearBtnRc{16.0f, 592.0f, 46.0f, 622.0f};
    RegisterHit(ControlId::SettingsBtn, gearBtnRc, true);
    if (IsControlHovered(ControlId::SettingsBtn) || IsControlPressed(ControlId::SettingsBtn)) {
        DrawRoundedBox(gearBtnRc, 6.0f,
                       IsControlPressed(ControlId::SettingsBtn) ? kClrBorder : kClrDivider,
                       D2dColor(0, 0, 0, 0), 0.0f);
    }
    m_brush->SetColor(kClrSlateDark);
    icons::DrawIcon(m_rt.Get(), m_d2dFactory.Get(), m_brush.Get(), m_roundStroke.Get(),
                    icons::IconKind::SettingsGear, D2D1::RectF(23.0f, 599.0f, 39.0f, 615.0f),
                    1.7f, m_gearAngle);

    if (!m_actionStatusText.empty()) {
        DrawTextStrict(m_actionStatusText, m_fmtBold105.Get(),
                       D2D1::RectF(60.0f, 592.0f, 1060.0f, 622.0f),
                       m_actionStatusError ? kClrRed : kClrBlue,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    const D2D1_RECT_F trayBtnRc{1074.0f, 592.0f, 1104.0f, 622.0f};
    RegisterHit(ControlId::MinimizeTrayBtn, trayBtnRc, true);
    if (IsControlHovered(ControlId::MinimizeTrayBtn) || IsControlPressed(ControlId::MinimizeTrayBtn)) {
        DrawRoundedBox(trayBtnRc, 6.0f,
                       IsControlPressed(ControlId::MinimizeTrayBtn) ? kClrBorder : kClrDivider,
                       D2dColor(0, 0, 0, 0), 0.0f);
    }
    m_brush->SetColor(kClrSlateDark);
    icons::DrawIcon(m_rt.Get(), m_d2dFactory.Get(), m_brush.Get(), m_roundStroke.Get(),
                    icons::IconKind::TrayMinimize, D2D1::RectF(1081.0f, 599.0f, 1097.0f, 615.0f), 1.7f);
}

void AppWindow::DrawPopups() {
    if (!m_powerPlanPopupOpen && !m_runningProcPopupOpen) return;

    RegisterHit(ControlId::ModalBackdrop, D2D1::RectF(0.0f, 43.0f, kClientDipWidth, kClientDipHeight), true);

    if (m_powerPlanPopupOpen) {
        const float itemH = 30.0f;
        const float gap = 3.0f;
        const float contentH = m_cachedPlans.empty()
            ? 32.0f
            : static_cast<float>(m_cachedPlans.size()) * (itemH + gap) - gap;
        const float popupH = std::min(286.0f, contentH + 12.0f);
        m_planPopupRc = D2D1::RectF(868.0f, 88.0f, 1104.0f, 88.0f + popupH);

        DrawRoundedBox(D2D1::RectF(m_planPopupRc.left - 1.0f, m_planPopupRc.top + 2.0f,
                                   m_planPopupRc.right + 1.0f, m_planPopupRc.bottom + 4.0f),
                       9.0f, D2dColor(15, 23, 42, 18), D2dColor(0, 0, 0, 0), 0.0f);
        DrawRoundedBox(m_planPopupRc, 8.0f, kClrWhite, kClrBlueBorder, 1.0f);

        const D2D1_RECT_F clipRc{m_planPopupRc.left + 6.0f, m_planPopupRc.top + 6.0f,
                                 m_planPopupRc.right - 6.0f, m_planPopupRc.bottom - 6.0f};
        const float viewH = clipRc.bottom - clipRc.top;
        m_planPopupMaxScroll = std::max(0.0f, contentH - viewH);
        m_planPopupScroll = std::clamp(m_planPopupScroll, 0.0f, m_planPopupMaxScroll);

        m_rt->PushAxisAlignedClip(clipRc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float y = clipRc.top - m_planPopupScroll;
        for (const auto& p : m_cachedPlans) {
            if (y + itemH >= clipRc.top && y <= clipRc.bottom) {
                const D2D1_RECT_F rowRc{clipRc.left, y, clipRc.right, y + itemH};
                RegisterHit(ControlId::PowerPlanItem, rowRc, true, 0, p.guidStr);
                const bool hov = IsControlHovered(ControlId::PowerPlanItem, 0, p.guidStr);
                DrawRoundedBox(rowRc, 6.0f,
                               p.isActive ? kClrBlueTint : (hov ? kClrSurface : kClrWhite),
                               p.isActive ? D2dColor(147, 197, 253) : D2dColor(0, 0, 0, 0), 1.0f);
                m_brush->SetColor(p.isActive ? kClrBlue : kClrSlate300);
                m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rowRc.left + 12.0f, y + itemH * 0.5f), 3.5f, 3.5f),
                                  m_brush.Get());
                DrawTextStrict(p.name, p.isActive ? m_fmtBold115.Get() : m_fmtSemi11.Get(),
                               D2D1::RectF(rowRc.left + 23.0f, y, rowRc.right - (p.isActive ? 54.0f : 8.0f), y + itemH),
                               p.isActive ? kClrBlueHover : kClrSlateDark);
                if (p.isActive) {
                    const D2D1_RECT_F tagRc{rowRc.right - 50.0f, y + 6.5f, rowRc.right - 6.0f, y + 23.5f};
                    DrawRoundedBox(tagRc, 4.0f, kClrBlue, D2dColor(0, 0, 0, 0), 0.0f);
                    DrawTextStrict(L"ACTIVE", m_fmtBold9.Get(), tagRc, kClrWhite,
                                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                }
            }
            y += itemH + gap;
        }
        m_rt->PopAxisAlignedClip();
    }

    if (m_runningProcPopupOpen) {
        const float itemH = 26.0f;
        const float gap = 3.0f;
        const float contentH = m_cachedProcs.empty()
            ? 32.0f
            : static_cast<float>(m_cachedProcs.size()) * (itemH + gap) - gap;
        const float popupH = std::min(244.0f, contentH + 12.0f);
        m_procPopupRc = D2D1::RectF(122.0f, 236.0f, 442.0f, 236.0f + popupH);

        DrawRoundedBox(D2D1::RectF(m_procPopupRc.left - 1.0f, m_procPopupRc.top + 2.0f,
                                   m_procPopupRc.right + 1.0f, m_procPopupRc.bottom + 4.0f),
                       9.0f, D2dColor(15, 23, 42, 18), D2dColor(0, 0, 0, 0), 0.0f);
        DrawRoundedBox(m_procPopupRc, 8.0f, kClrWhite, kClrBlueBorder, 1.0f);

        const D2D1_RECT_F clipRc{m_procPopupRc.left + 6.0f, m_procPopupRc.top + 6.0f,
                                 m_procPopupRc.right - 6.0f, m_procPopupRc.bottom - 6.0f};
        const float viewH = clipRc.bottom - clipRc.top;
        m_procPopupMaxScroll = std::max(0.0f, contentH - viewH);
        m_procPopupScroll = std::clamp(m_procPopupScroll, 0.0f, m_procPopupMaxScroll);

        m_rt->PushAxisAlignedClip(clipRc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (m_cachedProcs.empty()) {
            DrawTextStrict(L"No user processes found", m_fmtSemi11.Get(), clipRc, kClrSlate500,
                           DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        } else {
            float y = clipRc.top - m_procPopupScroll;
            for (const auto& p : m_cachedProcs) {
                if (y + itemH >= clipRc.top && y <= clipRc.bottom) {
                    const D2D1_RECT_F rowRc{clipRc.left, y, clipRc.right, y + itemH};
                    RegisterHit(ControlId::RunningProcItem, rowRc, true, 0, p.exeName);
                    const bool hov = IsControlHovered(ControlId::RunningProcItem, 0, p.exeName);
                    DrawRoundedBox(rowRc, 5.0f, hov ? kClrDivider : kClrWhite, D2dColor(0, 0, 0, 0), 0.0f);
                    m_brush->SetColor(p.isForeground ? kClrBlue : kClrSlate300);
                    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rowRc.left + 11.0f, y + itemH * 0.5f), 3.2f, 3.2f),
                                      m_brush.Get());
                    DrawTextStrict(p.exeName, m_fmtSemi11.Get(),
                                   D2D1::RectF(rowRc.left + 21.0f, y, rowRc.right - 36.0f, y + itemH), kClrInk);
                    if (p.instanceCount > 1) {
                        wchar_t cntBuf[16]{};
                        std::swprintf(cntBuf, 16, L"x%u", p.instanceCount);
                        DrawTextStrict(cntBuf, m_fmtSemi10.Get(),
                                       D2D1::RectF(rowRc.right - 36.0f, y, rowRc.right - 8.0f, y + itemH),
                                       kClrSlate500, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    }
                }
                y += itemH + gap;
            }
        }
        m_rt->PopAxisAlignedClip();
    }
}

void AppWindow::DrawSettingsView() {
    const float iLeft = 20.0f;
    const float iRight = 1100.0f;

    // Settings sub-header
    const D2D1_RECT_F backRc{iLeft, 53.0f, iLeft + 28.0f, 81.0f};
    RegisterHit(ControlId::SettingsBackBtn, backRc, true);
    if (IsControlHovered(ControlId::SettingsBackBtn) || IsControlPressed(ControlId::SettingsBackBtn)) {
        DrawRoundedBox(backRc, 6.0f,
                       IsControlPressed(ControlId::SettingsBackBtn) ? kClrBorder : kClrDivider,
                       D2dColor(0, 0, 0, 0), 0.0f);
    }
    m_brush->SetColor(kClrInk);
    icons::DrawIcon(m_rt.Get(), m_d2dFactory.Get(), m_brush.Get(), m_roundStroke.Get(),
                    icons::IconKind::ChevronLeft,
                    D2D1::RectF(backRc.left + 6.5f, backRc.top + 6.5f, backRc.right - 6.5f, backRc.bottom - 6.5f), 1.9f);
    DrawTextStrict(L"Settings", m_fmtBold145.Get(), D2D1::RectF(iLeft + 36.0f, 52.0f, 300.0f, 82.0f), kClrInk);

    m_brush->SetColor(kClrBorder);
    m_rt->DrawLine(D2D1::Point2F(iLeft, 89.5f), D2D1::Point2F(iRight, 89.5f), m_brush.Get(), 1.0f);

    float y = 96.0f;
    auto drawSettingToggle = [&](ControlId id, const wchar_t* label, bool on, bool enabled, bool dividerBelow = true) {
        DrawTextStrict(label, m_fmtReg125.Get(), D2D1::RectF(iLeft, y, iRight - 56.0f, y + 34.0f),
                       enabled ? kClrInk : kClrSlate400);
        DrawToggleSwitch(id, iRight, y + 17.0f, on, enabled);
        y += 34.0f;
        if (dividerBelow) {
            m_brush->SetColor(kClrDivider);
            m_rt->DrawLine(D2D1::Point2F(iLeft, y), D2D1::Point2F(iRight, y), m_brush.Get(), 1.0f);
            y += 1.0f;
        }
    };

    drawSettingToggle(ControlId::ToggleAutoStart,      L"Run at startup",   m_config.autoStart,      true);
    drawSettingToggle(ControlId::ToggleStartMinimized, L"Start minimized",  m_config.startMinimized, true);
    drawSettingToggle(ControlId::ToggleMinToTray,      L"Minimize to tray", m_config.minimizeToTray, true);
    drawSettingToggle(ControlId::ToggleCloseToTray,    L"Close to tray",    m_config.closeToTray,    true);

    // Tray icon selector row
    DrawTextStrict(L"Tray icon", m_fmtReg125.Get(), D2D1::RectF(iLeft, y, iRight - 160.0f, y + 36.0f), kClrInk);
    const D2D1_RECT_F trayBox{iRight - 134.0f, y + 5.0f, iRight, y + 31.0f};
    DrawRoundedBox(trayBox, 7.0f, kClrDivider, kClrBorder, 1.0f);
    const bool isPctTray = (m_config.trayStyle == L"percent");

    auto drawTrayPill = [&](ControlId id, float px, float pw, const wchar_t* label, bool sel) {
        const D2D1_RECT_F r{px, trayBox.top + 3.0f, px + pw, trayBox.bottom - 3.0f};
        RegisterHit(id, r, true);
        if (sel || IsControlHovered(id)) {
            DrawRoundedBox(r, 5.0f, sel ? kClrBlue : kClrBlueTint, D2dColor(0, 0, 0, 0), 0.0f);
        }
        DrawTextStrict(label, m_fmtSemi10.Get(), r, sel ? kClrWhite : kClrSlate600,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };
    drawTrayPill(ControlId::TrayModeLogo, trayBox.left + 3.0f, 56.0f, L"Icon", !isPctTray);
    drawTrayPill(ControlId::TrayModePercent, trayBox.left + 61.0f, 70.0f, L"CPU %", isPctTray);

    y += 36.0f;
    m_brush->SetColor(kClrDivider);
    m_rt->DrawLine(D2D1::Point2F(iLeft, y), D2D1::Point2F(iRight, y), m_brush.Get(), 1.0f);
    y += 1.0f;

    drawSettingToggle(ControlId::ToggleAlwaysOnTop,   L"Always on top", m_config.alwaysOnTop,       true);
    drawSettingToggle(ControlId::ToggleNotifications, L"Notifications", m_config.showNotifications, true, false);

    y += 12.0f;
    DrawTextStrict(L"PERFORMANCE & AUTOMATION", m_fmtBold9.Get(),
                   D2D1::RectF(iLeft, y, 400.0f, y + 18.0f), kClrBlue);
    y += 20.0f;

    drawSettingToggle(ControlId::ToggleApplyOnStart, L"Auto-apply VPower on launch",
                      m_config.applyOnStartup, m_cpuInfo.isIntel);
    drawSettingToggle(ControlId::ToggleHighResTimer, L"0.5ms kernel timer",
                      m_config.highResTimer, m_cpuInfo.isIntel, !m_isElevated);

    if (!m_isElevated) {
        DrawTextStrict(L"Administrator mode", m_fmtReg125.Get(),
                       D2D1::RectF(iLeft, y, iRight - 100.0f, y + 36.0f), kClrInk);
        const D2D1_RECT_F elevRc{iRight - 78.0f, y + 4.0f, iRight, y + 32.0f};
        RegisterHit(ControlId::SettingsElevateBtn, elevRc, true);
        DrawRoundedBox(elevRc, 6.0f,
                       IsControlHovered(ControlId::SettingsElevateBtn) ? kClrBlueTint : kClrWhite,
                       kClrSlate300, 1.0f);
        DrawTextStrict(L"Elevate", m_fmtBold115.Get(), elevRc, kClrBlue,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Settings Footer
    const float footerTop = 556.0f;
    m_brush->SetColor(kClrBorder);
    m_rt->DrawLine(D2D1::Point2F(iLeft, footerTop), D2D1::Point2F(iRight, footerTop), m_brush.Get(), 1.0f);

    const float brandW = MeasureTextWidth(L"VOverclock Intel", m_fmtBold135.Get());
    const float brandRight = iLeft + std::max(106.0f, brandW);
    DrawTextStrict(L"VOverclock Intel", m_fmtBold135.Get(), D2D1::RectF(iLeft, 571.0f, brandRight + 4.0f, 591.0f), kClrInk);
    DrawTextStrict(L"Beta Phase 2", m_fmtBold115.Get(), D2D1::RectF(brandRight + 7.0f, 572.0f, brandRight + 140.0f, 591.0f), kClrBlue);
    DrawTextStrict(L"Created by : @vinnotdkk", m_fmtReg11.Get(), D2D1::RectF(iLeft, 592.0f, 320.0f, 610.0f), kClrSlate600);

    const D2D1_RECT_F supportRc{iRight - 152.0f, 573.0f, iRight, 607.0f};
    RegisterHit(ControlId::SupportDevBtn, supportRc, true);
    const bool sHov = IsControlHovered(ControlId::SupportDevBtn);
    const bool sPrs = IsControlPressed(ControlId::SupportDevBtn);
    DrawRoundedBox(supportRc, 7.0f, sPrs ? kClrBluePress : (sHov ? kClrBlueHover : kClrBlue), D2dColor(0, 0, 0, 0), 0.0f);
    m_brush->SetColor(kClrWhite);
    icons::DrawIcon(m_rt.Get(), m_d2dFactory.Get(), m_brush.Get(), m_roundStroke.Get(),
                    icons::IconKind::Heart, D2D1::RectF(supportRc.left + 14.0f, 583.5f, supportRc.left + 27.0f, 596.5f), 1.5f);
    DrawTextStrict(L"Support Developer", m_fmtBold115.Get(),
                   D2D1::RectF(supportRc.left + 31.0f, supportRc.top, supportRc.right - 10.0f, supportRc.bottom),
                   kClrWhite);
}

// --- System Tray Integration ---

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
    wcscpy_s(nid.szTip, L"VOverclock Intel Beta Phase 2");

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
        wcsncpy_s(nid.szTip, L"VOverclock Intel Beta Phase 2 - AMD CPU Blocked", _TRUNCATE);
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

} // namespace vint
