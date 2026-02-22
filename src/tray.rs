// System tray icon, menu, events

use muda::{Menu, MenuEvent, MenuItem, PredefinedMenuItem};
use tray_icon::{Icon, TrayIcon, TrayIconBuilder, TrayIconEvent};

use crate::config::OverlayMode;

/// Actions the tray can request from the main app.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum TrayAction {
    ToggleVisibility,
    ToggleMode,
    OpenSettings,
    Quit,
}

/// Manages the system tray icon and context menu.
pub struct TrayManager {
    _tray: TrayIcon,
    show_hide_item: MenuItem,
    mode_item: MenuItem,
    settings_item: MenuItem,
    quit_item: MenuItem,
}

impl TrayManager {
    /// Create and display the system tray icon with context menu.
    /// Must be called before the eframe event loop starts.
    pub fn new(visible: bool, mode: OverlayMode) -> Result<Self, String> {
        let icon = generate_icon().map_err(|e| format!("failed to create tray icon: {e}"))?;

        let show_hide_item = MenuItem::new(visibility_label(visible), true, None);
        let mode_item = MenuItem::new(mode_label(mode), true, None);
        let settings_item = MenuItem::new("Settings", true, None);
        let quit_item = MenuItem::new("Quit", true, None);

        let menu = Menu::new();
        menu.append(&show_hide_item).map_err(|e| format!("menu error: {e}"))?;
        menu.append(&mode_item).map_err(|e| format!("menu error: {e}"))?;
        menu.append(&settings_item).map_err(|e| format!("menu error: {e}"))?;
        menu.append(&PredefinedMenuItem::separator()).map_err(|e| format!("menu error: {e}"))?;
        menu.append(&quit_item).map_err(|e| format!("menu error: {e}"))?;

        let tray = TrayIconBuilder::new()
            .with_menu(Box::new(menu))
            .with_tooltip("Pacecar")
            .with_icon(icon)
            .build()
            .map_err(|e| format!("failed to build tray icon: {e}"))?;

        Ok(Self {
            _tray: tray,
            show_hide_item,
            mode_item,
            settings_item,
            quit_item,
        })
    }

    /// Poll for tray menu events. Returns `Some(TrayAction)` if a menu item was clicked,
    /// or if the tray icon was double-clicked.
    pub fn poll(&self) -> Option<TrayAction> {
        // Check for double-click on the tray icon
        if let Ok(event) = TrayIconEvent::receiver().try_recv() {
            if matches!(event, TrayIconEvent::DoubleClick { .. }) {
                return Some(TrayAction::ToggleVisibility);
            }
        }

        // Check for menu item clicks
        if let Ok(event) = MenuEvent::receiver().try_recv() {
            let id = event.id();
            if id == self.show_hide_item.id() {
                return Some(TrayAction::ToggleVisibility);
            }
            if id == self.mode_item.id() {
                return Some(TrayAction::ToggleMode);
            }
            if id == self.settings_item.id() {
                return Some(TrayAction::OpenSettings);
            }
            if id == self.quit_item.id() {
                return Some(TrayAction::Quit);
            }
        }

        None
    }

    /// Update menu labels to reflect current app state.
    pub fn update_labels(&self, visible: bool, mode: OverlayMode) {
        self.show_hide_item.set_text(visibility_label(visible));
        self.mode_item.set_text(mode_label(mode));
    }
}

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
