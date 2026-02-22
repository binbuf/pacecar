mod app;
mod config;
mod hotkey;
mod metrics;
mod overlay;
mod tray;
mod ui;

use app::PacecarApp;
use config::Config;
use hotkey::HotkeyManager;
use metrics::{SystemCollector, spawn_collector};
use tray::TrayManager;

use std::time::Duration;

fn main() -> eframe::Result {
    let config = Config::load();

    let viewport = overlay::build_viewport(&config);

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
    let tray_manager = match TrayManager::new(true, config.overlay_mode) {
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
