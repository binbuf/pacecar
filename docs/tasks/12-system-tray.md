# Task 12: System Tray

## Priority: P1
## Depends on: 01-project-setup, 02-config-system
## Blocks: 13-integration

## Description

Implement the system tray icon and menu in `tray.rs` using the `tray-icon` crate. The tray provides the primary control interface when the overlay is hidden or in click-through mode.

## Acceptance Criteria

- [ ] Tray icon displayed in the Windows system tray
  - Custom icon (simple gauge/meter icon, or use a placeholder for MVP)
  - Tooltip showing "Pacecar" or brief status
- [ ] Right-click context menu with items:
  - **Show/Hide** — toggles overlay visibility
  - **Mode: Interactive / Click-through** — toggles overlay mode (shows current mode)
  - **Settings** — opens settings panel
  - **Separator**
  - **Quit** — saves config and exits application
- [ ] Double-click tray icon toggles visibility
- [ ] Window close (X button) behavior:
  - Hides overlay to tray instead of quitting
  - Tray icon remains visible
- [ ] Menu item states update dynamically:
  - Show/Hide label reflects current visibility
  - Mode label reflects current overlay mode
- [ ] Tray events communicated to main app via channel or shared state

## Testing

- [ ] Unit test: menu item state reflects current app state
- [ ] Manual test: all menu items trigger correct actions
- [ ] Manual test: close button hides to tray, Quit exits

## Notes

- `tray-icon` requires icon data — use a simple embedded PNG or generate programmatically
- The tray must be initialized before the eframe event loop (or on a separate thread)
- Consider `muda` crate for menu handling (same ecosystem as `tray-icon`)
- On Windows, tray events may need to be polled or handled via Windows message loop
- The tray is critical UX: it's the only way to access the app when in click-through mode or hidden
