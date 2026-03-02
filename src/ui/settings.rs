// Settings popout window (separate OS viewport)

use eframe::egui;

use crate::config::{Config, CpuSelection, DeviceFilter, DiskTempMode, FanSpeedMode, GpuSelection, MainboardTempMode, OverlayMode, Visualization};
use crate::metrics::discovery::AvailableDevices;

/// The allowed polling interval presets in milliseconds.
const POLLING_PRESETS: &[u64] = &[250, 500, 1000, 2000, 5000];

/// Renders the settings panel as a separate OS-level popout window.
/// Returns `true` if the window is still open, `false` if it was closed.
pub fn show_settings(
    ctx: &egui::Context,
    config: &mut Config,
    available_devices: &AvailableDevices,
) -> bool {
    let mut open = true;
    let mut changed = false;

    ctx.show_viewport_immediate(
        egui::ViewportId::from_hash_of("pacecar_settings"),
        egui::ViewportBuilder::default()
            .with_title("Pacecar Settings")
            .with_inner_size([380.0, 820.0])
            .with_always_on_top()
            .with_minimize_button(false)
            .with_maximize_button(false),
        |ctx, _class| {
            if ctx.input(|i: &egui::InputState| i.viewport().close_requested()) {
                open = false;
                return;
            }

            // Apply dark theme to the settings viewport
            configure_settings_visuals(ctx);

            egui::CentralPanel::default()
                .frame(
                    egui::Frame::NONE
                        .fill(egui::Color32::from_rgb(24, 24, 28))
                        .inner_margin(20.0),
                )
                .show(ctx, |ui| {
                    egui::ScrollArea::vertical().show(ui, |ui| {
                        ui.spacing_mut().item_spacing = egui::vec2(6.0, 6.0);

                        // --- Header ---
                        ui.vertical_centered(|ui| {
                            ui.label(
                                egui::RichText::new("Settings")
                                    .size(20.0)
                                    .color(egui::Color32::WHITE)
                                    .strong(),
                            );
                        });
                        ui.add_space(12.0);

                        // --- Polling Interval ---
                        changed |= settings_section(ui, "Refresh Rate", |ui| {
                            let current_label = format!("{} ms", config.polling_interval_ms);
                            let mut section_changed = false;
                            ui.horizontal(|ui| {
                                ui.with_layout(
                                    egui::Layout::right_to_left(egui::Align::Center),
                                    |ui| {
                                        egui::ComboBox::from_id_salt("polling_interval")
                                            .selected_text(&current_label)
                                            .width(100.0)
                                            .show_ui(ui, |ui| {
                                                for &ms in POLLING_PRESETS {
                                                    let label = format!("{ms} ms");
                                                    if ui
                                                        .selectable_value(
                                                            &mut config.polling_interval_ms,
                                                            ms,
                                                            label,
                                                        )
                                                        .changed()
                                                    {
                                                        section_changed = true;
                                                    }
                                                }
                                            });
                                    },
                                );
                            });
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- Transparency ---
                        changed |= settings_section(ui, "Opacity", |ui| {
                            let mut pct = config.transparency * 100.0;
                            let slider = egui::Slider::new(&mut pct, 10.0..=100.0)
                                .suffix("%")
                                .fixed_decimals(0);
                            let resp = ui.add(slider);
                            if resp.changed() {
                                config.transparency = pct / 100.0;
                                return true;
                            }
                            false
                        });

                        ui.add_space(4.0);

                        // --- Visualization Mode ---
                        changed |= settings_section(ui, "Visualization", |ui| {
                            let mut section_changed = false;
                            ui.horizontal(|ui| {
                                ui.spacing_mut().item_spacing.x = 16.0;
                                if ui
                                    .radio_value(
                                        &mut config.visualization,
                                        Visualization::Gauges,
                                        "Gauges",
                                    )
                                    .changed()
                                {
                                    section_changed = true;
                                }
                                if ui
                                    .radio_value(
                                        &mut config.visualization,
                                        Visualization::Sparklines,
                                        "Sparklines",
                                    )
                                    .changed()
                                {
                                    section_changed = true;
                                }
                            });
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- Overlay Mode ---
                        changed |= settings_section(ui, "Overlay Mode", |ui| {
                            let mut section_changed = false;
                            ui.horizontal(|ui| {
                                ui.spacing_mut().item_spacing.x = 16.0;
                                if ui
                                    .radio_value(
                                        &mut config.overlay_mode,
                                        OverlayMode::Interactive,
                                        "Interactive",
                                    )
                                    .changed()
                                {
                                    section_changed = true;
                                }
                                if ui
                                    .radio_value(
                                        &mut config.overlay_mode,
                                        OverlayMode::ClickThrough,
                                        "Click-through",
                                    )
                                    .changed()
                                {
                                    section_changed = true;
                                }
                            });
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- Hotkey ---
                        changed |= settings_section(ui, "Toggle Hotkey", |ui| {
                            let resp = ui.add(
                                egui::TextEdit::singleline(&mut config.hotkey)
                                    .desired_width(160.0),
                            );
                            resp.changed()
                        });

                        ui.add_space(8.0);

                        // --- Device Selection Header ---
                        ui.label(
                            egui::RichText::new("Device Selection")
                                .size(16.0)
                                .color(egui::Color32::from_gray(220))
                                .strong(),
                        );
                        ui.add_space(4.0);

                        // --- GPU Device ---
                        changed |= settings_section(ui, "GPU Device", |ui| {
                            let current_label = gpu_selection_label(&config.gpu_selection, available_devices);
                            let mut section_changed = false;
                            ui.horizontal(|ui| {
                                ui.with_layout(
                                    egui::Layout::right_to_left(egui::Align::Center),
                                    |ui| {
                                        egui::ComboBox::from_id_salt("gpu_device")
                                            .selected_text(&current_label)
                                            .width(200.0)
                                            .show_ui(ui, |ui| {
                                                // Auto-detect option
                                                if ui
                                                    .selectable_label(
                                                        config.gpu_selection == GpuSelection::Auto,
                                                        "Auto-detect",
                                                    )
                                                    .clicked()
                                                {
                                                    config.gpu_selection = GpuSelection::Auto;
                                                    section_changed = true;
                                                }
                                                // List discovered GPUs
                                                for gpu in &available_devices.gpus {
                                                    let sel = config.gpu_selection
                                                        == GpuSelection::ByIndex(gpu.index);
                                                    if ui
                                                        .selectable_label(sel, &gpu.name)
                                                        .clicked()
                                                    {
                                                        config.gpu_selection =
                                                            GpuSelection::ByIndex(gpu.index);
                                                        section_changed = true;
                                                    }
                                                }
                                            });
                                    },
                                );
                            });
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- CPU Monitor ---
                        changed |= settings_section(ui, "CPU Monitor", |ui| {
                            let current_label = cpu_selection_label(&config.cpu_selection);
                            let mut section_changed = false;
                            ui.horizontal(|ui| {
                                ui.with_layout(
                                    egui::Layout::right_to_left(egui::Align::Center),
                                    |ui| {
                                        egui::ComboBox::from_id_salt("cpu_monitor")
                                            .selected_text(&current_label)
                                            .width(200.0)
                                            .show_ui(ui, |ui| {
                                                // Aggregate option
                                                if ui
                                                    .selectable_label(
                                                        config.cpu_selection
                                                            == CpuSelection::Aggregate,
                                                        "All Cores (Aggregate)",
                                                    )
                                                    .clicked()
                                                {
                                                    config.cpu_selection =
                                                        CpuSelection::Aggregate;
                                                    section_changed = true;
                                                }
                                                // Per-core options
                                                for i in 0..available_devices.cpu_core_count {
                                                    let label = format!("Core {i}");
                                                    let sel =
                                                        config.cpu_selection == CpuSelection::Core(i);
                                                    if ui.selectable_label(sel, &label).clicked() {
                                                        config.cpu_selection =
                                                            CpuSelection::Core(i);
                                                        section_changed = true;
                                                    }
                                                }
                                            });
                                    },
                                );
                            });
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- CPU Temperature ---
                        changed |= settings_section(ui, "CPU Temperature", |ui| {
                            ui.checkbox(&mut config.show_cpu_temperature, "Show CPU temperature")
                                .changed()
                        });

                        ui.add_space(4.0);

                        // --- Network Interface ---
                        changed |= settings_section(ui, "Network Interface", |ui| {
                            let current_label =
                                device_filter_label(&config.network_interface, "All Interfaces");
                            let mut section_changed = false;
                            ui.horizontal(|ui| {
                                ui.with_layout(
                                    egui::Layout::right_to_left(egui::Align::Center),
                                    |ui| {
                                        egui::ComboBox::from_id_salt("network_interface")
                                            .selected_text(&current_label)
                                            .width(200.0)
                                            .show_ui(ui, |ui| {
                                                if ui
                                                    .selectable_label(
                                                        config.network_interface == DeviceFilter::All,
                                                        "All Interfaces",
                                                    )
                                                    .clicked()
                                                {
                                                    config.network_interface = DeviceFilter::All;
                                                    section_changed = true;
                                                }
                                                for iface in &available_devices.network_interfaces {
                                                    let sel = config.network_interface
                                                        == DeviceFilter::Named(iface.clone());
                                                    if ui.selectable_label(sel, iface).clicked() {
                                                        config.network_interface =
                                                            DeviceFilter::Named(iface.clone());
                                                        section_changed = true;
                                                    }
                                                }
                                            });
                                    },
                                );
                            });
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- Disk Device ---
                        changed |= settings_section(ui, "Disk Device", |ui| {
                            let current_label =
                                device_filter_label(&config.disk_device, "All Disks");
                            let mut section_changed = false;
                            ui.horizontal(|ui| {
                                ui.with_layout(
                                    egui::Layout::right_to_left(egui::Align::Center),
                                    |ui| {
                                        egui::ComboBox::from_id_salt("disk_device")
                                            .selected_text(&current_label)
                                            .width(200.0)
                                            .show_ui(ui, |ui| {
                                                if ui
                                                    .selectable_label(
                                                        config.disk_device == DeviceFilter::All,
                                                        "All Disks",
                                                    )
                                                    .clicked()
                                                {
                                                    config.disk_device = DeviceFilter::All;
                                                    section_changed = true;
                                                }
                                                for disk in &available_devices.disks {
                                                    let sel = config.disk_device
                                                        == DeviceFilter::Named(
                                                            disk.mount_point.clone(),
                                                        );
                                                    if ui
                                                        .selectable_label(sel, &disk.display_label)
                                                        .clicked()
                                                    {
                                                        config.disk_device = DeviceFilter::Named(
                                                            disk.mount_point.clone(),
                                                        );
                                                        section_changed = true;
                                                    }
                                                }
                                            });
                                    },
                                );
                            });
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- Disk Temperature ---
                        changed |= settings_section(ui, "Disk Temperature", |ui| {
                            let mut section_changed = false;
                            if ui.checkbox(&mut config.show_disk_temperature, "Show disk temperature")
                                .changed()
                            {
                                section_changed = true;
                            }
                            if config.show_disk_temperature {
                                ui.add_space(4.0);
                                ui.horizontal(|ui| {
                                    ui.spacing_mut().item_spacing.x = 16.0;
                                    if ui
                                        .radio_value(
                                            &mut config.disk_temp_mode,
                                            DiskTempMode::SelectedDisk,
                                            "Selected Disk",
                                        )
                                        .changed()
                                    {
                                        section_changed = true;
                                    }
                                    if ui
                                        .radio_value(
                                            &mut config.disk_temp_mode,
                                            DiskTempMode::Highest,
                                            "Highest",
                                        )
                                        .changed()
                                    {
                                        section_changed = true;
                                    }
                                    if ui
                                        .radio_value(
                                            &mut config.disk_temp_mode,
                                            DiskTempMode::Average,
                                            "Average",
                                        )
                                        .changed()
                                    {
                                        section_changed = true;
                                    }
                                });
                            }
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- Fan Speed ---
                        changed |= settings_section(ui, "Fan Speed", |ui| {
                            let mut section_changed = false;
                            if ui.checkbox(&mut config.show_fan_speed, "Show fan speed")
                                .changed()
                            {
                                section_changed = true;
                            }
                            if config.show_fan_speed {
                                ui.add_space(4.0);
                                ui.horizontal(|ui| {
                                    ui.spacing_mut().item_spacing.x = 16.0;
                                    if ui
                                        .radio_value(
                                            &mut config.fan_speed_mode,
                                            FanSpeedMode::Highest,
                                            "Highest",
                                        )
                                        .changed()
                                    {
                                        section_changed = true;
                                    }
                                    if ui
                                        .radio_value(
                                            &mut config.fan_speed_mode,
                                            FanSpeedMode::Average,
                                            "Average",
                                        )
                                        .changed()
                                    {
                                        section_changed = true;
                                    }
                                });
                            }
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- RAM Temperature ---
                        changed |= settings_section(ui, "RAM Temperature", |ui| {
                            ui.checkbox(&mut config.show_ram_temperature, "Show RAM temperature")
                                .changed()
                        });

                        ui.add_space(4.0);

                        // --- CPU Fan Speed ---
                        changed |= settings_section(ui, "CPU Fan Speed", |ui| {
                            ui.checkbox(&mut config.show_cpu_fan_speed, "Show CPU fan speed")
                                .changed()
                        });

                        ui.add_space(4.0);

                        // --- GPU Fan Speed ---
                        changed |= settings_section(ui, "GPU Fan Speed", |ui| {
                            ui.checkbox(&mut config.show_gpu_fan_speed, "Show GPU fan speed")
                                .changed()
                        });

                        ui.add_space(4.0);

                        // --- Mainboard Temperature ---
                        changed |= settings_section(ui, "Mainboard Temperature", |ui| {
                            let mut section_changed = false;
                            if ui.checkbox(&mut config.show_mainboard_temp, "Show mainboard temperature")
                                .changed()
                            {
                                section_changed = true;
                            }
                            if config.show_mainboard_temp {
                                ui.add_space(4.0);
                                ui.horizontal(|ui| {
                                    ui.spacing_mut().item_spacing.x = 16.0;
                                    if ui
                                        .radio_value(
                                            &mut config.mainboard_temp_mode,
                                            MainboardTempMode::Highest,
                                            "Highest",
                                        )
                                        .changed()
                                    {
                                        section_changed = true;
                                    }
                                    if ui
                                        .radio_value(
                                            &mut config.mainboard_temp_mode,
                                            MainboardTempMode::Average,
                                            "Average",
                                        )
                                        .changed()
                                    {
                                        section_changed = true;
                                    }
                                });
                            }
                            section_changed
                        });

                        ui.add_space(8.0);

                        // --- Tile Visibility Header ---
                        ui.label(
                            egui::RichText::new("Tile Visibility")
                                .size(16.0)
                                .color(egui::Color32::from_gray(220))
                                .strong(),
                        );
                        ui.add_space(4.0);

                        changed |= settings_section(ui, "Show / Hide Tiles", |ui| {
                            let mut section_changed = false;
                            section_changed |= ui.checkbox(&mut config.show_cpu, "CPU").changed();
                            section_changed |= ui.checkbox(&mut config.show_ram, "RAM").changed();
                            section_changed |= ui.checkbox(&mut config.show_gpu, "GPU").changed();
                            section_changed |= ui.checkbox(&mut config.show_network, "Network").changed();
                            section_changed |= ui.checkbox(&mut config.show_disk, "Disk I/O").changed();
                            section_changed |= ui.checkbox(&mut config.show_ping, "Ping").changed();
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- Display Options ---
                        changed |= settings_section(ui, "Display Options", |ui| {
                            let mut section_changed = false;
                            section_changed |= ui.checkbox(&mut config.show_graphs, "Show graphs / gauges").changed();
                            section_changed |= ui.checkbox(&mut config.show_percentage, "Show percentage value").changed();
                            section_changed |= ui.checkbox(&mut config.show_secondary, "Show secondary info").changed();
                            section_changed |= ui.checkbox(&mut config.show_tertiary, "Show tertiary info (temps, VRAM)").changed();
                            section_changed |= ui.checkbox(&mut config.show_mini_sparklines, "Show mini sparklines on tiles").changed();
                            section_changed
                        });

                        ui.add_space(4.0);

                        // --- History Retention ---
                        changed |= settings_section(ui, "History Retention", |ui| {
                            let current_label = format!("{} min", config.history_retention_minutes);
                            let mut section_changed = false;
                            ui.horizontal(|ui| {
                                ui.with_layout(
                                    egui::Layout::right_to_left(egui::Align::Center),
                                    |ui| {
                                        egui::ComboBox::from_id_salt("history_retention_settings")
                                            .selected_text(&current_label)
                                            .width(100.0)
                                            .show_ui(ui, |ui| {
                                                for &mins in &[1u32, 5, 10, 15, 30, 60, 120] {
                                                    let label = format!("{mins} min");
                                                    if ui
                                                        .selectable_value(
                                                            &mut config.history_retention_minutes,
                                                            mins,
                                                            label,
                                                        )
                                                        .changed()
                                                    {
                                                        section_changed = true;
                                                    }
                                                }
                                            });
                                    },
                                );
                            });
                            section_changed
                        });

                        ui.add_space(8.0);

                        // --- Network Settings Header ---
                        ui.label(
                            egui::RichText::new("Network")
                                .size(16.0)
                                .color(egui::Color32::from_gray(220))
                                .strong(),
                        );
                        ui.add_space(4.0);

                        // --- Ping Target ---
                        changed |= settings_section(ui, "Ping Target", |ui| {
                            let resp = ui.add(
                                egui::TextEdit::singleline(&mut config.ping_target)
                                    .desired_width(160.0)
                                    .hint_text("e.g. 8.8.8.8"),
                            );
                            resp.changed()
                        });

                        ui.add_space(12.0);

                        // --- Reset to Defaults ---
                        ui.vertical_centered(|ui| {
                            let btn = egui::Button::new(
                                egui::RichText::new("Reset to Defaults").color(egui::Color32::from_rgb(200, 200, 200)),
                            )
                            .fill(egui::Color32::from_rgb(55, 55, 60))
                            .corner_radius(6.0)
                            .min_size(egui::vec2(160.0, 32.0));

                            if ui.add(btn).clicked() {
                                let defaults = Config::default();
                                config.polling_interval_ms = defaults.polling_interval_ms;
                                config.transparency = defaults.transparency;
                                config.visualization = defaults.visualization;
                                config.overlay_mode = defaults.overlay_mode;
                                config.hotkey = defaults.hotkey;
                                config.gpu_selection = defaults.gpu_selection;
                                config.cpu_selection = defaults.cpu_selection;
                                config.network_interface = defaults.network_interface;
                                config.disk_device = defaults.disk_device;
                                config.ping_target = defaults.ping_target;
                                config.show_cpu_temperature = defaults.show_cpu_temperature;
                                config.show_disk_temperature = defaults.show_disk_temperature;
                                config.disk_temp_mode = defaults.disk_temp_mode;
                                config.show_fan_speed = defaults.show_fan_speed;
                                config.fan_speed_mode = defaults.fan_speed_mode;
                                config.show_ram_temperature = defaults.show_ram_temperature;
                                config.show_cpu_fan_speed = defaults.show_cpu_fan_speed;
                                config.show_gpu_fan_speed = defaults.show_gpu_fan_speed;
                                config.show_mainboard_temp = defaults.show_mainboard_temp;
                                config.mainboard_temp_mode = defaults.mainboard_temp_mode;
                                config.show_cpu = defaults.show_cpu;
                                config.show_ram = defaults.show_ram;
                                config.show_gpu = defaults.show_gpu;
                                config.show_network = defaults.show_network;
                                config.show_disk = defaults.show_disk;
                                config.show_ping = defaults.show_ping;
                                config.show_graphs = defaults.show_graphs;
                                config.show_percentage = defaults.show_percentage;
                                config.show_secondary = defaults.show_secondary;
                                config.show_tertiary = defaults.show_tertiary;
                                config.layout_preset = defaults.layout_preset;
                                config.show_mini_sparklines = defaults.show_mini_sparklines;
                                config.history_retention_minutes = defaults.history_retention_minutes;
                                changed = true;
                            }
                        });
                    });
                });
        },
    );

    if changed {
        config.clamp();
        let _ = config.save();
    }

    open
}

/// Label for the current GPU selection.
fn gpu_selection_label(selection: &GpuSelection, devices: &AvailableDevices) -> String {
    match selection {
        GpuSelection::Auto => "Auto-detect".to_string(),
        GpuSelection::ByIndex(idx) => devices
            .gpus
            .iter()
            .find(|g| g.index == *idx)
            .map(|g| g.name.clone())
            .unwrap_or_else(|| format!("GPU {idx}")),
        GpuSelection::ByName(name) => name.clone(),
    }
}

/// Label for the current CPU selection.
fn cpu_selection_label(selection: &CpuSelection) -> String {
    match selection {
        CpuSelection::Aggregate => "All Cores (Aggregate)".to_string(),
        CpuSelection::Core(idx) => format!("Core {idx}"),
    }
}

/// Label for a DeviceFilter.
fn device_filter_label(filter: &DeviceFilter, all_label: &str) -> String {
    match filter {
        DeviceFilter::All => all_label.to_string(),
        DeviceFilter::Named(name) => name.clone(),
    }
}

/// Render a settings section with a label and content inside a subtle card.
/// Returns whether the content reported a change.
fn settings_section(
    ui: &mut egui::Ui,
    title: &str,
    content: impl FnOnce(&mut egui::Ui) -> bool,
) -> bool {
    let mut changed = false;

    egui::Frame::NONE
        .fill(egui::Color32::from_rgb(32, 32, 36))
        .corner_radius(8.0)
        .inner_margin(egui::Margin::symmetric(12, 10))
        .stroke(egui::Stroke::new(1.0, egui::Color32::from_rgb(50, 50, 55)))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.label(
                egui::RichText::new(title)
                    .size(11.0)
                    .color(egui::Color32::from_rgb(140, 140, 160))
                    .strong(),
            );
            ui.add_space(4.0);
            changed = content(ui);
        });

    changed
}

/// Apply dark visuals to the settings viewport.
pub(crate) fn configure_settings_visuals(ctx: &egui::Context) {
    let mut visuals = egui::Visuals::dark();
    visuals.panel_fill = egui::Color32::from_rgb(24, 24, 28);
    visuals.window_fill = egui::Color32::from_rgb(24, 24, 28);
    visuals.widgets.noninteractive.bg_fill = egui::Color32::from_rgb(40, 40, 45);
    visuals.widgets.noninteractive.fg_stroke =
        egui::Stroke::new(1.0, egui::Color32::from_gray(190));
    visuals.widgets.inactive.bg_fill = egui::Color32::from_rgb(50, 50, 55);
    visuals.widgets.inactive.fg_stroke =
        egui::Stroke::new(1.0, egui::Color32::from_gray(180));
    visuals.widgets.hovered.bg_fill = egui::Color32::from_rgb(65, 65, 72);
    visuals.widgets.active.bg_fill = egui::Color32::from_rgb(75, 75, 82);
    visuals.selection.bg_fill = egui::Color32::from_rgb(80, 120, 200);

    let mut style = (*ctx.style()).clone();
    style.spacing.item_spacing = egui::vec2(6.0, 6.0);
    style.visuals = visuals;
    ctx.set_style(style);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn polling_presets_are_within_valid_range() {
        for &ms in POLLING_PRESETS {
            assert!(ms >= 250, "preset {ms} below minimum");
            assert!(ms <= 5000, "preset {ms} above maximum");
        }
    }

    #[test]
    fn polling_presets_are_sorted() {
        for window in POLLING_PRESETS.windows(2) {
            assert!(window[0] < window[1], "presets must be ascending");
        }
    }

    #[test]
    fn reset_to_defaults_restores_config_values() {
        let mut config = Config {
            polling_interval_ms: 5000,
            transparency: 0.3,
            visualization: Visualization::Sparklines,
            overlay_mode: OverlayMode::ClickThrough,
            hotkey: "Alt+X".to_string(),
            gpu_selection: GpuSelection::ByIndex(2),
            cpu_selection: CpuSelection::Core(3),
            network_interface: DeviceFilter::Named("eth0".to_string()),
            disk_device: DeviceFilter::Named("C:\\".to_string()),
            ping_target: "1.1.1.1".to_string(),
            ..Config::default()
        };

        // Simulate what the Reset to Defaults button does
        let defaults = Config::default();
        config.polling_interval_ms = defaults.polling_interval_ms;
        config.transparency = defaults.transparency;
        config.visualization = defaults.visualization;
        config.overlay_mode = defaults.overlay_mode;
        config.hotkey = defaults.hotkey;
        config.gpu_selection = defaults.gpu_selection;
        config.cpu_selection = defaults.cpu_selection;
        config.network_interface = defaults.network_interface;
        config.disk_device = defaults.disk_device;
        config.ping_target = defaults.ping_target;
        config.show_cpu_temperature = defaults.show_cpu_temperature;
        config.show_disk_temperature = defaults.show_disk_temperature;
        config.disk_temp_mode = defaults.disk_temp_mode;
        config.show_fan_speed = defaults.show_fan_speed;
        config.fan_speed_mode = defaults.fan_speed_mode;
        config.show_ram_temperature = defaults.show_ram_temperature;
        config.show_cpu_fan_speed = defaults.show_cpu_fan_speed;
        config.show_gpu_fan_speed = defaults.show_gpu_fan_speed;
        config.show_mainboard_temp = defaults.show_mainboard_temp;
        config.mainboard_temp_mode = defaults.mainboard_temp_mode;
        config.show_cpu = defaults.show_cpu;
        config.show_ram = defaults.show_ram;
        config.show_gpu = defaults.show_gpu;
        config.show_network = defaults.show_network;
        config.show_disk = defaults.show_disk;
        config.show_ping = defaults.show_ping;
        config.show_graphs = defaults.show_graphs;
        config.show_percentage = defaults.show_percentage;
        config.show_secondary = defaults.show_secondary;
        config.show_tertiary = defaults.show_tertiary;
        config.layout_preset = defaults.layout_preset;
        config.show_mini_sparklines = defaults.show_mini_sparklines;
        config.history_retention_minutes = defaults.history_retention_minutes;

        assert_eq!(config.polling_interval_ms, 1000);
        assert_eq!(config.transparency, 0.65);
        assert_eq!(config.visualization, Visualization::Gauges);
        assert_eq!(config.overlay_mode, OverlayMode::Interactive);
        assert_eq!(config.hotkey, "Ctrl+Shift+P");
        assert_eq!(config.gpu_selection, GpuSelection::Auto);
        assert_eq!(config.cpu_selection, CpuSelection::Aggregate);
        assert_eq!(config.network_interface, DeviceFilter::All);
        assert_eq!(config.disk_device, DeviceFilter::All);
        assert_eq!(config.ping_target, "8.8.8.8");
        assert_eq!(config.show_cpu_temperature, true);
        assert_eq!(config.show_disk_temperature, true);
        assert_eq!(config.disk_temp_mode, DiskTempMode::SelectedDisk);
        assert_eq!(config.show_fan_speed, true);
        assert_eq!(config.fan_speed_mode, FanSpeedMode::Highest);
        assert_eq!(config.show_ram_temperature, true);
        assert_eq!(config.show_cpu_fan_speed, true);
        assert_eq!(config.show_mainboard_temp, true);
        assert_eq!(config.mainboard_temp_mode, MainboardTempMode::Highest);
    }

    #[test]
    fn transparency_slider_conversion_round_trip() {
        let config_value: f32 = 0.75;
        let slider_pct = config_value * 100.0;
        let back = slider_pct / 100.0;
        assert!((back - config_value).abs() < f32::EPSILON);
    }

    #[test]
    fn transparency_clamp_after_edit() {
        let mut config = Config::default();
        config.transparency = 0.05;
        config.clamp();
        assert_eq!(config.transparency, 0.1);

        config.transparency = 1.5;
        config.clamp();
        assert_eq!(config.transparency, 1.0);
    }

    #[test]
    fn gpu_selection_label_auto() {
        use crate::metrics::discovery::AvailableDevices;
        let devices = AvailableDevices {
            gpus: vec![],
            cpu_core_count: 4,
            network_interfaces: vec![],
            disks: vec![],
        };
        assert_eq!(
            gpu_selection_label(&GpuSelection::Auto, &devices),
            "Auto-detect"
        );
    }

    #[test]
    fn cpu_selection_label_values() {
        assert_eq!(
            cpu_selection_label(&CpuSelection::Aggregate),
            "All Cores (Aggregate)"
        );
        assert_eq!(cpu_selection_label(&CpuSelection::Core(5)), "Core 5");
    }

    #[test]
    fn device_filter_label_values() {
        assert_eq!(
            device_filter_label(&DeviceFilter::All, "All Interfaces"),
            "All Interfaces"
        );
        assert_eq!(
            device_filter_label(&DeviceFilter::Named("eth0".into()), "All Interfaces"),
            "eth0"
        );
    }
}
