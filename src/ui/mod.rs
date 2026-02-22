// Layout orchestration

pub mod gauge;
pub mod panel;
pub mod settings;
pub mod sparkline;
pub mod specs;

use eframe::egui;

use crate::config::Visualization;
use crate::metrics::MetricsSnapshot;

/// Accent colors for each metric category.
pub struct MetricColors;

impl MetricColors {
    pub const CPU: egui::Color32 = egui::Color32::from_rgb(100, 160, 255); // Bright blue
    pub const RAM: egui::Color32 = egui::Color32::from_rgb(80, 210, 130); // Emerald green
    pub const GPU: egui::Color32 = egui::Color32::from_rgb(240, 90, 90); // Soft red
    pub const NETWORK: egui::Color32 = egui::Color32::from_rgb(255, 175, 50); // Warm orange
    pub const DISK: egui::Color32 = egui::Color32::from_rgb(180, 140, 240); // Lavender purple
}

/// Action returned by the header bar.
pub enum HeaderAction {
    None,
    OpenSettings,
    OpenSpecs,
}

/// Render the header bar with app name, specs button, and gear button.
pub fn render_header(ui: &mut egui::Ui) -> HeaderAction {
    let mut action = HeaderAction::None;

    ui.horizontal(|ui| {
        ui.label(
            egui::RichText::new("PACECAR")
                .size(10.0)
                .color(egui::Color32::from_rgb(120, 120, 140))
                .strong(),
        );

        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
            let gear_btn = ui.add(
                egui::Button::new(
                    egui::RichText::new("\u{2699}")
                        .size(14.0)
                        .color(egui::Color32::from_rgb(150, 150, 170)),
                )
                .frame(false),
            );
            if gear_btn.clicked() {
                action = HeaderAction::OpenSettings;
            }

            let specs_btn = ui.add(
                egui::Button::new(
                    egui::RichText::new("\u{2139}")
                        .size(14.0)
                        .color(egui::Color32::from_rgb(150, 150, 170)),
                )
                .frame(false),
            );
            if specs_btn.clicked() {
                action = HeaderAction::OpenSpecs;
            }
        });
    });

    ui.add_space(2.0);

    // Subtle separator line
    let rect = ui.available_rect_before_wrap();
    let y = rect.top();
    ui.painter().line_segment(
        [
            egui::pos2(rect.left() + 2.0, y),
            egui::pos2(rect.right() - 2.0, y),
        ],
        egui::Stroke::new(0.5, egui::Color32::from_rgb(60, 60, 70)),
    );
    ui.add_space(4.0);

    action
}

/// Calculate the number of columns based on available width.
/// Uses breakpoints: <130 → 1, <250 → 2, ≥250 → 3 (capped).
pub fn column_count(available_width: f32) -> usize {
    if available_width < 130.0 {
        1
    } else if available_width < 250.0 {
        2
    } else {
        3
    }
}

/// Calculate available width per panel given total width and column count.
fn panel_width(available_width: f32, cols: usize) -> f32 {
    let spacing = 8.0;
    let total_spacing = spacing * (cols as f32 - 1.0).max(0.0);
    (available_width - total_spacing) / cols as f32
}

/// Format bytes/sec into a human-readable string (KB/s or MB/s).
pub(crate) fn format_bytes_per_sec(bytes: u64) -> String {
    if bytes >= 1_048_576 {
        format!("{:.1} MB/s", bytes as f64 / 1_048_576.0)
    } else {
        format!("{:.0} KB/s", bytes as f64 / 1_024.0)
    }
}

/// Format bytes/sec as a compact number without units (e.g., "1.1" or "42.9").
/// Uses the same KB/MB threshold as `format_bytes_per_sec`.
pub(crate) fn format_bytes_per_sec_compact(bytes: u64) -> String {
    if bytes >= 1_048_576 {
        format!("{:.1} MB", bytes as f64 / 1_048_576.0)
    } else {
        format!("{:.0} KB", bytes as f64 / 1_024.0)
    }
}

/// Render the full metrics grid layout.
pub fn render_layout(
    ui: &mut egui::Ui,
    snapshot: &MetricsSnapshot,
    visualization: Visualization,
) {
    use panel::MetricPanel;

    let available = ui.available_width();
    let cols = column_count(available);
    let pw = panel_width(available, cols);

    // Pre-format strings that outlive the grid closure.
    let cpu_primary = format!("{:.0}%", snapshot.cpu.total_usage);
    let cpu_secondary = format!("{:.1} GHz", snapshot.cpu.frequency_ghz);

    let ram_used_gb = snapshot.memory.used_bytes as f64 / 1_073_741_824.0;
    let ram_total_gb = snapshot.memory.total_bytes as f64 / 1_073_741_824.0;
    let ram_primary = format!("{:.0}%", snapshot.memory.usage_percent);
    let ram_secondary = format!("{:.1}/{:.0} GB", ram_used_gb, ram_total_gb);

    let gpu_primary;
    let gpu_secondary;
    let gpu_tertiary;
    if let Some(gpu) = &snapshot.gpu {
        gpu_primary = Some(format!("{:.0}%", gpu.usage_percent));
        gpu_secondary = Some(format!("{:.0}\u{00B0}C", gpu.temperature_celsius));
        let vram_used_gb = gpu.vram_used_bytes as f64 / 1_073_741_824.0;
        let vram_total_gb = gpu.vram_total_bytes as f64 / 1_073_741_824.0;
        gpu_tertiary = Some(format!("{:.1}/{:.0} GB", vram_used_gb, vram_total_gb));
    } else {
        gpu_primary = None;
        gpu_secondary = None;
        gpu_tertiary = None;
    }

    let net_total = snapshot.network.upload_bytes_per_sec + snapshot.network.download_bytes_per_sec;
    let net_primary = format_bytes_per_sec(net_total);
    let net_secondary = format!(
        "\u{2191} {}  \u{2193} {}",
        format_bytes_per_sec_compact(snapshot.network.upload_bytes_per_sec),
        format_bytes_per_sec_compact(snapshot.network.download_bytes_per_sec),
    );

    let disk_total = snapshot.disk.read_bytes_per_sec + snapshot.disk.write_bytes_per_sec;
    let disk_primary = format_bytes_per_sec(disk_total);
    let disk_secondary = format!(
        "R: {}  W: {}",
        format_bytes_per_sec_compact(snapshot.disk.read_bytes_per_sec),
        format_bytes_per_sec_compact(snapshot.disk.write_bytes_per_sec),
    );

    let mut panels_added = 0usize;

    egui::Grid::new("metrics_grid")
        .num_columns(cols)
        .spacing(egui::vec2(8.0, 8.0))
        .show(ui, |ui| {
            // CPU
            ui.add(
                MetricPanel::new("CPU", &cpu_primary, MetricColors::CPU)
                    .secondary_value(&cpu_secondary)
                    .gauge_value(snapshot.cpu.total_usage)
                    .visualization(visualization)
                    .panel_width(pw),
            );
            panels_added += 1;
            if panels_added % cols == 0 { ui.end_row(); }

            // RAM
            ui.add(
                MetricPanel::new("RAM", &ram_primary, MetricColors::RAM)
                    .secondary_value(&ram_secondary)
                    .gauge_value(snapshot.memory.usage_percent)
                    .visualization(visualization)
                    .panel_width(pw),
            );
            panels_added += 1;
            if panels_added % cols == 0 { ui.end_row(); }

            // GPU (conditional)
            if let (Some(gpu), Some(gp), Some(gs)) =
                (&snapshot.gpu, &gpu_primary, &gpu_secondary)
            {
                let mut gpu_panel = MetricPanel::new("GPU", gp, MetricColors::GPU)
                    .secondary_value(gs)
                    .gauge_value(gpu.usage_percent)
                    .visualization(visualization)
                    .panel_width(pw);
                if let Some(ref gt) = gpu_tertiary {
                    gpu_panel = gpu_panel.tertiary_value(gt);
                }
                ui.add(gpu_panel);
                panels_added += 1;
                if panels_added % cols == 0 { ui.end_row(); }
            }

            // Network
            ui.add(
                MetricPanel::new("Network", &net_primary, MetricColors::NETWORK)
                    .secondary_value(&net_secondary)
                    .visualization(visualization)
                    .panel_width(pw),
            );
            panels_added += 1;
            if panels_added % cols == 0 { ui.end_row(); }

            // Disk I/O
            ui.add(
                MetricPanel::new("Disk I/O", &disk_primary, MetricColors::DISK)
                    .secondary_value(&disk_secondary)
                    .visualization(visualization)
                    .panel_width(pw),
            );
        });
}

/// Configure custom dark-theme visuals for the app.
pub fn configure_visuals(ctx: &egui::Context) {
    let mut visuals = egui::Visuals::dark();

    // Transparent panel background (the overlay itself draws its own bg)
    visuals.panel_fill = egui::Color32::from_rgba_unmultiplied(20, 20, 24, 0);
    // Opaque window fill so context menus and popups have a visible background
    visuals.window_fill = egui::Color32::from_rgb(36, 36, 42);

    // Subtle widget styling
    visuals.widgets.noninteractive.bg_fill = egui::Color32::from_rgb(35, 35, 40);
    visuals.widgets.noninteractive.fg_stroke =
        egui::Stroke::new(1.0, egui::Color32::from_gray(190));
    visuals.widgets.inactive.bg_fill = egui::Color32::from_rgb(45, 45, 50);
    visuals.widgets.hovered.bg_fill = egui::Color32::from_rgb(55, 55, 62);

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
    fn column_count_very_narrow_window() {
        assert_eq!(column_count(100.0), 1);
        assert_eq!(column_count(129.0), 1);
    }

    #[test]
    fn column_count_narrow_window() {
        assert_eq!(column_count(130.0), 2);
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

    /// Count expected panels for a snapshot (GPU is conditional).
    fn expected_panel_count(snapshot: &MetricsSnapshot) -> usize {
        if snapshot.gpu.is_some() { 5 } else { 4 }
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
        assert_eq!(expected_panel_count(&snapshot), 5);
    }

    #[test]
    fn gpu_panel_excluded_when_none() {
        let snapshot = make_snapshot(None);
        assert_eq!(expected_panel_count(&snapshot), 4);
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

    #[test]
    fn format_bytes_per_sec_compact_kilobytes() {
        assert_eq!(format_bytes_per_sec_compact(512_000), "500 KB");
    }

    #[test]
    fn format_bytes_per_sec_compact_megabytes() {
        assert_eq!(format_bytes_per_sec_compact(10_485_760), "10.0 MB");
    }

    #[test]
    fn format_bytes_per_sec_compact_zero() {
        assert_eq!(format_bytes_per_sec_compact(0), "0 KB");
    }
}
