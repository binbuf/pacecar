// App icon loading from pre-converted raw RGBA assets.

use eframe::egui;

/// Pre-converted 32×32 RGBA icon (4096 bytes).
const ICON_32: &[u8] = include_bytes!("../assets/app_32x32.rgba");
/// Pre-converted 48×48 RGBA icon (9216 bytes).
const ICON_48: &[u8] = include_bytes!("../assets/app_48x48.rgba");

/// Load the app icon for use with eframe's viewport (window icon).
pub fn load_window_icon() -> Option<egui::IconData> {
    Some(egui::IconData {
        rgba: ICON_48.to_vec(),
        width: 48,
        height: 48,
    })
}

/// Load the app icon for use with the system tray.
pub fn load_tray_icon() -> Result<tray_icon::Icon, String> {
    tray_icon::Icon::from_rgba(ICON_32.to_vec(), 32, 32)
        .map_err(|e| format!("failed to create tray icon: {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn icon_32_has_correct_size() {
        assert_eq!(ICON_32.len(), 32 * 32 * 4);
    }

    #[test]
    fn icon_48_has_correct_size() {
        assert_eq!(ICON_48.len(), 48 * 48 * 4);
    }

    #[test]
    fn load_window_icon_succeeds() {
        assert!(load_window_icon().is_some());
    }

    #[test]
    fn load_tray_icon_succeeds() {
        assert!(load_tray_icon().is_ok());
    }
}
