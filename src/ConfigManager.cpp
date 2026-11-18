#include "ConfigManager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>

using Microsoft::WRL::ComPtr;

namespace vint {

namespace {

constexpr wchar_t kRunSubKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"VOverclockIntel";

bool ParseBool(const wchar_t* val, bool fallback) {
    if (!val) return fallback;
    if (_wcsicmp(val, L"true") == 0 || _wcsicmp(val, L"1") == 0) return true;
    if (_wcsicmp(val, L"false") == 0 || _wcsicmp(val, L"0") == 0) return false;
    return fallback;
}

int ParseInt(const wchar_t* val, int fallback) {
    if (!val || !*val) return fallback;
    wchar_t* end = nullptr;
    long v = wcstol(val, &end, 10);
    if (end == val) return fallback;
    return static_cast<int>(v);
}

std::wstring NormalizeExeName(const std::wstring& raw) {
    std::wstring s = raw;
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    const size_t slash = s.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        s = s.substr(slash + 1);
    }
    return s;
}

std::wstring GetExePathQuoted(bool startMinimized) {
    wchar_t exePath[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    std::wstring cmd = L"\"";
    cmd += exePath;
    cmd += L"\"";
    if (startMinimized) {
        cmd += L" --minimized";
    }
    return cmd;
}

}

std::wstring ConfigManager::GetConfigFilePath() {
    std::wstring dir;
    PWSTR appDataPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath)) && appDataPath) {
        dir = appDataPath;
        CoTaskMemFree(appDataPath);
    } else {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        PathRemoveFileSpecW(exePath);
        dir = exePath;
    }

    dir += L"\\VOverclockIntel";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir + L"\\config.xml";
}

AppConfig ConfigManager::Load() {
    AppConfig cfg{};
    const std::wstring path = GetConfigFilePath();

    ComPtr<IStream> stream;
    if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
                                      FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream))) {
        cfg.autoStart = IsAutoStartupEnabled();
        return cfg;
    }

    ComPtr<IXmlReader> reader;
    if (FAILED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr))) {
        cfg.autoStart = IsAutoStartupEnabled();
        return cfg;
    }

    if (FAILED(reader->SetInput(stream.Get()))) {
        cfg.autoStart = IsAutoStartupEnabled();
        return cfg;
    }

    XmlNodeType nodeType = XmlNodeType_None;
    while (reader->Read(&nodeType) == S_OK) {
        if (nodeType != XmlNodeType_Element) {
            continue;
        }
        const wchar_t* elemName = nullptr;
        if (FAILED(reader->GetLocalName(&elemName, nullptr)) || !elemName) {
            continue;
        }

        if (wcscmp(elemName, L"Startup") == 0) {
            for (HRESULT hr = reader->MoveToFirstAttribute(); hr == S_OK; hr = reader->MoveToNextAttribute()) {
                const wchar_t* name = nullptr;
                const wchar_t* val = nullptr;
                reader->GetLocalName(&name, nullptr);
                reader->GetValue(&val, nullptr);
                if (!name || !val) continue;
                if (wcscmp(name, L"autoStart") == 0) cfg.autoStart = ParseBool(val, cfg.autoStart);
                else if (wcscmp(name, L"startMinimized") == 0) cfg.startMinimized = ParseBool(val, cfg.startMinimized);
                else if (wcscmp(name, L"applyOnStartup") == 0) cfg.applyOnStartup = ParseBool(val, cfg.applyOnStartup);
                else if (wcscmp(name, L"highResTimer") == 0) cfg.highResTimer = ParseBool(val, cfg.highResTimer);
                else if (wcscmp(name, L"liveApply") == 0) cfg.liveApply = ParseBool(val, cfg.liveApply);
            }
        } else if (wcscmp(elemName, L"Window") == 0) {
            for (HRESULT hr = reader->MoveToFirstAttribute(); hr == S_OK; hr = reader->MoveToNextAttribute()) {
                const wchar_t* name = nullptr;
                const wchar_t* val = nullptr;
                reader->GetLocalName(&name, nullptr);
                reader->GetValue(&val, nullptr);
                if (!name || !val) continue;
                if (wcscmp(name, L"minimizeToTray") == 0) cfg.minimizeToTray = ParseBool(val, cfg.minimizeToTray);
                else if (wcscmp(name, L"closeToTray") == 0) cfg.closeToTray = ParseBool(val, cfg.closeToTray);
                else if (wcscmp(name, L"alwaysOnTop") == 0) cfg.alwaysOnTop = ParseBool(val, cfg.alwaysOnTop);
                else if (wcscmp(name, L"trayStyle") == 0) cfg.trayStyle = (wcscmp(val, L"percent") == 0) ? L"percent" : L"logo";
                else if (wcscmp(name, L"showNotifications") == 0) cfg.showNotifications = ParseBool(val, cfg.showNotifications);
            }
        } else if (wcscmp(elemName, L"Tuning") == 0) {
            for (HRESULT hr = reader->MoveToFirstAttribute(); hr == S_OK; hr = reader->MoveToNextAttribute()) {
                const wchar_t* name = nullptr;
                const wchar_t* val = nullptr;
                reader->GetLocalName(&name, nullptr);
                reader->GetValue(&val, nullptr);
                if (!name || !val) continue;
                if (wcscmp(name, L"boostMode") == 0) cfg.boostMode = std::clamp(ParseInt(val, cfg.boostMode), 0, 6);
                else if (wcscmp(name, L"turboBoostPolicy") == 0) cfg.turboBoostPolicy = std::clamp(ParseInt(val, cfg.turboBoostPolicy), 0, 100);
                else if (wcscmp(name, L"eppPercent") == 0) cfg.eppPercent = std::clamp(ParseInt(val, cfg.eppPercent), 0, 100);
                else if (wcscmp(name, L"minStatePercent") == 0) cfg.minStatePercent = std::clamp(ParseInt(val, cfg.minStatePercent), 5, 100);
                else if (wcscmp(name, L"maxStatePercent") == 0) cfg.maxStatePercent = std::clamp(ParseInt(val, cfg.maxStatePercent), 20, 100);
                else if (wcscmp(name, L"freqCapMhz") == 0) cfg.freqCapMhz = std::clamp(ParseInt(val, cfg.freqCapMhz), 0, 8000);
                else if (wcscmp(name, L"unparkCoresPercent") == 0) cfg.unparkCoresPercent = std::clamp(ParseInt(val, cfg.unparkCoresPercent), 0, 100);
                else if (wcscmp(name, L"schedPolicy") == 0) cfg.schedPolicy = std::clamp(ParseInt(val, cfg.schedPolicy), 0, 5);
                else if (wcscmp(name, L"incThreshold") == 0) cfg.incThreshold = std::clamp(ParseInt(val, cfg.incThreshold), 5, 100);
                else if (wcscmp(name, L"decThreshold") == 0) cfg.decThreshold = std::clamp(ParseInt(val, cfg.decThreshold), 2, 100);
                else if (wcscmp(name, L"heteroFloorPerf") == 0) cfg.heteroFloorPerf = std::clamp(ParseInt(val, cfg.heteroFloorPerf), 0, 100);
                else if (wcscmp(name, L"concurrencyThreshold") == 0) cfg.concurrencyThreshold = std::clamp(ParseInt(val, cfg.concurrencyThreshold), 5, 100);
                else if (wcscmp(name, L"activeCooling") == 0) cfg.activeCooling = ParseBool(val, cfg.activeCooling);
                else if (wcscmp(name, L"hwpAutonomous") == 0) cfg.hwpAutonomous = ParseBool(val, cfg.hwpAutonomous);
                else if (wcscmp(name, L"disableIdleStates") == 0) cfg.disableIdleStates = ParseBool(val, cfg.disableIdleStates);
                else if (wcscmp(name, L"disableOsThrottling") == 0) cfg.disableOsThrottling = ParseBool(val, cfg.disableOsThrottling);
                else if (wcscmp(name, L"boostForegroundPCore") == 0) cfg.boostForegroundPCore = ParseBool(val, cfg.boostForegroundPCore);
            }
        } else if (wcscmp(elemName, L"TargetProfile") == 0) {
            for (HRESULT hr = reader->MoveToFirstAttribute(); hr == S_OK; hr = reader->MoveToNextAttribute()) {
                const wchar_t* name = nullptr;
                const wchar_t* val = nullptr;
                reader->GetLocalName(&name, nullptr);
                reader->GetValue(&val, nullptr);
                if (!name || !val) continue;
                if (wcscmp(name, L"priorityMode") == 0) cfg.targetPriorityMode = std::clamp(ParseInt(val, cfg.targetPriorityMode), 0, 3);
                else if (wcscmp(name, L"coreMode") == 0) cfg.targetCoreMode = std::clamp(ParseInt(val, cfg.targetCoreMode), 0, 2);
                else if (wcscmp(name, L"deepBoost") == 0) cfg.targetDeepBoost = ParseBool(val, cfg.targetDeepBoost);
                else if (wcscmp(name, L"ifeoPersist") == 0) cfg.targetIfeoPersist = ParseBool(val, cfg.targetIfeoPersist);
            }
        } else if (wcscmp(elemName, L"TargetExe") == 0) {
            TargetExeRule rule{};
            for (HRESULT hr = reader->MoveToFirstAttribute(); hr == S_OK; hr = reader->MoveToNextAttribute()) {
                const wchar_t* name = nullptr;
                const wchar_t* val = nullptr;
                reader->GetLocalName(&name, nullptr);
                reader->GetValue(&val, nullptr);
                if (!name || !val) continue;
                if (wcscmp(name, L"name") == 0) rule.exeName = NormalizeExeName(val);
                else if (wcscmp(name, L"enabled") == 0) rule.enabled = ParseBool(val, true);
            }
            if (!rule.exeName.empty()) {
                bool dup = false;
                for (const auto& ex : cfg.targetExes) {
                    if (_wcsicmp(ex.exeName.c_str(), rule.exeName.c_str()) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    cfg.targetExes.push_back(std::move(rule));
                }
            }
        } else if (wcscmp(elemName, L"PowerSchemes") == 0) {
            for (HRESULT hr = reader->MoveToFirstAttribute(); hr == S_OK; hr = reader->MoveToNextAttribute()) {
                const wchar_t* name = nullptr;
                const wchar_t* val = nullptr;
                reader->GetLocalName(&name, nullptr);
                reader->GetValue(&val, nullptr);
                if (!name || !val) continue;
                if (wcscmp(name, L"originalGuid") == 0) cfg.originalSchemeGuid = val;
                else if (wcscmp(name, L"vpowerGuid") == 0) cfg.vpowerSchemeGuid = val;
            }
        }
    }

    cfg.autoStart = IsAutoStartupEnabled() || cfg.autoStart;
    return cfg;
}

bool ConfigManager::Save(const AppConfig& cfg) {
    const std::wstring path = GetConfigFilePath();

    ComPtr<IStream> stream;
    if (FAILED(SHCreateStreamOnFileEx(path.c_str(),
                                      STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
                                      FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream))) {
        return false;
    }

    ComPtr<IXmlWriter> writer;
    if (FAILED(CreateXmlWriter(__uuidof(IXmlWriter), reinterpret_cast<void**>(writer.GetAddressOf()), nullptr))) {
        return false;
    }

    if (FAILED(writer->SetOutput(stream.Get()))) {
        return false;
    }
    writer->SetProperty(XmlWriterProperty_Indent, TRUE);

    auto bstr = [](bool v) { return v ? L"true" : L"false"; };

    writer->WriteStartDocument(XmlStandalone_Omit);
    writer->WriteStartElement(nullptr, L"VOverclockIntelConfig", nullptr);
    writer->WriteAttributeString(nullptr, L"version", nullptr, L"1.0");

    writer->WriteStartElement(nullptr, L"Startup", nullptr);
    writer->WriteAttributeString(nullptr, L"autoStart", nullptr, bstr(cfg.autoStart));
    writer->WriteAttributeString(nullptr, L"startMinimized", nullptr, bstr(cfg.startMinimized));
    writer->WriteAttributeString(nullptr, L"applyOnStartup", nullptr, bstr(cfg.applyOnStartup));
    writer->WriteAttributeString(nullptr, L"highResTimer", nullptr, bstr(cfg.highResTimer));
    writer->WriteAttributeString(nullptr, L"liveApply", nullptr, bstr(cfg.liveApply));
    writer->WriteEndElement();

    writer->WriteStartElement(nullptr, L"Window", nullptr);
    writer->WriteAttributeString(nullptr, L"minimizeToTray", nullptr, bstr(cfg.minimizeToTray));
    writer->WriteAttributeString(nullptr, L"closeToTray", nullptr, bstr(cfg.closeToTray));
    writer->WriteAttributeString(nullptr, L"alwaysOnTop", nullptr, bstr(cfg.alwaysOnTop));
    writer->WriteAttributeString(nullptr, L"trayStyle", nullptr, cfg.trayStyle.c_str());
    writer->WriteAttributeString(nullptr, L"showNotifications", nullptr, bstr(cfg.showNotifications));
    writer->WriteEndElement();

    writer->WriteStartElement(nullptr, L"Tuning", nullptr);
    writer->WriteAttributeString(nullptr, L"boostMode", nullptr, std::to_wstring(cfg.boostMode).c_str());
    writer->WriteAttributeString(nullptr, L"turboBoostPolicy", nullptr, std::to_wstring(cfg.turboBoostPolicy).c_str());
    writer->WriteAttributeString(nullptr, L"eppPercent", nullptr, std::to_wstring(cfg.eppPercent).c_str());
    writer->WriteAttributeString(nullptr, L"minStatePercent", nullptr, std::to_wstring(cfg.minStatePercent).c_str());
    writer->WriteAttributeString(nullptr, L"maxStatePercent", nullptr, std::to_wstring(cfg.maxStatePercent).c_str());
    writer->WriteAttributeString(nullptr, L"freqCapMhz", nullptr, std::to_wstring(cfg.freqCapMhz).c_str());
    writer->WriteAttributeString(nullptr, L"unparkCoresPercent", nullptr, std::to_wstring(cfg.unparkCoresPercent).c_str());
    writer->WriteAttributeString(nullptr, L"schedPolicy", nullptr, std::to_wstring(cfg.schedPolicy).c_str());
    writer->WriteAttributeString(nullptr, L"incThreshold", nullptr, std::to_wstring(cfg.incThreshold).c_str());
    writer->WriteAttributeString(nullptr, L"decThreshold", nullptr, std::to_wstring(cfg.decThreshold).c_str());
    writer->WriteAttributeString(nullptr, L"heteroFloorPerf", nullptr, std::to_wstring(cfg.heteroFloorPerf).c_str());
    writer->WriteAttributeString(nullptr, L"concurrencyThreshold", nullptr, std::to_wstring(cfg.concurrencyThreshold).c_str());
    writer->WriteAttributeString(nullptr, L"activeCooling", nullptr, bstr(cfg.activeCooling));
    writer->WriteAttributeString(nullptr, L"hwpAutonomous", nullptr, bstr(cfg.hwpAutonomous));
    writer->WriteAttributeString(nullptr, L"disableIdleStates", nullptr, bstr(cfg.disableIdleStates));
    writer->WriteAttributeString(nullptr, L"disableOsThrottling", nullptr, bstr(cfg.disableOsThrottling));
    writer->WriteAttributeString(nullptr, L"boostForegroundPCore", nullptr, bstr(cfg.boostForegroundPCore));
    writer->WriteEndElement();

    writer->WriteStartElement(nullptr, L"TargetProfile", nullptr);
    writer->WriteAttributeString(nullptr, L"priorityMode", nullptr, std::to_wstring(cfg.targetPriorityMode).c_str());
    writer->WriteAttributeString(nullptr, L"coreMode", nullptr, std::to_wstring(cfg.targetCoreMode).c_str());
    writer->WriteAttributeString(nullptr, L"deepBoost", nullptr, bstr(cfg.targetDeepBoost));
    writer->WriteAttributeString(nullptr, L"ifeoPersist", nullptr, bstr(cfg.targetIfeoPersist));
    writer->WriteEndElement();

    writer->WriteStartElement(nullptr, L"Targets", nullptr);
    for (const auto& rule : cfg.targetExes) {
        const std::wstring clean = NormalizeExeName(rule.exeName);
        if (clean.empty()) continue;
        writer->WriteStartElement(nullptr, L"TargetExe", nullptr);
        writer->WriteAttributeString(nullptr, L"name", nullptr, clean.c_str());
        writer->WriteAttributeString(nullptr, L"enabled", nullptr, bstr(rule.enabled));
        writer->WriteEndElement();
    }
    writer->WriteEndElement();

    writer->WriteStartElement(nullptr, L"PowerSchemes", nullptr);
    writer->WriteAttributeString(nullptr, L"originalGuid", nullptr, cfg.originalSchemeGuid.c_str());
    writer->WriteAttributeString(nullptr, L"vpowerGuid", nullptr, cfg.vpowerSchemeGuid.c_str());
    writer->WriteEndElement();

    writer->WriteEndElement();
    writer->WriteEndDocument();
    writer->Flush();
    return true;
}

bool ConfigManager::IsAutoStartupEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    LONG status = RegQueryValueExW(hKey, kRunValueName, nullptr, &type, nullptr, &size);
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && size > sizeof(wchar_t);
}

bool ConfigManager::SetAutoStartup(bool enabled, bool startMinimized) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    if (!enabled) {
        LONG del = RegDeleteValueW(hKey, kRunValueName);
        RegCloseKey(hKey);
        return del == ERROR_SUCCESS || del == ERROR_FILE_NOT_FOUND;
    }

    const std::wstring cmd = GetExePathQuoted(startMinimized);
    if (cmd.empty()) {
        RegCloseKey(hKey);
        return false;
    }

    const DWORD bytes = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
    LONG status = RegSetValueExW(hKey, kRunValueName, 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(cmd.c_str()), bytes);
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

}
