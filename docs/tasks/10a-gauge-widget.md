# Task 10a: Circular Arc Gauge Widget

## Priority: P1
## Depends on: 10-ui-framework
## Blocks: 10c-panel-widget

## Description

Implement a reusable circular arc gauge widget in `ui/gauge.rs` for displaying percentage-based metrics (CPU, RAM, GPU usage).

## Acceptance Criteria

- [ ] `Gauge` widget struct:
  ```rust
  struct Gauge {
      value: f32,        // 0.0–1.0 (percentage as fraction)
      color: Color32,    // accent color
      label: String,     // e.g., "42%"
      size: f32,         // diameter in pixels
  }
  ```
- [ ] Renders a circular arc (270 degrees sweep max) representing the value
- [ ] Visual properties:
  - Background track (dark, subtle arc showing full range)
  - Foreground arc filled to `value` proportion with `color`
  - Anti-aliased rendering via egui's `Painter`
  - Line width proportional to gauge size
  - Centered percentage label inside the arc
- [ ] Implements `egui::Widget` trait for easy integration:
  ```rust
  impl Widget for Gauge {
      fn ui(self, ui: &mut Ui) -> Response { ... }
  }
  ```
- [ ] Configurable size (adapts to panel constraints)
- [ ] Smooth value transitions (optional: lerp between old and new values)

## Testing

- [ ] Unit test: gauge clamps values outside 0.0–1.0
- [ ] Unit test: correct arc angle calculation for known values (0%, 50%, 100%)

## Notes

- Use `egui::Painter::arc()` or manually compute arc points with `sin`/`cos`
- The arc should start from the bottom-left, sweep clockwise to bottom-right (typical gauge orientation)
- Consider a subtle glow or gradient effect on the filled arc for visual polish
- Keep the widget stateless — animation state (if any) should be managed by the parent
