# T01 — Solution and Build Scaffolding

## Goal

Restructure the freshly generated Visual Studio solution into the target architecture and lock in
the build conventions: a `Pacecar.Core` static library, a thin `Pacecar.Overlay` executable, a
`Pacecar.App.Tests` Google Test project, shared MSBuild properties, vcpkg manifest mode, static CRT,
and an embedded application manifest (`asInvoker`, Per-Monitor V2). The current `pacecar/pacecar.cpp`
Win32 template becomes the starting point for `Pacecar.Overlay`.

The optional elevated `Pacecar.Sensors` helper is intentionally **not** scaffolded here: per
[01-architecture.md](../design/01-architecture.md) it is a separate, self-contained .NET/
LibreHardwareMonitor process (or the documented native PawnIO CPU-temp-only fallback), introduced in
task 16 with its own build conventions. Reserve a `helper/` folder now so the layout is stable.

## Context (read first)

- **Priority:** P0 (blocking)
- **Depends on:** none
- **Blocks:** all other tasks
- **Design refs:** [08-project-layout-and-testing.md](../design/08-project-layout-and-testing.md), [05-performance.md](../design/05-performance.md)
- The linked design docs under `docs/design/` are the source of truth for this task.

## Scope

- Solution `pacecar.slnx` updated to reference the new projects.
- New folder layout under the repo root:
  ```
  src/
    Pacecar.Core/        # static lib (Application), empty but linkable
    Pacecar.Overlay/     # Win32 EXE, refs Core (moved from pacecar/)
    Pacecar.App.Tests/   # Google Test EXE
  helper/                # placeholder; .NET Pacecar.Sensors added in task 16
  build/
    Directory.Build.props
    Directory.Build.targets
  vcpkg.json
  .clang-format
  .editorconfig
  ```
- `vcpkg.json` (manifest mode) declaring `wil`, `nlohmann-json`, `gtest`, using the
  `x64-windows-static` triplet.
- Application manifest embedded in `Pacecar.Overlay` (later also the helper if it is native):
  `requestedExecutionLevel level="asInvoker"`, Per-Monitor V2 DPI awareness, UTF-8 active code page,
  `longPathAware`.
- Shared resources: multi-resolution icon, version info, string table (MUI-ready).

## Out of scope

- The elevated `Pacecar.Sensors` helper - its own process and build conventions are owned by T16.
- Real metric providers, widgets, and overlay rendering - owned by later tasks (T04-T12).

## Design notes

- Do not pull in MFC, WinUI 3, or the Windows App SDK; the core stack is plain Win32 + D2D + DComp
  + DXGI + DirectWrite, all in-box.
- Keep `vcpkg.json` minimal. Add dependencies only when a task needs them.
- Since the CRT must match across modules, never mix `/MT` and `/MD`; vcpkg's static triplet keeps
  dependencies aligned.
- `Pacecar.Sensors` (the helper) is not part of this scaffold; it is a separate .NET/LHM process
  introduced in task 16 under `helper/` with its own build conventions. A no-op native
  `SensorHelperClient` stub lives in Core and is wired in task 13.

## Done when

- [x] `Pacecar.Core` is a static library; `Pacecar.Overlay` is a Windows EXE; `Pacecar.App.Tests`
      is a console test EXE. Both EXEs/test project reference `Pacecar.Core`. (The .NET
      `Pacecar.Sensors` helper is out of scope here and arrives in task 16.)
- [x] `Directory.Build.props` sets: `stdcpp20`, `/permissive-`, `/W4`, `MultiProcessorCompilation`,
      `RuntimeLibrary=MultiThreaded` (`/MT`) for Release and `MultiThreadedDebug` for Debug,
      common include dirs, and the vcpkg import. `Directory.Build.targets` holds override/target
      logic only.
- [x] Warnings-as-errors enabled in CI/release configuration; code analysis enabled.
- [x] The existing `pacecar/` skeleton is moved to `src/Pacecar.Overlay/` and still compiles.
- [x] `.clang-format` (Microsoft-based) and `.editorconfig` committed.
- [x] `Pacecar.App.Tests` contains one passing smoke test and is runnable from Test Explorer and
      the command line.
- [x] `Debug|x64` and `Release|x64` both build with zero warnings.
- [x] The application manifest is embedded and verifiable (e.g. `mt.exe -inputresource:` inspection
      or `sigcheck` output shows `asInvoker` + Per-Monitor V2).
- [x] A short `docs/BUILDING.md` documents prerequisites (VS with C++ workload, vcpkg), configure
      command, and build/test commands.

### Tests

- [x] `Pacecar.App.Tests` runs green via `vstest`/CTest-equivalent on a clean checkout.
- [x] The harness's independent verify command passes: `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/verify.ps1`
      builds the solution (`Release|x64`, using the vcpkg manifest) and runs `Pacecar.App.Tests`
      green. Update the script if the final solution/test paths differ; it must exit non-zero on any
      build or test failure.
- [x] Add a CI-less local build script or document the exact `msbuild`/`vcpkg` invocation in
      `docs/BUILDING.md` and verify it from a clean `out/` directory.
- [x] Confirm no VC++ runtime DLL is required to launch `Pacecar.Overlay.exe` (static CRT).

## Hand-off

Landed the full scaffold described in Scope.

- Projects: `Pacecar.Core` (StaticLibrary, minimal `pacecar::CoreVersion()` symbol in
  `include/pacecar/core.h` + `src/core.cpp`), `Pacecar.Overlay` (Application, moved from `pacecar/`,
  references Core, embeds `app.manifest`), `Pacecar.App.Tests` (console Google Test EXE, references
  Core, one smoke test). `pacecar.slnx` lists all three (Debug/Release, x64). `helper/` reserved.
- Build: `build/Directory.Build.props` (C++20, `/permissive-`, `/W4`, `MultiProcessorCompilation`,
  `/MTd`/`/MT`, Core include dir, Release `/WX` + `/analyze`, vcpkg import) and
  `build/Directory.Build.targets` (vcpkg targets import). Each `.vcxproj` imports both explicitly
  because they live under `build/` (keeps the T16 .NET helper out of these conventions).
- vcpkg manifest `vcpkg.json` (`wil`, `nlohmann-json`, `gtest`, `x64-windows-static`), pinned with a
  `builtin-baseline`. Static CRT confirmed: `dumpbin /dependents` shows only USER32/KERNEL32.
- Manifest verified via `mt.exe -inputresource:...;#1`: `asInvoker`, `uiAccess=false`, PerMonitorV2,
  UTF-8 active code page, longPathAware.
- Docs/tooling: `.clang-format`, `.editorconfig`, `docs/BUILDING.md`, `scripts/build.ps1`,
  `ADR 0006`, `docs/progress/T01.md`.

Deviations (recorded in ADR 0006 and `docs/progress/T01.md`):
- `VcpkgConfiguration` is set explicitly (props import precedes `UseDebugLibraries`);
  `VCPkgLocalAppDataDisabled=true` to avoid double-importing VS's per-user vcpkg props;
  `ExternalWarningLevel=TurnOffAllWarnings` + `DisableAnalyzeExternal=true` for third-party headers.
- Code analysis (`/analyze`) is disabled for `Pacecar.App.Tests` only (Google Test C6326 false
  positive); it stays on for Core/Overlay.
- `scripts/verify.ps1` test discovery now scans `out/` only (a stale `legacy/**/testhost.exe` was
  being executed otherwise).

Check results (this session, clean `out/`):
- `scripts/build.ps1 -Configuration Debug -Test` → build succeeded, 0 warnings, test PASSED.
- `scripts/build.ps1 -Configuration Release -Test` → build succeeded, 0 warnings, test PASSED.
- `scripts/verify.ps1` → build succeeded (0 warnings, 0 errors), `Pacecar.App.Tests` PASSED, `verify: ok`.

Next task (T02) can add Core sources under `src/Pacecar.Core/{include,src}` and tests under
`src/Pacecar.App.Tests`; import `pacecar/core.h` via `#include <pacecar/core.h>`. Do not edit
`build/Directory.Build.props`/`.targets` unless adding a centrally-shared setting.

