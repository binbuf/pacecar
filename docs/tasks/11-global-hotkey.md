# Task 11: Global Hotkey

## Priority: P1
## Depends on: 01-project-setup, 02-config-system
## Blocks: 13-integration

## Description

Implement global hotkey registration and handling in `hotkey.rs` using the `global-hotkeys` crate. The hotkey toggles overlay visibility.

## Acceptance Criteria

- [ ] Default hotkey: `Ctrl+Shift+P`
- [ ] Hotkey string parsed from config (e.g., "Ctrl+Shift+P" → modifier + key combination)
- [ ] Hotkey registered at app startup via `global-hotkeys` crate
- [ ] Hotkey event triggers overlay visibility toggle (show/hide)
- [ ] Hotkey works regardless of which application is focused
- [ ] Graceful handling:
  - Hotkey already registered by another app → log warning, continue without hotkey
  - Invalid hotkey string in config → fall back to default
- [ ] Hotkey unregistered on app shutdown (cleanup)

## Testing

- [ ] Unit test: hotkey string parsing for various formats
- [ ] Unit test: invalid hotkey string falls back to default
- [ ] Manual test: hotkey toggles overlay from any focused application

## Notes

- `global-hotkeys` requires an event loop; integrate with eframe's event loop or handle on a separate thread
- On Windows, `RegisterHotKey` has system-wide scope — conflicts are possible
- The hotkey should also be able to toggle click-through mode back to interactive (important: click-through mode has no other way to interact with the overlay)
- Consider supporting hotkey re-registration when config changes
