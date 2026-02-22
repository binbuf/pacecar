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

    eframe::run_native(
        "Pacecar",
        options,
        Box::new(move |_cc| Ok(Box::new(PacecarApp::new(config, receiver, hotkey_manager)))),
    )
}
