#pragma once

#include <d2d1.h>

namespace vint::icons {

enum class IconKind {
    Minimize,
    Close,
    ChevronDown,
    ChevronLeft,
    ChevronRight,
    BackArrow,
    SettingsGear,
    TrayMinimize,
    ShieldAlert,
    Heart
};

/// Draws a crisp vector icon scaled inside `bounds` (mapped from a normalized 24x24 viewBox).
void DrawIcon(ID2D1RenderTarget* rt,
              ID2D1Factory* factory,
              ID2D1SolidColorBrush* brush,
              ID2D1StrokeStyle* roundStroke,
              IconKind kind,
              const D2D1_RECT_F& bounds,
              float strokeWidth = 1.8f,
              float rotationDeg = 0.0f);

} // namespace vint::icons
