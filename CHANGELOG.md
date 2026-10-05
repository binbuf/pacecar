# Changelog

All notable changes to Pacecar are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-10-04

The native rewrite: a ground-up C++20 Win32/Direct2D implementation replacing the original
Rust/egui app, which is retained under `legacy/` for reference.

### Added

- Native Win32 + Direct2D + DirectWrite overlay (static CRT, no runtime dependency), running
  `asInvoker` and never elevating.
- Render-on-change overlay with six presentation views (full panel, large visuals, small text,
  stat rows, FPS text, FPS only), per-tile and per-field toggles, gauges or sparklines, and a
  custom drag/resize layout grid.
- Baseline metrics with no admin rights or vendor SDK: CPU total/per-core utilization and
  effective frequency, memory used/total/commit/cache, GPU utilization (PDH `GPU Engine` +
  D3DKMT VRAM fallback), network up/down, disk read/write, and ICMP ping RTT.
- Runtime-loaded vendor GPU providers: NVML (NVIDIA) enrichment, with ADLX (AMD) and IGCL (Intel)
  detected as enrichment-only.
- Optional elevated `Pacecar.Sensors` helper (PawnIO-based LibreHardwareMonitor, built on .NET 8)
  streaming CPU/board/DIMM/disk temperature and fan RPM over a DACL-secured local named pipe.
- Opt-in ETW frame-time / FPS capture owned by the helper (never hooks or injects into games).
- Tray menu, global hotkeys, single-instance guard, and per-monitor layout persistence.
- Settings, Specs, and History windows; atomic, debounced JSON config at
  `%APPDATA%\Pacecar\config.json`.
- "Start with Windows" support (per-user registry).
- Portable ZIP and Inno Setup installer release artifacts produced by CI on tagged releases.
- Third-party notices shipped with the sensor helper.

### Changed

- Complete rewrite of the application in C++20 (MSVC, MSBuild + vcpkg manifest mode); the original
  Rust/egui implementation now lives under `legacy/`.

### Security

- The UI process stays unelevated; privileged sensors and ETW run in the separate, on-demand helper.
- Capture exclusion (`WDA_EXCLUDEFROMCAPTURE`) is enabled by default.
- No network calls of the app's own; only configurable ping targets and explicitly loaded vendor
  SDKs are outbound surfaces.

## [0.1.1] - 2026-04-01

### Fixed

- GitHub Actions build fixes and Rust test fixes; added tests for the hardware-monitor shim.

## [0.1.0] - 2026-03-31

### Added

- Initial release of the Rust/egui implementation: an always-on-top system-metrics overlay with a
  hardware-monitor shim and an Inno Setup installer.