// Settings overlay/modal

use eframe::egui;

use crate::config::{Config, OverlayMode, Visualization};

/// The allowed polling interval presets in milliseconds.
const POLLING_PRESETS: &[u64] = &[250, 500, 1000, 2000, 5000];

/// Renders the settings panel as a floating egui::Window.
/// Returns `true` if the panel is still open, `false` if it was closed.
pub fn show_settings(ctx: &egui::Context, config: &mut Config) -> bool {
    let mut open = true;
    let mut changed = false;

    egui::Window::new("Settings")
        .open(&mut open)
        .collapsible(false)
        .resizable(false)
        .anchor(egui::Align2::CENTER_CENTER, egui::vec2(0.0, 0.0))
        .default_width(260.0)
        .show(ctx, |ui| {
            ui.spacing_mut().item_spacing = egui::vec2(6.0, 8.0);

            // --- Polling Interval ---
            ui.label(
                egui::RichText::new("Polling Interval")
                    .color(egui::Color32::from_gray(200))
                    .strong(),
            );
            let current_label = format!("{} ms", config.polling_interval_ms);
            egui::ComboBox::from_id_salt("polling_interval")
                .selected_text(&current_label)
                .show_ui(ui, |ui| {
                    for &ms in POLLING_PRESETS {
                        let label = format!("{ms} ms");
                        if ui
                            .selectable_value(&mut config.polling_interval_ms, ms, label)
                            .changed()
                        {
                            changed = true;
                        }
                    }
                });

            ui.add_space(2.0);

            // --- Transparency ---
            ui.label(
                egui::RichText::new("Transparency")
                    .color(egui::Color32::from_gray(200))
                    .strong(),
            );
            let mut pct = config.transparency * 100.0;
            let slider = egui::Slider::new(&mut pct, 10.0..=100.0)
                .suffix("%")
                .fixed_decimals(0);
            if ui.add(slider).changed() {
                config.transparency = pct / 100.0;
                changed = true;
            }

            ui.add_space(2.0);

            // --- Visualization Mode ---
            ui.label(
                egui::RichText::new("Visualization")
                    .color(egui::Color32::from_gray(200))
                    .strong(),
            );
            ui.horizontal(|ui| {
                if ui
                    .radio_value(&mut config.visualization, Visualization::Gauges, "Gauges")
                    .changed()
                {
                    changed = true;
                }
                if ui
                    .radio_value(
                        &mut config.visualization,
                        Visualization::Sparklines,
                        "Sparklines",
                    )
                    .changed()
                {
                    changed = true;
                }
            });

            ui.add_space(2.0);

            // --- Overlay Mode ---
            ui.label(
                egui::RichText::new("Overlay Mode")
                    .color(egui::Color32::from_gray(200))
                    .strong(),
            );
            ui.horizontal(|ui| {
                if ui
                    .radio_value(
                        &mut config.overlay_mode,
                        OverlayMode::Interactive,
                        "Interactive",
                    )
                    .changed()
                {
                    changed = true;
                }
                if ui
                    .radio_value(
                        &mut config.overlay_mode,
                        OverlayMode::ClickThrough,
                        "Click-through",
                    )
                    .changed()
                {
                    changed = true;
                }
            });

            ui.add_space(2.0);

            // --- Hotkey ---
            ui.label(
                egui::RichText::new("Hotkey")
                    .color(egui::Color32::from_gray(200))
                    .strong(),
            );
            if ui.text_edit_singleline(&mut config.hotkey).changed() {
                changed = true;
            }

            ui.add_space(6.0);
            ui.separator();
            ui.add_space(2.0);

            // --- Reset to Defaults ---
            if ui.button("Reset to Defaults").clicked() {
                let defaults = Config::default();
                config.polling_interval_ms = defaults.polling_interval_ms;
                config.transparency = defaults.transparency;
                config.visualization = defaults.visualization;
                config.overlay_mode = defaults.overlay_mode;
                config.hotkey = defaults.hotkey;
                changed = true;
            }
        });

    // Save on any change (live preview — changes are applied immediately via config mutation)
    if changed {
        config.clamp();
        let _ = config.save();
    }

    open
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
        // Simulate slider <-> config conversion
        let config_value: f32 = 0.75;
        let slider_pct = config_value * 100.0; // 75.0
        let back = slider_pct / 100.0;
        assert!((back - config_value).abs() < f32::EPSILON);
    }

    #[test]
    fn transparency_clamp_after_edit() {
        let mut config = Config::default();
        // Simulate an out-of-range slider value that somehow gets through
        config.transparency = 0.05;
        config.clamp();
        assert_eq!(config.transparency, 0.1);

        config.transparency = 1.5;
        config.clamp();
        assert_eq!(config.transparency, 1.0);
    }
}
