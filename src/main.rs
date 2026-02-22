#![windows_subsystem = "windows"]

use pacecar::app::PacecarApp;
use pacecar::config::Config;
use pacecar::hotkey::HotkeyManager;
use pacecar::icon;
use pacecar::metrics::discovery::discover_devices;
use pacecar::metrics::{CollectorConfig, SystemCollector, spawn_collector};
use pacecar::overlay;
use pacecar::specs;
use pacecar::tray::TrayManager;

use std::sync::{Arc, Mutex};
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

    // Discover available hardware devices (GPUs, CPU cores, NICs, disks).
    let available_devices = discover_devices();

    // Create the shared collector config for device selection.
    let shared_collector_config = Arc::new(Mutex::new(CollectorConfig::from_config(&config)));

    let collector = SystemCollector::new(Arc::clone(&shared_collector_config));
    let interval = Duration::from_millis(config.polling_interval_ms);
    let (handle, receiver) = spawn_collector(Box::new(collector), interval);

    // Register a CTRL+C handler so the app exits cleanly when launched from a
    // terminal. The handler triggers the collector's shutdown signal and then
    // calls `ExitProcess` to tear down the process immediately (the eframe
    // event loop cannot be signaled to close from an arbitrary thread).
    register_ctrl_handler(handle.shutdown_signal());

    let hotkey_manager = HotkeyManager::new(&config.hotkey);

    let specs_receiver = specs::spawn_specs_collector();

    let result = eframe::run_native(
        "Pacecar",
        options,
        Box::new(move |cc| {
            // Create the tray on a dedicated thread now that we have the
            // egui::Context — the tray thread uses it to wake the event loop
            // the instant a menu item is clicked.
            let tray_manager = match TrayManager::new(
                true,
                config.overlay_mode,
                tray_icon,
                cc.egui_ctx.clone(),
            ) {
                Ok(tm) => Some(tm),
                Err(e) => {
                    eprintln!("warn: failed to create system tray: {e}");
                    None
                }
            };

            Ok(Box::new(PacecarApp::new(
                config,
                receiver,
                hotkey_manager,
                tray_manager,
                specs_receiver,
                available_devices,
                Arc::clone(&shared_collector_config),
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
        fn GetConsoleWindow() -> *mut core::ffi::c_void;
        fn SetConsoleCtrlHandler(
            handler: unsafe extern "system" fn(u32) -> i32,
            add: i32,
        ) -> i32;
    }

    // Only register the handler when a console is already attached (i.e. the
    // app was launched from a terminal). Calling SetConsoleCtrlHandler without
    // a console can briefly flash one on screen.
    unsafe {
        if !GetConsoleWindow().is_null() {
            SetConsoleCtrlHandler(handler, 1);
        }
    }
}

#[cfg(not(target_os = "windows"))]
fn register_ctrl_handler(_shutdown: pacecar::metrics::ShutdownSignal) {
    // On non-Windows platforms, the default SIGINT handler terminates the
    // process, which is acceptable.
}
