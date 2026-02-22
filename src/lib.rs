pub static CTRL_C_RECEIVED: std::sync::atomic::AtomicBool =
    std::sync::atomic::AtomicBool::new(false);

pub mod app;
pub mod config;
pub mod hotkey;
pub mod icon;
pub mod metrics;
pub mod overlay;
pub mod tray;
pub mod ui;
