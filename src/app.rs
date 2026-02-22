// egui App impl, main render loop

use std::time::Duration;

use eframe::egui;

use crate::config::{Config, OverlayMode, Position};
use crate::metrics::{MetricsReceiver, MetricsSnapshot};
use crate::overlay;
use crate::ui;

pub struct PacecarApp {
    config: Config,
    receiver: MetricsReceiver,
    /// Most recent snapshot received from the collector thread.
    snapshot: Option<MetricsSnapshot>,
    /// Track the last saved position to avoid writing config on every frame.
    last_saved_position: Option<Position>,
    /// Whether visuals have been configured.
    visuals_configured: bool,
}

impl PacecarApp {
    pub fn new(config: Config, receiver: MetricsReceiver) -> Self {
        Self {
            last_saved_position: config.window_position,
            config,
            receiver,
            snapshot: None,
            visuals_configured: false,
        }
    }
}

impl eframe::App for PacecarApp {
    fn clear_color(&self, _visuals: &egui::Visuals) -> [f32; 4] {
        // Fully transparent clear — we paint our own background with alpha
        [0.0, 0.0, 0.0, 0.0]
    }

    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        // One-time visuals setup
        if !self.visuals_configured {
            ui::configure_visuals(ctx);
            self.visuals_configured = true;
        }

        // Receive latest metrics (non-blocking)
        if let Some(snap) = self.receiver.latest() {
            self.snapshot = Some(snap);
        }

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
            .show(ctx, |ui_ctx: &mut egui::Ui| {
                // In interactive mode, allow dragging the window from the background
                if self.config.overlay_mode == OverlayMode::Interactive {
                    let response = ui_ctx.interact(
                        ui_ctx.max_rect(),
                        egui::Id::new("overlay_drag"),
                        egui::Sense::click_and_drag(),
                    );

                    if response.is_pointer_button_down_on() {
                        ctx.send_viewport_cmd(egui::ViewportCommand::StartDrag);
                    }

                    // Right-click context menu
                    response.context_menu(|ui_ctx: &mut egui::Ui| {
                        if ui_ctx.button("Click-through mode").clicked() {
                            self.config.overlay_mode = OverlayMode::ClickThrough;
                            overlay::apply_overlay_mode(ctx, self.config.overlay_mode);
                            let _ = self.config.save();
                            ui_ctx.close_menu();
                        }
                        if ui_ctx.button("Quit").clicked() {
                            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                            ui_ctx.close_menu();
                        }
                    });
                }

                // Render metric panels or placeholder
                if let Some(snapshot) = &self.snapshot {
                    ui::render_layout(ui_ctx, snapshot, self.config.visualization);
                } else {
                    ui_ctx.colored_label(
                        egui::Color32::from_gray(150),
                        "Waiting for metrics\u{2026}",
                    );
                }
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

        // Schedule next repaint to match polling interval (avoid burning CPU)
        ctx.request_repaint_after(Duration::from_millis(self.config.polling_interval_ms));
    }
}
