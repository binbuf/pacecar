# Pacecar - Design Document

## Overview

Pacecar is a lightweight, always-on-top system performance overlay built with Rust and egui. It provides real-time CPU, RAM, GPU, network, and disk I/O metrics in a compact, modern dashboard — similar to Windows Task Manager's Performance tab but as a floating overlay.

**Target:** Windows 11 MVP, cross-platform later (macOS, Linux).

---

## Architecture

### Tech Stack

| Layer | Choice | Rationale |
|-------|--------|-----------|
| Language | Rust | Low memory footprint, native performance |
| GUI Framework | egui via eframe | Batteries-included, wgpu backend on Windows, well-documented |
| System Tray | tray-icon | Modern, cross-platform (used by Tauri) |
| Global Hotkey | global-hotkeys | Cross-platform hotkey registration |
| Config | JSON file | Universal, easy to hand-edit |
| Metrics (Windows) | sysinfo + windows-rs | sysinfo for CPU/RAM/disk/network, windows-rs for GPU via NVAPI/D3DKMT |

### Crate Dependencies (Initial)

```toml
eframe = "0.29"          # egui + wgpu rendering
sysinfo = "0.32"         # CPU, RAM, disk, network, processes
tray-icon = "0.19"       # System tray
global-hotkeys = "0.6"   # Configurable global hotkey
serde = "1"              # Serialization
serde_json = "1"         # JSON config
directories = "5"        # Platform config paths
nvml-wrapper = "0.10"    # NVIDIA GPU metrics (optional)
```

### Module Structure

```
src/
  main.rs              # Entry point, tray setup, app lifecycle
  app.rs               # egui App impl, main render loop
  config.rs            # Settings load/save, defaults
  metrics/
    mod.rs             # MetricsCollector trait, aggregated snapshot
    cpu.rs             # CPU usage, frequency, per-core
    memory.rs          # RAM used/total/percentage
    gpu.rs             # GPU usage, temp, VRAM (NVIDIA via NVML, fallback via D3DKMT)
    network.rs         # Upload/download speeds
    disk.rs            # Read/write speeds, latency
  ui/
    mod.rs             # Layout orchestration
    gauge.rs           # Circular/arc gauge widget
    sparkline.rs       # Rolling line chart widget
    panel.rs           # Individual metric panel (gauge or sparkline + value)
    settings.rs        # Settings overlay/modal
  overlay.rs           # Window transparency, click-through, always-on-top
  hotkey.rs            # Global hotkey registration and handling
  tray.rs              # System tray icon, menu, events
```

---

## Metrics (MVP)

All metrics are collected on a background thread and sent to the UI via a channel.

| Metric | Source | Values |
|--------|--------|--------|
| CPU | sysinfo | Total %, per-core %, frequency (GHz) |
| RAM | sysinfo | Used / Total (GB), percentage |
| GPU | nvml-wrapper / D3DKMT | Usage %, temperature, VRAM used/total |
| Network | sysinfo | Upload speed, download speed (KB/s or MB/s) |
| Disk I/O | sysinfo | Read speed, write speed (MB/s) |

### Collection Design

- A `MetricsCollector` runs on a dedicated thread with a configurable polling interval (250ms–5000ms, default 1000ms).
- Each tick, it populates a `MetricsSnapshot` struct and sends it over a `crossbeam-channel` or `std::sync::mpsc`.
- The UI thread receives the latest snapshot each frame and renders it. No locking on the render path.
- `sysinfo::System` is reused across ticks (not re-created) to minimize allocations.

---

## UI Design

### Layout

Compact grid layout. Each metric gets a "panel" containing:
- A **circular arc gauge** (default) or **sparkline chart** (togglable in settings)
- Current numeric value
- Label

Default arrangement (2–3 columns, auto-flowing):

```
┌──────────┬──────────┬──────────┐
│   CPU    │   RAM    │   GPU    │
│  ◠ 42%  │  ◠ 61%  │  ◠ 28%  │
│  3.8GHz  │ 10/16GB │  72°C   │
├──────────┼──────────┼──────────┤
│ Network  │ Disk I/O │          │
│ ↑ 1.2MB  │ R: 45MB  │          │
│ ↓ 12MB   │ W: 12MB  │          │
└──────────┴──────────┴──────────┘
```

### Visualization Modes (User-togglable)

1. **Gauges + numbers** (default): Circular arc gauges for percentage metrics, numeric values for speeds/temps.
2. **Sparklines + numbers**: Rolling 60-sample line charts with current value overlay.

The toggle lives in Settings. Both modes share the same panel layout.

### Styling

- Dark theme by default, semi-transparent background
- Rounded corners, subtle borders
- Monospace font for numeric values
- Accent colors per metric (e.g., blue=CPU, green=RAM, red=GPU, orange=network, purple=disk)
- Small font sizes for compact display

---

## Overlay Behavior

### Window Modes (User-togglable)

1. **Interactive mode** (default): Always-on-top, captures mouse input. User can drag to reposition, access settings via right-click.
2. **Click-through mode**: Always-on-top, mouse events pass through to windows below. Toggle back via hotkey or tray.

### Transparency

- Configurable transparency level (10%–100%, default 85%)
- Stored in config, adjustable from settings panel
- Implemented via eframe's `ViewportBuilder::with_transparent(true)` + custom background alpha

### Window Properties

- No title bar (decorated = false)
- No taskbar entry
- Always on top
- Remembers position across sessions (saved in config)
- Resizable in interactive mode

---

## System Tray

### Menu Items

- **Show/Hide** — toggle overlay visibility
- **Mode: Interactive / Click-through** — toggle overlay mode
- **Settings** — open settings panel
- **Separator**
- **Quit** — exit application

### Behavior

- Closing the window (X) hides to tray, does not exit
- Double-click tray icon toggles visibility
- Tray icon shows a small status indicator

---

## Global Hotkey

- Default: `Ctrl+Shift+P`
- Configurable in settings and config file
- Toggles overlay visibility (show/hide)
- Registered via `global-hotkeys` crate

---

## Configuration

Stored at `%APPDATA%/pacecar/config.json` (Windows) / `~/.config/pacecar/config.json` (Linux/macOS).

```json
{
  "polling_interval_ms": 1000,
  "transparency": 0.85,
  "visualization": "gauges",
  "overlay_mode": "interactive",
  "hotkey": "Ctrl+Shift+P",
  "window_position": { "x": 100, "y": 100 },
  "window_size": { "width": 320, "height": 240 },
  "theme": "dark"
}
```

Defaults are applied for any missing keys. Config is loaded at startup and saved on change.

---

## App Lifecycle

```
main()
  ├── Load config (or create defaults)
  ├── Start metrics collector thread
  ├── Initialize system tray
  ├── Register global hotkey
  └── Run eframe app loop
        ├── Receive latest MetricsSnapshot
        ├── Render UI panels
        ├── Handle tray events
        └── Handle hotkey events

On close (X button) → hide to tray
On tray "Quit"     → save config → stop collector → exit
```

---

## Memory Footprint Goals

- Target: < 15 MB working set at idle
- Strategies:
  - Reuse `sysinfo::System` instance (don't recreate)
  - Fixed-size ring buffers for sparkline history (60 samples per metric)
  - No heap allocation in the render hot path
  - Minimal crate dependencies
  - Release build with `opt-level = "s"`, `lto = true`, `strip = true`

---

## Future Considerations (Post-MVP)

- Per-process top consumers view
- AMD GPU support (via ROCm/ADL)
- macOS / Linux support
- Custom themes / color schemes
- Multiple monitor support
- Widget-style resizable panels
- Export metrics to file/API
