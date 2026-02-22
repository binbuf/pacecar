# Task 10d: Settings UI Overlay

## Priority: P2
## Depends on: 10-ui-framework, 02-config-system
## Blocks: None

## Description

Implement the settings overlay/modal in `ui/settings.rs` that allows users to configure Pacecar's behavior without editing the JSON file manually.

## Acceptance Criteria

- [ ] Settings panel rendered as a modal/overlay on top of the main UI
- [ ] Toggled via:
  - System tray menu "Settings" option
  - Future: right-click context menu on overlay
- [ ] Configurable options:
  - **Polling interval**: slider or dropdown (250ms, 500ms, 1000ms, 2000ms, 5000ms)
  - **Transparency**: slider (10%–100%)
  - **Visualization mode**: toggle between Gauges and Sparklines
  - **Overlay mode**: toggle between Interactive and Click-through
  - **Hotkey**: text input or key capture (stretch goal — text input is fine for MVP)
- [ ] Changes applied immediately (live preview)
- [ ] Changes saved to config file on close/apply
- [ ] Close button or click-outside-to-dismiss behavior
- [ ] Styled consistently with main UI (dark theme, same fonts)

## Testing

- [ ] Unit test: settings changes produce correct config mutations
- [ ] Manual test: all settings controls function and persist across restarts

## Notes

- egui's `Window` widget works well for modal-like overlays
- Use `egui::Slider`, `egui::ComboBox`, and `egui::RadioButton` for controls
- Consider a "Reset to Defaults" button
- Hotkey configuration is complex (key capture) — for MVP, a simple text field is acceptable
