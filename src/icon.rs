// App icon loading from embedded assets/app.ico

use image::ImageReader;
use std::io::Cursor;

/// Raw ICO bytes embedded at compile time.
const ICO_BYTES: &[u8] = include_bytes!("../assets/app.ico");

/// Decoded RGBA icon data at a specific size.
pub struct IconData {
    pub rgba: Vec<u8>,
    pub width: u32,
    pub height: u32,
}

/// Decode the embedded ICO file into RGBA pixel data.
/// Returns the largest image in the ICO (typically 256x256 or 48x48).
fn decode_ico() -> Result<IconData, String> {
    let reader = ImageReader::new(Cursor::new(ICO_BYTES))
        .with_guessed_format()
        .map_err(|e| format!("failed to read ICO: {e}"))?;

    let img = reader
        .decode()
        .map_err(|e| format!("failed to decode ICO: {e}"))?;

    let rgba = img.to_rgba8();
    let width = rgba.width();
    let height = rgba.height();

    Ok(IconData {
        rgba: rgba.into_raw(),
        width,
        height,
    })
}

/// Load the app icon for use with eframe's viewport (window icon).
pub fn load_window_icon() -> Option<egui::IconData> {
    match decode_ico() {
        Ok(data) => Some(egui::IconData {
            rgba: data.rgba,
            width: data.width,
            height: data.height,
        }),
        Err(e) => {
            eprintln!("warn: failed to load window icon: {e}");
            None
        }
    }
}

/// Load the app icon for use with the system tray.
pub fn load_tray_icon() -> Result<tray_icon::Icon, String> {
    let data = decode_ico()?;

    // Tray icons work best at 32x32; resize if needed
    let img = image::RgbaImage::from_raw(data.width, data.height, data.rgba)
        .ok_or("failed to create image from decoded ICO")?;

    let resized = image::imageops::resize(&img, 32, 32, image::imageops::FilterType::Lanczos3);
    let width = resized.width();
    let height = resized.height();

    tray_icon::Icon::from_rgba(resized.into_raw(), width, height)
        .map_err(|e| format!("failed to create tray icon: {e}"))
}

use eframe::egui;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decode_ico_succeeds() {
        let data = decode_ico();
        assert!(data.is_ok(), "ICO decode failed: {:?}", data.err());
        let data = data.unwrap();
        assert!(data.width > 0);
        assert!(data.height > 0);
        assert_eq!(data.rgba.len(), (data.width * data.height * 4) as usize);
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
