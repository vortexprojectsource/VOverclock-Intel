#include "VectorIcons.hpp"

#include <wrl/client.h>

#include <cmath>

using Microsoft::WRL::ComPtr;

namespace vint::icons {

namespace {

constexpr float kPi = 3.14159265358979323846f;

void AddOpenPolyline(ID2D1GeometrySink* sink, const D2D1_POINT_2F* pts, size_t count) {
    if (!pts || count < 2) return;
    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLines(pts + 1, static_cast<UINT32>(count - 1));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
}

ComPtr<ID2D1PathGeometry> BuildGearOuterPath(ID2D1Factory* factory) {
    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(factory->CreatePathGeometry(&path))) return nullptr;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink))) return nullptr;

    // Parametric 8-tooth precision cog centered at (12, 12) on 24x24 grid.
    constexpr int kTeeth = 8;
    constexpr int kPtsPerTooth = 4;
    constexpr float cx = 12.0f;
    constexpr float cy = 12.0f;
    constexpr float rInner = 6.9f;
    constexpr float rOuter = 9.6f;

    D2D1_POINT_2F pts[kTeeth * kPtsPerTooth]{};
    const float sector = (2.0f * kPi) / static_cast<float>(kTeeth);
    for (int i = 0; i < kTeeth; ++i) {
        const float a0 = sector * static_cast<float>(i);
        const float angles[4] = {
            a0 - sector * 0.24f,
            a0 - sector * 0.14f,
            a0 + sector * 0.14f,
            a0 + sector * 0.24f
        };
        const float radii[4] = {rInner, rOuter, rOuter, rInner};
        for (int k = 0; k < 4; ++k) {
            pts[i * 4 + k] = D2D1::Point2F(
                cx + radii[k] * std::cos(angles[k]),
                cy + radii[k] * std::sin(angles[k])
            );
        }
    }

    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLines(pts + 1, static_cast<UINT32>(kTeeth * kPtsPerTooth - 1));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return path;
}

ComPtr<ID2D1PathGeometry> BuildHeartPath(ID2D1Factory* factory) {
    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(factory->CreatePathGeometry(&path))) return nullptr;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink))) return nullptr;

    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(D2D1::Point2F(12.0f, 20.6f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(6.5f, 16.9f),
        D2D1::Point2F(2.5f, 13.0f),
        D2D1::Point2F(2.5f, 8.6f)
    ));
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(2.5f, 5.4f),
        D2D1::Point2F(5.0f, 3.6f),
        D2D1::Point2F(7.6f, 3.6f)
    ));
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(9.5f, 3.6f),
        D2D1::Point2F(11.1f, 4.7f),
        D2D1::Point2F(12.0f, 6.2f)
    ));
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(12.9f, 4.7f),
        D2D1::Point2F(14.5f, 3.6f),
        D2D1::Point2F(16.4f, 3.6f)
    ));
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(19.0f, 3.6f),
        D2D1::Point2F(21.5f, 5.4f),
        D2D1::Point2F(21.5f, 8.6f)
    ));
    sink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(21.5f, 13.0f),
        D2D1::Point2F(17.5f, 16.9f),
        D2D1::Point2F(12.0f, 20.6f)
    ));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return path;
}

} // namespace

void DrawIcon(ID2D1RenderTarget* rt,
              ID2D1Factory* factory,
              ID2D1SolidColorBrush* brush,
              ID2D1StrokeStyle* roundStroke,
              IconKind kind,
              const D2D1_RECT_F& bounds,
              float strokeWidth,
              float rotationDeg) {
    if (!rt || !factory || !brush) return;
    const float w = bounds.right - bounds.left;
    const float h = bounds.bottom - bounds.top;
    if (w <= 0.0f || h <= 0.0f) return;

    const float side = (w < h) ? w : h;
    const float scale = side / 24.0f;
    const float tx = bounds.left + (w - side) * 0.5f;
    const float ty = bounds.top + (h - side) * 0.5f;

    D2D1_MATRIX_3X2_F oldXform{};
    rt->GetTransform(&oldXform);

    D2D1::Matrix3x2F mat = D2D1::Matrix3x2F::Identity();
    if (std::fabs(rotationDeg) > 0.01f) {
        mat = D2D1::Matrix3x2F::Rotation(rotationDeg, D2D1::Point2F(12.0f, 12.0f));
    }
    mat = mat * D2D1::Matrix3x2F::Scale(scale, scale) *
          D2D1::Matrix3x2F::Translation(tx, ty);
    rt->SetTransform(mat * (*D2D1::Matrix3x2F::ReinterpretBaseType(&oldXform)));

    // Keep stroke crisp in icon space.
    const float sw = (scale > 0.01f) ? (strokeWidth / scale) : strokeWidth;

    switch (kind) {
    case IconKind::Minimize:
        rt->DrawLine(D2D1::Point2F(5.0f, 12.0f), D2D1::Point2F(19.0f, 12.0f),
                     brush, sw, roundStroke);
        break;

    case IconKind::Close:
        rt->DrawLine(D2D1::Point2F(6.0f, 6.0f), D2D1::Point2F(18.0f, 18.0f),
                     brush, sw, roundStroke);
        rt->DrawLine(D2D1::Point2F(18.0f, 6.0f), D2D1::Point2F(6.0f, 18.0f),
                     brush, sw, roundStroke);
        break;

    case IconKind::ChevronDown: {
        const D2D1_POINT_2F pts[3] = {{6.0f, 9.0f}, {12.0f, 15.0f}, {18.0f, 9.0f}};
        ComPtr<ID2D1PathGeometry> p;
        if (SUCCEEDED(factory->CreatePathGeometry(&p))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(p->Open(&sink))) {
                AddOpenPolyline(sink.Get(), pts, 3);
                sink->Close();
                rt->DrawGeometry(p.Get(), brush, sw, roundStroke);
            }
        }
        break;
    }

    case IconKind::ChevronLeft: {
        const D2D1_POINT_2F pts[3] = {{15.0f, 6.0f}, {9.0f, 12.0f}, {15.0f, 18.0f}};
        ComPtr<ID2D1PathGeometry> p;
        if (SUCCEEDED(factory->CreatePathGeometry(&p))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(p->Open(&sink))) {
                AddOpenPolyline(sink.Get(), pts, 3);
                sink->Close();
                rt->DrawGeometry(p.Get(), brush, sw, roundStroke);
            }
        }
        break;
    }

    case IconKind::ChevronRight: {
        const D2D1_POINT_2F pts[3] = {{9.0f, 6.0f}, {15.0f, 12.0f}, {9.0f, 18.0f}};
        ComPtr<ID2D1PathGeometry> p;
        if (SUCCEEDED(factory->CreatePathGeometry(&p))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(p->Open(&sink))) {
                AddOpenPolyline(sink.Get(), pts, 3);
                sink->Close();
                rt->DrawGeometry(p.Get(), brush, sw, roundStroke);
            }
        }
        break;
    }

    case IconKind::BackArrow: {
        rt->DrawLine(D2D1::Point2F(19.0f, 12.0f), D2D1::Point2F(6.0f, 12.0f),
                     brush, sw, roundStroke);
        const D2D1_POINT_2F pts[3] = {{12.0f, 18.0f}, {6.0f, 12.0f}, {12.0f, 6.0f}};
        ComPtr<ID2D1PathGeometry> p;
        if (SUCCEEDED(factory->CreatePathGeometry(&p))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(p->Open(&sink))) {
                AddOpenPolyline(sink.Get(), pts, 3);
                sink->Close();
                rt->DrawGeometry(p.Get(), brush, sw, roundStroke);
            }
        }
        break;
    }

    case IconKind::SettingsGear: {
        if (auto path = BuildGearOuterPath(factory)) {
            rt->DrawGeometry(path.Get(), brush, sw, roundStroke);
        }
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(12.0f, 12.0f), 3.1f, 3.1f),
                        brush, sw, roundStroke);
        break;
    }

    case IconKind::TrayMinimize: {
        const D2D1_ROUNDED_RECT box = D2D1::RoundedRect(D2D1::RectF(3.0f, 4.0f, 21.0f, 20.0f), 2.0f, 2.0f);
        rt->DrawRoundedRectangle(box, brush, sw, roundStroke);
        rt->DrawLine(D2D1::Point2F(3.0f, 15.0f), D2D1::Point2F(21.0f, 15.0f),
                     brush, sw, roundStroke);
        const D2D1_POINT_2F pts[3] = {{9.5f, 8.8f}, {12.0f, 11.6f}, {14.5f, 8.8f}};
        ComPtr<ID2D1PathGeometry> p;
        if (SUCCEEDED(factory->CreatePathGeometry(&p))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(p->Open(&sink))) {
                AddOpenPolyline(sink.Get(), pts, 3);
                sink->Close();
                rt->DrawGeometry(p.Get(), brush, sw, roundStroke);
            }
        }
        break;
    }

    case IconKind::ShieldAlert: {
        const D2D1_POINT_2F shieldPts[7] = {
            {12.0f, 2.5f},
            {4.0f, 5.5f},
            {4.0f, 11.8f},
            {12.0f, 21.5f},
            {20.0f, 11.8f},
            {20.0f, 5.5f},
            {12.0f, 2.5f}
        };
        ComPtr<ID2D1PathGeometry> p;
        if (SUCCEEDED(factory->CreatePathGeometry(&p))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(p->Open(&sink))) {
                AddOpenPolyline(sink.Get(), shieldPts, 7);
                sink->Close();
                rt->DrawGeometry(p.Get(), brush, sw, roundStroke);
            }
        }
        rt->DrawLine(D2D1::Point2F(12.0f, 8.2f), D2D1::Point2F(12.0f, 13.0f),
                     brush, sw, roundStroke);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(12.0f, 16.4f), sw * 0.58f, sw * 0.58f), brush);
        break;
    }

    case IconKind::Heart: {
        if (auto heart = BuildHeartPath(factory)) {
            rt->FillGeometry(heart.Get(), brush);
        }
        break;
    }
    }

    rt->SetTransform(oldXform);
}

} // namespace vint::icons
