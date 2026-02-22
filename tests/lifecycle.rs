// Integration tests for app lifecycle: startup sequence, config round-trip, and shutdown.

use std::time::Duration;

#[test]
fn config_round_trip_through_lifecycle() {
    // Simulate the lifecycle: load config, modify it, save, reload, verify.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("config.json");

    // 1. Start with defaults (simulates first launch — no config file exists)
    let mut config = pacecar::config::Config::default();
    assert_eq!(config.polling_interval_ms, 1000);
    assert_eq!(config.transparency, 0.85);

    // 2. User changes settings during the session
    config.polling_interval_ms = 500;
    config.transparency = 0.7;
    config.visualization = pacecar::config::Visualization::Sparklines;
    config.window_position = Some(pacecar::config::Position { x: 200.0, y: 150.0 });
    config.hotkey = "Alt+F1".to_string();

    // 3. Save on quit
    config.save_to_path(&path).unwrap();

    // 4. Reload on next startup
    let reloaded = pacecar::config::Config::load_from_path(Some(path));
    assert_eq!(reloaded.polling_interval_ms, 500);
    assert_eq!(reloaded.transparency, 0.7);
    assert_eq!(reloaded.visualization, pacecar::config::Visualization::Sparklines);
    assert_eq!(
        reloaded.window_position,
        Some(pacecar::config::Position { x: 200.0, y: 150.0 })
    );
    assert_eq!(reloaded.hotkey, "Alt+F1");
    // Unchanged fields retain defaults
    assert_eq!(reloaded.overlay_mode, pacecar::config::OverlayMode::Interactive);
    assert_eq!(reloaded.theme, pacecar::config::Theme::Dark);
}

#[test]
fn metrics_collector_starts_and_shuts_down_cleanly() {
    // Simulate the startup/shutdown lifecycle for the metrics subsystem.
    let collector = pacecar::metrics::SystemCollector::new();
    let interval = Duration::from_millis(100);

    let (handle, receiver) = pacecar::metrics::spawn_collector(Box::new(collector), interval);

    // Wait for at least one snapshot to arrive
    std::thread::sleep(Duration::from_millis(300));
    let snapshot = receiver.latest();
    assert!(snapshot.is_some(), "should receive at least one snapshot");

    // Verify snapshot has reasonable data
    let snap = snapshot.unwrap();
    assert!(snap.cpu.total_usage >= 0.0);
    assert!(snap.memory.total_bytes > 0);

    // Shutdown — should not hang or panic
    handle.shutdown();
}

#[test]
fn collector_handle_drop_triggers_shutdown() {
    // Verify that dropping the CollectorHandle (as happens when main() returns)
    // cleanly stops the background thread.
    let collector = pacecar::metrics::SystemCollector::new();
    let (_handle, _receiver) =
        pacecar::metrics::spawn_collector(Box::new(collector), Duration::from_millis(50));

    std::thread::sleep(Duration::from_millis(100));

    // Dropping both handle and receiver should not hang.
    // (This test will timeout if shutdown is broken.)
}

#[test]
fn startup_sequence_order() {
    // Verify the startup steps can execute in the correct order without panicking.
    // This mirrors main() but without launching the GUI event loop.

    // Step 1: Load config
    let config = pacecar::config::Config::default();

    // Step 2: Build viewport (doesn't need a display)
    let _viewport = pacecar::overlay::build_viewport(&config, None);

    // Step 3: Start metrics collector
    let collector = pacecar::metrics::SystemCollector::new();
    let interval = Duration::from_millis(config.polling_interval_ms);
    let (handle, _receiver) = pacecar::metrics::spawn_collector(Box::new(collector), interval);

    // Step 4: Register hotkey (may fail in CI — that's fine, it returns None)
    let _hotkey = pacecar::hotkey::HotkeyManager::new(&config.hotkey);

    // Step 5: Would launch eframe here — skip in tests

    // Shutdown
    handle.shutdown();
}

#[test]
fn config_saves_window_position_on_change() {
    // Simulate the pattern in app.rs where position is saved when it changes.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("config.json");

    let mut config = pacecar::config::Config::default();
    assert!(config.window_position.is_none());

    // Simulate user dragging the window
    let new_pos = pacecar::config::Position { x: 500.0, y: 300.0 };
    config.window_position = Some(new_pos);
    config.save_to_path(&path).unwrap();

    // Verify it persists
    let reloaded = pacecar::config::Config::load_from_path(Some(path));
    assert_eq!(reloaded.window_position, Some(new_pos));
}
