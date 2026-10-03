# 0006 - Build scaffolding: central MSBuild props, vcpkg manifest, static CRT

## Status
accepted

## Context
Task T01 restructures the generated VS template into the target layout (`Pacecar.Core` static lib,
`Pacecar.Overlay` Win32 EXE, `Pacecar.App.Tests` Google Test EXE) and locks in build conventions.
The settings must be shared across the native projects but exclude the future .NET
`helper/Pacecar.Sensors` (task T16), and must work both from Visual Studio and from a plain
`msbuild` invocation (the harness's `scripts/verify.ps1`). Design refs: `08-project-layout-and-testing.md`,
`05-performance.md`; ADR `0001-native-cpp-stack.md`.

## Decision
- Put shared settings in `build/Directory.Build.props` and dependent logic (the `vcpkg.targets`
  import) in `build/Directory.Build.targets`; each `.vcxproj` imports both explicitly. Keeping them
  under `build/` (instead of the MSBuild default next to each project) prevents the .NET helper from
  inheriting C++/vcpkg/static-CRT conventions via `Directory.Build.props` auto-discovery.
- Use **vcpkg manifest mode** (`vcpkg.json` pinned with `builtin-baseline`,
  `x64-windows-static` triplet) for `wil`, `nlohmann-json`, `gtest`.
- **Static CRT**: `/MTd` in Debug, `/MT` in Release, set centrally.
- Set `VcpkgConfiguration` explicitly in the props (the props are imported before
  `UseDebugLibraries` exists, so vcpkg cannot infer debug/release lib subdirectories itself).
- Set `VCPkgLocalAppDataDisabled=true` so the repo's explicit vcpkg import is the single source of
  truth and Visual Studio's per-user integration does not import the same props twice.
- Release: `/W4` + `/WX` + MSVC code analysis (`/analyze`). External (vcpkg) headers are excluded
  from compiler and analyzer diagnostics (`DisableAnalyzeExternal`, `ExternalWarningLevel`).
  `Pacecar.App.Tests` disables code analysis only, because Google Test registration templates trip a
  known `/analyze` false positive (C6326).
- Test output is centralized under `out/<Platform>/<Configuration>/`; `scripts/build.ps1` and
  `scripts/verify.ps1` discover test executables there.

## Consequences
- New native projects must import `build/Directory.Build.props`/`.targets`; they get C++20, `/W4`,
  static CRT, and vcpkg automatically.
- Adding a vcpkg dependency is a one-line `vcpkg.json` change; the baseline keeps restores
  reproducible.
- The shipping binaries need no VC++ runtime and declare `asInvoker` + Per-Monitor V2.
- `Pacecar.Sensors` must provide `helper/Directory.Build.props` to stop inheritance (task T16).
- `build/` is listed in `.gitignore` with explicit negations for the two tracked files, because
  the harness auto-ignores top-level `build/` directories.