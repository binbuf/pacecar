#![windows_subsystem = "windows"]

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
    let (handle, receiver) = spawn_collector(Box::new(collector), interval);

    // Register a CTRL+C handler so the app exits cleanly when launched from a
    // terminal. The handler triggers the collector's shutdown signal and then
    // calls `ExitProcess` to tear down the process immediately (the eframe
    // event loop cannot be signaled to close from an arbitrary thread).
    register_ctrl_handler(handle.shutdown_signal());

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

    let result = eframe::run_native(
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
    );

    // Drop the collector handle explicitly so the background thread is joined
    // before the process exits. The interruptible sleep + join timeout ensure
    // this completes promptly.
    drop(handle);

    result
}

/// Register a Windows console control handler so CTRL+C from a terminal
/// triggers a clean shutdown. When the handler fires, it signals the collector
/// thread to stop and then calls `ExitProcess(0)` because the eframe/winit
/// event loop cannot be woken from an arbitrary OS callback thread.
#[cfg(target_os = "windows")]
fn register_ctrl_handler(shutdown: pacecar::metrics::ShutdownSignal) {
    use std::sync::OnceLock;

    static SHUTDOWN: OnceLock<pacecar::metrics::ShutdownSignal> = OnceLock::new();
    SHUTDOWN.get_or_init(|| shutdown);

    unsafe extern "system" fn handler(ctrl_type: u32) -> i32 {
        // CTRL_C_EVENT = 0, CTRL_BREAK_EVENT = 1, CTRL_CLOSE_EVENT = 2
        if ctrl_type <= 2 {
            if let Some(s) = SHUTDOWN.get() {
                s.trigger();
            }
            // Signal the eframe event loop to initiate a graceful close
            // instead of calling process::exit(), which skips all destructors
            // and can leave GPU resources in a bad state.
            pacecar::CTRL_C_RECEIVED.store(true, std::sync::atomic::Ordering::SeqCst);
            return 1; // handled — don't let Windows kill the process
        }
        0 // not handled
    }

    unsafe extern "system" {
        fn SetConsoleCtrlHandler(
            handler: unsafe extern "system" fn(u32) -> i32,
            add: i32,
        ) -> i32;
    }

    unsafe {
        SetConsoleCtrlHandler(handler, 1);
    }
}

#[cfg(not(target_os = "windows"))]
fn register_ctrl_handler(_shutdown: pacecar::metrics::ShutdownSignal) {
    // On non-Windows platforms, the default SIGINT handler terminates the
    // process, which is acceptable.
}
