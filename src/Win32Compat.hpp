#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace vint::compat {

/// Returns system DPI cleanly on both modern Windows 10/11 and older builds (Win7/8/early Win10).
inline UINT GetDpiForSystemCompat() noexcept {
    using PfnGetDpiForSystem = UINT(WINAPI*)();
    static const auto pfn = reinterpret_cast<PfnGetDpiForSystem>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
    if (pfn) {
        const UINT d = pfn();
        if (d > 0) return d;
    }
    if (HDC hdc = GetDC(nullptr)) {
        const int px = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        if (px > 0) return static_cast<UINT>(px);
    }
    return 96;
}

/// Returns per-window DPI dynamically without hard-linking Win10 1607 user32 exports.
inline UINT GetDpiForWindowCompat(HWND hwnd) noexcept {
    if (hwnd) {
        using PfnGetDpiForWindow = UINT(WINAPI*)(HWND);
        static const auto pfn = reinterpret_cast<PfnGetDpiForWindow>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (pfn) {
            const UINT d = pfn(hwnd);
            if (d > 0) return d;
        }
    }
    return GetDpiForSystemCompat();
}

/// Returns DPI-scaled system metric without hard-linking GetSystemMetricsForDpi.
inline int GetSystemMetricsForDpiCompat(int index, UINT dpi) noexcept {
    using PfnGetSystemMetricsForDpi = int(WINAPI*)(int, UINT);
    static const auto pfn = reinterpret_cast<PfnGetSystemMetricsForDpi>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
    if (pfn) {
        const int v = pfn(index, dpi);
        if (v > 0) return v;
    }
    const UINT sysDpi = GetDpiForSystemCompat();
    const int base = GetSystemMetrics(index);
    return (sysDpi > 0) ? MulDiv(base, static_cast<int>(dpi), static_cast<int>(sysDpi)) : base;
}

inline int ScaleDip(float dip, UINT dpi) noexcept {
    return static_cast<int>(std::lround(dip * (static_cast<float>(dpi) / 96.0f)));
}

/// Decodes raw PNG bytes in memory into premultiplied 32bpp PBGRA pixels via WIC.
inline bool DecodePngToPbgra(const uint8_t* pngBytes,
                              size_t size,
                              uint32_t& outW,
                              uint32_t& outH,
                              std::vector<uint8_t>& outPixels) {
    outW = 0;
    outH = 0;
    outPixels.clear();
    if (!pngBytes || size == 0) return false;

    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> wicFactory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wicFactory)))) {
        return false;
    }

    ComPtr<IWICStream> stream;
    if (FAILED(wicFactory->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(pngBytes), static_cast<DWORD>(size)))) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wicFactory->CreateDecoderFromStream(stream.Get(), nullptr,
                                                   WICDecodeMetadataCacheOnDemand, &decoder))) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(&converter))) return false;
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return false;
    }

    const UINT stride = w * 4;
    std::vector<uint8_t> buf(static_cast<size_t>(stride) * h);
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(buf.size()), buf.data()))) {
        return false;
    }

    outW = w;
    outH = h;
    outPixels = std::move(buf);
    return true;
}

} // namespace vint::compat
