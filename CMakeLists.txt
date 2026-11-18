cmake_minimum_required(VERSION 3.20)
project(VOverclockIntel VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(VOverclockIntel WIN32
    src/main.cpp
    src/AppWindow.cpp
    src/AppWindow.hpp
    src/CpuIntelEngine.cpp
    src/CpuIntelEngine.hpp
    src/ConfigManager.cpp
    src/ConfigManager.hpp
    src/TrayIconRenderer.cpp
    src/TrayIconRenderer.hpp
    resources/VOverclock.rc
    resources/app.manifest
)

set_source_files_properties(resources/VOverclock.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/AppLayout.xaml;${CMAKE_CURRENT_SOURCE_DIR}/assets/logo_ui.png;${CMAKE_CURRENT_SOURCE_DIR}/assets/vointel.ico"
)

target_include_directories(VOverclockIntel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
)

target_compile_definitions(VOverclockIntel PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(VOverclockIntel PRIVATE
        /W4
        /EHsc
        /permissive-
        /utf-8
        /Zc:__cplusplus
    )
    target_link_options(VOverclockIntel PRIVATE
        /SUBSYSTEM:WINDOWS
    )
endif()

target_link_libraries(VOverclockIntel PRIVATE
    windowsapp
    user32
    gdi32
    shell32
    advapi32
    psapi
    dwmapi
    comctl32
    comdlg32
    shlwapi
    xmllite
    d2d1
    d3d11
    dwrite
    windowscodecs
    ole32
    oleaut32
    wbemuuid
    powrprof
    pdh
)
