// Layout orchestration

pub mod gauge;
pub mod panel;
pub mod settings;
pub mod sparkline;

use eframe::egui;

use crate::config::Visualization;
use crate::metrics::MetricsSnapshot;

/// Accent colors for each metric category.
pub struct MetricColors;

impl MetricColors {
    pub const CPU: egui::Color32 = egui::Color32::from_rgb(100, 149, 237); // Cornflower blue
    pub const RAM: egui::Color32 = egui::Color32::from_rgb(80, 200, 120); // Emerald green
    pub const GPU: egui::Color32 = egui::Color32::from_rgb(230, 80, 80); // Soft red
    pub const NETWORK: egui::Color32 = egui::Color32::from_rgb(255, 165, 0); // Orange
    pub const DISK: egui::Color32 = egui::Color32::from_rgb(180, 130, 230); // Lavender purple
}

/// Calculate the number of columns based on available width.
/// Uses breakpoints: <250 → 2, <400 → 3, ≥400 → 3 (capped).
pub fn column_count(available_width: f32) -> usize {
    if available_width < 250.0 {
        2
    } else {
        3
    }
}

/// Determine which panels to show. Returns labels, accent colors, and display lines
/// for each visible panel.
fn build_panels(snapshot: &MetricsSnapshot) -> Vec<PanelData> {
    let mut panels = Vec::with_capacity(5);

    // CPU
    panels.push(PanelData {
        label: "CPU",
        accent: MetricColors::CPU,
        value_percent: Some(snapshot.cpu.total_usage),
        line1: format!("{:.0}%", snapshot.cpu.total_usage),
        line2: format!("{:.1} GHz", snapshot.cpu.frequency_ghz),
    });

    // RAM
    let ram_used_gb = snapshot.memory.used_bytes as f64 / 1_073_741_824.0;
    let ram_total_gb = snapshot.memory.total_bytes as f64 / 1_073_741_824.0;
    panels.push(PanelData {
        label: "RAM",
        accent: MetricColors::RAM,
        value_percent: Some(snapshot.memory.usage_percent),
        line1: format!("{:.0}%", snapshot.memory.usage_percent),
        line2: format!("{:.1}/{:.0} GB", ram_used_gb, ram_total_gb),
    });

    // GPU (only if present)
    if let Some(gpu) = &snapshot.gpu {
        panels.push(PanelData {
            label: "GPU",
            accent: MetricColors::GPU,
            value_percent: Some(gpu.usage_percent),
            line1: format!("{:.0}%", gpu.usage_percent),
            line2: format!("{:.0}\u{00B0}C", gpu.temperature_celsius),
        });
    }

    // Network
    let up = format_bytes_per_sec(snapshot.network.upload_bytes_per_sec);
    let down = format_bytes_per_sec(snapshot.network.download_bytes_per_sec);
    panels.push(PanelData {
        label: "Network",
        accent: MetricColors::NETWORK,
        value_percent: None,
        line1: format!("\u{2191} {up}"),
        line2: format!("\u{2193} {down}"),
    });

    // Disk I/O
    let read = format_bytes_per_sec(snapshot.disk.read_bytes_per_sec);
    let write = format_bytes_per_sec(snapshot.disk.write_bytes_per_sec);
    panels.push(PanelData {
        label: "Disk I/O",
        accent: MetricColors::DISK,
        value_percent: None,
        line1: format!("R: {read}"),
        line2: format!("W: {write}"),
    });

    panels
}

struct PanelData {
    label: &'static str,
    accent: egui::Color32,
    value_percent: Option<f32>,
    line1: String,
    line2: String,
}

/// Format bytes/sec into a human-readable string (KB/s or MB/s).
fn format_bytes_per_sec(bytes: u64) -> String {
    if bytes >= 1_048_576 {
        format!("{:.1} MB/s", bytes as f64 / 1_048_576.0)
    } else {
        format!("{:.0} KB/s", bytes as f64 / 1_024.0)
    }
}

/// Render the full metrics grid layout.
pub fn render_layout(
    ui: &mut egui::Ui,
    snapshot: &MetricsSnapshot,
    _visualization: Visualization,
) {
    let panels = build_panels(snapshot);
    let cols = column_count(ui.available_width());

    egui::Grid::new("metrics_grid")
        .num_columns(cols)
        .spacing(egui::vec2(8.0, 8.0))
        .show(ui, |ui| {
            for (i, panel) in panels.iter().enumerate() {
                render_panel(ui, panel);
                if (i + 1) % cols == 0 {
                    ui.end_row();
                }
            }
        });
}

/// Render a single metric panel.
fn render_panel(ui: &mut egui::Ui, data: &PanelData) {
    let panel_frame = egui::Frame::none()
        .fill(egui::Color32::from_rgba_unmultiplied(40, 40, 40, 200))
        .rounding(6.0)
        .inner_margin(8.0)
        .stroke(egui::Stroke::new(1.0, data.accent.linear_multiply(0.4)));

    panel_frame.show(ui, |ui| {
        ui.set_min_width(80.0);
        ui.vertical(|ui| {
            // Label
            ui.label(
                egui::RichText::new(data.label)
                    .color(data.accent)
                    .size(11.0)
                    .strong(),
            );
            ui.add_space(2.0);

            // Primary value (monospace)
            ui.label(
                egui::RichText::new(&data.line1)
                    .color(egui::Color32::WHITE)
                    .size(16.0)
                    .monospace(),
            );

            // Secondary value (monospace, dimmed)
            ui.label(
                egui::RichText::new(&data.line2)
                    .color(egui::Color32::from_gray(180))
                    .size(10.0)
                    .monospace(),
            );
        });
    });
}

/// Configure custom dark-theme visuals for the app.
pub fn configure_visuals(ctx: &egui::Context) {
    let mut visuals = egui::Visuals::dark();

    // Darker background
    visuals.panel_fill = egui::Color32::from_rgba_unmultiplied(20, 20, 20, 0);
    visuals.window_fill = egui::Color32::from_rgba_unmultiplied(20, 20, 20, 0);

    // Subtle widget styling
    visuals.widgets.noninteractive.bg_fill = egui::Color32::from_gray(35);
    visuals.widgets.noninteractive.fg_stroke =
        egui::Stroke::new(1.0, egui::Color32::from_gray(180));

    // Compact spacing
    let mut style = (*ctx.style()).clone();
    style.spacing.item_spacing = egui::vec2(4.0, 4.0);
    style.spacing.window_margin = egui::Margin::same(6.0);
    style.visuals = visuals;
    ctx.set_style(style);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::metrics::cpu::CpuMetrics;
    use crate::metrics::disk::DiskMetrics;
    use crate::metrics::gpu::GpuMetrics;
    use crate::metrics::memory::MemoryMetrics;
    use crate::metrics::network::NetworkMetrics;
    use std::time::Instant;

    fn make_snapshot(gpu: Option<GpuMetrics>) -> MetricsSnapshot {
        MetricsSnapshot {
            timestamp: Instant::now(),
            cpu: CpuMetrics {
                total_usage: 42.0,
                per_core_usage: vec![40.0, 44.0],
                frequency_ghz: 3.8,
            },
            memory: MemoryMetrics {
                used_bytes: 8_000_000_000,
                total_bytes: 16_000_000_000,
                usage_percent: 50.0,
            },
            gpu,
            network: NetworkMetrics {
                upload_bytes_per_sec: 1_200_000,
                download_bytes_per_sec: 12_000_000,
            },
            disk: DiskMetrics {
                read_bytes_per_sec: 45_000_000,
                write_bytes_per_sec: 12_000_000,
            },
        }
    }

    #[test]
    fn column_count_narrow_window() {
        assert_eq!(column_count(200.0), 2);
        assert_eq!(column_count(249.0), 2);
    }

    #[test]
    fn column_count_medium_window() {
        assert_eq!(column_count(250.0), 3);
        assert_eq!(column_count(350.0), 3);
    }

    #[test]
    fn column_count_wide_window() {
        assert_eq!(column_count(500.0), 3);
        assert_eq!(column_count(1000.0), 3);
    }

    #[test]
    fn gpu_panel_included_when_present() {
        let gpu = GpuMetrics {
            usage_percent: 28.0,
            temperature_celsius: 72.0,
            vram_used_bytes: 2_000_000_000,
            vram_total_bytes: 8_000_000_000,
        };
        let snapshot = make_snapshot(Some(gpu));
        let panels = build_panels(&snapshot);
        assert_eq!(panels.len(), 5);
        assert!(panels.iter().any(|p| p.label == "GPU"));
    }

    #[test]
    fn gpu_panel_excluded_when_none() {
        let snapshot = make_snapshot(None);
        let panels = build_panels(&snapshot);
        assert_eq!(panels.len(), 4);
        assert!(!panels.iter().any(|p| p.label == "GPU"));
    }

    #[test]
    fn panel_order_without_gpu() {
        let snapshot = make_snapshot(None);
        let panels = build_panels(&snapshot);
        let labels: Vec<&str> = panels.iter().map(|p| p.label).collect();
        assert_eq!(labels, vec!["CPU", "RAM", "Network", "Disk I/O"]);
    }

    #[test]
    fn panel_order_with_gpu() {
        let gpu = GpuMetrics {
            usage_percent: 50.0,
            temperature_celsius: 65.0,
            vram_used_bytes: 1_000_000_000,
            vram_total_bytes: 4_000_000_000,
        };
        let snapshot = make_snapshot(Some(gpu));
        let panels = build_panels(&snapshot);
        let labels: Vec<&str> = panels.iter().map(|p| p.label).collect();
        assert_eq!(labels, vec!["CPU", "RAM", "GPU", "Network", "Disk I/O"]);
    }

    #[test]
    fn format_bytes_per_sec_kilobytes() {
        assert_eq!(format_bytes_per_sec(512_000), "500 KB/s");
    }

    #[test]
    fn format_bytes_per_sec_megabytes() {
        assert_eq!(format_bytes_per_sec(10_485_760), "10.0 MB/s");
    }

    #[test]
    fn format_bytes_per_sec_zero() {
        assert_eq!(format_bytes_per_sec(0), "0 KB/s");
    }
}
