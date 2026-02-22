// egui App impl, main render loop

use eframe::egui;

use crate::config::{Config, OverlayMode, Position};
use crate::overlay;

pub struct PacecarApp {
    config: Config,
    /// Track the last saved position to avoid writing config on every frame.
    last_saved_position: Option<Position>,
}

impl PacecarApp {
    pub fn new(config: Config) -> Self {
        Self {
            last_saved_position: config.window_position,
            config,
        }
    }
}

impl eframe::App for PacecarApp {
    fn clear_color(&self, _visuals: &egui::Visuals) -> [f32; 4] {
        // Fully transparent clear — we paint our own background with alpha
        [0.0, 0.0, 0.0, 0.0]
    }

    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        // Ensure always-on-top is maintained
        ctx.send_viewport_cmd(egui::ViewportCommand::WindowLevel(
            egui::WindowLevel::AlwaysOnTop,
        ));

        // Apply current overlay mode
        overlay::apply_overlay_mode(ctx, self.config.overlay_mode);

        let bg = overlay::background_color(self.config.transparency);
        let panel_frame = egui::Frame::none()
            .fill(bg)
            .rounding(8.0)
            .inner_margin(8.0);

        egui::CentralPanel::default()
            .frame(panel_frame)
            .show(ctx, |ui: &mut egui::Ui| {
                // In interactive mode, allow dragging the window from the background
                if self.config.overlay_mode == OverlayMode::Interactive {
                    let response = ui.interact(
                        ui.max_rect(),
                        egui::Id::new("overlay_drag"),
                        egui::Sense::click_and_drag(),
                    );

                    if response.is_pointer_button_down_on() {
                        ctx.send_viewport_cmd(egui::ViewportCommand::StartDrag);
                    }

                    // Right-click context menu
                    response.context_menu(|ui: &mut egui::Ui| {
                        if ui.button("Click-through mode").clicked() {
                            self.config.overlay_mode = OverlayMode::ClickThrough;
                            overlay::apply_overlay_mode(ctx, self.config.overlay_mode);
                            let _ = self.config.save();
                            ui.close_menu();
                        }
                        if ui.button("Quit").clicked() {
                            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                            ui.close_menu();
                        }
                    });
                }

                // Placeholder content — will be replaced by UI panels in later tasks
                ui.colored_label(egui::Color32::WHITE, "Pacecar Overlay");
            });

        // Persist window position when it changes
        if self.config.overlay_mode == OverlayMode::Interactive {
            if let Some(pos) = overlay::read_window_position(ctx) {
                let changed = self.last_saved_position.as_ref() != Some(&pos);
                if changed {
                    self.config.window_position = Some(pos);
                    self.last_saved_position = Some(pos);
                    let _ = self.config.save();
                }
            }
        }
    }
}
