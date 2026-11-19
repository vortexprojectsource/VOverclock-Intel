#include "CpuIntelEngine.hpp"

#include <intrin.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <powrprof.h>
#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wbemidl.h>
#include <comdef.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vint {

namespace {

constexpr wchar_t kVPowerPlanName[] = L"VPower Intel Beta";
constexpr wchar_t kLegacyVPowerPlanName[] = L"VPower Intel V1.0";
constexpr wchar_t kVPowerPlanDesc[] =
    L"Custom Intel Hybrid P-Core & E-Core Maximum Performance Power Plan [Balanced-HWP-V2] (VOverclock Intel Beta Phase 2)";

bool IsVPowerSchemeName(const std::wstring& name) {
    return _wcsicmp(name.c_str(), kVPowerPlanName) == 0 ||
           _wcsicmp(name.c_str(), kLegacyVPowerPlanName) == 0;
}

const GUID kGuidHighPerf     = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
const GUID kGuidBalanced     = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
const GUID kGuidSubProc      = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};

const GUID kGuidProcMin0     = {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}};
const GUID kGuidProcMin1     = {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4d}};
const GUID kGuidProcMin2     = {0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4e}};
const GUID kGuidProcMax0     = {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}};
const GUID kGuidProcMax1     = {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xed}};
const GUID kGuidProcMax2     = {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xee}};
const GUID kGuidBoostMode    = {0xbe337238, 0x0d82, 0x4146, {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}};
const GUID kGuidBoostPol     = {0x45bcc044, 0xd885, 0x43e2, {0x86, 0x05, 0xee, 0x0e, 0xc6, 0xe9, 0x6b, 0x59}};
const GUID kGuidEpp0         = {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63}};
const GUID kGuidEpp1         = {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x64}};
const GUID kGuidEpp2         = {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x65}};
const GUID kGuidMinCores0    = {0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83}};
const GUID kGuidMinCores1    = {0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x84}};
const GUID kGuidMaxCores0    = {0xea062031, 0x0e34, 0x4ff1, {0x9b, 0x6d, 0xeb, 0x10, 0x59, 0x33, 0x40, 0x28}};
const GUID kGuidMaxCores1    = {0xea062031, 0x0e34, 0x4ff1, {0x9b, 0x6d, 0xeb, 0x10, 0x59, 0x33, 0x40, 0x29}};
const GUID kGuidFreqMax0     = {0x75b0ae3f, 0xbce0, 0x45a7, {0x8c, 0x89, 0xc9, 0x61, 0x1c, 0x25, 0xe1, 0x00}};
const GUID kGuidFreqMax1     = {0x75b0ae3f, 0xbce0, 0x45a7, {0x8c, 0x89, 0xc9, 0x61, 0x1c, 0x25, 0xe1, 0x01}};
const GUID kGuidSchedPolicy  = {0x93b8b6dc, 0x0698, 0x4d1c, {0x9e, 0xe4, 0x06, 0x44, 0xe9, 0x00, 0xc8, 0x5d}};
const GUID kGuidShortSched   = {0xbae08b81, 0x2d5e, 0x4688, {0xad, 0x6a, 0x13, 0x24, 0x33, 0x56, 0x65, 0x4b}};
const GUID kGuidIncThresh0   = {0x06cadf0e, 0x64ed, 0x448a, {0x89, 0x27, 0xce, 0x7b, 0xf9, 0x0e, 0xb3, 0x5d}};
const GUID kGuidIncThresh1   = {0x06cadf0e, 0x64ed, 0x448a, {0x89, 0x27, 0xce, 0x7b, 0xf9, 0x0e, 0xb3, 0x5e}};
const GUID kGuidDecThresh0   = {0x12a0ab44, 0xfe28, 0x4fa9, {0xb3, 0xbd, 0x4b, 0x64, 0xf4, 0x49, 0x60, 0xa6}};
const GUID kGuidDecThresh1   = {0x12a0ab44, 0xfe28, 0x4fa9, {0xb3, 0xbd, 0x4b, 0x64, 0xf4, 0x49, 0x60, 0xa7}};
const GUID kGuidCpConcur     = {0x2430ab6f, 0xa520, 0x44a2, {0x96, 0x01, 0xf7, 0xf2, 0x3b, 0x51, 0x34, 0xb1}};
const GUID kGuidSysCoolPol   = {0x94d3a615, 0xa899, 0x4ac5, {0xae, 0x2b, 0xe4, 0xd8, 0xf6, 0x34, 0x36, 0x7f}};
const GUID kGuidIdleDisable  = {0x5d76a2ca, 0xe8c0, 0x402f, {0xa1, 0x33, 0x21, 0x58, 0x49, 0x2d, 0x58, 0xad}};
const GUID kGuidIdleStateMax = {0x9943e905, 0x9a30, 0x4ec1, {0x9b, 0x99, 0x44, 0xdd, 0x3b, 0x76, 0xf7, 0xa2}};
const GUID kGuidAutonomous   = {0x8baa4a8a, 0x14c6, 0x4451, {0x8e, 0x8b, 0x14, 0xbd, 0xbd, 0x19, 0x75, 0x37}};
const GUID kGuidPerfAutoWin  = {0xcfeda3d0, 0x7697, 0x4566, {0xa9, 0x22, 0xa9, 0x08, 0x6c, 0xd4, 0x9d, 0xfa}};
const GUID kGuidHeteroPol    = {0x7f2f5cfa, 0xf10c, 0x4823, {0xb5, 0xe1, 0xe9, 0x3a, 0xe8, 0x5f, 0x46, 0xb5}};
const GUID kGuidHeteroFloor  = {0xfddc842b, 0x8364, 0x4edc, {0x94, 0xcf, 0xc1, 0x7f, 0x60, 0xde, 0x1c, 0x80}};
const GUID kGuidHeteroInit1  = {0x1facfc65, 0xa930, 0x4bc5, {0x9f, 0x38, 0x50, 0x4e, 0xc0, 0x97, 0xbb, 0xc0}};
const GUID kGuidThrottling   = {0x3b04d4fd, 0x1cc7, 0x4f23, {0xab, 0x1c, 0xd1, 0x33, 0x78, 0x19, 0xc4, 0xbb}};
const GUID kGuidPerfCheck    = {0x4d2b0152, 0x7d5c, 0x498b, {0x88, 0xe2, 0x34, 0x34, 0x53, 0x92, 0xa2, 0xc5}};
const GUID kGuidPerfIncPol0  = {0x465e1f50, 0xb610, 0x473a, {0xab, 0x58, 0x00, 0xd1, 0x07, 0x7d, 0xc4, 0x18}};
const GUID kGuidPerfIncPol1  = {0x465e1f50, 0xb610, 0x473a, {0xab, 0x58, 0x00, 0xd1, 0x07, 0x7d, 0xc4, 0x19}};
const GUID kGuidPerfDecPol0  = {0x40fbefc7, 0x2e9d, 0x4d25, {0xa1, 0x85, 0x0c, 0xfd, 0x85, 0x74, 0xba, 0xc6}};
const GUID kGuidPerfDecPol1  = {0x40fbefc7, 0x2e9d, 0x4d25, {0xa1, 0x85, 0x0c, 0xfd, 0x85, 0x74, 0xba, 0xc7}};
const GUID kGuidPerfIncTime0 = {0x984cf492, 0x3bed, 0x4488, {0xa8, 0xf9, 0x42, 0x86, 0xc9, 0x7b, 0xf5, 0xaa}};
const GUID kGuidPerfIncTime1 = {0x4009efa7, 0xe72d, 0x4cba, {0x9e, 0xdf, 0x91, 0x08, 0x4e, 0xa8, 0xcb, 0xc3}};
const GUID kGuidPerfDecTime0 = {0xd8edeb9b, 0x95cf, 0x4f95, {0xa7, 0x3c, 0xb0, 0x61, 0x97, 0x36, 0x93, 0xc8}};
const GUID kGuidPerfDecTime1 = {0x7f2492b6, 0x60b1, 0x45e5, {0xae, 0x55, 0x77, 0x3f, 0x8c, 0xd5, 0xca, 0xec}};
const GUID kGuidCpIncPol     = {0xc7be0679, 0x2817, 0x4d69, {0x9d, 0x02, 0x51, 0x9a, 0x53, 0x7e, 0xd0, 0xc6}};
const GUID kGuidCpDecPol     = {0x71021b41, 0xc749, 0x4d21, {0xbe, 0x74, 0xa0, 0x0f, 0x33, 0x5d, 0x58, 0x2b}};
const GUID kGuidCpIncTime    = {0x2ddd5a84, 0x5a71, 0x437e, {0x91, 0x2a, 0xdb, 0x0b, 0x8c, 0x78, 0x87, 0x32}};
const GUID kGuidCpDecTime    = {0xdfd10d17, 0xd5eb, 0x45dd, {0x87, 0x7a, 0x9a, 0x34, 0xdd, 0xd1, 0x5c, 0x82}};
const GUID kGuidSmtUnpark    = {0xb28a6829, 0xc5f7, 0x444e, {0x8f, 0x61, 0x10, 0xe2, 0x4e, 0x85, 0xc5, 0x32}};
const GUID kGuidDistUtil     = {0xe0007330, 0xf589, 0x42ed, {0xa4, 0x01, 0x5d, 0xdb, 0x10, 0xe7, 0x85, 0xd3}};
const GUID kGuidLongArchLow  = {0x43f278bc, 0x0f8a, 0x46d0, {0x8b, 0x31, 0x9a, 0x23, 0xe6, 0x15, 0xd7, 0x13}};
const GUID kGuidShortArchLow = {0x53824d46, 0x87bd, 0x4739, {0xaa, 0x1b, 0xaa, 0x79, 0x3f, 0xac, 0x36, 0xd6}};
const GUID kGuidResPrio0     = {0x603fe9ce, 0x8d01, 0x4b48, {0xa9, 0x68, 0x1d, 0x70, 0x6c, 0x28, 0xfd, 0x5c}};
const GUID kGuidResPrio1     = {0x603fe9ce, 0x8d01, 0x4b48, {0xa9, 0x68, 0x1d, 0x70, 0x6c, 0x28, 0xfd, 0x5d}};
const GUID kGuidResPrio2     = {0x603fe9ce, 0x8d01, 0x4b48, {0xa9, 0x68, 0x1d, 0x70, 0x6c, 0x28, 0xfd, 0x5e}};
const GUID kGuidDutyCycle    = {0x4e4450b3, 0x6179, 0x4e91, {0xb8, 0xf1, 0x5b, 0xb9, 0x93, 0x8f, 0x81, 0xa1}};
const GUID kGuidIdleScale    = {0x6c2993b0, 0x8f48, 0x481f, {0xbc, 0xc6, 0x00, 0xdd, 0x27, 0x42, 0xaa, 0x06}};
const GUID kGuidLatPerf0     = {0x619b7505, 0x003b, 0x4e82, {0xb7, 0xa6, 0x4d, 0xd2, 0x9c, 0x30, 0x09, 0x71}};
const GUID kGuidLatPerf1     = {0x619b7505, 0x003b, 0x4e82, {0xb7, 0xa6, 0x4d, 0xd2, 0x9c, 0x30, 0x09, 0x72}};
const GUID kGuidLatPerf2     = {0x619b7505, 0x003b, 0x4e82, {0xb7, 0xa6, 0x4d, 0xd2, 0x9c, 0x30, 0x09, 0x73}};
const GUID kGuidLatUnpark0   = {0x616cdaa5, 0x695e, 0x4545, {0x97, 0xad, 0x97, 0xdc, 0x2d, 0x1b, 0xdd, 0x88}};
const GUID kGuidLatUnpark1   = {0x616cdaa5, 0x695e, 0x4545, {0x97, 0xad, 0x97, 0xdc, 0x2d, 0x1b, 0xdd, 0x89}};
const GUID kGuidLatEpp0      = {0x4b70f900, 0xcdd9, 0x4e66, {0xaa, 0x26, 0xae, 0x84, 0x17, 0xf9, 0x81, 0x73}};
const GUID kGuidLatEpp1      = {0x4b70f900, 0xcdd9, 0x4e66, {0xaa, 0x26, 0xae, 0x84, 0x17, 0xf9, 0x81, 0x74}};
const GUID kGuidLatEpp2      = {0x4b70f900, 0xcdd9, 0x4e66, {0xaa, 0x26, 0xae, 0x84, 0x17, 0xf9, 0x81, 0x75}};
const GUID kGuidOvMaxPerf    = {0xded574b5, 0x45a0, 0x4f42, {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}};
const GUID kGuidOvZero       = {0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

using pfnPowerSetOverlay = DWORD(WINAPI*)(const GUID*);
using pfnPowerGetActualOverlay = DWORD(WINAPI*)(GUID*);
using pfnNtQuerySystemInformation = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using pfnNtQueryInformationProcess = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using pfnNtSetInformationProcess = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG);
using pfnNtSetTimerResolution = LONG(NTAPI*)(ULONG, BOOLEAN, PULONG);
using pfnNtQueryTimerResolution = LONG(NTAPI*)(PULONG, PULONG, PULONG);
using pfnSetProcessDefaultCpuSets = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
using pfnGetSystemCpuSetInformation = BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);

bool GetOverlaySafe(GUID& outOverlay) {
    HMODULE hPowr = GetModuleHandleW(L"powrprof.dll");
    if (!hPowr) hPowr = LoadLibraryW(L"powrprof.dll");
    if (!hPowr) return false;
    auto fn = reinterpret_cast<pfnPowerGetActualOverlay>(GetProcAddress(hPowr, "PowerGetActualOverlayScheme"));
    if (!fn) return false;
    return fn(&outOverlay) == ERROR_SUCCESS;
}

void SetOverlaySafe(const GUID& overlay) {
    HMODULE hPowr = GetModuleHandleW(L"powrprof.dll");
    if (!hPowr) hPowr = LoadLibraryW(L"powrprof.dll");
    if (!hPowr) return;
    auto fn = reinterpret_cast<pfnPowerSetOverlay>(GetProcAddress(hPowr, "PowerSetActiveOverlayScheme"));
    if (fn) {
        GUID g = overlay;
        fn(&g);
    }
}

constexpr wchar_t kRegBackupKey[] = L"Software\\VOverclockIntel\\KernelRegBackup";

void WriteRegDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD value) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(hKey);
    }
}

void DeleteRegValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, valueName);
        RegCloseKey(hKey);
    }
}

void CleanupRegBackupKeyIfEmpty() {
    HKEY hBak = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegBackupKey, 0, KEY_QUERY_VALUE, &hBak) != ERROR_SUCCESS) {
        return;
    }
    DWORD subKeys = 0;
    DWORD values = 0;
    const LONG st = RegQueryInfoKeyW(hBak, nullptr, nullptr, nullptr, &subKeys, nullptr, nullptr,
                                     &values, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(hBak);
    if (st == ERROR_SUCCESS && subKeys == 0 && values == 0) {
        RegDeleteKeyW(HKEY_CURRENT_USER, kRegBackupKey);
    }
}

void BackupAndWriteHklmDword(const wchar_t* id, const wchar_t* subKey, const wchar_t* valueName, DWORD newVal) {
    HKEY hBak = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegBackupKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &hBak, nullptr) == ERROR_SUCCESS) {
        const std::wstring flagName = std::wstring(L"Has_") + id;
        DWORD existingFlag = 0;
        DWORD sz = sizeof(existingFlag);
        if (RegQueryValueExW(hBak, flagName.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(&existingFlag), &sz) != ERROR_SUCCESS) {
            DWORD origVal = 0;
            DWORD origSz = sizeof(origVal);
            DWORD origType = 0;
            HKEY hSrc = nullptr;
            bool existed = false;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_QUERY_VALUE, &hSrc) == ERROR_SUCCESS) {
                existed = (RegQueryValueExW(hSrc, valueName, nullptr, &origType,
                                            reinterpret_cast<BYTE*>(&origVal), &origSz) == ERROR_SUCCESS &&
                           origType == REG_DWORD);
                RegCloseKey(hSrc);
            }
            const DWORD present = existed ? 1u : 0u;
            RegSetValueExW(hBak, (std::wstring(L"Ex_") + id).c_str(), 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&present), sizeof(present));
            if (existed) {
                RegSetValueExW(hBak, (std::wstring(L"Val_") + id).c_str(), 0, REG_DWORD,
                               reinterpret_cast<const BYTE*>(&origVal), sizeof(origVal));
            }
            const DWORD one = 1u;
            RegSetValueExW(hBak, flagName.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
        }
        RegCloseKey(hBak);
    }
    WriteRegDword(HKEY_LOCAL_MACHINE, subKey, valueName, newVal);
}

void RestoreBackedUpHklmDword(const wchar_t* id, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY hBak = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegBackupKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hBak) != ERROR_SUCCESS) {
        return;
    }
    const std::wstring flagName = std::wstring(L"Has_") + id;
    const std::wstring exName   = std::wstring(L"Ex_") + id;
    const std::wstring valName  = std::wstring(L"Val_") + id;
    DWORD hasFlag = 0, sz = sizeof(hasFlag);
    if (RegQueryValueExW(hBak, flagName.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(&hasFlag), &sz) == ERROR_SUCCESS && hasFlag != 0) {
        DWORD present = 0, psz = sizeof(present);
        RegQueryValueExW(hBak, exName.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(&present), &psz);
        if (present != 0) {
            DWORD origVal = 0, vsz = sizeof(origVal);
            if (RegQueryValueExW(hBak, valName.c_str(), nullptr, nullptr,
                                 reinterpret_cast<BYTE*>(&origVal), &vsz) == ERROR_SUCCESS) {
                WriteRegDword(HKEY_LOCAL_MACHINE, subKey, valueName, origVal);
            }
        } else {
            DeleteRegValue(HKEY_LOCAL_MACHINE, subKey, valueName);
        }
        RegDeleteValueW(hBak, flagName.c_str());
    }
    RegDeleteValueW(hBak, exName.c_str());
    RegDeleteValueW(hBak, valName.c_str());
    RegCloseKey(hBak);
}

void WriteRegString(HKEY root, const wchar_t* subKey, const wchar_t* valueName, const wchar_t* value) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        const DWORD bytes = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), bytes);
        RegCloseKey(hKey);
    }
}

void BackupAndWriteHklmString(const wchar_t* id, const wchar_t* subKey, const wchar_t* valueName, const wchar_t* newVal) {
    HKEY hBak = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegBackupKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &hBak, nullptr) == ERROR_SUCCESS) {
        const std::wstring flagName = std::wstring(L"Has_") + id;
        DWORD existingFlag = 0;
        DWORD sz = sizeof(existingFlag);
        if (RegQueryValueExW(hBak, flagName.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(&existingFlag), &sz) != ERROR_SUCCESS) {
            wchar_t origBuf[512]{};
            DWORD origSz = sizeof(origBuf);
            DWORD origType = 0;
            HKEY hSrc = nullptr;
            bool existed = false;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_QUERY_VALUE, &hSrc) == ERROR_SUCCESS) {
                existed = (RegQueryValueExW(hSrc, valueName, nullptr, &origType,
                                            reinterpret_cast<BYTE*>(origBuf), &origSz) == ERROR_SUCCESS &&
                           (origType == REG_SZ || origType == REG_EXPAND_SZ));
                RegCloseKey(hSrc);
            }
            const DWORD present = existed ? 1u : 0u;
            RegSetValueExW(hBak, (std::wstring(L"Ex_") + id).c_str(), 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&present), sizeof(present));
            if (existed) {
                const DWORD bytes = static_cast<DWORD>((wcsnlen(origBuf, 511) + 1) * sizeof(wchar_t));
                RegSetValueExW(hBak, (std::wstring(L"Val_") + id).c_str(), 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(origBuf), bytes);
            }
            const DWORD one = 1u;
            RegSetValueExW(hBak, flagName.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
        }
        RegCloseKey(hBak);
    }
    WriteRegString(HKEY_LOCAL_MACHINE, subKey, valueName, newVal);
}

void RestoreBackedUpHklmString(const wchar_t* id, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY hBak = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegBackupKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hBak) != ERROR_SUCCESS) {
        return;
    }
    const std::wstring flagName = std::wstring(L"Has_") + id;
    const std::wstring exName   = std::wstring(L"Ex_") + id;
    const std::wstring valName  = std::wstring(L"Val_") + id;
    DWORD hasFlag = 0, sz = sizeof(hasFlag);
    if (RegQueryValueExW(hBak, flagName.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(&hasFlag), &sz) == ERROR_SUCCESS && hasFlag != 0) {
        DWORD present = 0, psz = sizeof(present);
        RegQueryValueExW(hBak, exName.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(&present), &psz);
        if (present != 0) {
            wchar_t origBuf[512]{};
            DWORD vsz = sizeof(origBuf);
            DWORD vtype = 0;
            if (RegQueryValueExW(hBak, valName.c_str(), nullptr, &vtype,
                                 reinterpret_cast<BYTE*>(origBuf), &vsz) == ERROR_SUCCESS &&
                vtype == REG_SZ) {
                WriteRegString(HKEY_LOCAL_MACHINE, subKey, valueName, origBuf);
            }
        } else {
            DeleteRegValue(HKEY_LOCAL_MACHINE, subKey, valueName);
        }
        RegDeleteValueW(hBak, flagName.c_str());
    }
    RegDeleteValueW(hBak, exName.c_str());
    RegDeleteValueW(hBak, valName.c_str());
    RegCloseKey(hBak);
}

bool IsProtectedSystemProcessName(const std::wstring& baseName) {
    if (baseName.empty()) return true;
    static constexpr const wchar_t* kSkip[] = {
        L"System", L"Registry", L"Idle", L"smss.exe", L"csrss.exe", L"wininit.exe",
        L"winlogon.exe", L"services.exe", L"lsass.exe", L"svchost.exe", L"fontdrvhost.exe",
        L"dwm.exe", L"explorer.exe", L"sihost.exe", L"taskhostw.exe", L"ctfmon.exe",
        L"ShellExperienceHost.exe", L"StartMenuExperienceHost.exe", L"SearchHost.exe",
        L"TextInputHost.exe", L"LockApp.exe", L"ApplicationFrameHost.exe",
        L"RuntimeBroker.exe", L"SearchIndexer.exe", L"MsMpEng.exe", L"NisSrv.exe",
        L"VOverclockIntel.exe"
    };
    for (const wchar_t* s : kSkip) {
        if (_wcsicmp(baseName.c_str(), s) == 0) return true;
    }
    return false;
}

struct SysProcPerfInfo {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
};

struct ProcPowerInfo {
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
};

bool QueryAllProcessorPowerInfo(std::vector<ProcPowerInfo>& out, size_t minCount) {
    const DWORD activeCount = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    size_t count = std::max<size_t>({minCount, static_cast<size_t>(activeCount), static_cast<size_t>(1)});
    for (int attempt = 0; attempt < 4; ++attempt) {
        out.assign(count, ProcPowerInfo{});
        const LONG st = CallNtPowerInformation(
            ProcessorInformation,
            nullptr,
            0,
            out.data(),
            static_cast<ULONG>(out.size() * sizeof(ProcPowerInfo))
        );
        if (st == 0) {
            return true;
        }
        if (st != static_cast<LONG>(0xC0000023L)) {
            out.clear();
            return false;
        }
        count = std::min<size_t>(count * 2, 1024);
    }
    out.clear();
    return false;
}

std::wstring TrimWide(const std::wstring& s) {
    std::wstring collapsed;
    collapsed.reserve(s.size());
    bool prevSpace = true;
    for (wchar_t ch : s) {
        if (ch == L'\0') break;
        if (iswspace(ch)) {
            if (!prevSpace) {
                collapsed.push_back(L' ');
                prevSpace = true;
            }
        } else {
            collapsed.push_back(ch);
            prevSpace = false;
        }
    }
    if (!collapsed.empty() && collapsed.back() == L' ') {
        collapsed.pop_back();
    }
    return collapsed;
}

std::wstring AsciiToWide(const char* str, size_t maxLen) {
    if (!str) return {};
    size_t len = 0;
    while (len < maxLen && str[len] != '\0') {
        ++len;
    }
    if (len == 0) return {};
    int wLen = MultiByteToWideChar(CP_ACP, 0, str, static_cast<int>(len), nullptr, 0);
    if (wLen <= 0) return {};
    std::wstring out(static_cast<size_t>(wLen), L'\0');
    MultiByteToWideChar(CP_ACP, 0, str, static_cast<int>(len), out.data(), wLen);
    return out;
}

std::wstring GuidToString(const GUID& g) {
    wchar_t buf[64]{};
    StringFromGUID2(g, buf, 64);
    return buf;
}

bool StringToGuid(const std::wstring& str, GUID& out) {
    if (str.empty()) return false;
    return SUCCEEDED(CLSIDFromString(str.c_str(), &out));
}

void BackupAndSetOverlay(const GUID& nextOverlay) {
    HKEY hBak = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegBackupKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &hBak, nullptr) == ERROR_SUCCESS) {
        DWORD existingFlag = 0;
        DWORD sz = sizeof(existingFlag);
        if (RegQueryValueExW(hBak, L"Has_PowerOverlay", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(&existingFlag), &sz) != ERROR_SUCCESS) {
            GUID curOverlay = kGuidOvZero;
            GetOverlaySafe(curOverlay);
            const std::wstring gStr = GuidToString(curOverlay);
            const DWORD bytes = static_cast<DWORD>((gStr.size() + 1) * sizeof(wchar_t));
            RegSetValueExW(hBak, L"Val_PowerOverlay", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(gStr.c_str()), bytes);
            const DWORD one = 1u;
            RegSetValueExW(hBak, L"Has_PowerOverlay", 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&one), sizeof(one));
        }
        RegCloseKey(hBak);
    }
    SetOverlaySafe(nextOverlay);
}

void RestoreBackedUpOverlay() {
    GUID restoreOv = kGuidOvZero;
    HKEY hBak = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegBackupKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hBak) == ERROR_SUCCESS) {
        DWORD hasFlag = 0, sz = sizeof(hasFlag);
        if (RegQueryValueExW(hBak, L"Has_PowerOverlay", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(&hasFlag), &sz) == ERROR_SUCCESS && hasFlag != 0) {
            wchar_t ovBuf[96]{};
            DWORD vsz = sizeof(ovBuf);
            DWORD vtype = 0;
            if (RegQueryValueExW(hBak, L"Val_PowerOverlay", nullptr, &vtype,
                                 reinterpret_cast<BYTE*>(ovBuf), &vsz) == ERROR_SUCCESS && vtype == REG_SZ) {
                GUID parsed{};
                if (StringToGuid(ovBuf, parsed)) {
                    restoreOv = parsed;
                }
            }
            RegDeleteValueW(hBak, L"Has_PowerOverlay");
        }
        RegDeleteValueW(hBak, L"Val_PowerOverlay");
        RegCloseKey(hBak);
    }
    SetOverlaySafe(restoreOv);
}

void SaveOriginalSchemeBackup(const std::wstring& guidStr) {
    if (guidStr.empty()) return;
    WriteRegString(HKEY_CURRENT_USER, kRegBackupKey, L"OriginalSchemeGuid", guidStr.c_str());
}

std::wstring LoadOriginalSchemeBackup() {
    HKEY hBak = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegBackupKey, 0, KEY_QUERY_VALUE, &hBak) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buf[96]{};
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    std::wstring out;
    if (RegQueryValueExW(hBak, L"OriginalSchemeGuid", nullptr, &type,
                         reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS && type == REG_SZ) {
        out = buf;
    }
    RegCloseKey(hBak);
    return out;
}

void ClearOriginalSchemeBackup() {
    DeleteRegValue(HKEY_CURRENT_USER, kRegBackupKey, L"OriginalSchemeGuid");
}

std::wstring ReadSchemeFriendlyName(const GUID& scheme) {
    DWORD bufSize = 0;
    if (PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, nullptr, &bufSize) != ERROR_SUCCESS ||
        bufSize < sizeof(wchar_t)) {
        return {};
    }
    std::vector<BYTE> buf(bufSize, 0);
    if (PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, buf.data(), &bufSize) != ERROR_SUCCESS) {
        return {};
    }
    return std::wstring(reinterpret_cast<const wchar_t*>(buf.data()));
}

std::wstring ReadSchemeDescription(const GUID& scheme) {
    DWORD bufSize = 0;
    if (PowerReadDescription(nullptr, &scheme, nullptr, nullptr, nullptr, &bufSize) != ERROR_SUCCESS ||
        bufSize < sizeof(wchar_t)) {
        return {};
    }
    std::vector<BYTE> buf(bufSize, 0);
    if (PowerReadDescription(nullptr, &scheme, nullptr, nullptr, buf.data(), &bufSize) != ERROR_SUCCESS) {
        return {};
    }
    return std::wstring(reinterpret_cast<const wchar_t*>(buf.data()));
}

bool SchemeExists(const GUID& scheme) {
    DWORD sz = 0;
    if (PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, nullptr, &sz) == ERROR_SUCCESS && sz >= sizeof(wchar_t)) {
        return true;
    }
    for (ULONG idx = 0;; ++idx) {
        GUID g{};
        DWORD gsz = sizeof(g);
        DWORD st = PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, idx, reinterpret_cast<UCHAR*>(&g), &gsz);
        if (st != ERROR_SUCCESS) break;
        if (IsEqualGUID(g, scheme)) return true;
    }
    return false;
}

bool FindSchemeByName(const wchar_t* targetName, GUID& outGuid) {
    for (ULONG idx = 0;; ++idx) {
        GUID g{};
        DWORD sz = sizeof(g);
        DWORD st = PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, idx, reinterpret_cast<UCHAR*>(&g), &sz);
        if (st != ERROR_SUCCESS) break;
        if (_wcsicmp(ReadSchemeFriendlyName(g).c_str(), targetName) == 0) {
            outGuid = g;
            return true;
        }
    }

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes",
                      0, KEY_ENUMERATE_SUB_KEYS, &hKey) == ERROR_SUCCESS) {
        wchar_t subKey[64]{};
        for (DWORD i = 0;; ++i) {
            DWORD len = ARRAYSIZE(subKey);
            if (RegEnumKeyExW(hKey, i, subKey, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            std::wstring br = L"{" + std::wstring(subKey) + L"}";
            GUID g{};
            if (SUCCEEDED(CLSIDFromString(br.c_str(), &g))) {
                if (_wcsicmp(ReadSchemeFriendlyName(g).c_str(), targetName) == 0) {
                    outGuid = g;
                    RegCloseKey(hKey);
                    return true;
                }
            }
        }
        RegCloseKey(hKey);
    }
    return false;
}

DWORD WriteSettingBothModes(const GUID& scheme, const GUID& subGroup, const GUID& setting, DWORD val) {
    DWORD r1 = PowerWriteACValueIndex(nullptr, &scheme, &subGroup, &setting, val);
    DWORD r2 = PowerWriteDCValueIndex(nullptr, &scheme, &subGroup, &setting, val);
    if (r1 != ERROR_SUCCESS) return r1;
    return r2;
}

uint32_t ReadSettingValue(const GUID& scheme, const GUID& subGroup, const GUID& setting, bool acPower, uint32_t fallback) {
    DWORD val = 0;
    DWORD st = acPower
        ? PowerReadACValueIndex(nullptr, &scheme, &subGroup, &setting, &val)
        : PowerReadDCValueIndex(nullptr, &scheme, &subGroup, &setting, &val);
    return (st == ERROR_SUCCESS) ? static_cast<uint32_t>(val) : fallback;
}

uint32_t ReadRegistryBaseMhz() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return 0;
    }
    DWORD val = 0;
    DWORD sz = sizeof(val);
    DWORD type = 0;
    LONG st = RegQueryValueExW(hKey, L"~MHz", nullptr, &type, reinterpret_cast<BYTE*>(&val), &sz);
    RegCloseKey(hKey);
    return (st == ERROR_SUCCESS && type == REG_DWORD) ? static_cast<uint32_t>(val) : 0;
}

bool EnablePrivilegeName(const wchar_t* name) {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }
    TOKEN_PRIVILEGES tp{};
    LUID luid{};
    bool ok = false;
    if (LookupPrivilegeValueW(nullptr, name, &luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr) != FALSE &&
             GetLastError() == ERROR_SUCCESS;
    }
    CloseHandle(hToken);
    return ok;
}

}

struct CpuIntelEngine::Impl {
    PDH_HQUERY pdhQuery = nullptr;
    struct LpCounters {
        WORD group = 0;
        uint32_t lp = 0;
        CoreKind kind = CoreKind::Standard;
        uint32_t cachedNominalMhz = 0;
        PDH_HCOUNTER perfPct = nullptr;
        PDH_HCOUNTER freqMhz = nullptr;
    };
    std::vector<LpCounters> pdhCounters;
    bool pdhInitialized = false;
    bool pdhPrimed = false;

    std::vector<SysProcPerfInfo> prevPerf;
    bool hasPrevPerf = false;

    pfnNtQuerySystemInformation fnNtQuerySysInfo = nullptr;
    pfnNtQueryInformationProcess fnNtQueryInfoProc = nullptr;
    pfnNtSetInformationProcess fnNtSetInfoProc = nullptr;
    pfnNtSetTimerResolution fnNtSetTimerRes = nullptr;
    pfnNtQueryTimerResolution fnNtQueryTimerRes = nullptr;
    pfnSetProcessDefaultCpuSets fnSetProcCpuSets = nullptr;
    pfnGetSystemCpuSetInformation fnGetCpuSets = nullptr;

    IWbemServices* wmiSvc = nullptr;
    bool wmiChecked = false;
    bool wmiAvailable = false;
    int wmiFailCount = 0;

    struct OriginalPidState {
        DWORD priorityClass = NORMAL_PRIORITY_CLASS;
        BOOL disablePriorityBoost = FALSE;
        ULONG ioPriority = 2u;
        DWORD_PTR affinityMask = 0;
        bool hasAffinity = false;
    };

    bool highResTimerActive = false;
    DWORD lastFgBoostPid = 0;
    std::wstring lastBoostedName;
    std::map<DWORD, uint64_t> boostedPidSignatures;
    std::map<DWORD, OriginalPidState> boostedPidOrigStates;
    std::set<std::wstring> boostedIfeoExes;

    struct PCoreRouting {
        DWORD_PTR pCoreMask = 0;
        std::vector<DWORD> pCoreLogical;
        std::vector<ULONG> pCoreCpuSetIds;
        bool initialized = false;
    } routing;

    ~Impl() {
        if (highResTimerActive && fnNtSetTimerRes) {
            ULONG dummy = 0;
            fnNtSetTimerRes(156250, FALSE, &dummy);
            highResTimerActive = false;
        }
        if (pdhQuery) {
            PdhCloseQuery(pdhQuery);
            pdhQuery = nullptr;
        }
        if (wmiSvc) {
            ULONG_PTR comToken = 0;
            if (SUCCEEDED(CoGetContextToken(&comToken))) {
                wmiSvc->Release();
            }
            wmiSvc = nullptr;
        }
    }

    void EnsurePdh(const CpuHardwareInfo& info) {
        if (pdhInitialized) return;
        pdhInitialized = true;

        if (PdhOpenQueryW(nullptr, 0, &pdhQuery) != ERROR_SUCCESS || !pdhQuery) {
            pdhQuery = nullptr;
            return;
        }

        for (const auto& core : info.cores) {
            for (const auto& lp : core.logicalProcessors) {
                LpCounters lc{};
                lc.group = lp.first;
                lc.lp = lp.second;
                lc.kind = core.kind;

                wchar_t perfPath[128]{};
                wchar_t freqPath[128]{};
                std::swprintf(perfPath, 128, L"\\Processor Information(%u,%u)\\%% Processor Performance",
                              static_cast<unsigned>(lp.first), lp.second);
                std::swprintf(freqPath, 128, L"\\Processor Information(%u,%u)\\Processor Frequency",
                              static_cast<unsigned>(lp.first), lp.second);

                PdhAddEnglishCounterW(pdhQuery, perfPath, 0, &lc.perfPct);
                PdhAddEnglishCounterW(pdhQuery, freqPath, 0, &lc.freqMhz);
                pdhCounters.push_back(lc);
            }
        }

        if (!pdhCounters.empty()) {
            PdhCollectQueryData(pdhQuery);
            pdhPrimed = true;
        }
    }
};

CpuIntelEngine::CpuIntelEngine()
    : m_impl(new Impl()) {
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    if (hNt) {
        m_impl->fnNtQuerySysInfo = reinterpret_cast<pfnNtQuerySystemInformation>(
            GetProcAddress(hNt, "NtQuerySystemInformation"));
        m_impl->fnNtQueryInfoProc = reinterpret_cast<pfnNtQueryInformationProcess>(
            GetProcAddress(hNt, "NtQueryInformationProcess"));
        m_impl->fnNtSetInfoProc = reinterpret_cast<pfnNtSetInformationProcess>(
            GetProcAddress(hNt, "NtSetInformationProcess"));
        m_impl->fnNtSetTimerRes = reinterpret_cast<pfnNtSetTimerResolution>(
            GetProcAddress(hNt, "NtSetTimerResolution"));
        m_impl->fnNtQueryTimerRes = reinterpret_cast<pfnNtQueryTimerResolution>(
            GetProcAddress(hNt, "NtQueryTimerResolution"));
    }
    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (hK32) {
        m_impl->fnSetProcCpuSets = reinterpret_cast<pfnSetProcessDefaultCpuSets>(
            GetProcAddress(hK32, "SetProcessDefaultCpuSets"));
        m_impl->fnGetCpuSets = reinterpret_cast<pfnGetSystemCpuSetInformation>(
            GetProcAddress(hK32, "GetSystemCpuSetInformation"));
    }
}

CpuIntelEngine::~CpuIntelEngine() {
    delete m_impl;
    m_impl = nullptr;
}

bool CpuIntelEngine::IsElevated() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }
    TOKEN_ELEVATION elev{};
    DWORD retLen = 0;
    const BOOL ok = GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &retLen);
    CloseHandle(hToken);
    return ok && elev.TokenIsElevated != 0;
}

bool CpuIntelEngine::RestartElevated(bool startMinimized) {
    wchar_t exePath[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return false;
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = startMinimized ? L"--minimized --elevated-restart" : L"--elevated-restart";
    sei.nShow = startMinimized ? SW_HIDE : SW_SHOWNORMAL;

    return ShellExecuteExW(&sei) != FALSE;
}

bool CpuIntelEngine::EvaluateVendorIsIntel(const std::wstring& vendorId, const std::wstring& brandName) {
    if (vendorId == L"GenuineIntel") {
        return true;
    }
    if (vendorId == L"AuthenticAMD" || vendorId == L"HygonGenuine") {
        return false;
    }
    std::wstring upper = brandName;
    for (auto& c : upper) c = towupper(c);
    if (upper.find(L"AMD") != std::wstring::npos ||
        upper.find(L"RYZEN") != std::wstring::npos ||
        upper.find(L"EPYC") != std::wstring::npos ||
        upper.find(L"ATHLON") != std::wstring::npos) {
        return false;
    }
    return false;
}

bool CpuIntelEngine::DetectUnlockedIntelSku(const std::wstring& brandName) {
    std::wstring upper = brandName;
    for (auto& c : upper) c = towupper(c);
    if (upper.find(L"EXTREME") != std::wstring::npos) {
        return true;
    }

    std::wstring tok;
    auto checkToken = [](const std::wstring& t) -> bool {
        if (t.size() < 4) return false;
        bool hasDigit = false;
        for (wchar_t c : t) {
            if (iswdigit(c)) {
                hasDigit = true;
                break;
            }
        }
        if (!hasDigit) return false;
        if (t.size() >= 2 && (t.compare(t.size() - 2, 2, L"KF") == 0 ||
                              t.compare(t.size() - 2, 2, L"KS") == 0 ||
                              t.compare(t.size() - 2, 2, L"HX") == 0 ||
                              t.compare(t.size() - 2, 2, L"HK") == 0 ||
                              t.compare(t.size() - 2, 2, L"XE") == 0)) {
            return true;
        }
        if (t.back() == L'K' || t.back() == L'X') {
            return true;
        }
        return false;
    };

    for (wchar_t c : upper) {
        if (iswalnum(c)) {
            tok.push_back(c);
        } else {
            if (checkToken(tok)) return true;
            tok.clear();
        }
    }
    return checkToken(tok);
}

const wchar_t* CpuIntelEngine::BoostModeToString(uint32_t mode) {
    switch (mode) {
    case 0: return L"Disabled";
    case 1: return L"Enabled";
    case 2: return L"Aggressive";
    case 3: return L"Eff. Enabled";
    case 4: return L"Eff. Aggr";
    case 5: return L"Agg. Guar";
    case 6: return L"Eff. Guar";
    default: return L"Custom";
    }
}

const wchar_t* CpuIntelEngine::SchedPolicyToString(uint32_t policy) {
    switch (policy) {
    case 0: return L"All Cores";
    case 1: return L"P-Core Only";
    case 2: return L"Prefer P-Core";
    case 3: return L"E-Core Only";
    case 4: return L"Prefer E-Core";
    case 5: return L"Automatic";
    default: return L"Custom";
    }
}

void CpuIntelEngine::Initialize() {
    DetectHardwareAndTopology();
    m_impl->EnsurePdh(m_info);
    double t = QueryAcpiThermalC();
    m_info.hasAcpiThermal = (t >= 0.0);
    BuildFeatureCompatibilityMatrix();
}

void CpuIntelEngine::DetectHardwareAndTopology() {
    CpuHardwareInfo info{};

    int r0[4]{};
    __cpuid(r0, 0);
    const uint32_t maxBasic = static_cast<uint32_t>(r0[0]);

    char vendorBuf[13]{};
    memcpy(vendorBuf + 0, &r0[1], 4);
    memcpy(vendorBuf + 4, &r0[3], 4);
    memcpy(vendorBuf + 8, &r0[2], 4);
    info.vendorId = AsciiToWide(vendorBuf, 12);

    int ext0[4]{};
    __cpuid(ext0, static_cast<int>(0x80000000));
    const uint32_t maxExt = static_cast<uint32_t>(ext0[0]);
    if (maxExt >= 0x80000004) {
        char brandBuf[49]{};
        for (int i = 0; i < 3; ++i) {
            int rb[4]{};
            __cpuid(rb, static_cast<int>(0x80000002 + i));
            memcpy(brandBuf + i * 16, rb, 16);
        }
        info.brandName = TrimWide(AsciiToWide(brandBuf, 48));
    }
    if (info.brandName.empty()) {
        info.brandName = info.vendorId;
    }

    info.isIntel = EvaluateVendorIsIntel(info.vendorId, info.brandName);
    info.isAmd = (info.vendorId == L"AuthenticAMD") || (!info.isIntel && info.brandName.find(L"AMD") != std::wstring::npos);

    if (maxBasic >= 1) {
        int r1[4]{};
        __cpuid(r1, 1);
        const uint32_t eax = static_cast<uint32_t>(r1[0]);
        uint32_t baseModel = (eax >> 4) & 0xF;
        uint32_t baseFamily = (eax >> 8) & 0xF;
        uint32_t extModel = (eax >> 16) & 0xF;
        uint32_t extFamily = (eax >> 20) & 0xFF;
        info.stepping = eax & 0xF;
        info.family = (baseFamily == 0xF) ? (baseFamily + extFamily) : baseFamily;
        info.model = (baseFamily == 0x6 || baseFamily == 0xF) ? ((extModel << 4) + baseModel) : baseModel;
        info.hasVtX = (static_cast<uint32_t>(r1[2]) & (1u << 5)) != 0;
        info.hasAes = (static_cast<uint32_t>(r1[2]) & (1u << 25)) != 0;
    }

    if (info.isIntel && maxBasic >= 6) {
        int r6[4]{};
        __cpuid(r6, 6);
        const uint32_t eax6 = static_cast<uint32_t>(r6[0]);
        info.hasThermalSensor = (eax6 & (1u << 0)) != 0;
        info.hasTurboBoost    = (eax6 & (1u << 1)) != 0;
        info.hasPkgThermal    = (eax6 & (1u << 6)) != 0;
        info.hasHwp           = (eax6 & (1u << 7)) != 0;
        info.hasHwpEpp        = (eax6 & (1u << 10)) != 0;
        info.hasTurboMax3     = (eax6 & (1u << 14)) != 0;
    }

    if (maxBasic >= 7) {
        int r7[4]{};
        __cpuidex(r7, 7, 0);
        info.hasAvx2 = (static_cast<uint32_t>(r7[1]) & (1u << 5)) != 0;
        if (info.isIntel) {
            info.isHybrid = (static_cast<uint32_t>(r7[3]) & (1u << 15)) != 0;
        }
    }

    if (info.isIntel && maxBasic >= 0x16) {
        int r16[4]{};
        __cpuid(r16, 0x16);
        info.baseMhz = static_cast<uint32_t>(r16[0] & 0xFFFF);
        info.maxTurboMhz = static_cast<uint32_t>(r16[1] & 0xFFFF);
        info.busMhz = static_cast<uint32_t>(r16[2] & 0xFFFF);
    }

    if (info.baseMhz == 0) {
        info.baseMhz = ReadRegistryBaseMhz();
    }
    if (info.isIntel) {
        info.isUnlockedSku = DetectUnlockedIntelSku(info.brandName);
        std::vector<ProcPowerInfo> ppi;
        if (QueryAllProcessorPowerInfo(ppi, 1) && !ppi.empty() && ppi[0].MaxMhz > 0) {
            info.baseMhz = ppi[0].MaxMhz;
        }
    }

    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    std::vector<uint8_t> coreBuf(len);
    if (len > 0 && GetLogicalProcessorInformationEx(RelationProcessorCore,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(coreBuf.data()), &len)) {
        uint8_t minEff = 255;
        uint8_t maxEff = 0;
        uint32_t totalLp = 0;

        for (DWORD off = 0; off < len;) {
            auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(coreBuf.data() + off);
            if (entry->Relationship == RelationProcessorCore) {
                CoreTopology ct{};
                ct.coreIndex = static_cast<uint32_t>(info.cores.size());
                ct.efficiencyClass = entry->Processor.EfficiencyClass;
                ct.hasSmt = (entry->Processor.Flags & LTP_PC_SMT) != 0;
                minEff = std::min(minEff, ct.efficiencyClass);
                maxEff = std::max(maxEff, ct.efficiencyClass);

                for (WORD g = 0; g < entry->Processor.GroupCount; ++g) {
                    const KAFFINITY mask = entry->Processor.GroupMask[g].Mask;
                    for (uint32_t bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit) {
                        if ((mask & (static_cast<KAFFINITY>(1) << bit)) != 0) {
                            ct.logicalProcessors.emplace_back(entry->Processor.GroupMask[g].Group, bit);
                            ++totalLp;
                        }
                    }
                }

                if (info.isIntel && maxBasic >= 0x1A && !ct.logicalProcessors.empty()) {
                    GROUP_AFFINITY ga{};
                    GROUP_AFFINITY prevGa{};
                    ga.Group = ct.logicalProcessors[0].first;
                    ga.Mask = static_cast<KAFFINITY>(1) << ct.logicalProcessors[0].second;
                    if (SetThreadGroupAffinity(GetCurrentThread(), &ga, &prevGa)) {
                        int r1a[4]{};
                        __cpuidex(r1a, 0x1A, 0);
                        const uint32_t coreTypeByte = (static_cast<uint32_t>(r1a[0]) >> 24) & 0xFF;
                        if (coreTypeByte == 0x40) {
                            ct.kind = CoreKind::PCore;
                        } else if (coreTypeByte == 0x20) {
                            ct.kind = CoreKind::ECore;
                        }
                        SetThreadGroupAffinity(GetCurrentThread(), &prevGa, nullptr);
                    }
                }

                info.cores.push_back(std::move(ct));
            }
            off += entry->Size;
        }

        info.physicalCores = static_cast<uint32_t>(info.cores.size());
        info.logicalThreads = totalLp;

        for (auto& ct : info.cores) {
            if (ct.kind == CoreKind::Standard && info.isIntel && (info.isHybrid || maxEff > minEff)) {
                ct.kind = (ct.efficiencyClass > minEff) ? CoreKind::PCore : CoreKind::ECore;
            }
            if (ct.kind == CoreKind::PCore) {
                ct.typeLocalIndex = info.pCoreCount++;
            } else if (ct.kind == CoreKind::ECore) {
                ct.typeLocalIndex = info.eCoreCount++;
            } else {
                ct.typeLocalIndex = ct.coreIndex;
            }
        }
        if (info.pCoreCount > 0 && info.eCoreCount > 0) {
            info.isHybrid = true;
        }
    }

    DWORD cacheLen = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &cacheLen);
    std::vector<uint8_t> cacheBuf(cacheLen);
    if (cacheLen > 0 && GetLogicalProcessorInformationEx(RelationCache,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(cacheBuf.data()), &cacheLen)) {
        std::set<std::pair<WORD, KAFFINITY>> seenL3;
        for (DWORD off = 0; off < cacheLen;) {
            auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(cacheBuf.data() + off);
            if (entry->Relationship == RelationCache) {
                const uint32_t kb = entry->Cache.CacheSize / 1024;
                const KAFFINITY m = entry->Cache.GroupMask.Mask;
                if (entry->Cache.Level == 1) {
                    info.l1CacheKB += kb;
                } else if (entry->Cache.Level == 2) {
                    info.l2CacheKB += kb;
                } else if (entry->Cache.Level == 3) {
                    if (seenL3.emplace(entry->Cache.GroupMask.Group, m).second) {
                        info.l3CacheKB += kb;
                    }
                }
            }
            off += entry->Size;
        }
    }

    m_info = std::move(info);
}

void CpuIntelEngine::BuildFeatureCompatibilityMatrix() {
    m_info.featureChecks.clear();
    const bool elevated = IsElevated();

    auto add = [&](const wchar_t* id, const wchar_t* name, const wchar_t* detail, SupportLevel lvl) {
        FeatureCheckItem item;
        item.id = id;
        item.name = name;
        item.detail = detail;
        item.level = lvl;
        switch (lvl) {
        case SupportLevel::Supported:        item.badgeText = L"SUPPORTED"; break;
        case SupportLevel::ConditionalAdmin: item.badgeText = L"ADMIN REQ"; break;
        case SupportLevel::LockedOrLimited:  item.badgeText = L"CPU LOCKED"; break;
        case SupportLevel::Unsupported:      item.badgeText = L"UNSUPPORTED"; break;
        case SupportLevel::Blocked:          item.badgeText = L"BLOCKED"; break;
        }
        m_info.featureChecks.push_back(std::move(item));
    };

    if (!m_info.isIntel) {
        add(L"powerplan", L"VPower Intel Beta", L"Requires GenuineIntel CPU", SupportLevel::Blocked);
        add(L"turbo", L"Turbo Boost Control", L"Requires GenuineIntel CPU", SupportLevel::Blocked);
        add(L"hwp", L"Speed Shift (HWP EPP)", L"Requires GenuineIntel CPU", SupportLevel::Blocked);
        add(L"hybrid", L"P/E Hybrid Scheduler", L"Requires GenuineIntel CPU", SupportLevel::Blocked);
        add(L"unpark", L"Core Unpark & Clocks", L"Requires GenuineIntel CPU", SupportLevel::Blocked);
        add(L"freqcap", L"Frequency Cap Override", L"Requires GenuineIntel CPU", SupportLevel::Blocked);
        add(L"cstate", L"Idle C-State Control", L"Requires GenuineIntel CPU", SupportLevel::Blocked);
        add(L"timer", L"OS Boost & 0.5ms Timer", L"Requires GenuineIntel CPU", SupportLevel::Blocked);
        add(L"multiplier", L"Ratio / BCLK OC", L"Requires GenuineIntel CPU", SupportLevel::Blocked);
        return;
    }

    add(L"powerplan",
        L"VPower Intel Beta",
        elevated ? L"Custom Intel power scheme ready" : L"Elevate to modify power scheme",
        elevated ? SupportLevel::Supported : SupportLevel::ConditionalAdmin);

    add(L"turbo",
        L"Turbo Boost Control",
        m_info.hasTurboBoost
            ? (m_info.hasTurboMax3 ? L"CPUID 0x06 Turbo + Turbo Max 3.0" : L"CPUID 0x06 Turbo Boost")
            : L"Not detected via CPUID 0x06",
        m_info.hasTurboBoost ? SupportLevel::Supported : SupportLevel::Unsupported);

    add(L"hwp",
        L"Speed Shift (HWP EPP)",
        (m_info.hasHwp && m_info.hasHwpEpp) ? L"Hardware P-state & EPP 0-100%" : L"Legacy ACPI P-states only",
        (m_info.hasHwp && m_info.hasHwpEpp) ? SupportLevel::Supported : SupportLevel::Unsupported);

    wchar_t hybridBuf[96]{};
    if (m_info.isHybrid) {
        std::swprintf(hybridBuf, 96, L"%u P-Core + %u E-Core (%u threads)",
                      m_info.pCoreCount, m_info.eCoreCount, m_info.logicalThreads);
        add(L"hybrid", L"P/E Hybrid Scheduler", hybridBuf, SupportLevel::Supported);
    } else {
        std::swprintf(hybridBuf, 96, L"%u uniform cores", m_info.physicalCores);
        add(L"hybrid", L"P/E Hybrid Scheduler", hybridBuf, SupportLevel::Unsupported);
    }

    add(L"unpark",
        L"Core Unpark & Clocks",
        L"CPMINCORES + PROCTHROTTLEMIN/MAX (Class 0/1/2)",
        elevated ? SupportLevel::Supported : SupportLevel::ConditionalAdmin);

    add(L"freqcap",
        L"Frequency Cap Override",
        L"Uncapped (0 MHz) or manual MHz limit",
        elevated ? SupportLevel::Supported : SupportLevel::ConditionalAdmin);

    add(L"cstate",
        L"Idle C-States",
        L"IDLEDISABLE + HWP Window + Hold Time",
        elevated ? SupportLevel::Supported : SupportLevel::ConditionalAdmin);

    add(L"timer",
        L"Apps Tweak & Kernel",
        L"P-Core Routing, EcoQoS Off, I/O High, IFEO, 0.5ms",
        SupportLevel::Supported);

    if (m_info.isUnlockedSku) {
        add(L"multiplier",
            L"Ratio / BCLK OC",
            L"Unlocked -K / -HX / -XE SKU",
            SupportLevel::Supported);
    } else {
        add(L"multiplier",
            L"Ratio / BCLK OC",
            L"Locked mobile/desktop SKU (Turbo & PPM active)",
            SupportLevel::LockedOrLimited);
    }
}

double CpuIntelEngine::QueryAcpiThermalC() {
    if (m_impl->wmiChecked && !m_impl->wmiAvailable) {
        return -1.0;
    }

    if (!m_impl->wmiChecked) {
        m_impl->wmiChecked = true;
        IWbemLocator* loc = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IWbemLocator, reinterpret_cast<void**>(&loc));
        if (SUCCEEDED(hr) && loc) {
            IWbemServices* svc = nullptr;
            hr = loc->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
            loc->Release();
            if (SUCCEEDED(hr) && svc) {
                CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                  RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                                  nullptr, EOAC_NONE);
                m_impl->wmiSvc = svc;
                m_impl->wmiAvailable = true;
            }
        }
    }

    if (!m_impl->wmiAvailable || !m_impl->wmiSvc) {
        return -1.0;
    }

    IEnumWbemClassObject* enumerator = nullptr;
    HRESULT hr = m_impl->wmiSvc->ExecQuery(
        _bstr_t(L"WQL"),
        _bstr_t(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        &enumerator
    );
    if (FAILED(hr) || !enumerator) {
        if (++m_impl->wmiFailCount >= 3) {
            m_impl->wmiAvailable = false;
        }
        return -1.0;
    }

    double bestC = -1.0;
    IWbemClassObject* obj = nullptr;
    ULONG ret = 0;
    while (enumerator->Next(200, 1, &obj, &ret) == WBEM_S_NO_ERROR && ret > 0) {
        VARIANT vt{};
        if (SUCCEEDED(obj->Get(L"CurrentTemperature", 0, &vt, nullptr, nullptr))) {
            double rawDeciK = 0.0;
            if (vt.vt == VT_I4) rawDeciK = static_cast<double>(vt.lVal);
            else if (vt.vt == VT_UI4) rawDeciK = static_cast<double>(vt.ulVal);
            else if (vt.vt == VT_I8) rawDeciK = static_cast<double>(vt.llVal);
            else if (vt.vt == VT_R8) rawDeciK = vt.dblVal;

            if (rawDeciK > 2000.0 && rawDeciK < 4500.0) {
                const double c = (rawDeciK / 10.0) - 273.15;
                if (c >= 10.0 && c <= 125.0) {
                    bestC = std::max(bestC, c);
                }
            }
        }
        VariantClear(&vt);
        obj->Release();
    }
    enumerator->Release();
    if (bestC < 0.0) {
        if (++m_impl->wmiFailCount >= 3) {
            m_impl->wmiAvailable = false;
        }
    } else {
        m_impl->wmiFailCount = 0;
    }
    return bestC;
}

CpuLiveSnapshot CpuIntelEngine::SampleLiveSnapshot() {
    CpuLiveSnapshot snap{};
    snap.isIntel = m_info.isIntel;
    snap.isAmdBlocked = !m_info.isIntel;
    snap.highResTimerActive = m_impl->highResTimerActive;

    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        snap.onAcPower = (sps.ACLineStatus != 0);
    }

    PERFORMANCE_INFORMATION pi{};
    pi.cb = sizeof(pi);
    if (GetPerformanceInfo(&pi, sizeof(pi))) {
        snap.processCount = pi.ProcessCount;
        snap.threadCount = pi.ThreadCount;
    }

    const size_t totalLp = std::max<size_t>(1, m_info.logicalThreads);
    std::vector<double> lpUsage(totalLp, 0.0);
    std::vector<uint32_t> lpMhz(totalLp, m_info.baseMhz);
    std::vector<uint32_t> lpBaseMhz(totalLp, m_info.baseMhz);

    if (m_impl->fnNtQuerySysInfo) {
        std::vector<SysProcPerfInfo> curPerf(totalLp);
        ULONG retLen = 0;
        LONG st = m_impl->fnNtQuerySysInfo(
            8,
            curPerf.data(),
            static_cast<ULONG>(curPerf.size() * sizeof(SysProcPerfInfo)),
            &retLen
        );
        const size_t validCount = std::min<size_t>(totalLp, retLen / sizeof(SysProcPerfInfo));
        if (st == 0 && validCount > 0) {
            if (m_impl->hasPrevPerf && m_impl->prevPerf.size() == curPerf.size()) {
                for (size_t i = 0; i < validCount; ++i) {
                    const LONGLONG dIdle = curPerf[i].IdleTime.QuadPart - m_impl->prevPerf[i].IdleTime.QuadPart;
                    const LONGLONG dKern = curPerf[i].KernelTime.QuadPart - m_impl->prevPerf[i].KernelTime.QuadPart;
                    const LONGLONG dUser = curPerf[i].UserTime.QuadPart - m_impl->prevPerf[i].UserTime.QuadPart;
                    const LONGLONG dTot = dKern + dUser;
                    if (dTot > 0) {
                        const double busy = static_cast<double>(dTot - dIdle);
                        lpUsage[i] = std::clamp((busy / static_cast<double>(dTot)) * 100.0, 0.0, 100.0);
                    }
                }
            }
            m_impl->prevPerf = std::move(curPerf);
            m_impl->hasPrevPerf = true;
        }
    }

    std::vector<ProcPowerInfo> pwrInfo;
    if (QueryAllProcessorPowerInfo(pwrInfo, totalLp)) {
        const size_t nPwr = std::min<size_t>(totalLp, pwrInfo.size());
        for (size_t i = 0; i < nPwr; ++i) {
            if (pwrInfo[i].MaxMhz > 0) lpBaseMhz[i] = pwrInfo[i].MaxMhz;
            if (pwrInfo[i].CurrentMhz > 0) lpMhz[i] = pwrInfo[i].CurrentMhz;
        }
    }

    if (m_impl->pdhQuery && m_impl->pdhPrimed && m_impl->pdhCounters.size() == totalLp) {
        if (PdhCollectQueryData(m_impl->pdhQuery) == ERROR_SUCCESS) {
            for (size_t i = 0; i < totalLp; ++i) {
                auto& lc = m_impl->pdhCounters[i];
                const uint32_t pkgBase = lpBaseMhz[i] > 0 ? lpBaseMhz[i] : m_info.baseMhz;
                double baseM = static_cast<double>(pkgBase);
                if (lc.freqMhz) {
                    PDH_FMT_COUNTERVALUE vFreq{};
                    if (PdhGetFormattedCounterValue(lc.freqMhz, PDH_FMT_DOUBLE, nullptr, &vFreq) == ERROR_SUCCESS &&
                        vFreq.CStatus == ERROR_SUCCESS && vFreq.doubleValue > 100.0) {
                        const uint32_t fMhz = static_cast<uint32_t>(std::lround(vFreq.doubleValue));
                        if (lc.kind == CoreKind::ECore) {
                            if (fMhz <= pkgBase && fMhz > lc.cachedNominalMhz) {
                                lc.cachedNominalMhz = fMhz;
                            }
                            if (lc.cachedNominalMhz > 0) {
                                baseM = static_cast<double>(lc.cachedNominalMhz);
                                lpBaseMhz[i] = lc.cachedNominalMhz;
                            }
                        } else if (fMhz > pkgBase) {
                            baseM = static_cast<double>(fMhz);
                            lpBaseMhz[i] = fMhz;
                        }
                    }
                } else if (lc.kind == CoreKind::ECore && lc.cachedNominalMhz > 0) {
                    baseM = static_cast<double>(lc.cachedNominalMhz);
                    lpBaseMhz[i] = lc.cachedNominalMhz;
                }
                if (lc.perfPct && baseM > 100.0) {
                    PDH_FMT_COUNTERVALUE vPerf{};
                    if (PdhGetFormattedCounterValue(lc.perfPct, PDH_FMT_DOUBLE, nullptr, &vPerf) == ERROR_SUCCESS &&
                        vPerf.CStatus == ERROR_SUCCESS && vPerf.doubleValue > 1.0) {
                        lpMhz[i] = static_cast<uint32_t>(std::lround(baseM * (vPerf.doubleValue / 100.0)));
                    }
                }
            }
        }
    }

    size_t lpCursor = 0;
    double sumAllUsage = 0.0;
    double sumPUsage = 0.0;
    double sumEUsage = 0.0;
    uint64_t sumAllMhz = 0;
    uint64_t sumPMhz = 0;
    uint64_t sumEMhz = 0;
    uint32_t pCount = 0;
    uint32_t eCount = 0;

    for (const auto& ct : m_info.cores) {
        CoreLiveSample cs{};
        cs.coreIndex = ct.coreIndex;
        cs.typeLocalIndex = ct.typeLocalIndex;
        cs.kind = ct.kind;
        cs.threadCount = static_cast<uint32_t>(ct.logicalProcessors.size());

        double coreUsageSum = 0.0;
        uint32_t corePeakMhz = 0;
        uint32_t coreBaseMhz = m_info.baseMhz;
        for (size_t s = 0; s < ct.logicalProcessors.size() && lpCursor < totalLp; ++s, ++lpCursor) {
            coreUsageSum += lpUsage[lpCursor];
            corePeakMhz = std::max(corePeakMhz, lpMhz[lpCursor]);
            if (lpBaseMhz[lpCursor] > 0) coreBaseMhz = lpBaseMhz[lpCursor];
        }
        cs.usagePercent = (cs.threadCount > 0) ? (coreUsageSum / static_cast<double>(cs.threadCount)) : 0.0;
        cs.currentMhz = corePeakMhz;
        cs.baseMhz = coreBaseMhz;

        sumAllUsage += cs.usagePercent;
        sumAllMhz += cs.currentMhz;
        snap.peakCoreMhz = std::max(snap.peakCoreMhz, cs.currentMhz);

        if (cs.kind == CoreKind::PCore) {
            sumPUsage += cs.usagePercent;
            sumPMhz += cs.currentMhz;
            ++pCount;
        } else if (cs.kind == CoreKind::ECore) {
            sumEUsage += cs.usagePercent;
            sumEMhz += cs.currentMhz;
            ++eCount;
        }

        snap.cores.push_back(cs);
    }

    if (!snap.cores.empty()) {
        snap.totalUsagePercent = sumAllUsage / static_cast<double>(snap.cores.size());
        snap.packageMhz = (snap.peakCoreMhz > 0)
            ? static_cast<uint32_t>(sumAllMhz / snap.cores.size())
            : m_info.baseMhz;
    }
    if (pCount > 0) {
        snap.pCoreAvgUsage = sumPUsage / static_cast<double>(pCount);
        snap.pCoreAvgMhz = static_cast<uint32_t>(sumPMhz / pCount);
    }
    if (eCount > 0) {
        snap.eCoreAvgUsage = sumEUsage / static_cast<double>(eCount);
        snap.eCoreAvgMhz = static_cast<uint32_t>(sumEMhz / eCount);
    }

    snap.tempCelsius = QueryAcpiThermalC();

    GUID vpowerGuid{};
    snap.vpowerPlanExists = FindSchemeByName(kVPowerPlanName, vpowerGuid);

    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) == ERROR_SUCCESS && pActive) {
        snap.activePlanGuid = GuidToString(*pActive);
        snap.activePlanName = ReadSchemeFriendlyName(*pActive);
        if (snap.activePlanName.empty()) snap.activePlanName = L"Balanced";
        snap.isVPowerActive = (_wcsicmp(snap.activePlanName.c_str(), kVPowerPlanName) == 0);

        const bool ac = snap.onAcPower;
        snap.curBoostMode = ReadSettingValue(*pActive, kGuidSubProc, kGuidBoostMode, ac, 1);
        snap.curTurboBoostPol = ReadSettingValue(*pActive, kGuidSubProc, kGuidBoostPol, ac, 100);
        snap.curEpp = m_info.isHybrid
            ? ReadSettingValue(*pActive, kGuidSubProc, kGuidEpp1, ac, 50)
            : ReadSettingValue(*pActive, kGuidSubProc, kGuidEpp0, ac, 50);
        snap.curMinState = m_info.isHybrid
            ? ReadSettingValue(*pActive, kGuidSubProc, kGuidProcMin1, ac, 5)
            : ReadSettingValue(*pActive, kGuidSubProc, kGuidProcMin0, ac, 5);
        snap.curMaxState = m_info.isHybrid
            ? ReadSettingValue(*pActive, kGuidSubProc, kGuidProcMax1, ac, 100)
            : ReadSettingValue(*pActive, kGuidSubProc, kGuidProcMax0, ac, 100);
        snap.curMinCores = ReadSettingValue(*pActive, kGuidSubProc, kGuidMinCores0, ac, 100);
        snap.curSchedPolicy = ReadSettingValue(*pActive, kGuidSubProc, kGuidSchedPolicy, ac, 5);
        snap.curFreqCapMhz = m_info.isHybrid
            ? ReadSettingValue(*pActive, kGuidSubProc, kGuidFreqMax1, ac, 0)
            : ReadSettingValue(*pActive, kGuidSubProc, kGuidFreqMax0, ac, 0);
        snap.curIdleDisabled = ReadSettingValue(*pActive, kGuidSubProc, kGuidIdleDisable, ac, 0) != 0;

        LocalFree(pActive);
    } else {
        snap.activePlanName = L"Balanced";
    }

    return snap;
}

std::vector<PowerPlanEntry> CpuIntelEngine::ListAllPowerPlans() const {
    std::vector<PowerPlanEntry> list;
    std::set<std::wstring> seenGuids;
    std::map<std::wstring, size_t> seenNameIndex;

    GUID activeGuid{};
    bool hasActive = false;
    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) == ERROR_SUCCESS && pActive) {
        activeGuid = *pActive;
        hasActive = true;
        LocalFree(pActive);
    }

    auto tryAddGuid = [&](const GUID& g) {
        std::wstring gStr = GuidToString(g);
        for (auto& c : gStr) c = towupper(c);
        if (gStr == L"{3AF9B8D9-7C97-431D-AD78-34A8BFEA439F}" ||
            gStr == L"{961CC777-2547-4F9D-8174-7D86181B8A7A}" ||
            gStr == L"{DED574B5-45A0-4F42-8737-46345C09C238}") {
            return;
        }
        if (!seenGuids.insert(gStr).second) return;

        std::wstring name = ReadSchemeFriendlyName(g);
        if (name.empty()) return;

        std::wstring lowerName = name;
        for (auto& ch : lowerName) ch = static_cast<wchar_t>(towlower(ch));
        if (lowerName.find(L"overlay") != std::wstring::npos) return;

        PowerPlanEntry e;
        e.guidStr = gStr;
        e.name = name;
        e.isActive = hasActive && IsEqualGUID(g, activeGuid);
        e.isVPower = IsVPowerSchemeName(name);

        auto it = seenNameIndex.find(lowerName);
        if (it == seenNameIndex.end()) {
            seenNameIndex[lowerName] = list.size();
            list.push_back(std::move(e));
        } else if (e.isActive && !list[it->second].isActive) {
            list[it->second] = std::move(e);
        }
    };

    if (hasActive) {
        tryAddGuid(activeGuid);
    }

    for (ULONG idx = 0;; ++idx) {
        GUID g{};
        DWORD sz = sizeof(g);
        if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, idx, reinterpret_cast<UCHAR*>(&g), &sz) != ERROR_SUCCESS) {
            break;
        }
        tryAddGuid(g);
    }

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes",
                      0, KEY_ENUMERATE_SUB_KEYS, &hKey) == ERROR_SUCCESS) {
        wchar_t subKey[64]{};
        for (DWORD i = 0;; ++i) {
            DWORD len = ARRAYSIZE(subKey);
            if (RegEnumKeyExW(hKey, i, subKey, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            std::wstring br = L"{" + std::wstring(subKey) + L"}";
            GUID g{};
            if (SUCCEEDED(CLSIDFromString(br.c_str(), &g))) {
                tryAddGuid(g);
            }
        }
        RegCloseKey(hKey);
    }

    std::sort(list.begin(), list.end(), [](const PowerPlanEntry& a, const PowerPlanEntry& b) {
        if (a.isActive != b.isActive) return a.isActive;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    return list;
}

bool CpuIntelEngine::SyncConfigFromActivePlan(AppConfig& cfg) const {
    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) != ERROR_SUCCESS || !pActive) {
        return false;
    }

    SYSTEM_POWER_STATUS sps{};
    const bool ac = GetSystemPowerStatus(&sps) ? (sps.ACLineStatus != 0) : true;
    const GUID& g = *pActive;

    cfg.boostMode = static_cast<int>(ReadSettingValue(g, kGuidSubProc, kGuidBoostMode, ac, static_cast<uint32_t>(cfg.boostMode)));
    cfg.turboBoostPolicy = static_cast<int>(ReadSettingValue(g, kGuidSubProc, kGuidBoostPol, ac, 100));
    cfg.eppPercent = static_cast<int>(m_info.isHybrid
        ? ReadSettingValue(g, kGuidSubProc, kGuidEpp1, ac, static_cast<uint32_t>(cfg.eppPercent))
        : ReadSettingValue(g, kGuidSubProc, kGuidEpp0, ac, static_cast<uint32_t>(cfg.eppPercent)));
    cfg.minStatePercent = static_cast<int>(m_info.isHybrid
        ? ReadSettingValue(g, kGuidSubProc, kGuidProcMin1, ac, 5)
        : ReadSettingValue(g, kGuidSubProc, kGuidProcMin0, ac, 5));
    cfg.maxStatePercent = static_cast<int>(m_info.isHybrid
        ? ReadSettingValue(g, kGuidSubProc, kGuidProcMax1, ac, 100)
        : ReadSettingValue(g, kGuidSubProc, kGuidProcMax0, ac, 100));
    cfg.unparkCoresPercent = static_cast<int>(ReadSettingValue(g, kGuidSubProc, kGuidMinCores0, ac, 100));
    cfg.freqCapMhz = static_cast<int>(m_info.isHybrid
        ? ReadSettingValue(g, kGuidSubProc, kGuidFreqMax1, ac, 0)
        : ReadSettingValue(g, kGuidSubProc, kGuidFreqMax0, ac, 0));
    cfg.schedPolicy = static_cast<int>(ReadSettingValue(g, kGuidSubProc, kGuidSchedPolicy, ac, 5));
    cfg.incThreshold = static_cast<int>(ReadSettingValue(g, kGuidSubProc, kGuidIncThresh0, ac, 15));
    cfg.decThreshold = static_cast<int>(ReadSettingValue(g, kGuidSubProc, kGuidDecThresh0, ac, 8));
    cfg.heteroFloorPerf = static_cast<int>(ReadSettingValue(g, kGuidSubProc, kGuidHeteroFloor, ac, 100));
    cfg.concurrencyThreshold = static_cast<int>(ReadSettingValue(g, kGuidSubProc, kGuidCpConcur, ac, 50));
    cfg.activeCooling = ReadSettingValue(g, kGuidSubProc, kGuidSysCoolPol, ac, 1) != 0;
    cfg.hwpAutonomous = ReadSettingValue(g, kGuidSubProc, kGuidAutonomous, ac, 1) != 0;
    cfg.disableIdleStates = ReadSettingValue(g, kGuidSubProc, kGuidIdleDisable, ac, 0) != 0;
    cfg.disableOsThrottling = ReadSettingValue(g, kGuidSubProc, kGuidThrottling, ac, 2) == 0;

    cfg.minStatePercent = std::clamp(cfg.minStatePercent, 5, 100);
    cfg.maxStatePercent = std::clamp(cfg.maxStatePercent, 20, 100);
    cfg.eppPercent = std::clamp(cfg.eppPercent, 0, 100);
    cfg.unparkCoresPercent = std::clamp(cfg.unparkCoresPercent, 0, 100);
    cfg.turboBoostPolicy = std::clamp(cfg.turboBoostPolicy, 0, 100);

    LocalFree(pActive);
    return true;
}

TuneApplyResult CpuIntelEngine::SwitchPowerPlan(const std::wstring& guidStr, AppConfig& cfg) {
    TuneApplyResult res{};
    if (!m_info.isIntel) {
        res.blockedNonIntel = true;
        res.message = L"Blocked: Non-Intel / AMD CPU.";
        return res;
    }

    GUID target{};
    if (!StringToGuid(guidStr, target)) {
        res.message = L"Invalid power plan GUID.";
        return res;
    }

    DWORD st = PowerSetActiveScheme(nullptr, &target);
    if (st != ERROR_SUCCESS) {
        res.accessDenied = (st == ERROR_ACCESS_DENIED);
        res.message = res.accessDenied
            ? L"Admin privileges required to switch plan."
            : L"Failed to switch power plan.";
        return res;
    }

    SyncConfigFromActivePlan(cfg);
    ConfigManager::Save(cfg);

    res.success = true;
    res.schemeGuid = GuidToString(target);
    res.message = L"Switched: " + ReadSchemeFriendlyName(target);
    return res;
}

namespace {

DWORD ApplySchemeProfileSettings(const GUID& targetGuid, const AppConfig& cfg, bool isHybrid) {
    DWORD st = WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidBoostMode, static_cast<DWORD>(cfg.boostMode));
    if (st == ERROR_ACCESS_DENIED) {
        return st;
    }

    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidBoostPol, static_cast<DWORD>(cfg.turboBoostPolicy));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidEpp0, static_cast<DWORD>(cfg.eppPercent));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidEpp1, static_cast<DWORD>(cfg.eppPercent));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidEpp2, static_cast<DWORD>(cfg.eppPercent));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidProcMin0, static_cast<DWORD>(cfg.minStatePercent));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidProcMin1, static_cast<DWORD>(cfg.minStatePercent));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidProcMin2, static_cast<DWORD>(cfg.minStatePercent));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidProcMax0, static_cast<DWORD>(cfg.maxStatePercent));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidProcMax1, static_cast<DWORD>(cfg.maxStatePercent));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidProcMax2, static_cast<DWORD>(cfg.maxStatePercent));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidFreqMax0, static_cast<DWORD>(cfg.freqCapMhz));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidFreqMax1, static_cast<DWORD>(cfg.freqCapMhz));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidMinCores0, static_cast<DWORD>(cfg.unparkCoresPercent));
    // On Intel Hybrid CPUs, let idle E-cores park under Auto/Prefer-P/P-Only so 1T & P-core bursts retain full PL1/PL2 headroom.
    const bool parkIdleECores = isHybrid && (cfg.schedPolicy == 5 || cfg.schedPolicy == 2 || cfg.schedPolicy == 1);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidMinCores1,
                          parkIdleECores ? 0u : static_cast<DWORD>(cfg.unparkCoresPercent));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidMaxCores0, 100);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidMaxCores1, 100);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidIncThresh0, static_cast<DWORD>(cfg.incThreshold));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidIncThresh1, static_cast<DWORD>(cfg.incThreshold));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidDecThresh0, static_cast<DWORD>(cfg.decThreshold));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidDecThresh1, static_cast<DWORD>(cfg.decThreshold));
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidCpConcur, static_cast<DWORD>(cfg.concurrencyThreshold));

    if (isHybrid) {
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidSchedPolicy, static_cast<DWORD>(cfg.schedPolicy));
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidShortSched, static_cast<DWORD>(cfg.schedPolicy));
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidHeteroPol, 0);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidHeteroFloor, static_cast<DWORD>(cfg.heteroFloorPerf));
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidHeteroInit1, 100);
        const DWORD pCoreFloor = (cfg.schedPolicy == 1) ? 1u : 0u;
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidLongArchLow, pCoreFloor);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidShortArchLow, pCoreFloor);
    }

    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidSysCoolPol, cfg.activeCooling ? 1u : 0u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidAutonomous, cfg.hwpAutonomous ? 1u : 0u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidIdleDisable, cfg.disableIdleStates ? 1u : 0u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidIdleStateMax, 0u);

    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfAutoWin, cfg.hwpAutonomous ? 30000u : 1000u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfIncTime0, 1u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfIncTime1, 1u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfDecTime0, cfg.disableOsThrottling ? 100u : 1u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfDecTime1, cfg.disableOsThrottling ? 100u : 1u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidCpIncTime, 1u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidCpDecTime, cfg.disableOsThrottling ? 100u : 2u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidSmtUnpark, 0u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidDistUtil, 0u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidResPrio0, 100u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidResPrio1, 100u);
    WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidResPrio2, 100u);

    if (cfg.disableOsThrottling) {
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidThrottling, 0);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidDutyCycle, 0);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidIdleScale, 0);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfCheck, 5);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfIncPol0, 3);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfIncPol1, 3);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfDecPol0, 0);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfDecPol1, 0);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidCpIncPol, 2);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidCpDecPol, 0);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidLatPerf0, 100);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidLatPerf1, 100);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidLatPerf2, 100);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidLatUnpark0, 100);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidLatUnpark1, 100);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidLatEpp0, 0);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidLatEpp1, 0);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidLatEpp2, 0);
    } else {
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidThrottling, 2);
        WriteSettingBothModes(targetGuid, kGuidSubProc, kGuidPerfCheck, 15);
    }

    return ERROR_SUCCESS;
}

}

TuneApplyResult CpuIntelEngine::CreateOrActivateVPowerPlan(AppConfig& cfg, bool applyMaxIntelPreset) {
    TuneApplyResult res{};
    if (!m_info.isIntel) {
        res.blockedNonIntel = true;
        res.message = L"Blocked: Non-Intel / AMD CPU.";
        return res;
    }

    if (applyMaxIntelPreset) {
        cfg.unparkCoresPercent = 100;
        cfg.minStatePercent = 5;
        cfg.maxStatePercent = 100;
        cfg.turboBoostPolicy = 100;
        cfg.boostMode = 4;
        cfg.eppPercent = 0;
        cfg.freqCapMhz = 0;
        cfg.schedPolicy = 5;
        cfg.incThreshold = 5;
        cfg.decThreshold = 2;
        cfg.heteroFloorPerf = 100;
        cfg.concurrencyThreshold = 15;
        cfg.activeCooling = true;
        cfg.hwpAutonomous = true;
        cfg.disableIdleStates = false;
        cfg.disableOsThrottling = true;
        cfg.boostForegroundPCore = true;
        cfg.highResTimer = true;
    }

    GUID* curActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &curActive) == ERROR_SUCCESS && curActive) {
        const std::wstring curName = ReadSchemeFriendlyName(*curActive);
        if (!IsVPowerSchemeName(curName)) {
            cfg.originalSchemeGuid = GuidToString(*curActive);
            SaveOriginalSchemeBackup(cfg.originalSchemeGuid);
        } else {
            GUID parsedOrig{};
            const bool origValid = !cfg.originalSchemeGuid.empty() &&
                                   StringToGuid(cfg.originalSchemeGuid, parsedOrig) &&
                                   SchemeExists(parsedOrig) &&
                                   !IsVPowerSchemeName(ReadSchemeFriendlyName(parsedOrig));
            if (!origValid) {
                const std::wstring bakOrig = LoadOriginalSchemeBackup();
                if (!bakOrig.empty()) {
                    cfg.originalSchemeGuid = bakOrig;
                }
            }
        }
        LocalFree(curActive);
    }

    GUID targetGuid{};
    bool found = false;
    if (!cfg.vpowerSchemeGuid.empty()) {
        GUID cached{};
        if (StringToGuid(cfg.vpowerSchemeGuid, cached) && SchemeExists(cached)) {
            targetGuid = cached;
            found = true;
        }
    }
    if (!found) {
        found = FindSchemeByName(kVPowerPlanName, targetGuid) ||
                FindSchemeByName(kLegacyVPowerPlanName, targetGuid);
    }

    // Migrate any legacy VPower created from High Performance (hidden ASPM/throttle defaults) to Balanced base.
    if (found) {
        const std::wstring desc = ReadSchemeDescription(targetGuid);
        if (desc.find(L"[Balanced-HWP-V2]") == std::wstring::npos) {
            GUID* curNow = nullptr;
            if (PowerGetActiveScheme(nullptr, &curNow) == ERROR_SUCCESS && curNow) {
                if (IsEqualGUID(*curNow, targetGuid)) {
                    if (SchemeExists(kGuidBalanced)) {
                        PowerSetActiveScheme(nullptr, &kGuidBalanced);
                    }
                }
                LocalFree(curNow);
            }
            if (PowerDeleteScheme(nullptr, &targetGuid) == ERROR_SUCCESS) {
                found = false;
                targetGuid = {};
            }
        }
    }

    if (!found) {
        GUID baseGuid = kGuidBalanced;
        if (!SchemeExists(baseGuid)) {
            if (SchemeExists(kGuidHighPerf)) {
                baseGuid = kGuidHighPerf;
            } else {
                GUID* pActive = nullptr;
                if (PowerGetActiveScheme(nullptr, &pActive) == ERROR_SUCCESS && pActive) {
                    baseGuid = *pActive;
                    LocalFree(pActive);
                }
            }
        }

        GUID* dupGuid = nullptr;
        DWORD st = PowerDuplicateScheme(nullptr, &baseGuid, &dupGuid);
        if (st != ERROR_SUCCESS || !dupGuid) {
            res.accessDenied = (st == ERROR_ACCESS_DENIED);
            res.message = res.accessDenied
                ? L"Admin privileges required to create VPower."
                : L"Failed to duplicate base power scheme.";
            return res;
        }
        targetGuid = *dupGuid;
        LocalFree(dupGuid);
    }

    std::wstring planName(kVPowerPlanName);
    const DWORD nameSize = static_cast<DWORD>((planName.size() + 1) * sizeof(wchar_t));
    DWORD stName = PowerWriteFriendlyName(nullptr, &targetGuid, nullptr, nullptr,
                                          reinterpret_cast<UCHAR*>(planName.data()), nameSize);
    if (stName == ERROR_ACCESS_DENIED) {
        res.accessDenied = true;
        res.message = L"Elevate to Admin to edit VPower Intel Beta.";
        return res;
    }

    std::wstring planDesc(kVPowerPlanDesc);
    const DWORD descSize = static_cast<DWORD>((planDesc.size() + 1) * sizeof(wchar_t));
    PowerWriteDescription(nullptr, &targetGuid, nullptr, nullptr,
                          reinterpret_cast<UCHAR*>(planDesc.data()), descSize);

    ApplySchemeProfileSettings(targetGuid, cfg, m_info.isHybrid);

    DWORD stAct = PowerSetActiveScheme(nullptr, &targetGuid);
    if (stAct != ERROR_SUCCESS) {
        res.accessDenied = (stAct == ERROR_ACCESS_DENIED);
        res.message = res.accessDenied
            ? L"Admin privileges required to activate VPower."
            : L"Failed to activate VPower Intel Beta.";
        return res;
    }

    ApplyOsBrutalTweaks(cfg.disableOsThrottling);
    SetHighResTimer(cfg.highResTimer);

    cfg.vpowerSchemeGuid = GuidToString(targetGuid);
    ConfigManager::Save(cfg);

    res.success = true;
    res.schemeGuid = cfg.vpowerSchemeGuid;
    res.message = L"VPower Intel Beta active";
    return res;
}

TuneApplyResult CpuIntelEngine::ApplyTuneProfile(AppConfig& cfg, bool ensureVPowerScheme) {
    TuneApplyResult res{};
    if (!m_info.isIntel) {
        res.blockedNonIntel = true;
        res.message = L"Blocked: Non-Intel / AMD CPU.";
        return res;
    }

    if (ensureVPowerScheme) {
        return CreateOrActivateVPowerPlan(cfg, false);
    }

    GUID* pActive = nullptr;
    if (PowerGetActiveScheme(nullptr, &pActive) != ERROR_SUCCESS || !pActive) {
        res.message = L"Unable to query active scheme.";
        return res;
    }

    const GUID activeGuid = *pActive;
    LocalFree(pActive);

    DWORD st = ApplySchemeProfileSettings(activeGuid, cfg, m_info.isHybrid);
    if (st == ERROR_ACCESS_DENIED) {
        res.accessDenied = true;
        res.message = L"Admin privileges required to write policy.";
        return res;
    }

    PowerSetActiveScheme(nullptr, &activeGuid);
    ApplyOsBrutalTweaks(cfg.disableOsThrottling);
    SetHighResTimer(cfg.highResTimer);
    ConfigManager::Save(cfg);

    res.success = true;
    res.schemeGuid = GuidToString(activeGuid);
    res.message = L"Applied to " + ReadSchemeFriendlyName(activeGuid);
    return res;
}

TuneApplyResult CpuIntelEngine::RestoreStockPlan(AppConfig& cfg) {
    TuneApplyResult res{};
    if (!m_info.isIntel) {
        res.blockedNonIntel = true;
        res.message = L"Blocked: Non-Intel / AMD CPU.";
        return res;
    }

    auto isValidRestoreCandidate = [](const std::wstring& gStr, GUID& outGuid) {
        if (gStr.empty() || !StringToGuid(gStr, outGuid) || !SchemeExists(outGuid)) {
            return false;
        }
        return !IsVPowerSchemeName(ReadSchemeFriendlyName(outGuid));
    };

    GUID targetGuid{};
    bool hasTarget = false;
    if (isValidRestoreCandidate(cfg.originalSchemeGuid, targetGuid)) {
        hasTarget = true;
    } else if (isValidRestoreCandidate(LoadOriginalSchemeBackup(), targetGuid)) {
        hasTarget = true;
    } else if (SchemeExists(kGuidBalanced)) {
        targetGuid = kGuidBalanced;
        hasTarget = true;
    }
    if (!hasTarget) {
        for (const auto& p : ListAllPowerPlans()) {
            GUID parsed{};
            if (!p.isVPower && StringToGuid(p.guidStr, parsed) && SchemeExists(parsed)) {
                targetGuid = parsed;
                hasTarget = true;
                break;
            }
        }
    }

    DWORD st = hasTarget ? PowerSetActiveScheme(nullptr, &targetGuid) : ERROR_FILE_NOT_FOUND;
    if (st != ERROR_SUCCESS) {
        res.accessDenied = (st == ERROR_ACCESS_DENIED);
        res.message = res.accessDenied
            ? L"Admin privileges required to restore plan."
            : L"Failed to restore previous power plan.";
        return res;
    }

    if (!cfg.vpowerSchemeGuid.empty()) {
        GUID vpGuid{};
        if (StringToGuid(cfg.vpowerSchemeGuid, vpGuid) &&
            !IsEqualGUID(vpGuid, targetGuid) &&
            SchemeExists(vpGuid) &&
            IsVPowerSchemeName(ReadSchemeFriendlyName(vpGuid))) {
            PowerDeleteScheme(nullptr, &vpGuid);
        }
    }
    for (const wchar_t* vpName : {kVPowerPlanName, kLegacyVPowerPlanName}) {
        for (int i = 0; i < 8; ++i) {
            GUID vpGuid{};
            if (!FindSchemeByName(vpName, vpGuid) || IsEqualGUID(vpGuid, targetGuid)) {
                break;
            }
            if (PowerDeleteScheme(nullptr, &vpGuid) != ERROR_SUCCESS) {
                break;
            }
        }
    }
    cfg.vpowerSchemeGuid.clear();
    ClearOriginalSchemeBackup();

    ApplyOsBrutalTweaks(false);
    cfg.applyOnStartup = false;
    cfg.boostForegroundPCore = false;
    cfg.highResTimer = false;
    cfg.disableOsThrottling = false;
    cfg.targetIfeoPersist = false;
    for (auto& rule : cfg.targetExes) {
        rule.enabled = false;
    }
    SetHighResTimer(false);
    RestoreAllBoostedProcesses(cfg);

    SyncConfigFromActivePlan(cfg);
    cfg.disableOsThrottling = false;
    ConfigManager::Save(cfg);

    res.success = true;
    res.schemeGuid = GuidToString(targetGuid);
    res.message = L"Restored: " + ReadSchemeFriendlyName(targetGuid);
    return res;
}

bool CpuIntelEngine::SetHighResTimer(bool enable) {
    if (!m_info.isIntel) {
        return false;
    }
    if (!m_impl->fnNtSetTimerRes) {
        return false;
    }

    ULONG actual = 0;
    if (enable) {
        ULONG minRes = 156250;
        ULONG maxRes = 5000;
        ULONG curRes = 0;
        if (m_impl->fnNtQueryTimerRes) {
            m_impl->fnNtQueryTimerRes(&minRes, &maxRes, &curRes);
        }
        const ULONG target = std::clamp<ULONG>(5000, maxRes, minRes);
        if (m_impl->fnNtSetTimerRes(target, TRUE, &actual) == 0) {
            m_impl->highResTimerActive = true;
            return true;
        }
        return false;
    } else {
        m_impl->fnNtSetTimerRes(156250, FALSE, &actual);
        m_impl->highResTimerActive = false;
        return true;
    }
}

void CpuIntelEngine::ApplyOsBrutalTweaks(bool enable) const {
    if (!m_info.isIntel) return;
    if (!enable) {
        RestoreBackedUpOverlay();
    } else {
        BackupAndSetOverlay(kGuidOvMaxPerf);
    }
    if (!IsElevated()) {
        if (!enable) CleanupRegBackupKeyIfEmpty();
        return;
    }

    // Never disable Windows EcoQoS PowerThrottling globally; per-app EcoQoS bypass runs in Apps Tweak.
    DeleteRegValue(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling",
        L"PowerThrottlingOff");

    constexpr wchar_t kGamesKey[] =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";

    if (!enable) {
        RestoreBackedUpHklmDword(L"GlobalTimerRes",
            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",
            L"GlobalTimerResolutionRequests");
        RestoreBackedUpHklmDword(L"SerializeTimer",
            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",
            L"SerializeTimerExpiration");
        RestoreBackedUpHklmDword(L"Win32PrioSep",
            L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
            L"Win32PrioritySeparation");
        RestoreBackedUpHklmDword(L"SysResp",
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
            L"SystemResponsiveness");
        RestoreBackedUpHklmDword(L"NetThrot",
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
            L"NetworkThrottlingIndex");

        RestoreBackedUpHklmDword(L"GamesAffinity", kGamesKey, L"Affinity");
        RestoreBackedUpHklmString(L"GamesBgOnly", kGamesKey, L"Background Only");
        RestoreBackedUpHklmDword(L"GamesClockRate", kGamesKey, L"Clock Rate");
        RestoreBackedUpHklmDword(L"GamesGpuPrio", kGamesKey, L"GPU Priority");
        RestoreBackedUpHklmDword(L"GamesPrio", kGamesKey, L"Priority");
        RestoreBackedUpHklmString(L"GamesSchedCat", kGamesKey, L"Scheduling Category");
        RestoreBackedUpHklmString(L"GamesSfioPrio", kGamesKey, L"SFIO Priority");

        CleanupRegBackupKeyIfEmpty();
        return;
    }

    BackupAndWriteHklmDword(L"GlobalTimerRes",
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",
        L"GlobalTimerResolutionRequests", 1u);
    BackupAndWriteHklmDword(L"SerializeTimer",
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",
        L"SerializeTimerExpiration", 0u);
    BackupAndWriteHklmDword(L"Win32PrioSep",
        L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
        L"Win32PrioritySeparation", 0x26u);
    BackupAndWriteHklmDword(L"SysResp",
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
        L"SystemResponsiveness", 0u);
    BackupAndWriteHklmDword(L"NetThrot",
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
        L"NetworkThrottlingIndex", 0xFFFFFFFFu);

    BackupAndWriteHklmDword(L"GamesAffinity", kGamesKey, L"Affinity", 0u);
    BackupAndWriteHklmString(L"GamesBgOnly", kGamesKey, L"Background Only", L"False");
    BackupAndWriteHklmDword(L"GamesClockRate", kGamesKey, L"Clock Rate", 10000u);
    BackupAndWriteHklmDword(L"GamesGpuPrio", kGamesKey, L"GPU Priority", 8u);
    BackupAndWriteHklmDword(L"GamesPrio", kGamesKey, L"Priority", 6u);
    BackupAndWriteHklmString(L"GamesSchedCat", kGamesKey, L"Scheduling Category", L"High");
    BackupAndWriteHklmString(L"GamesSfioPrio", kGamesKey, L"SFIO Priority", L"High");
}

namespace {

std::wstring LowerWide(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

DWORD PriorityModeToWin32(int mode) {
    switch (mode) {
    case 0: return NORMAL_PRIORITY_CLASS;
    case 1: return ABOVE_NORMAL_PRIORITY_CLASS;
    case 3: return REALTIME_PRIORITY_CLASS;
    case 2:
    default: return HIGH_PRIORITY_CLASS;
    }
}

DWORD PriorityModeToIfeoDword(int mode) {
    switch (mode) {
    case 0: return 2u; // Normal
    case 1: return 6u; // Above Normal
    case 3: return 4u; // Realtime
    case 2:
    default: return 3u; // High
    }
}

void SyncIfeoPerfOptions(const std::wstring& exeName, bool enable, int priorityMode, bool ioHigh) {
    if (exeName.empty()) return;
    const std::wstring parentKey =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" + exeName;
    const std::wstring subKey = parentKey + L"\\PerfOptions";

    if (!enable) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, L"CpuPriorityClass");
            RegDeleteValueW(hKey, L"IoPriority");
            RegCloseKey(hKey);
            RegDeleteKeyW(HKEY_LOCAL_MACHINE, subKey.c_str());
        }
        HKEY hParent = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, parentKey.c_str(), 0, KEY_QUERY_VALUE, &hParent) == ERROR_SUCCESS) {
            DWORD subKeys = 0;
            DWORD values = 0;
            const LONG qSt = RegQueryInfoKeyW(hParent, nullptr, nullptr, nullptr, &subKeys,
                                              nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(hParent);
            if (qSt == ERROR_SUCCESS && subKeys == 0 && values == 0) {
                RegDeleteKeyW(HKEY_LOCAL_MACHINE, parentKey.c_str());
            }
        }
        return;
    }

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        const DWORD prio = PriorityModeToIfeoDword(priorityMode);
        RegSetValueExW(hKey, L"CpuPriorityClass", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&prio), sizeof(prio));
        if (ioHigh) {
            const DWORD io = 3u; // High I/O Priority
            RegSetValueExW(hKey, L"IoPriority", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&io), sizeof(io));
        } else {
            RegDeleteValueW(hKey, L"IoPriority");
        }
        RegCloseKey(hKey);
    }
}

void SweepThreadsForPid(DWORD pid, bool deepBoost, int coreMode, const std::vector<DWORD>& pCoreLogical) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    size_t rr = 0;

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE hThread = OpenThread(THREAD_SET_INFORMATION | THREAD_SET_LIMITED_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION,
                                        FALSE, te.th32ThreadID);
            if (!hThread) continue;

            // Never disable dynamic priority boosting (bDisablePriorityBoost = FALSE keeps OS default boost enabled).
            SetThreadPriorityBoost(hThread, FALSE);

            THREAD_POWER_THROTTLING_STATE tps{};
            tps.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
            tps.ControlMask = deepBoost ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0u;
            tps.StateMask = 0u;
            SetThreadInformation(hThread, ThreadPowerThrottling, &tps, sizeof(tps));

            if (coreMode != 0 && !pCoreLogical.empty()) {
                const DWORD idealCpu = pCoreLogical[rr % pCoreLogical.size()];
                if (idealCpu < MAXIMUM_PROC_PER_GROUP) {
                    SetThreadIdealProcessor(hThread, idealCpu);
                }
                ++rr;
            }

            CloseHandle(hThread);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

} // namespace

std::vector<RunningProcessEntry> CpuIntelEngine::ListRunningProcesses() const {
    std::map<std::wstring, RunningProcessEntry> byKey;

    std::wstring fgExe;
    if (HWND fg = GetForegroundWindow()) {
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid > 4) {
            HANDLE hFg = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, fgPid);
            if (hFg) {
                wchar_t path[MAX_PATH]{};
                DWORD len = MAX_PATH;
                if (QueryFullProcessImageNameW(hFg, 0, path, &len)) {
                    const wchar_t* sl = wcsrchr(path, L'\\');
                    fgExe = LowerWide(sl ? (sl + 1) : path);
                }
                CloseHandle(hFg);
            }
        }
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID <= 4 || pe.th32ProcessID == GetCurrentProcessId()) continue;
            std::wstring name = pe.szExeFile;
            if (IsProtectedSystemProcessName(name)) continue;

            const std::wstring key = LowerWide(name);
            auto it = byKey.find(key);
            if (it == byKey.end()) {
                RunningProcessEntry r{};
                r.exeName = name;
                r.instanceCount = 1;
                r.isForeground = (!fgExe.empty() && key == fgExe);
                byKey.emplace(key, std::move(r));
            } else {
                ++it->second.instanceCount;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    std::vector<RunningProcessEntry> out;
    out.reserve(byKey.size());
    for (auto& kv : byKey) {
        out.push_back(std::move(kv.second));
    }
    std::sort(out.begin(), out.end(), [](const RunningProcessEntry& a, const RunningProcessEntry& b) {
        if (a.isForeground != b.isForeground) return a.isForeground;
        return _wcsicmp(a.exeName.c_str(), b.exeName.c_str()) < 0;
    });
    return out;
}

void CpuIntelEngine::InvalidateProcessBoostCache() {
    m_impl->boostedPidSignatures.clear();
    m_impl->lastFgBoostPid = 0;
}

namespace {

void EnsurePCoreRoutingInitialized(const CpuHardwareInfo& info,
                                   pfnGetSystemCpuSetInformation fnGetCpuSets,
                                   DWORD_PTR& pCoreMask,
                                   std::vector<DWORD>& pCoreLogical,
                                   std::vector<ULONG>& pCoreCpuSetIds,
                                   bool& initialized) {
    if (initialized) return;
    initialized = true;
    pCoreMask = 0;
    pCoreLogical.clear();
    pCoreCpuSetIds.clear();

    std::set<std::pair<WORD, DWORD>> pCoreSet;
    for (const auto& ct : info.cores) {
        const bool includeCore = info.isHybrid ? (ct.kind == CoreKind::PCore) : true;
        if (!includeCore) continue;
        for (const auto& lp : ct.logicalProcessors) {
            pCoreSet.emplace(lp.first, static_cast<DWORD>(lp.second));
            if (lp.first == 0 && lp.second < sizeof(DWORD_PTR) * 8) {
                pCoreMask |= (static_cast<DWORD_PTR>(1) << lp.second);
                pCoreLogical.push_back(static_cast<DWORD>(lp.second));
            }
        }
    }

    if (fnGetCpuSets && !pCoreSet.empty()) {
        ULONG bufLen = 0;
        fnGetCpuSets(nullptr, 0, &bufLen, GetCurrentProcess(), 0);
        if (bufLen > 0) {
            std::vector<uint8_t> buf(bufLen);
            if (fnGetCpuSets(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()),
                             bufLen, &bufLen, GetCurrentProcess(), 0)) {
                for (ULONG off = 0; off < bufLen;) {
                    auto* item = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data() + off);
                    if (item->Size == 0) break;
                    if (item->Type == CpuSetInformation &&
                        pCoreSet.count(std::make_pair(item->CpuSet.Group,
                                                      static_cast<DWORD>(item->CpuSet.LogicalProcessorIndex))) > 0) {
                        pCoreCpuSetIds.push_back(item->CpuSet.Id);
                    }
                    off += item->Size;
                }
            }
        }
    }
}

struct ProcessStateParams {
    DWORD priorityClass = NORMAL_PRIORITY_CLASS;
    BOOL disablePriorityBoost = FALSE;
    ULONG ioPriority = 2u;
    bool deepBoost = false;
    int coreMode = 0;
    DWORD_PTR explicitAffinityMask = 0;
};

void CapturePidOriginalState(DWORD pid,
                             pfnNtQueryInformationProcess fnNtQueryInfoProc,
                             DWORD& outPrio,
                             BOOL& outDisableBoost,
                             ULONG& outIoPrio,
                             DWORD_PTR& outAffinity,
                             bool& outHasAffinity) {
    outPrio = NORMAL_PRIORITY_CLASS;
    outDisableBoost = FALSE;
    outIoPrio = 2u;
    outAffinity = 0;
    outHasAffinity = false;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return;

    const DWORD prio = GetPriorityClass(hProc);
    if (prio != 0) outPrio = prio;

    BOOL disBoost = FALSE;
    if (GetProcessPriorityBoost(hProc, &disBoost)) {
        outDisableBoost = disBoost;
    }

    if (fnNtQueryInfoProc) {
        ULONG ioPrio = 2u;
        ULONG retLen = 0;
        if (fnNtQueryInfoProc(hProc, 33u, &ioPrio, sizeof(ioPrio), &retLen) == 0 && ioPrio <= 3u) {
            outIoPrio = ioPrio;
        }
    }

    DWORD_PTR procMask = 0;
    DWORD_PTR sysMask = 0;
    if (GetProcessAffinityMask(hProc, &procMask, &sysMask) && procMask != 0) {
        outAffinity = procMask;
        outHasAffinity = true;
    }

    CloseHandle(hProc);
}

bool ApplyProcessStateParams(DWORD pid,
                             const ProcessStateParams& params,
                             DWORD_PTR pCoreMask,
                             const std::vector<DWORD>& pCoreLogical,
                             const std::vector<ULONG>& pCoreCpuSetIds,
                             pfnNtSetInformationProcess fnNtSetInfoProc,
                             pfnSetProcessDefaultCpuSets fnSetCpuSets) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, pid);
    if (!hProc) return false;

    if (params.priorityClass == REALTIME_PRIORITY_CLASS || params.deepBoost) {
        EnablePrivilegeName(SE_INC_BASE_PRIORITY_NAME);
    }
    SetPriorityClass(hProc, params.priorityClass);
    SetProcessPriorityBoost(hProc, params.disablePriorityBoost);

    PROCESS_POWER_THROTTLING_STATE pts{};
    pts.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (params.deepBoost) {
        pts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                          PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        pts.StateMask = 0u;
    } else {
        pts.ControlMask = 0u;
        pts.StateMask = 0u;
    }
    SetProcessInformation(hProc, ProcessPowerThrottling, &pts, sizeof(pts));

    if (fnNtSetInfoProc) {
        ULONG ioPrio = params.ioPriority;
        fnNtSetInfoProc(hProc, 33u, &ioPrio, sizeof(ioPrio));
    }

    DWORD_PTR procMask = 0;
    DWORD_PTR sysMask = 0;
    if (GetProcessAffinityMask(hProc, &procMask, &sysMask)) {
        if (params.coreMode == 1 && pCoreMask != 0) {
            const DWORD_PTR target = pCoreMask & sysMask;
            if (target != 0 && procMask != target) {
                SetProcessAffinityMask(hProc, target);
            }
        } else if (params.explicitAffinityMask != 0 && sysMask != 0) {
            const DWORD_PTR target = params.explicitAffinityMask & sysMask;
            if (target != 0 && procMask != target) {
                SetProcessAffinityMask(hProc, target);
            }
        } else if (sysMask != 0 && procMask != sysMask) {
            SetProcessAffinityMask(hProc, sysMask);
        }
    }

    if (fnSetCpuSets) {
        if (params.coreMode == 2 && !pCoreCpuSetIds.empty()) {
            fnSetCpuSets(hProc, pCoreCpuSetIds.data(), static_cast<ULONG>(pCoreCpuSetIds.size()));
        } else {
            fnSetCpuSets(hProc, nullptr, 0);
        }
    }

    SweepThreadsForPid(pid, params.deepBoost, params.coreMode, pCoreLogical);

    CloseHandle(hProc);
    return true;
}

} // namespace

void CpuIntelEngine::ClearTargetExeRuleEffect(const std::wstring& exeName) {
    if (exeName.empty()) return;
    const std::wstring targetLower = LowerWide(exeName);

    if (IsElevated()) {
        SyncIfeoPerfOptions(exeName, false, 0, false);
        m_impl->boostedIfeoExes.erase(targetLower);
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID <= 4 || pe.th32ProcessID == GetCurrentProcessId()) continue;
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                ProcessStateParams restore{};
                auto itOrig = m_impl->boostedPidOrigStates.find(pe.th32ProcessID);
                if (itOrig != m_impl->boostedPidOrigStates.end()) {
                    restore.priorityClass = itOrig->second.priorityClass;
                    restore.disablePriorityBoost = itOrig->second.disablePriorityBoost;
                    restore.ioPriority = itOrig->second.ioPriority;
                    if (itOrig->second.hasAffinity) {
                        restore.explicitAffinityMask = itOrig->second.affinityMask;
                    }
                    m_impl->boostedPidOrigStates.erase(itOrig);
                }
                ApplyProcessStateParams(pe.th32ProcessID, restore, 0, {}, {},
                                        m_impl->fnNtSetInfoProc, m_impl->fnSetProcCpuSets);
                m_impl->boostedPidSignatures.erase(pe.th32ProcessID);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

void CpuIntelEngine::RestoreAllBoostedProcesses(const AppConfig& cfg) {
    for (const auto& exeName : m_impl->boostedIfeoExes) {
        if (IsElevated()) {
            SyncIfeoPerfOptions(exeName, false, 0, false);
        }
    }
    m_impl->boostedIfeoExes.clear();

    for (const auto& rule : cfg.targetExes) {
        if (!rule.exeName.empty()) {
            ClearTargetExeRuleEffect(rule.exeName);
        }
    }

    for (const auto& kv : m_impl->boostedPidSignatures) {
        ProcessStateParams restore{};
        auto itOrig = m_impl->boostedPidOrigStates.find(kv.first);
        if (itOrig != m_impl->boostedPidOrigStates.end()) {
            restore.priorityClass = itOrig->second.priorityClass;
            restore.disablePriorityBoost = itOrig->second.disablePriorityBoost;
            restore.ioPriority = itOrig->second.ioPriority;
            if (itOrig->second.hasAffinity) {
                restore.explicitAffinityMask = itOrig->second.affinityMask;
            }
        }
        ApplyProcessStateParams(kv.first, restore, 0, {}, {},
                                m_impl->fnNtSetInfoProc, m_impl->fnSetProcCpuSets);
    }
    m_impl->boostedPidSignatures.clear();
    m_impl->boostedPidOrigStates.clear();
    m_impl->lastFgBoostPid = 0;
    m_impl->lastBoostedName.clear();
}

std::wstring CpuIntelEngine::TickProcessBoosts(const AppConfig& cfg, uint32_t& outBoostedTargetCount, bool forceReapply) {
    outBoostedTargetCount = 0;
    if (!m_info.isIntel) {
        m_impl->lastBoostedName.clear();
        return {};
    }

    EnsurePCoreRoutingInitialized(
        m_info,
        m_impl->fnGetCpuSets,
        m_impl->routing.pCoreMask,
        m_impl->routing.pCoreLogical,
        m_impl->routing.pCoreCpuSetIds,
        m_impl->routing.initialized
    );

    if (forceReapply) {
        m_impl->boostedPidSignatures.clear();
    }

    std::set<std::wstring> enabledTargetsLower;
    std::set<std::wstring> disabledTargetsLower;
    for (const auto& rule : cfg.targetExes) {
        const std::wstring k = LowerWide(rule.exeName);
        if (k.empty()) continue;
        if (rule.enabled) enabledTargetsLower.insert(k);
        else disabledTargetsLower.insert(k);
    }

    DWORD fgPid = 0;
    if (cfg.boostForegroundPCore) {
        if (HWND fg = GetForegroundWindow()) {
            GetWindowThreadProcessId(fg, &fgPid);
        }
    }

    if (enabledTargetsLower.empty() && !cfg.boostForegroundPCore) {
        m_impl->lastFgBoostPid = 0;
        m_impl->lastBoostedName.clear();
        return {};
    }

    const int effCoreMode = m_info.isHybrid ? cfg.targetCoreMode : 0;
    const uint64_t sig = (static_cast<uint64_t>(cfg.targetPriorityMode & 0xF)) |
                         (static_cast<uint64_t>(effCoreMode & 0xF) << 4) |
                         (static_cast<uint64_t>(cfg.targetDeepBoost ? 1u : 0u) << 8) |
                         (static_cast<uint64_t>(0xA5A5u) << 16);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return m_impl->lastBoostedName;

    std::set<DWORD> alivePids;
    std::wstring activeDisplay;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            const DWORD pid = pe.th32ProcessID;
            if (pid <= 4 || pid == GetCurrentProcessId()) continue;
            alivePids.insert(pid);

            const std::wstring baseName = pe.szExeFile;
            const std::wstring lowerName = LowerWide(baseName);
            const bool isTarget = enabledTargetsLower.count(lowerName) > 0;
            const bool isExplicitlyDisabled = disabledTargetsLower.count(lowerName) > 0;
            const bool isFg = cfg.boostForegroundPCore && (pid == fgPid) && !isExplicitlyDisabled && !IsProtectedSystemProcessName(baseName);

            if (!isTarget && !isFg) continue;

            if (isTarget) {
                ++outBoostedTargetCount;
                activeDisplay = baseName;
            } else if (isFg && activeDisplay.empty()) {
                activeDisplay = baseName;
            }

            auto itSig = m_impl->boostedPidSignatures.find(pid);
            if (itSig == m_impl->boostedPidSignatures.end() || itSig->second != sig) {
                if (m_impl->boostedPidOrigStates.find(pid) == m_impl->boostedPidOrigStates.end()) {
                    Impl::OriginalPidState orig{};
                    CapturePidOriginalState(pid, m_impl->fnNtQueryInfoProc,
                                            orig.priorityClass, orig.disablePriorityBoost,
                                            orig.ioPriority, orig.affinityMask, orig.hasAffinity);
                    m_impl->boostedPidOrigStates.emplace(pid, orig);
                }

                ProcessStateParams boostParams{};
                boostParams.priorityClass = PriorityModeToWin32(cfg.targetPriorityMode);
                boostParams.disablePriorityBoost = FALSE;
                boostParams.ioPriority = cfg.targetDeepBoost ? 3u : 2u;
                boostParams.deepBoost = cfg.targetDeepBoost;
                boostParams.coreMode = effCoreMode;

                if (ApplyProcessStateParams(pid,
                                            boostParams,
                                            m_impl->routing.pCoreMask,
                                            m_impl->routing.pCoreLogical,
                                            m_impl->routing.pCoreCpuSetIds,
                                            m_impl->fnNtSetInfoProc,
                                            m_impl->fnSetProcCpuSets)) {
                    m_impl->boostedPidSignatures[pid] = sig;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    for (auto it = m_impl->boostedPidSignatures.begin(); it != m_impl->boostedPidSignatures.end();) {
        if (alivePids.count(it->first) == 0) it = m_impl->boostedPidSignatures.erase(it);
        else ++it;
    }
    for (auto it = m_impl->boostedPidOrigStates.begin(); it != m_impl->boostedPidOrigStates.end();) {
        if (alivePids.count(it->first) == 0) it = m_impl->boostedPidOrigStates.erase(it);
        else ++it;
    }

    m_impl->lastFgBoostPid = fgPid;
    m_impl->lastBoostedName = activeDisplay;
    return m_impl->lastBoostedName;
}

TuneApplyResult CpuIntelEngine::ApplyTargetExeRulesNow(const AppConfig& cfg) {
    TuneApplyResult res{};
    if (!m_info.isIntel) {
        res.blockedNonIntel = true;
        res.message = L"Blocked: Non-Intel / AMD CPU.";
        return res;
    }

    const bool elevated = IsElevated();
    uint32_t enabledRules = 0;
    for (const auto& rule : cfg.targetExes) {
        if (rule.exeName.empty()) continue;
        const std::wstring lower = LowerWide(rule.exeName);
        if (rule.enabled) {
            ++enabledRules;
            if (elevated) {
                SyncIfeoPerfOptions(rule.exeName, cfg.targetIfeoPersist, cfg.targetPriorityMode, cfg.targetDeepBoost);
                if (cfg.targetIfeoPersist) m_impl->boostedIfeoExes.insert(lower);
                else m_impl->boostedIfeoExes.erase(lower);
            }
        } else {
            ClearTargetExeRuleEffect(rule.exeName);
        }
    }

    uint32_t runningBoosted = 0;
    TickProcessBoosts(cfg, runningBoosted, true);

    wchar_t msg[128]{};
    if (runningBoosted > 0) {
        std::swprintf(msg, 128, L"Boosted %u running target PID(s) · %u rule(s) active", runningBoosted, enabledRules);
    } else if (cfg.boostForegroundPCore) {
        std::swprintf(msg, 128, L"%u target rule(s) armed · Auto-boost active window ON", enabledRules);
    } else {
        std::swprintf(msg, 128, L"%u target rule(s) armed", enabledRules);
    }
    res.success = true;
    res.message = msg;
    return res;
}

}
