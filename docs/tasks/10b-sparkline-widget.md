# Task 10b: Sparkline Chart Widget

## Priority: P2
## Depends on: 10-ui-framework
## Blocks: 10c-panel-widget

## Description

Implement a rolling sparkline (line chart) widget in `ui/sparkline.rs` for displaying metric history over time. This is the alternative visualization mode to gauges.

## Acceptance Criteria

- [ ] `Sparkline` widget struct:
  ```rust
  struct Sparkline {
      history: &[f32],   // Ring buffer of values (up to 60 samples)
      color: Color32,    // accent color
      size: Vec2,        // width x height
      range: (f32, f32), // min/max value range for Y axis
  }
  ```
- [ ] Fixed-size ring buffer utility (60 samples):
  - `RingBuffer<T, const N: usize>` or simple `VecDeque` with max capacity
  - Push new values, oldest values drop off
  - Iterable in chronological order
- [ ] Renders a continuous line chart:
  - X axis: time (left = oldest, right = newest)
  - Y axis: metric value (scaled to `range`)
  - Line drawn with `color`, anti-aliased
  - Optional: filled area under the line with lower opacity
- [ ] Implements `egui::Widget` trait
- [ ] Current value overlaid as text (top-right or centered)
- [ ] Handles edge cases:
  - Fewer than 2 data points → don't draw line
  - All values identical → flat line at correct Y position

## Testing

- [ ] Unit test: ring buffer push/pop behavior, capacity enforcement
- [ ] Unit test: Y-axis scaling calculation for known ranges
- [ ] Property test: ring buffer never exceeds capacity

## Notes

- Use `egui::Painter::line()` or `line_segment()` for drawing
- Ring buffer should be allocated once and reused (no heap allocation per frame)
- 60 samples at 1s polling = 1 minute of history
- The sparkline should look clean at small sizes (50x30px range)
