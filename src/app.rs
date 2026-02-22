// egui App impl, main render loop

use std::sync::mpsc;
use std::time::Duration;

use eframe::egui;

use crate::config::{Config, OverlayMode, Position, Size};
use crate::hotkey::{HotkeyAction, HotkeyManager};
use crate::metrics::{MetricsReceiver, MetricsSnapshot};
use crate::overlay;
use crate::specs::SystemSpecs;
use crate::tray::{TrayAction, TrayManager};
use crate::ui;

pub struct PacecarApp {
    config: Config,
    receiver: MetricsReceiver,
    /// Most recent snapshot received from the collector thread.
    snapshot: Option<MetricsSnapshot>,
    /// Track the last saved position to avoid writing config on every frame.
    last_saved_position: Option<Position>,
    /// Track the last saved size to avoid writing config on every frame.
    last_saved_size: Size,
    /// Whether visuals have been configured.
    visuals_configured: bool,
    /// Whether the settings overlay is open.
    show_settings: bool,
    /// Global hotkey manager (None if registration failed).
    hotkey_manager: Option<HotkeyManager>,
    /// System tray manager (None if tray creation failed).
    tray_manager: Option<TrayManager>,
    /// Whether the specs view is open.
    show_specs: bool,
    /// Cached hardware specs (populated once from background thread).
    specs: Option<SystemSpecs>,
    /// Receiver for the one-shot specs collection result.
    specs_receiver: Option<mpsc::Receiver<SystemSpecs>>,
    /// Whether the overlay is currently visible.
    visible: bool,
    /// Set to true when the user requests a full quit (tray Quit or context menu Quit).
    /// Distinguishes quit from the X-button close (which hides to tray).
    quit_requested: bool,
    /// Two-phase close countdown. When > 0, we decrement each frame and only
    /// issue `ViewportCommand::Close` when it reaches 0. This gives the GPU
    /// driver a frame or two to flush pending commands before surface destruction.
    close_countdown: u8,
    /// Whether the background wakeup thread has been spawned.
    wakeup_spawned: bool,
    /// Last overlay mode applied to the viewport, used to avoid sending
    /// redundant MousePassthrough commands every frame.
    last_applied_mode: OverlayMode,
    /// Saved position before hiding. Used to restore the window when un-hiding
    /// instead of using ViewportCommand::Visible which suspends the eframe loop.
    pre_hide_position: Option<Position>,
    /// Overlay mode before hiding, so we can restore passthrough state.
    pre_hide_mode: OverlayMode,
}

impl PacecarApp {
    pub fn new(
        config: Config,
        receiver: MetricsReceiver,
        hotkey_manager: Option<HotkeyManager>,
        tray_manager: Option<TrayManager>,
        specs_receiver: mpsc::Receiver<SystemSpecs>,
    ) -> Self {
        let initial_mode = config.overlay_mode;
        Self {
            last_saved_position: config.window_position,
            last_saved_size: config.window_size,
            config,
            receiver,
            snapshot: None,
            visuals_configured: false,
            show_settings: false,
            show_specs: false,
            specs: None,
            specs_receiver: Some(specs_receiver),
            hotkey_manager,
            tray_manager,
            visible: true,
            quit_requested: false,
            close_countdown: 0,
            wakeup_spawned: false,
            last_applied_mode: initial_mode,
            pre_hide_position: None,
            pre_hide_mode: initial_mode,
        }
    }

    /// Toggle overlay visibility and update the tray menu label.
    ///
    /// Instead of using `ViewportCommand::Visible(false)` — which suspends the
    /// eframe event loop entirely on Windows (egui #5229) — we move the window
    /// far off-screen and enable mouse passthrough.  The root viewport stays
    /// "visible" to eframe so the event loop keeps pumping and tray/hotkey
    /// events continue to be processed.
    fn toggle_visibility(&mut self, ctx: &egui::Context) {
        self.visible = !self.visible;

        if self.visible {
            // Restore: move window back to its saved position
            if let Some(pos) = self.pre_hide_position.take() {
                ctx.send_viewport_cmd(egui::ViewportCommand::OuterPosition(
                    egui::pos2(pos.x as f32, pos.y as f32),
                ));
            }
            // Restore the overlay mode that was active before hiding
            self.config.overlay_mode = self.pre_hide_mode;
            overlay::apply_overlay_mode(ctx, self.config.overlay_mode);
        } else {
            // Save current position and mode before hiding
            self.pre_hide_position = overlay::read_window_position(ctx);
            self.pre_hide_mode = self.config.overlay_mode;
            // Move off-screen and enable passthrough so the window is invisible
            // but the event loop keeps running
            ctx.send_viewport_cmd(egui::ViewportCommand::OuterPosition(
                egui::pos2(-10000.0, -10000.0),
            ));
            ctx.send_viewport_cmd(egui::ViewportCommand::MousePassthrough(true));
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
        // Spawn a background thread that periodically wakes the event loop.
        // This guarantees update() is called even when the window is hidden,
        // so tray and hotkey events are always processed promptly.
        if !self.wakeup_spawned {
            self.wakeup_spawned = true;
            let ctx_clone = ctx.clone();
            std::thread::Builder::new()
                .name("wakeup".into())
                .spawn(move || loop {
                    std::thread::sleep(Duration::from_millis(100));
                    ctx_clone.request_repaint();
                })
                .ok();
        }

        // Two-phase close: count down frames before issuing Close to let the
        // GPU driver flush pending commands before surface destruction.
        if self.close_countdown > 0 {
            self.close_countdown -= 1;
            if self.close_countdown == 0 {
                ctx.send_viewport_cmd(egui::ViewportCommand::Close);
            } else {
                ctx.request_repaint();
            }
        }

        // Check for CTRL+C signal from the console handler
        if crate::CTRL_C_RECEIVED.load(std::sync::atomic::Ordering::SeqCst) && !self.quit_requested
        {
            self.quit_requested = true;
            let _ = self.config.save();
            if !self.visible {
                if let Some(pos) = self.pre_hide_position.take() {
                    ctx.send_viewport_cmd(egui::ViewportCommand::OuterPosition(
                        egui::pos2(pos.x as f32, pos.y as f32),
                    ));
                }
            }
            self.close_countdown = 2;
            ctx.request_repaint();
        }

        // One-time visuals setup
        if !self.visuals_configured {
            ui::configure_visuals(ctx);
            self.visuals_configured = true;
        }

        // Receive latest metrics (non-blocking)
        if let Some(snap) = self.receiver.latest() {
            self.snapshot = Some(snap);
        }

        // Poll for specs result (one-shot)
        if let Some(ref rx) = self.specs_receiver {
            if let Ok(specs) = rx.try_recv() {
                self.specs = Some(specs);
                self.specs_receiver = None;
            }
        }

        // Poll for global hotkey events
        if let Some(ref hk) = self.hotkey_manager {
            if let Some(HotkeyAction::ToggleOverlay) = hk.poll() {
                self.toggle_visibility(ctx);
            }
        }

        // Poll for tray events
        if let Some(ref tray) = self.tray_manager {
            if let Some(action) = tray.poll() {
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
                            self.toggle_visibility(ctx);
                        }
                        self.show_settings = true;
                    }
                    TrayAction::Quit => {
                        self.quit_requested = true;
                        let _ = self.config.save();
                        // Restore window on-screen so the close command is processed
                        if !self.visible {
                            if let Some(pos) = self.pre_hide_position.take() {
                                ctx.send_viewport_cmd(egui::ViewportCommand::OuterPosition(
                                    egui::pos2(pos.x as f32, pos.y as f32),
                                ));
                            }
                        }
                        self.close_countdown = 2;
                        ctx.request_repaint();
                    }
                }
            }
        }

        // Re-apply overlay mode only when it actually changes (sending
        // MousePassthrough every frame steals focus from tray popups).
        if self.config.overlay_mode != self.last_applied_mode {
            self.last_applied_mode = self.config.overlay_mode;
            overlay::apply_overlay_mode(ctx, self.config.overlay_mode);
        }

        let bg = overlay::background_color(self.config.transparency);
        let panel_frame = egui::Frame::NONE
            .fill(bg)
            .corner_radius(8.0)
            .inner_margin(8.0);

        egui::CentralPanel::default()
            .frame(panel_frame)
            .show(ctx, |ui_ctx: &mut egui::Ui| {
                // In interactive mode, handle edge resize and dragging
                if self.config.overlay_mode == OverlayMode::Interactive {
                    overlay::handle_edge_resize(ctx, ui_ctx);

                    // Use the content rect (inside margins) instead of max_rect
                    // so that the transparent edge pixels and rounded corners don't
                    // intercept OS pointer events meant for windows underneath
                    // (e.g. the taskbar / system tray).
                    let drag_rect = ui_ctx.max_rect().shrink(8.0);
                    let response = ui_ctx.interact(
                        drag_rect,
                        egui::Id::new("overlay_drag"),
                        egui::Sense::click_and_drag(),
                    );

                    // Right-click context menu (must come before drag to avoid swallowing clicks)
                    let mut context_menu_open = false;
                    response.context_menu(|ui_ctx: &mut egui::Ui| {
                        context_menu_open = true;
                        if ui_ctx.button("Settings").clicked() {
                            self.show_settings = true;
                            ui_ctx.close();
                        }
                        if ui_ctx.button("Click-through mode").clicked() {
                            self.config.overlay_mode = OverlayMode::ClickThrough;
                            overlay::apply_overlay_mode(ctx, self.config.overlay_mode);
                            let _ = self.config.save();
                            self.sync_tray_labels();
                            ui_ctx.close();
                        }
                        if ui_ctx.button("Quit").clicked() {
                            self.quit_requested = true;
                            let _ = self.config.save();
                            self.close_countdown = 2;
                            ctx.request_repaint();
                            ui_ctx.close();
                        }
                    });

                    if response.drag_started_by(egui::PointerButton::Primary) && !context_menu_open {
                        ctx.send_viewport_cmd(egui::ViewportCommand::StartDrag);
                    }
                }

                // Header bar with gear and specs buttons
                if self.config.overlay_mode == OverlayMode::Interactive {
                    match ui::render_header(ui_ctx) {
                        ui::HeaderAction::OpenSettings => {
                            self.show_settings = true;
                        }
                        ui::HeaderAction::OpenSpecs => {
                            self.show_specs = !self.show_specs;
                        }
                        ui::HeaderAction::None => {}
                    }
                }

                // Render specs view or metric panels
                if self.show_specs {
                    if let Some(ref specs) = self.specs {
                        ui::specs::render_specs(ui_ctx, specs);
                    } else {
                        ui_ctx.colored_label(
                            egui::Color32::from_gray(150),
                            "Loading specs\u{2026}",
                        );
                    }
                } else if let Some(snapshot) = &self.snapshot {
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
            // Re-apply overlay mode only if settings changed it
            if self.config.overlay_mode != self.last_applied_mode {
                self.last_applied_mode = self.config.overlay_mode;
                overlay::apply_overlay_mode(ctx, self.config.overlay_mode);
            }
            self.sync_tray_labels();
        }

        // Persist window position and size when they change (skip when hidden off-screen)
        if self.visible && self.config.overlay_mode == OverlayMode::Interactive {
            let mut layout_changed = false;

            if let Some(pos) = overlay::read_window_position(ctx) {
                if self.last_saved_position.as_ref() != Some(&pos) {
                    self.config.window_position = Some(pos);
                    self.last_saved_position = Some(pos);
                    layout_changed = true;
                }
            }

            if let Some(size) = overlay::read_window_size(ctx) {
                if self.last_saved_size != size {
                    self.config.window_size = size;
                    self.last_saved_size = size;
                    layout_changed = true;
                }
            }

            if layout_changed {
                let _ = self.config.save();
            }
        }

        // Handle window close: hide to tray instead of quitting (when tray is available)
        if ctx.input(|i| i.viewport().close_requested()) {
            if self.quit_requested {
                // Explicit quit — allow the close (config already saved by quit handler)
            } else if self.tray_manager.is_some() {
                // X button with tray available — cancel the close and hide to tray
                ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
                if self.visible {
                    self.toggle_visibility(ctx);
                }
            } else {
                // No tray — allow the close (save config first)
                let _ = self.config.save();
            }
        }

        // Schedule next repaint at a fast cadence so tray/hotkey events are
        // processed promptly (~100ms latency). The metrics collector runs on its
        // own background thread, so this interval is independent of polling_interval_ms.
        ctx.request_repaint_after(Duration::from_millis(100));
    }
}
