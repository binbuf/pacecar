# Task 13: App Lifecycle & Integration

## Priority: P0
## Depends on: 02-config-system, 04 through 08 (metrics), 09-overlay-behavior, 10-ui-framework, 10c-panel-widget, 11-global-hotkey, 12-system-tray
## Blocks: 14-polish

## Description

Wire everything together in `main.rs` and `app.rs`: the full application lifecycle from startup to shutdown, connecting config, metrics, UI, tray, and hotkey systems.

## Acceptance Criteria

- [ ] `main()` startup sequence (matches design's lifecycle):
  1. Load config (or create defaults)
  2. Start metrics collector background thread
  3. Initialize system tray with icon and menu
  4. Register global hotkey
  5. Launch eframe app loop with configured viewport
- [ ] App loop:
  - Each frame: receive latest `MetricsSnapshot`, render UI panels
  - Handle tray events (show/hide, mode toggle, settings, quit)
  - Handle hotkey events (visibility toggle)
- [ ] Shutdown sequence:
  - Triggered by tray "Quit" or process signal
  - Save current config (window position, etc.)
  - Stop metrics collector thread (graceful shutdown)
  - Unregister global hotkey
  - Remove tray icon
  - Exit process
- [ ] Close button (X) hides to tray, does not exit
- [ ] All channels connected:
  - Metrics thread → UI (MetricsSnapshot)
  - Tray → App (menu events)
  - Hotkey → App (toggle events)
- [ ] Error handling: startup failures logged, non-critical systems degrade gracefully (e.g., no GPU → skip GPU panel, no hotkey → warn and continue)

## Testing

- [ ] Integration test: app starts and shuts down cleanly
- [ ] Integration test: config round-trips through lifecycle
- [ ] Manual test: full user workflow (start, view metrics, toggle modes, change settings, quit)

## Notes

- Threading model: metrics collector on its own thread, tray may need its own thread (Windows message loop), hotkey listener may need its own thread — coordinate carefully
- eframe's `run_native()` blocks the main thread; other systems must be started before or run on background threads
- Consider using `Arc<Mutex<AppState>>` or channels for cross-thread communication
- The first `MetricsSnapshot` may have incomplete data (CPU warm-up) — UI should handle gracefully with "—" or "..." placeholders
