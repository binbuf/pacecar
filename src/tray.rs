// System tray icon, menu, events — runs on a dedicated thread with its own
// Win32 message pump so the context menu is always responsive regardless of
// eframe's sleep state.

use std::sync::mpsc;

use eframe::egui;
use muda::{Menu, MenuEvent, MenuItem, PredefinedMenuItem};
use tray_icon::{Icon, TrayIconBuilder, TrayIconEvent};

use crate::config::OverlayMode;

/// Actions the tray can request from the main app.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum TrayAction {
    ToggleVisibility,
    ToggleMode,
    OpenSettings,
    Quit,
}

/// Commands sent from the main thread to the tray thread.
enum TrayCommand {
    UpdateLabels { visible: bool, mode: OverlayMode },
}

/// Manages the system tray icon and context menu on a dedicated background
/// thread.  The tray thread runs its own Win32 message pump so that
/// `TrackPopupMenu` and `Shell_NotifyIcon` callbacks are always serviced,
/// even when the eframe event loop is dormant.
pub struct TrayManager {
    /// Receives actions from the tray thread whenever the user clicks a menu
    /// item or double-clicks the icon.
    action_rx: mpsc::Receiver<TrayAction>,
    /// Sends commands (e.g. label updates) to the tray thread. MenuItem is
    /// !Send so labels must be updated on the thread that owns them.
    command_tx: mpsc::Sender<TrayCommand>,
}

impl TrayManager {
    /// Spawn the tray thread, create the icon and menu, and begin the message
    /// pump.  `ctx` is used to wake the eframe event loop the instant a tray
    /// event fires so that `update()` processes it with near-zero latency.
    ///
    /// If `icon` is `None`, a procedurally generated default icon is used.
    pub fn new(
        visible: bool,
        mode: OverlayMode,
        icon: Option<Icon>,
        ctx: egui::Context,
    ) -> Result<Self, String> {
        let icon = match icon {
            Some(i) => i,
            None => generate_icon().map_err(|e| format!("failed to create tray icon: {e}"))?,
        };

        // Channel for the tray thread to send actions to the main thread.
        let (action_tx, action_rx) = mpsc::channel::<TrayAction>();

        // Channel for the main thread to send commands to the tray thread.
        let (command_tx, command_rx) = mpsc::channel::<TrayCommand>();

        // One-shot channel for the tray thread to report initialisation result.
        let (init_tx, init_rx) = mpsc::channel::<Result<(), String>>();

        std::thread::Builder::new()
            .name("tray".into())
            .spawn(move || {
                tray_thread(icon, visible, mode, ctx, action_tx, command_rx, init_tx);
            })
            .map_err(|e| format!("failed to spawn tray thread: {e}"))?;

        // Block briefly until the tray thread finishes initialisation.
        init_rx
            .recv()
            .map_err(|_| "tray thread exited before initialising".to_string())?
            .map_err(|e| format!("tray init failed: {e}"))?;

        Ok(Self {
            action_rx,
            command_tx,
        })
    }

    /// Non-blocking poll for tray actions.  Drains all pending events and
    /// returns the highest-priority action (Quit > others).
    pub fn poll(&self) -> Option<TrayAction> {
        let mut action: Option<TrayAction> = None;
        while let Ok(a) = self.action_rx.try_recv() {
            if a == TrayAction::Quit || action.is_none() {
                action = Some(a);
            }
        }
        action
    }

    /// Update menu labels to reflect current app state.  The command is sent
    /// to the tray thread which owns the MenuItem handles.
    pub fn update_labels(&self, visible: bool, mode: OverlayMode) {
        let _ = self.command_tx.send(TrayCommand::UpdateLabels { visible, mode });
    }
}

// ---------------------------------------------------------------------------
// Tray thread entry point
// ---------------------------------------------------------------------------

fn tray_thread(
    icon: Icon,
    visible: bool,
    mode: OverlayMode,
    ctx: egui::Context,
    action_tx: mpsc::Sender<TrayAction>,
    command_rx: mpsc::Receiver<TrayCommand>,
    init_tx: mpsc::Sender<Result<(), String>>,
) {
    // Build menu items
    let show_hide_item = MenuItem::new(visibility_label(visible), true, None);
    let mode_item = MenuItem::new(mode_label(mode), true, None);
    let settings_item = MenuItem::new("Settings", true, None);
    let quit_item = MenuItem::new("Quit", true, None);

    let menu = Menu::new();
    if let Err(e) = (|| -> Result<(), muda::Error> {
        menu.append(&show_hide_item)?;
        menu.append(&mode_item)?;
        menu.append(&settings_item)?;
        menu.append(&PredefinedMenuItem::separator())?;
        menu.append(&quit_item)?;
        Ok(())
    })() {
        let _ = init_tx.send(Err(format!("menu error: {e}")));
        return;
    }

    // Keep the tray icon alive for the lifetime of this thread.
    let _tray = match TrayIconBuilder::new()
        .with_menu(Box::new(menu))
        .with_tooltip("Pacecar")
        .with_icon(icon)
        .build()
    {
        Ok(t) => t,
        Err(e) => {
            let _ = init_tx.send(Err(format!("failed to build tray icon: {e}")));
            return;
        }
    };

    // --- Wire up event handlers ---
    // These fire synchronously on THIS thread (inside DispatchMessage) and
    // immediately wake the eframe loop via request_repaint().

    let quit_id = quit_item.id().clone();
    let show_hide_id = show_hide_item.id().clone();
    let mode_id = mode_item.id().clone();
    let settings_id = settings_item.id().clone();

    let menu_tx = action_tx.clone();
    let menu_ctx = ctx.clone();
    MenuEvent::set_event_handler(Some(move |event: MenuEvent| {
        let id = event.id();
        let action = if *id == quit_id {
            Some(TrayAction::Quit)
        } else if *id == show_hide_id {
            Some(TrayAction::ToggleVisibility)
        } else if *id == mode_id {
            Some(TrayAction::ToggleMode)
        } else if *id == settings_id {
            Some(TrayAction::OpenSettings)
        } else {
            None
        };
        if let Some(a) = action {
            let _ = menu_tx.send(a);
            menu_ctx.request_repaint();
        }
    }));

    let tray_tx = action_tx;
    let tray_ctx = ctx;
    TrayIconEvent::set_event_handler(Some(move |event: TrayIconEvent| {
        if matches!(event, TrayIconEvent::DoubleClick { .. }) {
            let _ = tray_tx.send(TrayAction::ToggleVisibility);
            tray_ctx.request_repaint();
        }
    }));

    // Report success — the main thread is blocking on this.
    let _ = init_tx.send(Ok(()));

    // --- Native Win32 message pump ---
    // This keeps the tray's hidden HWND serviced so TrackPopupMenu renders
    // and responds instantly, independent of eframe.
    #[cfg(target_os = "windows")]
    {
        use std::time::Duration;

        #[repr(C)]
        #[allow(non_snake_case)]
        struct MSG {
            hwnd: isize,
            message: u32,
            wParam: usize,
            lParam: isize,
            time: u32,
            pt_x: i32,
            pt_y: i32,
        }

        unsafe extern "system" {
            fn PeekMessageW(
                msg: *mut MSG,
                hwnd: isize,
                filter_min: u32,
                filter_max: u32,
                remove: u32,
            ) -> i32;
            fn TranslateMessage(msg: *const MSG) -> i32;
            fn DispatchMessageW(msg: *const MSG) -> isize;
        }

        const PM_REMOVE: u32 = 0x0001;

        loop {
            // Drain all pending Win32 messages (menu clicks, tray events, etc.)
            loop {
                let mut msg = std::mem::MaybeUninit::<MSG>::uninit();
                let has_msg = unsafe { PeekMessageW(msg.as_mut_ptr(), 0, 0, 0, PM_REMOVE) };
                if has_msg == 0 {
                    break;
                }
                unsafe {
                    let msg = msg.as_ptr();
                    TranslateMessage(msg);
                    DispatchMessageW(msg);
                }
            }

            // Process any pending label update commands from the main thread.
            while let Ok(cmd) = command_rx.try_recv() {
                match cmd {
                    TrayCommand::UpdateLabels { visible, mode } => {
                        show_hide_item.set_text(visibility_label(visible));
                        mode_item.set_text(mode_label(mode));
                    }
                }
            }

            // Sleep briefly to avoid busy-spinning.  16ms ≈ 60 Hz is more
            // than fast enough for tray interactions and keeps CPU near zero.
            std::thread::sleep(Duration::from_millis(16));
        }
    }

    // On non-Windows, just park the thread (tray-icon handles its own loop).
    #[cfg(not(target_os = "windows"))]
    {
        loop {
            // Process label commands periodically.
            while let Ok(cmd) = command_rx.try_recv() {
                match cmd {
                    TrayCommand::UpdateLabels { visible, mode } => {
                        show_hide_item.set_text(visibility_label(visible));
                        mode_item.set_text(mode_label(mode));
                    }
                }
            }
            std::thread::park();
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn visibility_label(visible: bool) -> &'static str {
    if visible { "Hide" } else { "Show" }
}

fn mode_label(mode: OverlayMode) -> &'static str {
    match mode {
        OverlayMode::Interactive => "Mode: Interactive",
        OverlayMode::ClickThrough => "Mode: Click-through",
    }
}

/// Generate a simple 16x16 RGBA icon (a colored gauge/meter circle).
fn generate_icon() -> Result<Icon, String> {
    const SIZE: usize = 16;
    let mut rgba = vec![0u8; SIZE * SIZE * 4];

    let center = SIZE as f32 / 2.0;
    let outer_r = center - 0.5;
    let inner_r = outer_r - 3.0;

    for y in 0..SIZE {
        for x in 0..SIZE {
            let dx = x as f32 + 0.5 - center;
            let dy = y as f32 + 0.5 - center;
            let dist = (dx * dx + dy * dy).sqrt();
            let i = (y * SIZE + x) * 4;

            if dist <= outer_r && dist >= inner_r {
                // Arc ring — teal/green accent
                rgba[i] = 0x00;     // R
                rgba[i + 1] = 0xCC; // G
                rgba[i + 2] = 0xAA; // B
                rgba[i + 3] = 0xFF; // A
            } else if dist < inner_r {
                // Inner fill — dark background
                rgba[i] = 0x1E;     // R
                rgba[i + 1] = 0x1E; // G
                rgba[i + 2] = 0x1E; // B
                rgba[i + 3] = 0xD0; // A
            }
            // Outside the circle: stays transparent (0,0,0,0)
        }
    }

    Icon::from_rgba(rgba, SIZE as u32, SIZE as u32).map_err(|e: tray_icon::BadIcon| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn visibility_label_visible() {
        assert_eq!(visibility_label(true), "Hide");
    }

    #[test]
    fn visibility_label_hidden() {
        assert_eq!(visibility_label(false), "Show");
    }

    #[test]
    fn mode_label_interactive() {
        assert_eq!(mode_label(OverlayMode::Interactive), "Mode: Interactive");
    }

    #[test]
    fn mode_label_click_through() {
        assert_eq!(
            mode_label(OverlayMode::ClickThrough),
            "Mode: Click-through"
        );
    }

    #[test]
    fn generate_icon_succeeds() {
        let icon = generate_icon();
        assert!(icon.is_ok(), "icon generation failed: {:?}", icon.err());
    }
}
