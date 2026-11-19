# VOverclock Intel Beta Phase 2 — Windows Native Intel CPU Monitor & Tuner

**VOverclock Intel Beta Phase 2** is an open-source, lightweight Intel CPU monitoring, Windows power-policy tuning, hybrid P/E-core scheduling, and per-application CPU priority optimizer for **Windows 10** and **Windows 11**, written in native **C++20**, **Win32**, **Direct2D**, and **DirectWrite** with a statically linked C/C++ runtime (`/MT`). It compiles to a standalone, single-file native Win32 x64 executable with **zero XAML Islands / WinRT runtime dependencies**, zero VC++ Redistributable requirements, zero Electron/WebView2 bloat, zero kernel-mode drivers, and **zero interference with RAM/memory allocation**.

> [!NOTE]
> **Project Identity**
> - **Product Name:** `VOverclock Intel Beta Phase 2`
> - **File Description:** `VOverclock Intel Beta Phase 2 - CPU Tuner & Core Monitor`
> - **Copyright:** `Vortex Open Source Project`
> - **Created by:** `@vinnotdkk`
> - **Repository Architecture Style:** Vortex Native C++20 / Direct2D + DirectWrite (`Win32` Double-Buffered DIB Host, `/MT` Static CRT)

---

## Table of Contents

1. [Honest Scope: What This Tool Does (and Does NOT Do)](#honest-scope-what-this-tool-does-and-does-not-do)
2. [Risks, Trade-Offs & Limitations (Read Before Using)](#risks-trade-offs--limitations-read-before-using)
3. [Architecture Overview](#architecture-overview)
4. [Technical Stack & Linked Libraries](#technical-stack--linked-libraries)
5. [Intel Hardware Compatibility Matrix](#intel-hardware-compatibility-matrix)
6. [Presets & `VPower Intel Beta` Power Plan](#presets--vpower-intel-beta-power-plan)
7. [Power Set, Kernel Tweaks & Apps Tweak Engine](#power-set-kernel-tweaks--apps-tweak-engine)
8. [Full State Restoration Guarantee (`Restore`)](#full-state-restoration-guarantee-restore)
9. [Privilege Model (Standard User vs. Administrator)](#privilege-model-standard-user-vs-administrator)
10. [Configuration & Persistence (`config.xml`)](#configuration--persistence-configxml)
11. [Command-Line Arguments](#command-line-arguments)
12. [Building from Source](#building-from-source)
13. [Repository Structure](#repository-structure)
14. [License & Support](#license--support)

---

## Honest Scope: What This Tool Does (and Does NOT Do)

**What VOverclock Intel Beta Phase 2 actually does:**
- Reads hardware capabilities directly via `__cpuid` / `__cpuidex`, `GetLogicalProcessorInformationEx`, and `CallNtPowerInformation(ProcessorInformation)` to map P-Cores, E-Cores, standard cores, SMT/Hyper-Threading pairs, and >64-thread multi-group topologies.
- Creates and manages a custom Windows power plan named **`VPower Intel Beta`** duplicated from Windows **Balanced** (tagged `[Balanced-HWP-V2]`), configuring official Windows Processor Power Management (`SUB_PROCESSOR`) settings: Intel Speed Shift (HWP EPP), Turbo Boost Policy, Autonomous Window, Core Parking (`CPMINCORES` / `CPMAXCORES`), Heterogeneous Scheduling Policy (`SCHEDPOLICY` / `SHORTVSCHEDPOL` / `HETEROFLOOR`), Frequency Cap Override (`PROCFREQMAX`), Ramp/Hold thresholds, and Idle C-States.
- Applies reversible OS scheduling and timer tweaks (`PowerSetActiveOverlayScheme`, `NtSetTimerResolution`, `Win32PrioritySeparation`, `SystemResponsiveness`, `NetworkThrottlingIndex`, and MMCSS `Tasks\Games`).
- Optimizes targeted `.exe` processes and the active foreground window using CPU priority classes, `SetProcessDefaultCpuSets` / `SetProcessAffinityMask`, `ProcessPowerThrottling` / `ThreadPowerThrottling` (EcoQoS bypass), High I/O priority (`ProcessIoPriority`), and optional HKLM Image File Execution Options (`PerfOptions`).
- Monitors real-time per-core load (`NtQuerySystemInformation`), nominal/effective core clocks (`PDH` `% Processor Performance` + `Processor Frequency`), ACPI thermal zones (`MSAcpi_ThermalZoneTemperature` via WMI), and active EPP state every **500 ms**.

**What VOverclock Intel Beta Phase 2 explicitly does NOT do:**
- **NOT hardware BIOS/Ring-0 overclocking:** Does **not** ship any kernel driver (`.sys`), does **not** write MSRs, does **not** unlock locked multipliers, does **not** raise BCLK, does **not** increase core voltage (`VCore`), and does **not** override motherboard/EC firmware power limits (`PL1`, `PL2`, `PL4`, `IccMax`). On locked desktop/mobile SKUs (`Ratio / BCLK OC -> CPU LOCKED`), peak frequency remains bounded by Intel factory fused turbo bins, OEM TDP limits, and thermal headroom.
- **Intel (`GenuineIntel`) only:** Non-Intel CPUs (such as `AuthenticAMD` or `HygonGenuine`) are detected via CPUID leaf `0` and intentionally blocked with a notice overlay.
- **Zero RAM/memory modification:** Never trims working sets, flushes standby memory lists, or modifies pagefile/RAM behavior.

---

## Risks, Trade-Offs & Limitations (Read Before Using)

Using aggressive OS power, timer, scheduler, and priority settings carries real engineering trade-offs. Read these honestly before enabling `Max OC`, `Power Set` toggles, or `Apps Tweak`:

1. **Thermal Load, Fan Noise & Unmonitored Desktop Temperatures**
   - `Performance` and `Max OC` keep Turbo Boost, EPP `0%`, core unparking, and aggressive ramp thresholds active under load. On laptops or thin desktops with marginal cooling, CPU temperatures can quickly hit **>= 85°C** (highlighted in red) or firmware **PROCHOT thermal throttling**.
   - **ACPI Thermal Limitation:** Many desktop motherboards do **not** expose `MSAcpi_ThermalZoneTemperature` through Windows WMI. In those cases, VOverclock Intel stops WMI thermal polling after 3 failed attempts and displays **`N/A`** rather than freezing or inventing numbers — meaning **the app cannot monitor or protect against overheating on boards without ACPI thermal zones**. Use a hardware EC/MSR sensor utility alongside stress tests if `N/A` is shown.

2. **Laptop Battery Drain & Heat When Unplugged**
   - Keeping **`High-resolution 0.5ms kernel timer`** (`NtSetTimerResolution`), `GlobalTimerResolutionRequests = 1`, `unparkCoresPercent = 100%`, and `eppPercent = 0%` active dramatically prevents deep package C-states and drains laptop batteries much faster.
   - Disabling idle C-States (`Disable Idle C-States`) forces cores to stay out of sleep states (`IDLEDISABLE = 1`), generating continuous idle heat and reducing single-thread peak turbo headroom on power-limited Intel CPUs. All built-in presets keep `disableIdleStates = false` by default for this reason.

3. **Why "Max Clock Everything" Can Actually Hurt 1T Boost**
   - Forcing `Minimum CPU State = 100%` or `CPMINCORES1 = 100%` (forcing idle E-Cores unparked under light load) or using `High Performance` as a base plan (which alters hidden PCIe ASPM & power policies) can split package power (`PL1`/`PL2`) and heat across idle cores, **lowering** peak P-Core single-thread boost speeds. That is why **`VPower Intel Beta`** clones **Balanced**, keeps `Min State = 5%` with `HWP Autonomous = ON`, and lets idle E-Cores park under `Automatic` / `Prefer P-Core` / `P-Core Only`.

4. **`Realtime` Priority Lockup Risk (`Apps Tweak`)**
   - Setting **`Realtime`** priority (`REALTIME_PRIORITY_CLASS`) puts a target application's threads above Windows input (`csrss`), audio, storage, network, and DWM threads. If the target `.exe` saturates all logical cores (e.g., shaders compiling, heavy emulators, video rendering, or CPU stress benchmarks), **Windows UI, mouse, and audio can stutter severely or become unresponsive** until the workload finishes. Prefer **`High`** or **`Above Normal`** for everyday gaming and emulation.

5. **Kernel Anti-Cheat Incompatibility (`IFEO` & `OpenProcess`)**
   - Competitive games using kernel anti-cheat drivers (e.g., Vanguard, Easy Anti-Cheat, BattlEye, FACEIT) actively strip external `OpenProcess(..., PROCESS_SET_INFORMATION)` handles and/or flag **`Image File Execution Options\<game>.exe`** registry overrides. **Do NOT enable `Apps Tweak` or `Persistent IFEO` for kernel anti-cheat games.**

6. **PDH Counter Dependency on Stripped Windows Builds**
   - Per-core frequency (`\Processor Information(g,l)\Processor Frequency` & `% Processor Performance`) relies on Windows Performance Data Helper (`PDH`) counters. If performance counters were disabled (`Disable Performance Counters` in stripped custom OS builds or corrupted registry `PerfOS`), telemetry falls back cleanly to `CallNtPowerInformation(ProcessorInformation)` base/current estimates and `NtQuerySystemInformation(8)` CPU usage.

7. **>64-Thread Multi-Group Limits in `Apps Tweak` Hard Affinity**
   - Core enumeration, per-core telemetry, and **`Prefer P-Core` (`SetProcessDefaultCpuSets`)** work across all Windows processor groups (>64 logical threads). However, Win32's **`P-Core Only` Hard Affinity** (`SetProcessAffinityMask`) and `NtQuerySystemInformation(8)` OS interfaces remain bounded to Processor Group 0 (up to 64 logical processors) by Windows API design.

8. **Persistent HKLM Registry Changes If Uninstalled Without `Restore`**
   - Clicking **Apply Presets Settings** or **Apply Targets** with **OS & Scheduler Tuning** or **Persistent IFEO** enabled writes to `HKLM` and creates the `VPower Intel Beta` Windows scheme. Always click **`Restore`** (or select **`Restore Stock Power Plan`** from the tray icon) before deleting or uninstalling VOverclock Intel so all registry keys, overlay settings, IFEO entries, and power plans are reverted cleanly.

9. **OS Portability & Compatibility Mode**
   - Uses pure **Direct2D (`ID2D1DCRenderTarget`) + DirectWrite** over a double-buffered Win32 32-bit DIB surface with **static CRT (`/MT`)** and **runtime dynamic DPI lookup (`Win32Compat.hpp`)** — completely free of XAML Islands (`WindowsXamlManager` / `DesktopWindowXamlSource`), WinRT, or VC++ Redistributable DLLs. Runs reliably on Windows 10 (including older/LTSC/stripped gaming ISOs), Windows 11, and Windows Compatibility Modes without `0x8000FFFF` / `MaxTestedVersion` startup failures.

---

## Architecture Overview

VOverclock Intel cleanly separates pure Win32/Direct2D presentation (`AppWindow` + `VectorIcons`) from hardware/kernel power operations (`CpuIntelEngine`):

```text
+-----------------------------------------------------------------------------+
|                           wWinMain (src/main.cpp)                           |
|  - Enforces single instance via Local\VOverclockIntel_V1_SingleInstance     |
|  - Parses --minimized / --elevated-restart startup flags                    |
|  - Initializes STA COM (WIC/WMI) & top-level exception error guard          |
+--------------------------------------+--------------------------------------+
                                       |
           +---------------------------+---------------------------+
           v                                                       v
+-----------------------------------+     +-----------------------------------+
|  AppWindow (Win32 + D2D + DWrite) |     |       CpuIntelEngine (Engine)     |
| - Frameless 1120x630 DIP light UI |     | - CPUID topology + CallNtPowerInfo|
| - PerMonitorV2 DPI + Layered Fade | --> | - PDH + NtQuerySystemInfo + WMI   |
| - 500ms live core sparklines      | <-- | - PowerWrite* (`VPower Intel Beta`|
| - 180ms debounced Live slider tick|     | - HKCU backup + HKLM MMCSS/Kernel |
| - Non-Intel guard & feature locks |     | - Process Priority/Affinity/CpuSet|
+-----------------+-----------------+     +-----------------------------------+
                  |
       +----------+----------+
       v                     v
+--------------------+  +----------------------------------+
|  TrayIconRenderer  |  |  ConfigManager                   |
| - Direct2D/DWrite  |  | - Native xmllite.dll I/O         |
| - Live CPU % badge |  | - %APPDATA%\VOverclockIntel\     |
| - High-DPI WIC/ICO |  |   config.xml + HKCU Run startup  |
+--------------------+  +----------------------------------+
```

---

## Technical Stack & Linked Libraries

| Component | Implementation | Purpose |
| :--- | :--- | :--- |
| **Language & Standard** | **C++20** (`/W4 /EHsc /permissive- /utf-8 /Zc:__cplusplus /MT`) | Native Win32 / Direct2D static-CRT build ([CMakeLists.txt](file:///C:/Users/Administrator/Documents/VOverclock%20Intel/CMakeLists.txt)) |
| **Window Host & DPI** | **Win32 (`HWND`)** + `Win32Compat.hpp` dynamic DPI + `PerMonitorV2` ([app.manifest](file:///C:/Users/Administrator/Documents/VOverclock%20Intel/resources/app.manifest)) | Frameless `1120x630` DIP host window, layered fades, universal Windows compat |
| **UI Rendering** | **Direct2D (`ID2D1DCRenderTarget`)** + **DirectWrite** + 32bpp DIB double-buffer | Crisp hardware/software 2D UI, draggable sliders, popups & core sparklines |
| **2D / Text Graphics** | **Direct2D (`d2d1`)**, **DirectWrite (`dwrite`)**, **WIC (`windowscodecs`)** | Custom UI controls, vector icons ([VectorIcons.cpp](file:///C:/Users/Administrator/Documents/VOverclock%20Intel/src/VectorIcons.cpp)), tray CPU% badge & PNG logo |
| **CPU Telemetry** | `__cpuid` / `__cpuidex`, `pdh`, `ntdll` (`NtQuerySystemInformation`), `powrprof` (`CallNtPowerInformation`), `wbemuuid` | Hybrid P/E/uniform topology detection, per-core MHz, usage %, WMI thermal |
| **Power & OS Policy** | `powrprof` (`PowerDuplicateScheme`, `PowerWriteAC/DCValueIndex`, `PowerSetActiveScheme`, `PowerSetActiveOverlayScheme`) | Non-destructive custom scheme tuning, HWP EPP, OS scheduler & overlay control |
| **Process Control** | `advapi32`, `ntdll` (`NtSetInformationProcess` / `NtSetTimerResolution`), `kernel32` (`SetProcessDefaultCpuSets`) | Priority, I/O priority, EcoQoS bypass, P-Core routing, HKLM IFEO persistence |
| **XML Persistence** | **Windows XML Lite (`xmllite.dll`)** + `shlwapi` | Dependency-free XML serialization (`config.xml`) |

---

## Intel Hardware Compatibility Matrix

VOverclock Intel adapts its feature compatibility matrix (`Support` tab) and UI controls dynamically based on real CPUID & Windows detections:

| CPU Generation / Topology | Telemetry & Unpark | HWP EPP (`0-100%`) & Direct HWP | Hybrid P/E Scheduler & Core Filter | `Frequency Limit` Slider |
| :--- | :---: | :---: | :---: | :---: |
| **Intel Gen 2–5 (Pre-HWP Legacy)** | **Supported** | Disabled (Legacy ACPI P-states only) | Collapsed (Uniform Cores `C0..Cn`) | **Supported** (`0` Uncapped / `1000..6600 MHz`) |
| **Intel Gen 6–11 (Non-Hybrid HWP)** | **Supported** | **Supported** (CPUID `0x06` HWP + EPP) | Disabled / Uniform Cores `C0..Cn` | **Supported** |
| **Intel Gen 12+ / Core Ultra (Hybrid P+E)** | **Supported** | **Supported** | **Supported** (`P0..Pn`, `E0..En`, CpuSets & Affinity) | **Supported** |
| **Intel `-K` / `-KS` / `-KF` / `-HX` / `-XE` Unlocked** | **Supported** | **Supported** | Per SKU generation | **Supported** (up to **6600 MHz** cap slider) |
| **Intel Xeon / HEDT (>64 Threads Multi-Group)** | **Supported** (`QueryAllProcessorPowerInfo`) | **Supported** | **Prefer P-Core CpuSets** multi-group supported | **Supported** |
| **AMD / Non-Intel CPUs** | **Blocked** | **Blocked** | **Blocked** | **Blocked** |

---

## Presets & `VPower Intel Beta` Power Plan

Selecting a preset updates the UI controls immediately **without applying changes to Windows** until you click **`Apply Presets Settings`** (or use **Live** mode).

| Setting | `Eco` | `Balanced` | `Performance` | `Max OC` (Default) |
| :--- | :---: | :---: | :---: | :---: |
| **Core Unpark (`CPMINCORES`)** | `50%` | `100%` | `100%` | `100%` |
| **Min / Max CPU State** | `5% / 75%` | `5% / 100%` | `5% / 100%` | `5% / 100%` |
| **Turbo Boost Policy** | `30%` | `80%` | `100%` | `100%` |
| **Boost Mode (`PERFBOOSTMODE`)** | `Disabled` (`0`) | `Eff. Aggr` (`4`) | `Eff. Aggr` (`4`) | `Eff. Aggr` (`4`) |
| **Speed Shift EPP (`PERFEPP`)** | `85%` | `45%` | `10%` | `0%` (Max Perf) |
| **Hybrid Scheduler Policy** | `Automatic` (`5`) | `Automatic` (`5`) | `Automatic` (`5`) | `Automatic` (`5`) |
| **Ramp / Hold Threshold (`INCR`/`DECR`)** | `60% / 35%` | `30% / 15%` | `10% / 5%` | `5% / 2%` |
| **P-Core Hetero Floor (`HETEROFLOOR`)** | `20%` | `50%` | `90%` | `100%` |
| **Parking Concurrency (`CPCONCURRENCY`)** | `80%` | `50%` | `25%` | `15%` |
| **Active Cooling** | `false` | `true` | `true` | `true` |
| **OS & Scheduler Tuning + FG Boost + 0.5ms Timer** | `false` | `false` | `true` | `true` |
| **Disable Idle C-States** | `false` | `false` | `false` | `false` |

> [!TIP]
> **Why `VPower Intel Beta` uses Balanced Base (`[Balanced-HWP-V2]`)**
> Any previous `VPower Intel Beta` scheme without `[Balanced-HWP-V2]` is automatically replaced cleanly from Windows **Balanced**, preserving safe default PCIe ASPM behavior and keeping E-Core idle parking (`CPMINCORES1 = 0`) enabled on `Automatic` / `Prefer P-Core` / `P-Core Only` so P-Core bursts keep full thermal and package power headroom.

---

## Power Set, Kernel Tweaks & Apps Tweak Engine

### 1. OS & Scheduler Registry Tweaks (`ApplyOsBrutalTweaks`, Elevated Required)
When enabled, VOverclock Intel captures pre-existing Windows values in `HKCU\Software\VOverclockIntel\KernelRegBackup` before writing:

| Target Key / API | Value / API Call | Applied State | Notes |
| :--- | :--- | :--- | :--- |
| **Power Overlay Scheme** | `PowerSetActiveOverlayScheme` | `ded574b5-...` (Max Perf) | Original overlay captured via `PowerGetActualOverlayScheme` |
| `HKLM\...\Session Manager\kernel` | `GlobalTimerResolutionRequests` | `1` (DWORD) | Propagates global timer resolution across processes on Win11 |
| `HKLM\...\Session Manager\kernel` | `SerializeTimerExpiration` | `0` (DWORD) | Disables coalesced timer expiration serialization |
| `HKLM\...\PriorityControl` | `Win32PrioritySeparation` | `0x26` (DWORD) | Short, variable quantum with 3x foreground boost |
| `HKLM\...\Multimedia\SystemProfile` | `SystemResponsiveness` | `0` (DWORD) | Minimizes reserved background multimedia percentage |
| `HKLM\...\Multimedia\SystemProfile` | `NetworkThrottlingIndex` | `0xFFFFFFFF` (DWORD) | Disables MMCSS network packet throttling |
| `HKLM\...\SystemProfile\Tasks\Games` | `Affinity` / `Clock Rate` / `GPU Priority` / `Priority` / `Background Only` / `Scheduling Category` / `SFIO Priority` | `0` / `10000` / `8` / `6` / `"False"` / `"High"` / `"High"` | All 7 values individually backed up (DWORD & `REG_SZ`) prior to edit |
| `HKLM\...\Power\PowerThrottling` | `PowerThrottlingOff` | **Always Deleted (Absent)** | Never disables EcoQoS globally; uses per-target API instead |

### 2. Apps Tweak (Per-EXE CPU Optimizer)
- **Target Picker:** Browse any `.exe` (`GetOpenFileNameW`, 1024-char buffer) or pick directly from running user processes (`ListRunningProcesses`), plus optional **`Auto-Boost Active Window`** (`boostForegroundPCore`, automatically excluding critical Windows processes like `csrss.exe`, `dwm.exe`, `explorer.exe`, `MsMpEng.exe`, etc.).
- **Priority Modes:** `Normal` (`0`), `Above Normal` (`1`), `High` (`2`), `Realtime` (`3`).
- **Core Routing (Hybrid CPUs):** `All Cores` (`0`), `P-Cores Only` (`1`, `SetProcessAffinityMask`), or `Prefer P-Core` (`2`, `SetProcessDefaultCpuSets` across processor groups + `SetThreadIdealProcessor`).
- **Deep Boost:** Disables `ProcessPowerThrottling` & `ThreadPowerThrottling` (`EXECUTION_SPEED` & `IGNORE_TIMER_RESOLUTION`), ensures dynamic priority boosting stays active (`bDisablePriorityBoost = FALSE`), and elevates `ProcessIoPriority` (`3` = High).
- **Persistent IFEO (`PerfOptions`):** Optionally writes `CpuPriorityClass` and `IoPriority` under `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\<exe>\PerfOptions`.

---

## Full State Restoration Guarantee (`Restore`)

Clicking **`Restore`** (bottom of main UI) or **`Restore Stock Power Plan`** (tray menu) runs `CpuIntelEngine::RestoreStockPlan` and reverts **every change** made by VOverclock Intel:

1. **Switches Active Power Plan Back:** Restores the exact non-VPower Windows Power Plan captured right before `VPower Intel Beta` was activated (`cfg.originalSchemeGuid`, backed up in both `config.xml` and `KernelRegBackup\OriginalSchemeGuid`; falls back to `Balanced` if the original plan was externally deleted).
2. **Deletes `VPower Intel Beta` Completely:** Deletes all `VPower Intel Beta` power scheme instances from Windows (`PowerDeleteScheme`) and clears `vpowerGuid`.
3. **Restores All HKLM Keys & Power Overlay Exact 1:1:** Restores the exact pre-VOverclock Windows Power Overlay, all 5 kernel/MMCSS DWORDs, and all 7 `MMCSS\Tasks\Games` keys (deleting any key that did not exist before VOverclock Intel ran), then deletes `HKCU\Software\VOverclockIntel\KernelRegBackup` once empty.
4. **Restores Running & Target Processes + Cleans All IFEO Registry Entries:** Iterates both tracked runtime PIDs (`OriginalPidState`: `PriorityClass`, `bDisablePriorityBoost`, `IoPriority`, `AffinityMask`) and **every entry** in `cfg.targetExes` (even across prior app restarts), clears `CpuSets` / `PowerThrottling` back to OS defaults, deletes `PerfOptions`, and removes empty `IFEO\<exe>` parent keys.
5. **Stops Timer & Prevents Re-Apply on Reboot:** Calls `NtSetTimerResolution(..., FALSE, ...)`, sets `applyOnStartup = false`, `highResTimer = false`, `boostForegroundPCore = false`, `disableOsThrottling = false`, `targetIfeoPersist = false`, disables active rules in `cfg.targetExes` (keeping your saved `.exe` names for convenience), and syncs UI sliders from the restored stock Windows scheme.

---

## Privilege Model (Standard User vs. Administrator)

The manifest ([app.manifest](file:///C:/Users/Administrator/Documents/VOverclock%20Intel/resources/app.manifest)) uses **`requestedExecutionLevel level="asInvoker"`** so standard users can launch and monitor CPUs without automatic UAC prompts.

| Capability | Standard User | Administrator (Elevated) |
| :--- | :---: | :---: |
| **Live Per-Core Clocks, Usage %, Temp & Tray Badge** | **Full Support** | **Full Support** |
| **Hardware Support Matrix (`Support` Tab)** | **Full Support** | **Full Support** |
| **Create / Edit / Activate `VPower Intel Beta`** | Limited (OS typically denies `PowerWrite*`) | **Full Support** |
| **HKLM Kernel, MMCSS `Games` & IFEO Registry Tweaks** | Skipped (`IsElevated() == false`) | **Full Support** |
| **User-Owned Process Priority / CpuSets Boost** | **Supported** (`Normal` / `Above` / `High`) | **Full Support** (`+ Realtime`, `+ High I/O`, cross-process) |

Open **Settings -> Administrator Privileges** and click **Elevate** to relaunch elevated via `ShellExecuteExW(..., L"runas", L"--elevated-restart", ...)`.

---

## Configuration & Persistence (`config.xml`)

Preferences are stored natively via **`xmllite.dll`**:

```text
%APPDATA%\VOverclockIntel\config.xml
```
*(Falls back to `<exe_dir>\VOverclockIntel\config.xml` if Roaming AppData cannot be resolved.)*

- **`<Startup>`:** `autoStart`, `startMinimized`, `applyOnStartup`, `highResTimer`, `liveApply`
- **`<Window>`:** `minimizeToTray`, `closeToTray`, `alwaysOnTop`, `trayStyle` (`"logo"` / `"percent"`), `showNotifications`
- **`<Tuning>`:** `boostMode`, `turboBoostPolicy`, `eppPercent`, `minStatePercent`, `maxStatePercent`, `freqCapMhz`, `unparkCoresPercent`, `schedPolicy`, `incThreshold`, `decThreshold`, `heteroFloorPerf`, `concurrencyThreshold`, `activeCooling`, `hwpAutonomous`, `disableIdleStates`, `disableOsThrottling`, `boostForegroundPCore`
- **`<TargetProfile>` & `<Targets>`:** `priorityMode`, `coreMode`, `deepBoost`, `ifeoPersist`, and per-app `<TargetExe name="..." enabled="..." />` rules
- **`<PowerSchemes>`:** `originalGuid`, `vpowerGuid`

---

## Command-Line Arguments

| Argument | Behavior |
| :--- | :--- |
| **`--minimized`** | Starts directly in the system tray (automatically passed when `autoStart` + `startMinimized` are enabled). |
| **`--elevated-restart`** | Internal flag used when relaunching via the UAC **Elevate** action in Settings to hand off the single-instance mutex cleanly. |

---

## Building from Source

### Prerequisites

| Requirement | Component / Version |
| :--- | :--- |
| **Operating System** | Windows 10 (all builds) or Windows 11 (x64) |
| **CMake** | `3.20` or newer (`Kitware.CMake`) |
| **Compiler & SDK** | Visual Studio 2019 / 2022 / 2026 or **Build Tools** with **Desktop development with C++** (`Microsoft.VisualStudio.Component.VC.Tools.x86.x64`) & Windows 10/11 SDK |

### Option 1: Automated Build Script (`build.bat`)

Run [`build.bat`](file:///C:/Users/Administrator/Documents/VOverclock%20Intel/build.bat) from the project root directory. It checks for CMake and MSVC via `vswhere.exe` (offering `winget` installation if missing), detects running `VOverclockIntel.exe` locks, configures CMake for x64, and builds Release:

```bat
build.bat
```

### Option 2: Manual CMake Commands

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**Compiled Output:**
```text
build\Release\VOverclockIntel.exe
```

---

## Repository Structure

```text
VOverclock Intel/
├── assets/
│   ├── logo_ui.png                  # Tight-cropped header logo embedded as IDR_PNG_LOGO
│   ├── vointel.ico                  # Multi-resolution application/tray icon (IDI_VO_APP)
│   └── vointel.png                  # High-resolution brand logo master source
├── resources/
│   ├── ui/
│   │   └── AppLayout.xaml           # UI layout design specification (1120x630 light theme)
│   ├── app.manifest                 # PerMonitorV2 DPI & asInvoker UAC manifest
│   ├── resource.h                   # Win32 resource IDs
│   └── VOverclock.rc                # Embedded icon, PNG logo & PE VersionInfo metadata
├── src/
│   ├── main.cpp                     # wWinMain, single-instance mutex, COM init & error guard
│   ├── AppWindow.hpp / .cpp         # Pure Direct2D/DirectWrite host window, UI controls & tray
│   ├── CpuIntelEngine.hpp / .cpp    # Intel topology, PDH/WMI, VPower scheme, Restore & Apps Tweak
│   ├── ConfigManager.hpp / .cpp     # xmllite XML persistence & HKCU Run startup entry
│   ├── TrayIconRenderer.hpp / .cpp  # Direct2D/DirectWrite live CPU % tray icon & PNG loader
│   ├── VectorIcons.hpp / .cpp       # Normalized Direct2D vector geometry icons
│   └── Win32Compat.hpp              # Dynamic DPI resolvers (GetProcAddress) & WIC PBGRA decoder
├── .gitignore                       # Ignores build/, out/, .vs/, *.obj, *.exe, *.pdb, etc.
├── build.bat                        # Automated CMake + MSVC dependency check & Release x64 build
├── CMakeLists.txt                   # C++20 CMake build configuration
└── README.md
```

---

## License & Support

- **Project:** **Vortex Open Source Project** — [`github.com/vortexprojectsource`](https://github.com/vortexprojectsource)
- **Author:** **`@vinnotdkk`**
- **Support Development:** https://sociabuzz.com/idnvinnmalay
