# Task 09: Overlay Window Behavior

## Priority: P1
## Depends on: 01-project-setup, 02-config-system
## Blocks: 13-integration

## Description

Implement the overlay window properties in `overlay.rs`: always-on-top, no title bar, no taskbar entry, transparency, click-through mode, position persistence, and dragging.

## Acceptance Criteria

- [ ] Window created with eframe `ViewportBuilder`:
  - `with_decorations(false)` — no title bar
  - `with_always_on_top()` — stays above other windows
  - `with_transparent(true)` — enables transparency
  - `with_taskbar(false)` — no taskbar entry
  - Initial position and size loaded from config
- [ ] **Transparency**:
  - Background rendered with configurable alpha (config `transparency` field, 0.1–1.0)
  - Applied via custom background fill with alpha channel
- [ ] **Interactive mode** (default):
  - Window captures mouse input
  - Draggable by clicking and dragging anywhere on the window background
  - Right-click opens context menu (or delegates to tray)
- [ ] **Click-through mode**:
  - Mouse events pass through to windows below
  - Implemented via platform-specific window style flags (`WS_EX_TRANSPARENT` on Windows via `windows-rs`)
  - Toggle back via hotkey or tray menu
- [ ] **Position persistence**:
  - Save window position to config on move
  - Restore position from config on startup
  - Handle multi-monitor edge cases (saved position is off-screen → reset to default)
- [ ] Mode toggle function callable from tray and hotkey handlers

## Testing

- [ ] Unit test: config values correctly applied to viewport builder
- [ ] Unit test: position save/restore round-trips correctly
- [ ] Manual test: verify always-on-top, transparency, click-through on Windows 11

## Notes

- Click-through requires platform-specific code; use `#[cfg(target_os = "windows")]` with `windows-rs` to set `WS_EX_TRANSPARENT` / `WS_EX_LAYERED` extended window styles
- eframe's `ViewportCommand` API may help with runtime always-on-top toggling
- Dragging without a title bar: implement custom drag by tracking mouse delta on background clicks
- Test transparency on both light and dark Windows themes
