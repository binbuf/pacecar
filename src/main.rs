mod app;
mod config;
mod hotkey;
mod metrics;
mod overlay;
mod tray;
mod ui;

use app::PacecarApp;
use config::Config;

fn main() -> eframe::Result {
    let config = Config::load();

    let viewport = overlay::build_viewport(&config);

    let options = eframe::NativeOptions {
        viewport,
        ..Default::default()
    };

    eframe::run_native(
        "Pacecar",
        options,
        Box::new(move |_cc| Ok(Box::new(PacecarApp::new(config)))),
    )
}
