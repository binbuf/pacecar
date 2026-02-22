# Task 02: Configuration System

## Priority: P0 (Blocking)
## Depends on: 01-project-setup
## Blocks: 09-overlay-behavior, 11-global-hotkey, 12-system-tray

## Description

Implement the configuration loading, saving, and default-generation system in `config.rs`. The config is a JSON file stored at platform-specific paths, with sensible defaults for all values.

## Acceptance Criteria

- [ ] `Config` struct defined with serde `Serialize`/`Deserialize`:
  ```rust
  struct Config {
      polling_interval_ms: u64,    // 250..=5000, default 1000
      transparency: f32,           // 0.1..=1.0, default 0.85
      visualization: Visualization, // enum: Gauges | Sparklines, default Gauges
      overlay_mode: OverlayMode,   // enum: Interactive | ClickThrough, default Interactive
      hotkey: String,              // default "Ctrl+Shift+P"
      window_position: Option<Position>, // { x, y }, None = system default
      window_size: Size,           // { width, height }, default 320x240
      theme: Theme,                // enum: Dark (only option for MVP)
  }
  ```
- [ ] `Default` impl provides all default values matching design spec
- [ ] Config file path resolved via `directories` crate:
  - Windows: `%APPDATA%/pacecar/config.json`
  - Linux/macOS: `~/.config/pacecar/config.json`
- [ ] `Config::load()` — reads from disk, falls back to defaults for missing keys
- [ ] `Config::save()` — writes to disk, creating parent directories if needed
- [ ] Graceful handling of:
  - Missing config file (create with defaults)
  - Malformed JSON (log warning, use defaults)
  - Missing keys (serde defaults fill gaps)
  - Invalid values (clamp to valid ranges)

## Testing

- [ ] Unit test: default config serializes/deserializes round-trip correctly
- [ ] Unit test: missing keys in JSON produce correct defaults
- [ ] Unit test: malformed JSON returns default config
- [ ] Unit test: value clamping for out-of-range inputs
- [ ] Snapshot test (insta): default config JSON output matches expected format
- [ ] 100% coverage target for config module

## Notes

- Use `#[serde(default)]` on the struct and individual fields for resilient deserialization
- Consider using `serde(rename_all = "snake_case")` for enum variants
- Config should be cheaply cloneable for passing to UI thread
