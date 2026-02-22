# Task 10: UI Framework & Layout

## Priority: P1
## Depends on: 01-project-setup, 03-metrics-infrastructure
## Blocks: 10a-gauge-widget, 10b-sparkline-widget, 10c-panel-widget, 10d-settings-ui

## Description

Implement the core UI framework in `app.rs` and `ui/mod.rs`: the egui `App` implementation, main render loop, layout orchestration, and styling/theming.

## Acceptance Criteria

- [ ] `PacecarApp` struct implementing `eframe::App`:
  - Holds `MetricsReceiver` for latest snapshot
  - Holds `Config` reference (or Arc for shared access)
  - Holds UI state (selected visualization mode, etc.)
- [ ] `update()` method:
  - Receives latest `MetricsSnapshot` from channel (non-blocking)
  - Calls layout orchestrator to render panels
  - Handles frame-level concerns (background fill with transparency)
- [ ] Layout orchestrator in `ui/mod.rs`:
  - Grid layout: 2–3 columns, auto-flowing rows
  - Panel order: CPU, RAM, GPU, Network, Disk I/O
  - GPU panel hidden if `GpuMetrics` is `None`
  - Responsive: adapts column count based on window width
- [ ] **Styling / Theming**:
  - Dark theme by default
  - Semi-transparent background
  - Rounded corners, subtle borders on panels
  - Monospace font for numeric values
  - Accent colors per metric:
    - CPU: blue
    - RAM: green
    - GPU: red
    - Network: orange
    - Disk: purple
  - Small, compact font sizes
- [ ] Custom egui `Visuals` configured at startup

## Testing

- [ ] Unit test: layout calculates correct column count for various window widths
- [ ] Unit test: GPU panel excluded from layout when metrics are `None`

## Notes

- egui's `Grid`, `columns()`, or manual `Area`/`Window` placement can be used for layout
- Consider using `egui::Frame` for panel backgrounds with per-metric accent colors
- The app should request repaints at a reasonable rate (e.g., match polling interval, not 60fps)
- Use `ctx.request_repaint_after(Duration)` to avoid burning CPU on idle
