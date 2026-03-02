// Individual metric panel (gauge or sparkline + value)

use eframe::egui::{self, Color32, Response, Ui, Vec2, Widget};

use crate::config::Visualization;

use super::gauge::Gauge;
use super::sparkline::Sparkline;

/// Minimum panel width to keep grid alignment consistent.
const MIN_PANEL_WIDTH: f32 = 60.0;

/// Panel width below which we switch to compact (text-only) mode.
const COMPACT_THRESHOLD: f32 = 90.0;

/// A composite metric panel combining a visualization (gauge or sparkline)
/// with a label, primary value, and optional secondary value.
pub struct MetricPanel<'a> {
    /// Metric label ("CPU", "RAM", etc.).
    pub label: &'a str,
    /// Primary display value ("42%", "10/16 GB", etc.).
    pub primary_value: &'a str,
    /// Optional secondary value ("3.8 GHz", upload/download speeds).
    pub secondary_value: Option<&'a str>,
    /// Optional tertiary value ("4.3/8 GB" VRAM, etc.).
    pub tertiary_value: Option<&'a str>,
    /// Optional quaternary value (e.g. CPU fan RPM on its own line).
    pub quaternary_value: Option<&'a str>,
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
    /// Available width for this panel (used for responsive sizing).
    pub panel_width: f32,
    /// Optional mini sparkline data (shown below tile content).
    pub mini_sparkline: Option<&'a [f32]>,
    /// Y-axis range for the mini sparkline.
    pub mini_sparkline_range: (f32, f32),
}

impl<'a> MetricPanel<'a> {
    pub fn new(label: &'a str, primary_value: &'a str, color: Color32) -> Self {
        Self {
            label,
            primary_value,
            secondary_value: None,
            tertiary_value: None,
            quaternary_value: None,
            gauge_value: None,
            sparkline_history: None,
            sparkline_range: (0.0, 100.0),
            color,
            visualization: Visualization::Gauges,
            panel_width: 140.0,
            mini_sparkline: None,
            mini_sparkline_range: (0.0, 100.0),
        }
    }

    pub fn secondary_value(mut self, value: &'a str) -> Self {
        self.secondary_value = Some(value);
        self
    }

    pub fn tertiary_value(mut self, value: &'a str) -> Self {
        self.tertiary_value = Some(value);
        self
    }

    pub fn quaternary_value(mut self, value: &'a str) -> Self {
        self.quaternary_value = Some(value);
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

    pub fn panel_width(mut self, width: f32) -> Self {
        self.panel_width = width;
        self
    }

    pub fn visualization(mut self, vis: Visualization) -> Self {
        self.visualization = vis;
        self
    }

    pub fn mini_sparkline(mut self, data: &'a [f32], range: (f32, f32)) -> Self {
        self.mini_sparkline = Some(data);
        self.mini_sparkline_range = range;
        self
    }

    /// Determine which visualization branch this panel will render.
    pub(crate) fn vis_branch(&self) -> VisBranch {
        // Compact mode: skip visualization when panel is too narrow
        if self.panel_width < COMPACT_THRESHOLD {
            return VisBranch::TextOnly;
        }

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
        let compact = self.panel_width < COMPACT_THRESHOLD;
        let inner_margin = if compact { 4.0 } else { (self.panel_width * 0.07).clamp(6.0, 10.0) };

        let panel_frame = egui::Frame::NONE
            .fill(Color32::from_rgba_unmultiplied(34, 34, 40, 210))
            .corner_radius(if compact { 4.0 } else { 8.0 })
            .inner_margin(inner_margin)
            .stroke(egui::Stroke::new(1.0, self.color.linear_multiply(0.3)));

        // Scale sizes based on panel width
        let usable_width = (self.panel_width - inner_margin * 2.0).max(40.0);
        let gauge_size = (usable_width * 0.55).clamp(36.0, 80.0);
        let sparkline_size = Vec2::new(
            usable_width.clamp(50.0, 120.0),
            (usable_width * 0.3).clamp(20.0, 40.0),
        );

        // Scale font sizes
        let label_size = if compact { 9.0 } else { (self.panel_width * 0.08).clamp(9.0, 12.0) };
        let primary_size = if compact { 12.0 } else { (self.panel_width * 0.11).clamp(12.0, 18.0) };
        let secondary_size = if compact { 8.0 } else { (self.panel_width * 0.07).clamp(8.0, 11.0) };

        panel_frame
            .show(ui, |ui| {
                // Claim full allocated column width so tiles don't shift
                ui.set_width((self.panel_width - inner_margin * 2.0).max(MIN_PANEL_WIDTH));

                // Set minimum height to prevent height jitter
                let vis_branch = self.vis_branch();
                let mini_extra = if self.mini_sparkline.is_some() { 16.0 } else { 0.0 };
                let min_height = mini_extra + match vis_branch {
                    VisBranch::TextOnly => {
                        // label + primary + secondary + spacing
                        label_size + primary_size + secondary_size + 16.0
                    }
                    VisBranch::Gauge => {
                        // gauge + spacing + label + primary + secondary
                        gauge_size + label_size + primary_size + secondary_size + 20.0
                    }
                    VisBranch::Sparkline => {
                        // sparkline + spacing + label + primary + secondary
                        sparkline_size.y + label_size + primary_size + secondary_size + 20.0
                    }
                };
                ui.set_min_height(min_height);

                ui.vertical(|ui| {
                    // Visualization widget
                    match vis_branch {
                        VisBranch::Gauge => {
                            let value = self.gauge_value.unwrap_or(0.0) / 100.0;
                            ui.add(Gauge::new(
                                value,
                                self.color,
                                self.primary_value,
                                gauge_size,
                            ));
                        }
                        VisBranch::Sparkline => {
                            let history = self.sparkline_history.unwrap_or(&[]);
                            ui.add(Sparkline::new(
                                history,
                                self.color,
                                sparkline_size,
                                self.sparkline_range,
                            ));
                        }
                        VisBranch::TextOnly => {
                            // No chart widget; values are shown below.
                        }
                    }

                    if !compact {
                        ui.add_space(2.0);
                    }

                    // Label
                    ui.label(
                        egui::RichText::new(self.label)
                            .color(self.color)
                            .size(label_size)
                            .strong(),
                    );

                    // Primary value (large monospace, wrapping)
                    ui.add(egui::Label::new(
                        egui::RichText::new(self.primary_value)
                            .color(Color32::WHITE)
                            .size(primary_size)
                            .monospace(),
                    ).wrap());

                    // Secondary value (small, dimmed, wrapping)
                    if let Some(secondary) = self.secondary_value {
                        ui.add(egui::Label::new(
                            egui::RichText::new(secondary)
                                .color(Color32::from_gray(180))
                                .size(secondary_size)
                                .monospace(),
                        ).wrap());
                    }

                    // Tertiary value (small, dimmed, wrapping)
                    if let Some(tertiary) = self.tertiary_value {
                        ui.add(egui::Label::new(
                            egui::RichText::new(tertiary)
                                .color(Color32::from_gray(160))
                                .size(secondary_size)
                                .monospace(),
                        ).wrap());
                    }

                    // Quaternary value (small, dimmed, wrapping)
                    if let Some(quaternary) = self.quaternary_value {
                        ui.add(egui::Label::new(
                            egui::RichText::new(quaternary)
                                .color(Color32::from_gray(160))
                                .size(secondary_size)
                                .monospace(),
                        ).wrap());
                    }

                    // Mini sparkline (small chart below tile content)
                    if let Some(data) = self.mini_sparkline {
                        if data.len() >= 2 {
                            let dimmed = Color32::from_rgba_unmultiplied(
                                self.color.r(),
                                self.color.g(),
                                self.color.b(),
                                140,
                            );
                            ui.add(Sparkline::new(
                                data,
                                dimmed,
                                Vec2::new(usable_width.clamp(40.0, 120.0), 12.0),
                                self.mini_sparkline_range,
                            ));
                        }
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
            .panel_width(140.0)
            .visualization(Visualization::Gauges);
        assert_eq!(panel.vis_branch(), VisBranch::Gauge);
    }

    #[test]
    fn gauge_mode_without_gauge_value_selects_text_only() {
        let panel = MetricPanel::new("Network", "1.2 MB/s", Color32::YELLOW)
            .panel_width(140.0)
            .visualization(Visualization::Gauges);
        assert_eq!(panel.vis_branch(), VisBranch::TextOnly);
    }

    #[test]
    fn sparkline_mode_with_history_selects_sparkline() {
        let history = [10.0, 20.0, 30.0];
        let panel = MetricPanel::new("CPU", "42%", Color32::BLUE)
            .gauge_value(42.0)
            .sparkline(&history, (0.0, 100.0))
            .panel_width(140.0)
            .visualization(Visualization::Sparklines);
        assert_eq!(panel.vis_branch(), VisBranch::Sparkline);
    }

    #[test]
    fn sparkline_mode_without_history_falls_back_to_gauge() {
        let panel = MetricPanel::new("CPU", "42%", Color32::BLUE)
            .gauge_value(42.0)
            .panel_width(140.0)
            .visualization(Visualization::Sparklines);
        assert_eq!(panel.vis_branch(), VisBranch::Gauge);
    }

    #[test]
    fn sparkline_mode_no_history_no_gauge_selects_text_only() {
        let panel = MetricPanel::new("Network", "1.2 MB/s", Color32::YELLOW)
            .panel_width(140.0)
            .visualization(Visualization::Sparklines);
        assert_eq!(panel.vis_branch(), VisBranch::TextOnly);
    }

    #[test]
    fn compact_mode_forces_text_only() {
        let panel = MetricPanel::new("CPU", "42%", Color32::BLUE)
            .gauge_value(42.0)
            .panel_width(80.0)
            .visualization(Visualization::Gauges);
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
            .tertiary_value("1.9/8 GB")
            .gauge_value(28.0)
            .sparkline(&history, (0.0, 100.0))
            .visualization(Visualization::Sparklines);
        insta::assert_debug_snapshot!("gpu_sparkline_panel", panel_snapshot(&panel));
    }

    #[test]
    fn snapshot_network_text_panel() {
        let panel = MetricPanel::new("Network", "12.5 MB/s", Color32::from_rgb(255, 165, 0))
            .secondary_value("\u{2191} 1.1 MB  \u{2193} 11.4 MB")
            .visualization(Visualization::Gauges);
        insta::assert_debug_snapshot!("network_text_panel", panel_snapshot(&panel));
    }

    #[test]
    fn snapshot_disk_text_panel() {
        let panel = MetricPanel::new("Disk I/O", "54.3 MB/s", Color32::from_rgb(180, 130, 230))
            .secondary_value("R: 42.9 MB  W: 11.4 MB")
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
        tertiary_value: Option<String>,
        vis_branch: VisBranch,
        color_rgb: (u8, u8, u8),
    }

    fn panel_snapshot(panel: &MetricPanel<'_>) -> PanelSnapshot {
        PanelSnapshot {
            label: panel.label.to_string(),
            primary_value: panel.primary_value.to_string(),
            secondary_value: panel.secondary_value.map(|s| s.to_string()),
            tertiary_value: panel.tertiary_value.map(|s| s.to_string()),
            vis_branch: panel.vis_branch(),
            color_rgb: (panel.color.r(), panel.color.g(), panel.color.b()),
        }
    }
}
