use pacecar::app::PacecarApp;
use pacecar::config::Config;
use pacecar::hotkey::HotkeyManager;
use pacecar::icon;
use pacecar::metrics::{SystemCollector, spawn_collector};
use pacecar::overlay;
use pacecar::tray::TrayManager;

use std::time::Duration;

fn main() -> eframe::Result {
    let config = Config::load();

    // Load the app icon from assets/app.ico
    let window_icon = icon::load_window_icon();
    let tray_icon = match icon::load_tray_icon() {
        Ok(i) => Some(i),
        Err(e) => {
            eprintln!("warn: failed to load tray icon: {e}");
            None
        }
    };

    let viewport = overlay::build_viewport(&config, window_icon);

    let options = eframe::NativeOptions {
        viewport,
        ..Default::default()
    };

    let collector = SystemCollector::new();
    let interval = Duration::from_millis(config.polling_interval_ms);
    let (_handle, receiver) = spawn_collector(Box::new(collector), interval);

    let hotkey_manager = HotkeyManager::new(&config.hotkey);

    // Initialize the system tray before the event loop.
    // The tray must be created on the main thread before eframe takes over.
    let tray_manager = match TrayManager::new(true, config.overlay_mode, tray_icon) {
        Ok(tm) => Some(tm),
        Err(e) => {
            eprintln!("warn: failed to create system tray: {e}");
            None
        }
    };

    eframe::run_native(
        "Pacecar",
        options,
        Box::new(move |_cc| {
            Ok(Box::new(PacecarApp::new(
                config,
                receiver,
                hotkey_manager,
                tray_manager,
            )))
        }),
    )
}
