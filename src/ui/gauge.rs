// Circular/arc gauge widget

use eframe::egui::{self, Color32, Pos2, Response, Stroke, Ui, Vec2, Widget};
use std::f32::consts::PI;

/// A circular arc gauge for displaying percentage-based metrics.
///
/// Renders a 270-degree arc with a background track and a filled foreground
/// arc proportional to `value`. The percentage label is centered inside.
pub struct Gauge {
    /// Value as a fraction (0.0–1.0). Clamped internally.
    value: f32,
    /// Accent color for the filled arc.
    color: Color32,
    /// Text label displayed inside the gauge (e.g., "42%").
    label: String,
    /// Diameter in pixels.
    size: f32,
}

impl Gauge {
    pub fn new(value: f32, color: Color32, label: impl Into<String>, size: f32) -> Self {
        Self {
            value: value.clamp(0.0, 1.0),
            color,
            label: label.into(),
            size,
        }
    }
}

impl Widget for Gauge {
    fn ui(self, ui: &mut Ui) -> Response {
        let desired_size = Vec2::splat(self.size);
        let (rect, response) = ui.allocate_exact_size(desired_size, egui::Sense::hover());

        if ui.is_rect_visible(rect) {
            let painter = ui.painter_at(rect);
            let center = rect.center();
            let radius = self.size * 0.40;
            let stroke_width = self.size * 0.08;

            // Arc sweep: 270 degrees total, starting from bottom-left (135°)
            // sweeping clockwise to bottom-right (45° from top, i.e. 405° = 135° + 270°).
            let start_angle = GAUGE_START_ANGLE;
            let sweep = GAUGE_SWEEP;

            // Background track
            draw_arc(
                &painter,
                center,
                radius,
                start_angle,
                sweep,
                Stroke::new(stroke_width, Color32::from_gray(50)),
            );

            // Foreground filled arc
            if self.value > 0.0 {
                let filled_sweep = sweep * self.value;
                draw_arc(
                    &painter,
                    center,
                    radius,
                    start_angle,
                    filled_sweep,
                    Stroke::new(stroke_width, self.color),
                );
            }

            // Centered label
            let font_size = self.size * 0.22;
            painter.text(
                center,
                egui::Align2::CENTER_CENTER,
                &self.label,
                egui::FontId::monospace(font_size),
                Color32::WHITE,
            );
        }

        response
    }
}

/// Start angle in radians. 135° measured clockwise from the +x axis
/// (i.e. bottom-left of the arc in screen coordinates where y increases downward).
const GAUGE_START_ANGLE: f32 = 3.0 * PI / 4.0;

/// Total sweep of the gauge arc: 270° in radians.
const GAUGE_SWEEP: f32 = 3.0 * PI / 2.0;

/// Draw an anti-aliased arc by emitting line segments.
fn draw_arc(
    painter: &egui::Painter,
    center: Pos2,
    radius: f32,
    start_angle: f32,
    sweep: f32,
    stroke: Stroke,
) {
    // Use enough segments for a smooth arc (~1 segment per 3°).
    let segments = ((sweep.abs() / PI * 60.0).ceil() as usize).max(4);
    let step = sweep / segments as f32;

    let points: Vec<Pos2> = (0..=segments)
        .map(|i| {
            let angle = start_angle + step * i as f32;
            Pos2::new(center.x + radius * angle.cos(), center.y + radius * angle.sin())
        })
        .collect();

    // Use a polyline for anti-aliased rendering.
    painter.add(egui::Shape::line(points, stroke));
}

/// Compute the arc angle (in radians) for a given gauge value.
/// Exposed for testing.
fn arc_angle_for_value(value: f32) -> f32 {
    GAUGE_SWEEP * value.clamp(0.0, 1.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clamps_value_below_zero() {
        let g = Gauge::new(-0.5, Color32::RED, "test", 100.0);
        assert_eq!(g.value, 0.0);
    }

    #[test]
    fn clamps_value_above_one() {
        let g = Gauge::new(1.5, Color32::RED, "test", 100.0);
        assert_eq!(g.value, 1.0);
    }

    #[test]
    fn preserves_valid_value() {
        let g = Gauge::new(0.42, Color32::RED, "42%", 100.0);
        assert!((g.value - 0.42).abs() < f32::EPSILON);
    }

    #[test]
    fn arc_angle_zero_percent() {
        let angle = arc_angle_for_value(0.0);
        assert!((angle - 0.0).abs() < f32::EPSILON);
    }

    #[test]
    fn arc_angle_fifty_percent() {
        let angle = arc_angle_for_value(0.5);
        let expected = GAUGE_SWEEP * 0.5; // 135°
        assert!((angle - expected).abs() < f32::EPSILON);
    }

    #[test]
    fn arc_angle_hundred_percent() {
        let angle = arc_angle_for_value(1.0);
        assert!((angle - GAUGE_SWEEP).abs() < f32::EPSILON);
    }

    #[test]
    fn arc_angle_clamps_out_of_range() {
        assert!((arc_angle_for_value(-1.0) - 0.0).abs() < f32::EPSILON);
        assert!((arc_angle_for_value(2.0) - GAUGE_SWEEP).abs() < f32::EPSILON);
    }
}
