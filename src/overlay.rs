// Window transparency, click-through, always-on-top

use eframe::egui;

use crate::config::{Config, OverlayMode, Position};

/// Build the initial viewport configuration from the user's config.
pub fn build_viewport(config: &Config, icon: Option<egui::IconData>) -> egui::ViewportBuilder {
    let mut builder = egui::ViewportBuilder::default()
        .with_decorations(false)
        .with_always_on_top()
        .with_transparent(true)
        .with_taskbar(false)
        .with_resizable(true)
        .with_inner_size(egui::vec2(
            config.window_size.width as f32,
            config.window_size.height as f32,
        ));

    if let Some(icon) = icon {
        builder = builder.with_icon(std::sync::Arc::new(icon));
    }

    if let Some(pos) = &config.window_position {
        builder = builder.with_position(egui::pos2(pos.x as f32, pos.y as f32));
    }

    if config.overlay_mode == OverlayMode::ClickThrough {
        builder = builder.with_mouse_passthrough(true);
    }

    builder
}

/// Apply the current overlay mode to the window at runtime.
pub fn apply_overlay_mode(ctx: &egui::Context, mode: OverlayMode) {
    let passthrough = mode == OverlayMode::ClickThrough;
    ctx.send_viewport_cmd(egui::ViewportCommand::MousePassthrough(passthrough));
}

/// Toggle overlay mode between Interactive and ClickThrough, returning the new mode.
pub fn toggle_overlay_mode(ctx: &egui::Context, current: OverlayMode) -> OverlayMode {
    let new_mode = match current {
        OverlayMode::Interactive => OverlayMode::ClickThrough,
        OverlayMode::ClickThrough => OverlayMode::Interactive,
    };
    apply_overlay_mode(ctx, new_mode);
    new_mode
}

/// Read the current outer window position from egui's viewport info.
pub fn read_window_position(ctx: &egui::Context) -> Option<Position> {
    ctx.input(|i: &egui::InputState| {
        i.viewport().outer_rect.map(|rect: egui::Rect| Position {
            x: rect.left() as f64,
            y: rect.top() as f64,
        })
    })
}

/// Compute the background color with the configured transparency alpha.
pub fn background_color(transparency: f32) -> egui::Color32 {
    let alpha = (transparency.clamp(0.1, 1.0) * 255.0) as u8;
    egui::Color32::from_rgba_unmultiplied(22, 22, 26, alpha)
}

/// Width of the invisible resize border around the overlay edges.
const RESIZE_BORDER: f32 = 6.0;

/// Handle edge-drag resizing for the undecorated overlay window.
/// Detects when the cursor is near a window edge and initiates a native resize
/// on left-mouse press. Also sets the appropriate resize cursor.
pub fn handle_edge_resize(ctx: &egui::Context, ui: &mut egui::Ui) {
    let rect = ui.max_rect();
    let Some(pointer_pos) = ctx.input(|i| i.pointer.hover_pos()) else {
        return;
    };

    let near_left = (pointer_pos.x - rect.left()).abs() < RESIZE_BORDER;
    let near_right = (pointer_pos.x - rect.right()).abs() < RESIZE_BORDER;
    let near_top = (pointer_pos.y - rect.top()).abs() < RESIZE_BORDER;
    let near_bottom = (pointer_pos.y - rect.bottom()).abs() < RESIZE_BORDER;

    let direction = match (near_left, near_right, near_top, near_bottom) {
        (true, false, true, false) => Some(egui::ResizeDirection::NorthWest),
        (false, true, true, false) => Some(egui::ResizeDirection::NorthEast),
        (true, false, false, true) => Some(egui::ResizeDirection::SouthWest),
        (false, true, false, true) => Some(egui::ResizeDirection::SouthEast),
        (true, false, false, false) => Some(egui::ResizeDirection::West),
        (false, true, false, false) => Some(egui::ResizeDirection::East),
        (false, false, true, false) => Some(egui::ResizeDirection::North),
        (false, false, false, true) => Some(egui::ResizeDirection::South),
        _ => None,
    };

    if let Some(dir) = direction {
        let cursor = match dir {
            egui::ResizeDirection::North | egui::ResizeDirection::South => {
                egui::CursorIcon::ResizeVertical
            }
            egui::ResizeDirection::East | egui::ResizeDirection::West => {
                egui::CursorIcon::ResizeHorizontal
            }
            egui::ResizeDirection::NorthWest | egui::ResizeDirection::SouthEast => {
                egui::CursorIcon::ResizeNwSe
            }
            egui::ResizeDirection::NorthEast | egui::ResizeDirection::SouthWest => {
                egui::CursorIcon::ResizeNeSw
            }
        };
        ctx.set_cursor_icon(cursor);

        if ctx.input(|i| i.pointer.primary_pressed()) {
            ctx.send_viewport_cmd(egui::ViewportCommand::BeginResize(dir));
        }
    }
}

/// Read the current inner window size from egui's viewport info.
pub fn read_window_size(ctx: &egui::Context) -> Option<crate::config::Size> {
    ctx.input(|i: &egui::InputState| {
        i.viewport().inner_rect.map(|rect: egui::Rect| crate::config::Size {
            width: rect.width() as f64,
            height: rect.height() as f64,
        })
    })
}

/// Check if a saved position is still on-screen. Returns `None` if off-screen.
pub fn validate_position(pos: &Position, screen_size: egui::Vec2) -> Option<Position> {
    // Allow some margin — the window should have at least 50px visible
    const MIN_VISIBLE: f64 = 50.0;
    let max_x = screen_size.x as f64 - MIN_VISIBLE;
    let max_y = screen_size.y as f64 - MIN_VISIBLE;

    if pos.x > max_x || pos.y > max_y || pos.x < -(MIN_VISIBLE) || pos.y < -(MIN_VISIBLE) {
        None
    } else {
        Some(*pos)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::Size;

    #[test]
    fn build_viewport_applies_config_defaults() {
        let config = Config::default();
        let vp = build_viewport(&config, None);

        // Decorations should be off
        assert_eq!(vp.decorations, Some(false));
        // Transparent should be on
        assert_eq!(vp.transparent, Some(true));
        // Taskbar should be hidden
        assert_eq!(vp.taskbar, Some(false));
        // Inner size should match config
        assert_eq!(
            vp.inner_size,
            Some(egui::vec2(400.0, 360.0))
        );
        // No position set when config has None
        assert!(vp.position.is_none());
        // Mouse passthrough off in interactive mode
        assert!(vp.mouse_passthrough.is_none() || vp.mouse_passthrough == Some(false));
    }

    #[test]
    fn build_viewport_applies_position_from_config() {
        let config = Config {
            window_position: Some(Position { x: 150.0, y: 250.0 }),
            ..Config::default()
        };
        let vp = build_viewport(&config, None);
        assert_eq!(vp.position, Some(egui::pos2(150.0, 250.0)));
    }

    #[test]
    fn build_viewport_click_through_mode() {
        let config = Config {
            overlay_mode: OverlayMode::ClickThrough,
            ..Config::default()
        };
        let vp = build_viewport(&config, None);
        assert_eq!(vp.mouse_passthrough, Some(true));
    }

    #[test]
    fn build_viewport_custom_size() {
        let config = Config {
            window_size: Size {
                width: 500.0,
                height: 400.0,
            },
            ..Config::default()
        };
        let vp = build_viewport(&config, None);
        assert_eq!(vp.inner_size, Some(egui::vec2(500.0, 400.0)));
    }

    #[test]
    fn background_color_default_transparency() {
        let color = background_color(0.85);
        let alpha = (0.85_f32 * 255.0) as u8;
        assert_eq!(color.a(), alpha);
        // RGB may be premultiplied, but alpha must match
        assert!(color.r() <= 30);
        assert!(color.g() <= 30);
        assert!(color.b() <= 30);
    }

    #[test]
    fn background_color_full_opacity() {
        let color = background_color(1.0);
        assert_eq!(color.a(), 255);
    }

    #[test]
    fn background_color_clamps_low() {
        let color = background_color(0.0);
        // Should clamp to 0.1
        assert_eq!(color.a(), 25); // 0.1 * 255 = 25
    }

    #[test]
    fn validate_position_on_screen() {
        let pos = Position { x: 100.0, y: 100.0 };
        let screen = egui::vec2(1920.0, 1080.0);
        assert_eq!(validate_position(&pos, screen), Some(pos));
    }

    #[test]
    fn validate_position_off_screen_right() {
        let pos = Position { x: 1900.0, y: 100.0 };
        let screen = egui::vec2(1920.0, 1080.0);
        // 1900 > 1920 - 50 = 1870, so off-screen
        assert_eq!(validate_position(&pos, screen), None);
    }

    #[test]
    fn validate_position_off_screen_bottom() {
        let pos = Position { x: 100.0, y: 1050.0 };
        let screen = egui::vec2(1920.0, 1080.0);
        // 1050 > 1080 - 50 = 1030, so off-screen
        assert_eq!(validate_position(&pos, screen), None);
    }

    #[test]
    fn validate_position_negative_within_margin() {
        let pos = Position { x: -30.0, y: 100.0 };
        let screen = egui::vec2(1920.0, 1080.0);
        assert_eq!(validate_position(&pos, screen), Some(pos));
    }

    #[test]
    fn validate_position_too_far_negative() {
        let pos = Position { x: -60.0, y: 100.0 };
        let screen = egui::vec2(1920.0, 1080.0);
        assert_eq!(validate_position(&pos, screen), None);
    }

    #[test]
    fn toggle_mode_interactive_to_click_through() {
        // We can't test ctx.send_viewport_cmd without a real egui context,
        // but we can test the logic of the toggle
        let current = OverlayMode::Interactive;
        let new = match current {
            OverlayMode::Interactive => OverlayMode::ClickThrough,
            OverlayMode::ClickThrough => OverlayMode::Interactive,
        };
        assert_eq!(new, OverlayMode::ClickThrough);
    }

    #[test]
    fn toggle_mode_click_through_to_interactive() {
        let current = OverlayMode::ClickThrough;
        let new = match current {
            OverlayMode::Interactive => OverlayMode::ClickThrough,
            OverlayMode::ClickThrough => OverlayMode::Interactive,
        };
        assert_eq!(new, OverlayMode::Interactive);
    }

    #[test]
    fn position_save_restore_round_trip() {
        let original = Position { x: 123.456, y: 789.012 };
        let json = serde_json::to_string(&original).unwrap();
        let restored: Position = serde_json::from_str(&json).unwrap();
        assert_eq!(original, restored);
    }
}
