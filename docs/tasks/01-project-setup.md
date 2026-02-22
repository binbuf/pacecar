# Task 01: Project Setup & Scaffolding

## Priority: P0 (Blocking)
## Depends on: None
## Blocks: All other tasks

## Description

Initialize the Rust project structure, configure `Cargo.toml` with all dependencies, and create the module scaffolding with empty files matching the design's module structure.

## Acceptance Criteria

- [ ] `cargo new pacecar` project initialized (or existing project verified)
- [ ] `Cargo.toml` configured with all dependencies from design:
  - `eframe = "0.29"` (egui + wgpu rendering)
  - `sysinfo = "0.32"` (CPU, RAM, disk, network, processes)
  - `tray-icon = "0.19"` (system tray)
  - `global-hotkeys = "0.6"` (configurable global hotkey)
  - `serde = { version = "1", features = ["derive"] }` (serialization)
  - `serde_json = "1"` (JSON config)
  - `directories = "5"` (platform config paths)
  - `nvml-wrapper = "0.10"` (NVIDIA GPU metrics, optional feature)
  - `crossbeam-channel` or confirm `std::sync::mpsc` usage
- [ ] Dev dependencies added:
  - `mockall` (trait-based mocking)
  - `insta` (snapshot testing)
  - `proptest` (property-based testing)
- [ ] Release profile configured in `Cargo.toml`:
  ```toml
  [profile.release]
  opt-level = "s"
  lto = true
  strip = true
  ```
- [ ] Module structure created (empty files with `mod` declarations):
  ```
  src/
    main.rs
    app.rs
    config.rs
    overlay.rs
    hotkey.rs
    tray.rs
    metrics/
      mod.rs
      cpu.rs
      memory.rs
      gpu.rs
      network.rs
      disk.rs
    ui/
      mod.rs
      gauge.rs
      sparkline.rs
      panel.rs
      settings.rs
  ```
- [ ] Project compiles with `cargo build` (even if modules are mostly empty)

## Notes

- Verify latest compatible versions of crates before pinning
- Consider using a Cargo workspace if future sub-crates are anticipated
- The `nvml-wrapper` dependency should be behind an optional feature flag since not all systems have NVIDIA GPUs
