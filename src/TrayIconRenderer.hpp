#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vint {

class TrayIconRenderer {
public:
    TrayIconRenderer();
    ~TrayIconRenderer();

    HICON CreateLogoIcon(HINSTANCE hInst, UINT dpi);
    HICON CreateCpuPercentIcon(uint32_t percent, bool blockedAmd, UINT dpi);

    static std::vector<uint8_t> LoadLogoPngBytes(HINSTANCE hInst);

private:
    static HICON BitmapPixelsToIcon(const uint8_t* bgraPixels, int width, int height);
};

}
