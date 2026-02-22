// Settings popout window (separate OS viewport)

use eframe::egui;

use crate::config::{Config, OverlayMode, Visualization};

/// The allowed polling interval presets in milliseconds.
const POLLING_PRESETS: &[u64] = &[250, 500, 1000, 2000, 5000];

/// Renders the settings panel as a separate OS-level popout window.
/// Returns `true` if the window is still open, `false` if it was closed.
pub fn show_settings(ctx: &egui::Context, config: &mut Config) -> bool {
    let mut open = true;
    let mut changed = false;

    ctx.show_viewport_immediate(
        egui::ViewportId::from_hash_of("pacecar_settings"),
        egui::ViewportBuilder::default()
            .with_title("Pacecar Settings")
            .with_inner_size([340.0, 440.0])
            .with_resizable(false)
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
                    egui::Frame::none()
                        .fill(egui::Color32::from_rgb(24, 24, 28))
                        .inner_margin(20.0),
                )
                .show(ctx, |ui| {
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

                    ui.add_space(12.0);

                    // --- Reset to Defaults ---
                    ui.vertical_centered(|ui| {
                        let btn = egui::Button::new(
                            egui::RichText::new("Reset to Defaults").color(egui::Color32::from_rgb(200, 200, 200)),
                        )
                        .fill(egui::Color32::from_rgb(55, 55, 60))
                        .rounding(6.0)
                        .min_size(egui::vec2(160.0, 32.0));

                        if ui.add(btn).clicked() {
                            let defaults = Config::default();
                            config.polling_interval_ms = defaults.polling_interval_ms;
                            config.transparency = defaults.transparency;
                            config.visualization = defaults.visualization;
                            config.overlay_mode = defaults.overlay_mode;
                            config.hotkey = defaults.hotkey;
                            changed = true;
                        }
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

/// Render a settings section with a label and content inside a subtle card.
/// Returns whether the content reported a change.
fn settings_section(
    ui: &mut egui::Ui,
    title: &str,
    content: impl FnOnce(&mut egui::Ui) -> bool,
) -> bool {
    let mut changed = false;

    egui::Frame::none()
        .fill(egui::Color32::from_rgb(32, 32, 36))
        .rounding(8.0)
        .inner_margin(egui::Margin::symmetric(12.0, 10.0))
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
fn configure_settings_visuals(ctx: &egui::Context) {
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
            ..Config::default()
        };

        // Simulate what the Reset to Defaults button does
        let defaults = Config::default();
        config.polling_interval_ms = defaults.polling_interval_ms;
        config.transparency = defaults.transparency;
        config.visualization = defaults.visualization;
        config.overlay_mode = defaults.overlay_mode;
        config.hotkey = defaults.hotkey;

        assert_eq!(config.polling_interval_ms, 1000);
        assert_eq!(config.transparency, 0.85);
        assert_eq!(config.visualization, Visualization::Gauges);
        assert_eq!(config.overlay_mode, OverlayMode::Interactive);
        assert_eq!(config.hotkey, "Ctrl+Shift+P");
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
}
