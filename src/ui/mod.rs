// Layout orchestration

pub mod gauge;
pub mod history;
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
    pub const PING: egui::Color32 = egui::Color32::from_rgb(80, 210, 210); // Teal/cyan
    pub const FANS: egui::Color32 = egui::Color32::from_rgb(230, 160, 180); // Soft pink
    pub const MAINBOARD: egui::Color32 = egui::Color32::from_rgb(200, 180, 120); // Warm gold
}

/// Action returned by the header bar.
pub enum HeaderAction {
    None,
    OpenSettings,
    OpenSpecs,
    SetPreset(crate::config::LayoutPreset),
    ToggleMiniSparklines,
    OpenHistory,
}

/// Render the header bar with app name, preset buttons, specs button, and gear button.
pub fn render_header(
    ui: &mut egui::Ui,
    current_preset: crate::config::LayoutPreset,
    mini_sparklines_enabled: bool,
) -> HeaderAction {
    use crate::config::LayoutPreset;

    let mut action = HeaderAction::None;

    ui.horizontal(|ui| {
        ui.label(
            egui::RichText::new("PACECAR")
                .size(10.0)
                .color(egui::Color32::from_rgb(120, 120, 140))
                .strong(),
        );

        // Preset buttons (small text toggles)
        let dim = egui::Color32::from_rgb(100, 100, 115);
        let bright = egui::Color32::from_rgb(180, 180, 200);

        let wide_color = if current_preset == LayoutPreset::Wide { bright } else { dim };
        let skinny_color = if current_preset == LayoutPreset::Skinny { bright } else { dim };

        if ui.add(egui::Button::new(
            egui::RichText::new("W").size(9.0).color(wide_color).strong(),
        ).frame(false)).on_hover_text("Wide layout (4 columns)").clicked() {
            action = HeaderAction::SetPreset(
                if current_preset == LayoutPreset::Wide { LayoutPreset::Auto } else { LayoutPreset::Wide }
            );
        }
        if ui.add(egui::Button::new(
            egui::RichText::new("S").size(9.0).color(skinny_color).strong(),
        ).frame(false)).on_hover_text("Skinny layout (1 column)").clicked() {
            action = HeaderAction::SetPreset(
                if current_preset == LayoutPreset::Skinny { LayoutPreset::Auto } else { LayoutPreset::Skinny }
            );
        }

        // Sparkline toggle button
        let spark_color = if mini_sparklines_enabled { bright } else { dim };
        if ui.add(egui::Button::new(
            egui::RichText::new("\u{2248}").size(11.0).color(spark_color).strong(), // ≈ symbol
        ).frame(false)).on_hover_text("Toggle mini sparklines").clicked() {
            action = HeaderAction::ToggleMiniSparklines;
        }

        // History window button (▤ = U+25A4, square with horizontal fill)
        if ui.add(egui::Button::new(
            egui::RichText::new("H").size(9.0).color(dim).strong(),
        ).frame(false)).on_hover_text("Metric history").clicked() {
            action = HeaderAction::OpenHistory;
        }

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

/// Calculate the number of columns based on available width and layout preset.
/// Uses breakpoints: <130 → 1, <250 → 2, <380 → 3, ≥380 → 4 (when Auto).
pub fn column_count(available_width: f32, preset: crate::config::LayoutPreset) -> usize {
    match preset {
        crate::config::LayoutPreset::Wide => 4,
        crate::config::LayoutPreset::Skinny => 1,
        crate::config::LayoutPreset::Auto => {
            if available_width < 130.0 {
                1
            } else if available_width < 250.0 {
                2
            } else if available_width < 380.0 {
                3
            } else {
                4
            }
        }
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
    config: &crate::config::Config,
    history: &history::MetricsHistory,
) {
    use history::MetricKey;
    use panel::MetricPanel;

    let visualization = config.visualization;
    let show_mini = config.show_mini_sparklines;
    let show_cpu_temperature = config.show_cpu_temperature;
    let show_disk_temperature = config.show_disk_temperature;
    let show_fan_speed = config.show_fan_speed;
    let show_ram_temperature = config.show_ram_temperature;
    let show_cpu_fan_speed = config.show_cpu_fan_speed;
    let show_gpu_fan_speed = config.show_gpu_fan_speed;
    let show_mainboard_temp = config.show_mainboard_temp;

    let available = ui.available_width();
    let cols = column_count(available, config.layout_preset);
    let pw = panel_width(available, cols);

    // Pre-format strings that outlive the grid closure.
    let cpu_primary = format!("{:.0}%", snapshot.cpu.total_usage);
    let cpu_secondary = format!("{:.1} GHz", snapshot.cpu.frequency_ghz);
    let cpu_tertiary = if show_cpu_temperature {
        snapshot.cpu.temperature_celsius.map(|t| format!("{:.0}\u{00B0}C", t))
    } else {
        None
    };
    let cpu_quaternary = if show_cpu_fan_speed {
        snapshot.cpu_fan_rpm.map(|rpm| format!("{:.0} RPM", rpm))
    } else {
        None
    };

    let ram_used_gb = snapshot.memory.used_bytes as f64 / 1_073_741_824.0;
    let ram_total_gb = snapshot.memory.total_bytes as f64 / 1_073_741_824.0;
    let ram_primary = format!("{:.0}%", snapshot.memory.usage_percent);
    let ram_secondary = format!("{:.1}/{:.0} GB", ram_used_gb, ram_total_gb);

    let ram_tertiary = if show_ram_temperature {
        snapshot.memory.temperature_celsius.map(|t| format!("{:.0}\u{00B0}C", t))
    } else {
        None
    };

    let gpu_primary;
    let gpu_secondary;
    let gpu_tertiary;
    let gpu_quaternary;
    if let Some(gpu) = &snapshot.gpu {
        gpu_primary = Some(format!("{:.0}%", gpu.usage_percent));
        gpu_secondary = Some(format!("{:.0}\u{00B0}C", gpu.temperature_celsius));
        let vram_used_gb = gpu.vram_used_bytes as f64 / 1_073_741_824.0;
        let vram_total_gb = gpu.vram_total_bytes as f64 / 1_073_741_824.0;
        gpu_tertiary = Some(format!("{:.1}/{:.0} GB", vram_used_gb, vram_total_gb));
        gpu_quaternary = if show_gpu_fan_speed {
            snapshot.gpu_fan_rpm.map(|val| {
                // Values > 100 are RPM (from hwmon/LHM), otherwise percentage (from NVML)
                if val > 100.0 {
                    format!("{:.0} RPM", val)
                } else {
                    format!("Fan {:.0}%", val)
                }
            })
        } else {
            None
        };
    } else {
        gpu_primary = None;
        gpu_secondary = None;
        gpu_tertiary = None;
        gpu_quaternary = None;
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
    let disk_tertiary = if show_disk_temperature {
        snapshot.disk.temperature_celsius.map(|t| format!("{:.0}\u{00B0}C", t))
    } else {
        None
    };

    let ping_primary = match snapshot.ping.latency_ms {
        Some(ms) => format!("{:.0} ms", ms),
        None => "--".to_string(),
    };

    // Mini sparkline data (60 recent values for each metric)
    let cpu_mini: Vec<f32>;
    let ram_mini: Vec<f32>;
    let gpu_mini: Vec<f32>;
    let net_up_mini: Vec<f32>;
    let net_down_mini: Vec<f32>;
    let disk_read_mini: Vec<f32>;
    let disk_write_mini: Vec<f32>;
    let fan_mini: Vec<f32>;
    let mb_mini: Vec<f32>;
    let ping_mini: Vec<f32>;
    if show_mini {
        cpu_mini = history.get(MetricKey::CpuUsage).map(|ts| ts.recent_values(60)).unwrap_or_default();
        ram_mini = history.get(MetricKey::RamUsage).map(|ts| ts.recent_values(60)).unwrap_or_default();
        gpu_mini = history.get(MetricKey::GpuUsage).map(|ts| ts.recent_values(60)).unwrap_or_default();
        net_up_mini = history.get(MetricKey::NetUp).map(|ts| ts.recent_values(60)).unwrap_or_default();
        net_down_mini = history.get(MetricKey::NetDown).map(|ts| ts.recent_values(60)).unwrap_or_default();
        disk_read_mini = history.get(MetricKey::DiskRead).map(|ts| ts.recent_values(60)).unwrap_or_default();
        disk_write_mini = history.get(MetricKey::DiskWrite).map(|ts| ts.recent_values(60)).unwrap_or_default();
        fan_mini = history.get(MetricKey::FanRpm).map(|ts| ts.recent_values(60)).unwrap_or_default();
        mb_mini = history.get(MetricKey::MainboardTemp).map(|ts| ts.recent_values(60)).unwrap_or_default();
        ping_mini = history.get(MetricKey::PingLatency).map(|ts| ts.recent_values(60)).unwrap_or_default();
    } else {
        cpu_mini = Vec::new();
        ram_mini = Vec::new();
        gpu_mini = Vec::new();
        net_up_mini = Vec::new();
        net_down_mini = Vec::new();
        disk_read_mini = Vec::new();
        disk_write_mini = Vec::new();
        fan_mini = Vec::new();
        mb_mini = Vec::new();
        ping_mini = Vec::new();
    }

    // Display option flags
    let show_graphs = config.show_graphs;
    let show_percentage = config.show_percentage;
    let show_secondary_opt = config.show_secondary;
    let show_tertiary_opt = config.show_tertiary;

    // Effective visualization: if graphs disabled, force text-only by not passing gauge/sparkline
    let effective_vis = if show_graphs { visualization } else { Visualization::Gauges };

    let mut panels_added = 0usize;

    // Helper: add a panel and manage row endings
    macro_rules! add_panel {
        ($ui:expr, $panel:expr) => {{
            $ui.add($panel);
            panels_added += 1;
            if panels_added % cols == 0 { $ui.end_row(); }
        }};
    }

    egui::Grid::new("metrics_grid")
        .num_columns(cols)
        .spacing(egui::vec2(8.0, 8.0))
        .show(ui, |ui| {
            // CPU
            if config.show_cpu {
                let primary = if show_percentage { &cpu_primary } else { "CPU" };
                let mut cpu_panel = MetricPanel::new("CPU", primary, MetricColors::CPU)
                    .visualization(effective_vis)
                    .panel_width(pw);
                if show_graphs {
                    cpu_panel = cpu_panel.gauge_value(snapshot.cpu.total_usage);
                }
                if show_secondary_opt {
                    cpu_panel = cpu_panel.secondary_value(&cpu_secondary);
                }
                if show_tertiary_opt {
                    if let Some(ref ct) = cpu_tertiary {
                        cpu_panel = cpu_panel.tertiary_value(ct);
                    }
                    if let Some(ref cq) = cpu_quaternary {
                        cpu_panel = cpu_panel.quaternary_value(cq);
                    }
                }
                if show_mini && !cpu_mini.is_empty() {
                    cpu_panel = cpu_panel.mini_sparkline(&cpu_mini, (0.0, 100.0));
                }
                add_panel!(ui, cpu_panel);
            }

            // RAM
            if config.show_ram {
                let primary = if show_percentage { &ram_primary } else { "RAM" };
                let mut ram_panel = MetricPanel::new("RAM", primary, MetricColors::RAM)
                    .visualization(effective_vis)
                    .panel_width(pw);
                if show_graphs {
                    ram_panel = ram_panel.gauge_value(snapshot.memory.usage_percent);
                }
                if show_secondary_opt {
                    ram_panel = ram_panel.secondary_value(&ram_secondary);
                }
                if show_tertiary_opt {
                    if let Some(ref rt) = ram_tertiary {
                        ram_panel = ram_panel.tertiary_value(rt);
                    }
                }
                if show_mini && !ram_mini.is_empty() {
                    ram_panel = ram_panel.mini_sparkline(&ram_mini, (0.0, 100.0));
                }
                add_panel!(ui, ram_panel);
            }

            // GPU (conditional on data + visibility)
            if config.show_gpu {
                if let (Some(gpu), Some(gp), Some(gs)) =
                    (&snapshot.gpu, &gpu_primary, &gpu_secondary)
                {
                    let primary = if show_percentage { gp.as_str() } else { "GPU" };
                    let mut gpu_panel = MetricPanel::new("GPU", primary, MetricColors::GPU)
                        .visualization(effective_vis)
                        .panel_width(pw);
                    if show_graphs {
                        gpu_panel = gpu_panel.gauge_value(gpu.usage_percent);
                    }
                    if show_secondary_opt {
                        gpu_panel = gpu_panel.secondary_value(gs);
                    }
                    if show_tertiary_opt {
                        if let Some(ref gt) = gpu_tertiary {
                            gpu_panel = gpu_panel.tertiary_value(gt);
                        }
                        if let Some(ref gq) = gpu_quaternary {
                            gpu_panel = gpu_panel.quaternary_value(gq);
                        }
                    }
                    if show_mini && !gpu_mini.is_empty() {
                        gpu_panel = gpu_panel.mini_sparkline(&gpu_mini, (0.0, 100.0));
                    }
                    add_panel!(ui, gpu_panel);
                }
            }

            // Network
            if config.show_network {
                let mut net_panel = MetricPanel::new("Network", &net_primary, MetricColors::NETWORK)
                    .visualization(effective_vis)
                    .panel_width(pw);
                if show_secondary_opt {
                    net_panel = net_panel.secondary_value(&net_secondary);
                }
                if show_mini && !net_down_mini.is_empty() {
                    let max_net = net_down_mini.iter().chain(net_up_mini.iter())
                        .cloned().fold(1.0f32, f32::max);
                    net_panel = net_panel.mini_sparkline(&net_down_mini, (0.0, max_net));
                }
                add_panel!(ui, net_panel);
            }

            // Disk I/O
            if config.show_disk {
                let mut disk_panel = MetricPanel::new("Disk I/O", &disk_primary, MetricColors::DISK)
                    .visualization(effective_vis)
                    .panel_width(pw);
                if show_secondary_opt {
                    disk_panel = disk_panel.secondary_value(&disk_secondary);
                }
                if show_tertiary_opt {
                    if let Some(ref dt) = disk_tertiary {
                        disk_panel = disk_panel.tertiary_value(dt);
                    }
                }
                if show_mini && !disk_read_mini.is_empty() {
                    let max_disk = disk_read_mini.iter().chain(disk_write_mini.iter())
                        .cloned().fold(1.0f32, f32::max);
                    disk_panel = disk_panel.mini_sparkline(&disk_read_mini, (0.0, max_disk));
                }
                add_panel!(ui, disk_panel);
            }

            // Fans (conditional)
            if show_fan_speed {
                if let Some(rpm) = snapshot.fan_rpm {
                    let fan_primary = format!("{:.0} RPM", rpm);
                    let mut fan_panel = MetricPanel::new("Fans", &fan_primary, MetricColors::FANS)
                        .visualization(effective_vis)
                        .panel_width(pw);
                    if show_mini && !fan_mini.is_empty() {
                        let max_fan = fan_mini.iter().cloned().fold(1.0f32, f32::max);
                        fan_panel = fan_panel.mini_sparkline(&fan_mini, (0.0, max_fan));
                    }
                    add_panel!(ui, fan_panel);
                }
            }

            // Mainboard (conditional)
            if show_mainboard_temp {
                if let Some(temp) = snapshot.mainboard_temp_celsius {
                    let mb_primary = format!("{:.0}\u{00B0}C", temp);
                    let mut mainboard_panel = MetricPanel::new("Mainboard", &mb_primary, MetricColors::MAINBOARD)
                        .visualization(effective_vis)
                        .panel_width(pw);
                    if show_mini && !mb_mini.is_empty() {
                        let max_mb = mb_mini.iter().cloned().fold(1.0f32, f32::max);
                        mainboard_panel = mainboard_panel.mini_sparkline(&mb_mini, (0.0, max_mb));
                    }
                    add_panel!(ui, mainboard_panel);
                }
            }

            // Ping
            if config.show_ping {
                let mut ping_panel = MetricPanel::new("Ping", &ping_primary, MetricColors::PING)
                    .visualization(effective_vis)
                    .panel_width(pw);
                if show_mini && !ping_mini.is_empty() {
                    let max_ping = ping_mini.iter().cloned().fold(1.0f32, f32::max);
                    ping_panel = ping_panel.mini_sparkline(&ping_mini, (0.0, max_ping));
                }
                add_panel!(ui, ping_panel);
            }
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
    style.spacing.window_margin = egui::Margin::same(6);
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
    use crate::metrics::ping::PingMetrics;
    use std::time::Instant;

    fn make_snapshot(gpu: Option<GpuMetrics>) -> MetricsSnapshot {
        MetricsSnapshot {
            timestamp: Instant::now(),
            cpu: CpuMetrics {
                total_usage: 42.0,
                frequency_ghz: 3.8,
                temperature_celsius: None,
            },
            memory: MemoryMetrics {
                used_bytes: 8_000_000_000,
                total_bytes: 16_000_000_000,
                usage_percent: 50.0,
                temperature_celsius: None,
            },
            gpu,
            network: NetworkMetrics {
                upload_bytes_per_sec: 1_200_000,
                download_bytes_per_sec: 12_000_000,
            },
            disk: DiskMetrics {
                read_bytes_per_sec: 45_000_000,
                write_bytes_per_sec: 12_000_000,
                temperature_celsius: None,
            },
            ping: PingMetrics {
                latency_ms: Some(12.0),
            },
            fan_rpm: None,
            cpu_fan_rpm: None,
            gpu_fan_rpm: None,
            mainboard_temp_celsius: None,
        }
    }

    #[test]
    fn column_count_very_narrow_window() {
        use crate::config::LayoutPreset;
        assert_eq!(column_count(100.0, LayoutPreset::Auto), 1);
        assert_eq!(column_count(129.0, LayoutPreset::Auto), 1);
    }

    #[test]
    fn column_count_narrow_window() {
        use crate::config::LayoutPreset;
        assert_eq!(column_count(130.0, LayoutPreset::Auto), 2);
        assert_eq!(column_count(200.0, LayoutPreset::Auto), 2);
        assert_eq!(column_count(249.0, LayoutPreset::Auto), 2);
    }

    #[test]
    fn column_count_medium_window() {
        use crate::config::LayoutPreset;
        assert_eq!(column_count(250.0, LayoutPreset::Auto), 3);
        assert_eq!(column_count(350.0, LayoutPreset::Auto), 3);
    }

    #[test]
    fn column_count_wide_window() {
        use crate::config::LayoutPreset;
        assert_eq!(column_count(380.0, LayoutPreset::Auto), 4);
        assert_eq!(column_count(500.0, LayoutPreset::Auto), 4);
        assert_eq!(column_count(1000.0, LayoutPreset::Auto), 4);
    }

    #[test]
    fn column_count_preset_overrides() {
        use crate::config::LayoutPreset;
        assert_eq!(column_count(100.0, LayoutPreset::Wide), 4);
        assert_eq!(column_count(1000.0, LayoutPreset::Skinny), 1);
    }

    /// Count expected panels for a snapshot (GPU is conditional).
    fn expected_panel_count(snapshot: &MetricsSnapshot) -> usize {
        if snapshot.gpu.is_some() { 6 } else { 5 }
    }

    #[test]
    fn gpu_panel_included_when_present() {
        let gpu = GpuMetrics {
            usage_percent: 28.0,
            temperature_celsius: 72.0,
            vram_used_bytes: 2_000_000_000,
            vram_total_bytes: 8_000_000_000,
            fan_speed_percent: None,
        };
        let snapshot = make_snapshot(Some(gpu));
        assert_eq!(expected_panel_count(&snapshot), 6);
    }

    #[test]
    fn gpu_panel_excluded_when_none() {
        let snapshot = make_snapshot(None);
        assert_eq!(expected_panel_count(&snapshot), 5);
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
