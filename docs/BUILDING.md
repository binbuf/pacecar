# Building Pacecar

Pacecar's native stack is built with MSBuild + vcpkg manifest mode. Everything is x64; the CRT is
statically linked (`/MT`), so the produced executables have no VC++ redistributable dependency.

## Prerequisites

- **Visual Studio 2022 or newer** with the **Desktop development with C++** workload (MSVC toolset
  `v145`/`v143`, Windows 10/11 SDK). The project files use `v145`; change `PlatformToolset` in the
  `.vcxproj` files if your toolset is newer.
- **vcpkg.** Either the copy bundled with Visual Studio (`<VS>\VC\vcpkg`) or a standalone clone.
  Point the build at it with the `VCPKG_ROOT` environment variable, or let the build locate the
  bundled copy automatically. `scripts/build.ps1` and `scripts/verify.ps1` also locate it.
- PowerShell 7+ for the helper scripts.

No CMake is required; the solution is MSBuild-native. No NuGet or global vcpkg installs are needed -
dependencies come from `vcpkg.json` in manifest mode into `vcpkg_installed/`.

## Layout

```
pacecar.slnx
  src/Pacecar.Core/        static library (C++20, no UI)
  src/Pacecar.Overlay/     Win32 EXE, references Core
  src/Pacecar.App.Tests/   Google Test console EXE, references Core
  build/                   Directory.Build.props / .targets (shared MSBuild settings + vcpkg import)
  vcpkg.json               manifest: wil, nlohmann-json, gtest (x64-windows-static)
  scripts/build.ps1        local build helper
  scripts/verify.ps1       harness build-and-test check
```

## Build

```pwsh
# Debug
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Debug
# Release (warnings-as-errors + MSVC code analysis)
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release -Test
# wipe out/ first
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release -Clean
```

Equivalent direct MSBuild invocation:

```pwsh
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe'
& $msbuild pacecar.slnx /m /p:Configuration=Release /p:Platform=x64
```

Artifacts land in `out\<Platform>\<Configuration>\` (`Pacecar.Overlay.exe`, `Pacecar.Core.lib`,
`Pacecar.App.Tests.exe`); intermediates under `out\obj\`.

The first build runs `vcpkg install` for the manifest and can take a few minutes. `vcpkg.json` pins
a `builtin-baseline`, so restores are reproducible.

## Test

```pwsh
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release -Test
# or directly:
.\out\x64\Release\Pacecar.App.Tests.exe
```

`Pacecar.App.Tests` is a Google Test console executable; a non-zero exit code means failure. To run
it from **Test Explorer**, install the *Test Adapter for Google Test* extension and open the
solution; the adapter discovers tests from the built executable.

## Conventions

- Shared settings live in `build/Directory.Build.props` (C++20, `/permissive-`, `/W4`,
  `MultiProcessorCompilation`, static CRT, vcpkg) and `build/Directory.Build.targets` (the vcpkg
  targets import). Each `.vcxproj` imports both explicitly; project-specific overrides belong in the
  `.vcxproj`.
- **Debug** uses `/MTd` (`MultiThreadedDebug`), **Release** uses `/MT` (`MultiThreaded`). Never mix
  `/MT` and `/MD`; the `x64-windows-static` triplet keeps dependencies aligned.
- Release treats warnings as errors (`/WX`) and enables MSVC code analysis (`/analyze`). Analysis is
  disabled for `Pacecar.App.Tests` only, because Google Test's registration templates trip a known
  `/analyze` false positive (C6326).
- Google Test's `gtest_main.lib` lives in vcpkg's `lib\manual-link`, which vcpkg does not autolink;
  the test project links it explicitly.

## Verify the static CRT and manifest

`Pacecar.Overlay.exe` embeds an `asInvoker`, Per-Monitor V2 manifest and links only in-box DLLs:

```pwsh
# No VCRUNTIME140.dll / MSVCP140.dll should appear:
dumpbin /dependents out\x64\Release\Pacecar.Overlay.exe

# Inspect the embedded manifest (mt.exe ships with the Windows SDK):
mt.exe "-inputresource:out\x64\Release\Pacecar.Overlay.exe;#1" -out:manifest.xml
```

## Troubleshooting

- **`vcpkg` not found / wrong root:** set `VCPKG_ROOT` to your vcpkg checkout before building.
- **Debug links Release vcpkg libs:** `VcpkgConfiguration` is set explicitly in
  `Directory.Build.props`; if you rename configurations, update it.
- **Stale third-party headers:** delete `vcpkg_installed/` and rebuild.