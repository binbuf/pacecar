// Rolling line chart widget

use eframe::egui::{self, Color32, Pos2, Response, Stroke, Ui, Vec2, Widget};

/// A fixed-capacity ring buffer that stores the most recent `N` values.
///
/// New values are pushed in; when full, the oldest value is overwritten.
/// Iteration yields values in chronological order (oldest first).
pub struct RingBuffer<T, const N: usize> {
    buf: [T; N],
    /// Points to the next write position.
    head: usize,
    /// Number of elements currently stored (≤ N).
    len: usize,
}

impl<T: Default + Copy, const N: usize> RingBuffer<T, N> {
    /// Create a new empty ring buffer.
    pub fn new() -> Self {
        assert!(N > 0, "RingBuffer capacity must be > 0");
        Self {
            buf: [T::default(); N],
            head: 0,
            len: 0,
        }
    }

    /// Push a value. If the buffer is full, the oldest value is overwritten.
    pub fn push(&mut self, value: T) {
        self.buf[self.head] = value;
        self.head = (self.head + 1) % N;
        if self.len < N {
            self.len += 1;
        }
    }

    /// Number of elements currently stored.
    pub fn len(&self) -> usize {
        self.len
    }

    /// Whether the buffer is empty.
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Iterate values in chronological order (oldest first).
    pub fn iter(&self) -> RingBufferIter<'_, T, N> {
        let start = if self.len < N {
            0
        } else {
            self.head // head points past the newest, which is the oldest when full
        };
        RingBufferIter {
            buf: self,
            pos: start,
            remaining: self.len,
        }
    }
}

impl<T: Default + Copy, const N: usize> Default for RingBuffer<T, N> {
    fn default() -> Self {
        Self::new()
    }
}

pub struct RingBufferIter<'a, T, const N: usize> {
    buf: &'a RingBuffer<T, N>,
    pos: usize,
    remaining: usize,
}

impl<'a, T: Copy, const N: usize> Iterator for RingBufferIter<'a, T, N> {
    type Item = T;

    fn next(&mut self) -> Option<T> {
        if self.remaining == 0 {
            return None;
        }
        let val = self.buf.buf[self.pos];
        self.pos = (self.pos + 1) % N;
        self.remaining -= 1;
        Some(val)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<'a, T: Copy, const N: usize> ExactSizeIterator for RingBufferIter<'a, T, N> {}

/// Default sparkline history depth: 60 samples (1 minute at 1 Hz polling).
pub const SPARKLINE_CAPACITY: usize = 60;

/// A rolling sparkline (line chart) widget for displaying metric history.
///
/// Renders a continuous line from oldest (left) to newest (right) data points,
/// scaled to the given Y-axis range. The current value is overlaid as text.
pub struct Sparkline<'a> {
    /// Slice of historical values in chronological order.
    history: &'a [f32],
    /// Accent color for the line.
    color: Color32,
    /// Widget size (width × height).
    size: Vec2,
    /// Y-axis range (min, max).
    range: (f32, f32),
}

impl<'a> Sparkline<'a> {
    pub fn new(history: &'a [f32], color: Color32, size: Vec2, range: (f32, f32)) -> Self {
        Self {
            history,
            color,
            size,
            range,
        }
    }
}

impl<'a> Widget for Sparkline<'a> {
    fn ui(self, ui: &mut Ui) -> Response {
        let (rect, response) = ui.allocate_exact_size(self.size, egui::Sense::hover());

        if ui.is_rect_visible(rect) {
            let painter = ui.painter_at(rect);

            // Draw line if we have at least 2 data points
            if self.history.len() >= 2 {
                let n = self.history.len();
                let (y_min, y_max) = self.range;

                let points: Vec<Pos2> = self
                    .history
                    .iter()
                    .enumerate()
                    .map(|(i, &val)| {
                        let x = rect.left()
                            + (i as f32 / (n - 1) as f32) * rect.width();
                        let y = scale_y(val, y_min, y_max, rect.top(), rect.bottom());
                        Pos2::new(x, y)
                    })
                    .collect();

                // Filled area under the line (lower opacity)
                let mut fill_points = points.clone();
                fill_points.push(Pos2::new(rect.right(), rect.bottom()));
                fill_points.push(Pos2::new(rect.left(), rect.bottom()));
                let fill_color = Color32::from_rgba_unmultiplied(
                    self.color.r(),
                    self.color.g(),
                    self.color.b(),
                    30,
                );
                painter.add(egui::Shape::convex_polygon(
                    fill_points,
                    fill_color,
                    Stroke::NONE,
                ));

                // Line
                painter.add(egui::Shape::line(
                    points,
                    Stroke::new(1.5, self.color),
                ));
            }

            // Current value text overlay (top-right)
            if let Some(&current) = self.history.last() {
                let font_size = (self.size.y * 0.35).clamp(8.0, 14.0);
                painter.text(
                    Pos2::new(rect.right() - 2.0, rect.top() + 2.0),
                    egui::Align2::RIGHT_TOP,
                    format!("{current:.0}"),
                    egui::FontId::monospace(font_size),
                    Color32::WHITE,
                );
            }
        }

        response
    }
}

/// Map a metric value to a Y pixel coordinate.
///
/// `y_min`/`y_max` define the value range; `top`/`bottom` define the pixel range.
/// Higher values map to lower Y (screen coordinates). When `y_min == y_max`,
/// the value is placed at the vertical center.
fn scale_y(value: f32, y_min: f32, y_max: f32, top: f32, bottom: f32) -> f32 {
    if (y_max - y_min).abs() < f32::EPSILON {
        // All values identical → center the flat line
        (top + bottom) / 2.0
    } else {
        let t = (value - y_min) / (y_max - y_min);
        // Invert: high values → top of widget
        bottom - t * (bottom - top)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ── RingBuffer tests ──

    #[test]
    fn ring_buffer_starts_empty() {
        let rb: RingBuffer<f32, 4> = RingBuffer::new();
        assert!(rb.is_empty());
        assert_eq!(rb.len(), 0);
        assert_eq!(rb.iter().count(), 0);
    }

    #[test]
    fn ring_buffer_push_and_iterate() {
        let mut rb: RingBuffer<i32, 4> = RingBuffer::new();
        rb.push(10);
        rb.push(20);
        rb.push(30);
        let vals: Vec<i32> = rb.iter().collect();
        assert_eq!(vals, vec![10, 20, 30]);
        assert_eq!(rb.len(), 3);
    }

    #[test]
    fn ring_buffer_wraps_at_capacity() {
        let mut rb: RingBuffer<i32, 3> = RingBuffer::new();
        rb.push(1);
        rb.push(2);
        rb.push(3);
        rb.push(4); // overwrites 1
        let vals: Vec<i32> = rb.iter().collect();
        assert_eq!(vals, vec![2, 3, 4]);
        assert_eq!(rb.len(), 3);
    }

    #[test]
    fn ring_buffer_wraps_multiple_times() {
        let mut rb: RingBuffer<i32, 2> = RingBuffer::new();
        for i in 0..10 {
            rb.push(i);
        }
        let vals: Vec<i32> = rb.iter().collect();
        assert_eq!(vals, vec![8, 9]);
        assert_eq!(rb.len(), 2);
    }

    #[test]
    fn ring_buffer_single_capacity() {
        let mut rb: RingBuffer<i32, 1> = RingBuffer::new();
        rb.push(42);
        assert_eq!(rb.iter().collect::<Vec<_>>(), vec![42]);
        rb.push(99);
        assert_eq!(rb.iter().collect::<Vec<_>>(), vec![99]);
        assert_eq!(rb.len(), 1);
    }

    #[test]
    fn ring_buffer_exact_size_iterator() {
        let mut rb: RingBuffer<f32, 4> = RingBuffer::new();
        rb.push(1.0);
        rb.push(2.0);
        let iter = rb.iter();
        assert_eq!(iter.len(), 2);
    }

    // ── Y-axis scaling tests ──

    #[test]
    fn scale_y_min_value_maps_to_bottom() {
        let y = scale_y(0.0, 0.0, 100.0, 10.0, 110.0);
        assert!((y - 110.0).abs() < f32::EPSILON);
    }

    #[test]
    fn scale_y_max_value_maps_to_top() {
        let y = scale_y(100.0, 0.0, 100.0, 10.0, 110.0);
        assert!((y - 10.0).abs() < f32::EPSILON);
    }

    #[test]
    fn scale_y_mid_value_maps_to_center() {
        let y = scale_y(50.0, 0.0, 100.0, 0.0, 100.0);
        assert!((y - 50.0).abs() < f32::EPSILON);
    }

    #[test]
    fn scale_y_identical_range_centers() {
        let y = scale_y(42.0, 42.0, 42.0, 0.0, 100.0);
        assert!((y - 50.0).abs() < f32::EPSILON);
    }

    // ── Edge cases ──

    #[test]
    fn sparkline_struct_accepts_empty_history() {
        // Should not panic; rendering will simply skip the line.
        let _s = Sparkline::new(&[], Color32::RED, Vec2::new(50.0, 30.0), (0.0, 100.0));
    }

    #[test]
    fn sparkline_struct_accepts_single_point() {
        let _s = Sparkline::new(&[42.0], Color32::RED, Vec2::new(50.0, 30.0), (0.0, 100.0));
    }

    // ── Property tests ──

    mod proptests {
        use super::*;
        use proptest::prelude::*;

        proptest! {
            #[test]
            fn ring_buffer_never_exceeds_capacity(values in proptest::collection::vec(any::<f32>(), 0..200)) {
                let mut rb: RingBuffer<f32, 60> = RingBuffer::new();
                for v in &values {
                    rb.push(*v);
                    prop_assert!(rb.len() <= 60);
                }
            }

            #[test]
            fn ring_buffer_len_is_min_of_pushes_and_capacity(
                count in 0usize..300
            ) {
                let mut rb: RingBuffer<i32, 60> = RingBuffer::new();
                for i in 0..count {
                    rb.push(i as i32);
                }
                prop_assert_eq!(rb.len(), count.min(60));
            }

            #[test]
            fn ring_buffer_preserves_most_recent_values(values in proptest::collection::vec(0i32..1000, 1..200)) {
                let mut rb: RingBuffer<i32, 60> = RingBuffer::new();
                for v in &values {
                    rb.push(*v);
                }
                let result: Vec<i32> = rb.iter().collect();
                let expected_start = values.len().saturating_sub(60);
                let expected: Vec<i32> = values[expected_start..].to_vec();
                prop_assert_eq!(result, expected);
            }

            #[test]
            fn scale_y_stays_in_bounds(
                value in -1000.0f32..1000.0,
                y_min in -100.0f32..100.0,
                spread in 0.01f32..200.0,
            ) {
                let y_max = y_min + spread;
                let y = scale_y(value, y_min, y_max, 0.0, 100.0);
                // Output is not clamped—values outside the range map outside the rect.
                // But for in-range values, result should be in [0, 100].
                if value >= y_min && value <= y_max {
                    prop_assert!(y >= -0.001 && y <= 100.001,
                        "value={value} y_min={y_min} y_max={y_max} → y={y}");
                }
            }
        }
    }
}
