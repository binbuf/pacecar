// Individual metric panel (gauge or sparkline + value)

use eframe::egui::{self, Color32, Response, Ui, Vec2, Widget};

use crate::config::Visualization;

use super::gauge::Gauge;
use super::sparkline::Sparkline;

/// Gauge diameter in pixels.
const GAUGE_SIZE: f32 = 60.0;
/// Sparkline widget dimensions.
const SPARKLINE_SIZE: Vec2 = Vec2::new(80.0, 30.0);
/// Minimum panel width to keep grid alignment consistent.
const MIN_PANEL_WIDTH: f32 = 80.0;

/// A composite metric panel combining a visualization (gauge or sparkline)
/// with a label, primary value, and optional secondary value.
pub struct MetricPanel<'a> {
    /// Metric label ("CPU", "RAM", etc.).
    pub label: &'a str,
    /// Primary display value ("42%", "10/16 GB", etc.).
    pub primary_value: &'a str,
    /// Optional secondary value ("3.8 GHz", upload/download speeds).
    pub secondary_value: Option<&'a str>,
    /// Normalized value (0.0–1.0) for gauge mode. `None` for metrics
    /// without a natural percentage (Network, Disk).
    pub gauge_value: Option<f32>,
    /// Historical values for sparkline mode.
    pub sparkline_history: Option<&'a [f32]>,
    /// Y-axis range for sparkline scaling.
    pub sparkline_range: (f32, f32),
    /// Accent color for this metric.
    pub color: Color32,
    /// Current visualization mode.
    pub visualization: Visualization,
}

impl<'a> MetricPanel<'a> {
    pub fn new(label: &'a str, primary_value: &'a str, color: Color32) -> Self {
        Self {
            label,
            primary_value,
            secondary_value: None,
            gauge_value: None,
            sparkline_history: None,
            sparkline_range: (0.0, 100.0),
            color,
            visualization: Visualization::Gauges,
        }
    }

    pub fn secondary_value(mut self, value: &'a str) -> Self {
        self.secondary_value = Some(value);
        self
    }

    pub fn gauge_value(mut self, value: f32) -> Self {
        self.gauge_value = Some(value);
        self
    }

    pub fn sparkline(mut self, history: &'a [f32], range: (f32, f32)) -> Self {
        self.sparkline_history = Some(history);
        self.sparkline_range = range;
        self
    }

    pub fn visualization(mut self, vis: Visualization) -> Self {
        self.visualization = vis;
        self
    }

    /// Determine which visualization branch this panel will render.
    pub(crate) fn vis_branch(&self) -> VisBranch {
        match self.visualization {
            Visualization::Gauges => {
                if self.gauge_value.is_some() {
                    VisBranch::Gauge
                } else {
                    VisBranch::TextOnly
                }
            }
            Visualization::Sparklines => {
                if self.sparkline_history.is_some() {
                    VisBranch::Sparkline
                } else if self.gauge_value.is_some() {
                    // Fallback: show gauge even in sparkline mode if no history
                    VisBranch::Gauge
                } else {
                    VisBranch::TextOnly
                }
            }
        }
    }
}

/// Which visualization sub-widget a panel will render.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum VisBranch {
    Gauge,
    Sparkline,
    TextOnly,
}

impl<'a> Widget for MetricPanel<'a> {
    fn ui(self, ui: &mut Ui) -> Response {
        let panel_frame = egui::Frame::none()
            .fill(Color32::from_rgba_unmultiplied(40, 40, 40, 200))
            .rounding(6.0)
            .inner_margin(8.0)
            .stroke(egui::Stroke::new(1.0, self.color.linear_multiply(0.4)));

        panel_frame
            .show(ui, |ui| {
                ui.set_min_width(MIN_PANEL_WIDTH);
                ui.vertical(|ui| {
                    // Visualization widget
                    match self.vis_branch() {
                        VisBranch::Gauge => {
                            let value = self.gauge_value.unwrap_or(0.0) / 100.0;
                            ui.add(Gauge::new(
                                value,
                                self.color,
                                self.primary_value,
                                GAUGE_SIZE,
                            ));
                        }
                        VisBranch::Sparkline => {
                            let history = self.sparkline_history.unwrap_or(&[]);
                            ui.add(Sparkline::new(
                                history,
                                self.color,
                                SPARKLINE_SIZE,
                                self.sparkline_range,
                            ));
                        }
                        VisBranch::TextOnly => {
                            // No chart widget; values are shown below.
                        }
                    }

                    ui.add_space(2.0);

                    // Label
                    ui.label(
                        egui::RichText::new(self.label)
                            .color(self.color)
                            .size(11.0)
                            .strong(),
                    );

                    // Primary value (large monospace)
                    ui.label(
                        egui::RichText::new(self.primary_value)
                            .color(Color32::WHITE)
                            .size(16.0)
                            .monospace(),
                    );

                    // Secondary value (small, dimmed)
                    if let Some(secondary) = self.secondary_value {
                        ui.label(
                            egui::RichText::new(secondary)
                                .color(Color32::from_gray(180))
                                .size(10.0)
                                .monospace(),
                        );
                    }
                });
            })
            .response
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn gauge_mode_with_gauge_value_selects_gauge() {
        let panel = MetricPanel::new("CPU", "42%", Color32::BLUE)
            .gauge_value(42.0)
            .visualization(Visualization::Gauges);
        assert_eq!(panel.vis_branch(), VisBranch::Gauge);
    }

    #[test]
    fn gauge_mode_without_gauge_value_selects_text_only() {
        let panel = MetricPanel::new("Network", "\u{2191} 1.2 MB/s", Color32::YELLOW)
            .visualization(Visualization::Gauges);
        assert_eq!(panel.vis_branch(), VisBranch::TextOnly);
    }

    #[test]
    fn sparkline_mode_with_history_selects_sparkline() {
        let history = [10.0, 20.0, 30.0];
        let panel = MetricPanel::new("CPU", "42%", Color32::BLUE)
            .gauge_value(42.0)
            .sparkline(&history, (0.0, 100.0))
            .visualization(Visualization::Sparklines);
        assert_eq!(panel.vis_branch(), VisBranch::Sparkline);
    }

    #[test]
    fn sparkline_mode_without_history_falls_back_to_gauge() {
        let panel = MetricPanel::new("CPU", "42%", Color32::BLUE)
            .gauge_value(42.0)
            .visualization(Visualization::Sparklines);
        assert_eq!(panel.vis_branch(), VisBranch::Gauge);
    }

    #[test]
    fn sparkline_mode_no_history_no_gauge_selects_text_only() {
        let panel = MetricPanel::new("Network", "\u{2191} 1.2 MB/s", Color32::YELLOW)
            .visualization(Visualization::Sparklines);
        assert_eq!(panel.vis_branch(), VisBranch::TextOnly);
    }

    #[test]
    fn snapshot_cpu_gauge_panel() {
        let panel = MetricPanel::new("CPU", "42%", Color32::from_rgb(100, 149, 237))
            .secondary_value("3.8 GHz")
            .gauge_value(42.0)
            .visualization(Visualization::Gauges);
        insta::assert_debug_snapshot!("cpu_gauge_panel", panel_snapshot(&panel));
    }

    #[test]
    fn snapshot_ram_gauge_panel() {
        let panel = MetricPanel::new("RAM", "50%", Color32::from_rgb(80, 200, 120))
            .secondary_value("7.5/16 GB")
            .gauge_value(50.0)
            .visualization(Visualization::Gauges);
        insta::assert_debug_snapshot!("ram_gauge_panel", panel_snapshot(&panel));
    }

    #[test]
    fn snapshot_gpu_sparkline_panel() {
        let history = [20.0, 25.0, 30.0, 28.0];
        let panel = MetricPanel::new("GPU", "28%", Color32::from_rgb(230, 80, 80))
            .secondary_value("72\u{00b0}C")
            .gauge_value(28.0)
            .sparkline(&history, (0.0, 100.0))
            .visualization(Visualization::Sparklines);
        insta::assert_debug_snapshot!("gpu_sparkline_panel", panel_snapshot(&panel));
    }

    #[test]
    fn snapshot_network_text_panel() {
        let panel = MetricPanel::new("Network", "\u{2191} 1.1 MB/s", Color32::from_rgb(255, 165, 0))
            .secondary_value("\u{2193} 11.4 MB/s")
            .visualization(Visualization::Gauges);
        insta::assert_debug_snapshot!("network_text_panel", panel_snapshot(&panel));
    }

    #[test]
    fn snapshot_disk_text_panel() {
        let panel = MetricPanel::new("Disk I/O", "R: 42.9 MB/s", Color32::from_rgb(180, 130, 230))
            .secondary_value("W: 11.4 MB/s")
            .visualization(Visualization::Gauges);
        insta::assert_debug_snapshot!("disk_text_panel", panel_snapshot(&panel));
    }

    /// Capture testable panel state for snapshot comparison.
    #[derive(Debug)]
    #[allow(dead_code)]
    struct PanelSnapshot {
        label: String,
        primary_value: String,
        secondary_value: Option<String>,
        vis_branch: VisBranch,
        color_rgb: (u8, u8, u8),
    }

    fn panel_snapshot(panel: &MetricPanel<'_>) -> PanelSnapshot {
        PanelSnapshot {
            label: panel.label.to_string(),
            primary_value: panel.primary_value.to_string(),
            secondary_value: panel.secondary_value.map(|s| s.to_string()),
            vis_branch: panel.vis_branch(),
            color_rgb: (panel.color.r(), panel.color.g(), panel.color.b()),
        }
    }
}
