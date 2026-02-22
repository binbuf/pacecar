// egui App impl, main render loop

use std::time::Duration;

use eframe::egui;

use crate::config::{Config, OverlayMode, Position};
use crate::hotkey::{HotkeyAction, HotkeyManager};
use crate::metrics::{MetricsReceiver, MetricsSnapshot};
use crate::overlay;
use crate::tray::{TrayAction, TrayManager};
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
    /// Whether the settings overlay is open.
    show_settings: bool,
    /// Global hotkey manager (None if registration failed).
    hotkey_manager: Option<HotkeyManager>,
    /// System tray manager (None if tray creation failed).
    tray_manager: Option<TrayManager>,
    /// Whether the overlay is currently visible.
    visible: bool,
}

impl PacecarApp {
    pub fn new(
        config: Config,
        receiver: MetricsReceiver,
        hotkey_manager: Option<HotkeyManager>,
        tray_manager: Option<TrayManager>,
    ) -> Self {
        Self {
            last_saved_position: config.window_position,
            config,
            receiver,
            snapshot: None,
            visuals_configured: false,
            show_settings: false,
            hotkey_manager,
            tray_manager,
            visible: true,
        }
    }

    /// Toggle overlay visibility and update the tray menu label.
    fn toggle_visibility(&mut self, ctx: &egui::Context) {
        self.visible = !self.visible;
        ctx.send_viewport_cmd(egui::ViewportCommand::Visible(self.visible));

        // If becoming visible while in click-through mode, switch to interactive
        if self.visible && self.config.overlay_mode == OverlayMode::ClickThrough {
            self.config.overlay_mode = OverlayMode::Interactive;
            overlay::apply_overlay_mode(ctx, self.config.overlay_mode);
            let _ = self.config.save();
        }

        self.sync_tray_labels();
    }

    /// Toggle overlay mode between Interactive and Click-through.
    fn toggle_mode(&mut self, ctx: &egui::Context) {
        self.config.overlay_mode = overlay::toggle_overlay_mode(ctx, self.config.overlay_mode);
        let _ = self.config.save();
        self.sync_tray_labels();
    }

    /// Update tray menu labels to reflect current state.
    fn sync_tray_labels(&self) {
        if let Some(ref tray) = self.tray_manager {
            tray.update_labels(self.visible, self.config.overlay_mode);
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

        // Poll for global hotkey events
        if let Some(ref hk) = self.hotkey_manager {
            if let Some(HotkeyAction::ToggleOverlay) = hk.poll() {
                self.toggle_visibility(ctx);
            }
        }

        // Poll for tray events
        if let Some(ref _tray) = self.tray_manager {
            if let Some(action) = _tray.poll() {
                match action {
                    TrayAction::ToggleVisibility => {
                        self.toggle_visibility(ctx);
                    }
                    TrayAction::ToggleMode => {
                        self.toggle_mode(ctx);
                    }
                    TrayAction::OpenSettings => {
                        // Ensure the overlay is visible so the user can see settings
                        if !self.visible {
                            self.visible = true;
                            ctx.send_viewport_cmd(egui::ViewportCommand::Visible(true));
                            self.sync_tray_labels();
                        }
                        self.show_settings = true;
                    }
                    TrayAction::Quit => {
                        let _ = self.config.save();
                        ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                    }
                }
            }
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
                        if ui_ctx.button("Settings").clicked() {
                            self.show_settings = true;
                            ui_ctx.close_menu();
                        }
                        if ui_ctx.button("Click-through mode").clicked() {
                            self.config.overlay_mode = OverlayMode::ClickThrough;
                            overlay::apply_overlay_mode(ctx, self.config.overlay_mode);
                            let _ = self.config.save();
                            self.sync_tray_labels();
                            ui_ctx.close_menu();
                        }
                        if ui_ctx.button("Quit").clicked() {
                            let _ = self.config.save();
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

        // Settings overlay
        if self.show_settings {
            if !ui::settings::show_settings(ctx, &mut self.config) {
                self.show_settings = false;
            }
            // Re-apply overlay mode in case settings changed it
            overlay::apply_overlay_mode(ctx, self.config.overlay_mode);
            self.sync_tray_labels();
        }

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

        // Handle window close: hide to tray instead of quitting (when tray is available)
        if ctx.input(|i| i.viewport().close_requested()) {
            if self.tray_manager.is_some() {
                // Cancel the close and hide to tray instead
                ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
                self.visible = false;
                ctx.send_viewport_cmd(egui::ViewportCommand::Visible(false));
                self.sync_tray_labels();
            } else {
                // No tray — allow the close (save config first)
                let _ = self.config.save();
            }
        }

        // Schedule next repaint to match polling interval (avoid burning CPU)
        ctx.request_repaint_after(Duration::from_millis(self.config.polling_interval_ms));
    }
}
