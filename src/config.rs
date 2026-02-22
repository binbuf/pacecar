use directories::ProjectDirs;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Visualization {
    Gauges,
    Sparklines,
}

impl Default for Visualization {
    fn default() -> Self {
        Self::Gauges
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum OverlayMode {
    Interactive,
    ClickThrough,
}

impl Default for OverlayMode {
    fn default() -> Self {
        Self::Interactive
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Theme {
    Dark,
}

impl Default for Theme {
    fn default() -> Self {
        Self::Dark
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Position {
    pub x: f64,
    pub y: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct Size {
    pub width: f64,
    pub height: f64,
}

impl Default for Size {
    fn default() -> Self {
        Self {
            width: 400.0,
            height: 360.0,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Config {
    pub polling_interval_ms: u64,
    pub transparency: f32,
    pub visualization: Visualization,
    pub overlay_mode: OverlayMode,
    pub hotkey: String,
    pub window_position: Option<Position>,
    pub window_size: Size,
    pub theme: Theme,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            polling_interval_ms: 1000,
            transparency: 0.85,
            visualization: Visualization::default(),
            overlay_mode: OverlayMode::default(),
            hotkey: "Ctrl+Shift+P".to_string(),
            window_position: None,
            window_size: Size::default(),
            theme: Theme::default(),
        }
    }
}

impl Config {
    /// Clamp values to valid ranges.
    pub fn clamp(&mut self) {
        self.polling_interval_ms = self.polling_interval_ms.clamp(250, 5000);
        self.transparency = self.transparency.clamp(0.1, 1.0);
        self.window_size.width = self.window_size.width.max(100.0);
        self.window_size.height = self.window_size.height.max(80.0);
    }

    /// Returns the platform-specific config file path.
    pub fn config_path() -> Option<PathBuf> {
        ProjectDirs::from("", "", "pacecar").map(|dirs| dirs.config_dir().join("config.json"))
    }

    /// Load config from disk, falling back to defaults on any error.
    pub fn load() -> Self {
        Self::load_from_path(Self::config_path())
    }

    pub fn load_from_path(path: Option<PathBuf>) -> Self {
        let Some(path) = path else {
            eprintln!("warn: could not determine config directory, using defaults");
            return Self::default();
        };

        match fs::read_to_string(&path) {
            Ok(contents) => match serde_json::from_str::<Config>(&contents) {
                Ok(mut config) => {
                    config.clamp();
                    config
                }
                Err(e) => {
                    eprintln!("warn: malformed config at {}: {e}, using defaults", path.display());
                    Self::default()
                }
            },
            Err(_) => Self::default(),
        }
    }

    /// Save config to disk, creating parent directories if needed.
    pub fn save(&self) -> Result<(), String> {
        let path = Self::config_path().ok_or("could not determine config directory")?;
        self.save_to_path(&path)
    }

    pub fn save_to_path(&self, path: &PathBuf) -> Result<(), String> {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)
                .map_err(|e| format!("failed to create config directory: {e}"))?;
        }
        let json =
            serde_json::to_string_pretty(self).map_err(|e| format!("failed to serialize config: {e}"))?;
        fs::write(path, json).map_err(|e| format!("failed to write config: {e}"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::NamedTempFile;

    #[test]
    fn default_values_match_spec() {
        let config = Config::default();
        assert_eq!(config.polling_interval_ms, 1000);
        assert_eq!(config.transparency, 0.85);
        assert_eq!(config.visualization, Visualization::Gauges);
        assert_eq!(config.overlay_mode, OverlayMode::Interactive);
        assert_eq!(config.hotkey, "Ctrl+Shift+P");
        assert_eq!(config.window_position, None);
        assert_eq!(config.window_size, Size { width: 400.0, height: 360.0 });
        assert_eq!(config.theme, Theme::Dark);
    }

    #[test]
    fn round_trip_serialize_deserialize() {
        let config = Config::default();
        let json = serde_json::to_string_pretty(&config).unwrap();
        let deserialized: Config = serde_json::from_str(&json).unwrap();
        assert_eq!(config, deserialized);
    }

    #[test]
    fn missing_keys_use_defaults() {
        let json = r#"{ "transparency": 0.5 }"#;
        let config: Config = serde_json::from_str(json).unwrap();
        assert_eq!(config.transparency, 0.5);
        assert_eq!(config.polling_interval_ms, 1000);
        assert_eq!(config.visualization, Visualization::Gauges);
        assert_eq!(config.hotkey, "Ctrl+Shift+P");
    }

    #[test]
    fn malformed_json_returns_defaults() {
        let path = Some(PathBuf::from("nonexistent_test_path_12345.json"));
        let config = Config::load_from_path(path);
        assert_eq!(config, Config::default());
    }

    #[test]
    fn malformed_json_content_returns_defaults() {
        let mut tmp = NamedTempFile::new().unwrap();
        write!(tmp, "{{not valid json!!!").unwrap();
        let config = Config::load_from_path(Some(tmp.path().to_path_buf()));
        assert_eq!(config, Config::default());
    }

    #[test]
    fn clamp_polling_interval() {
        let mut config = Config::default();
        config.polling_interval_ms = 100;
        config.clamp();
        assert_eq!(config.polling_interval_ms, 250);

        config.polling_interval_ms = 10000;
        config.clamp();
        assert_eq!(config.polling_interval_ms, 5000);
    }

    #[test]
    fn clamp_transparency() {
        let mut config = Config::default();
        config.transparency = 0.0;
        config.clamp();
        assert_eq!(config.transparency, 0.1);

        config.transparency = 2.0;
        config.clamp();
        assert_eq!(config.transparency, 1.0);
    }

    #[test]
    fn clamp_window_size() {
        let mut config = Config::default();
        config.window_size.width = 10.0;
        config.window_size.height = 10.0;
        config.clamp();
        assert_eq!(config.window_size.width, 100.0);
        assert_eq!(config.window_size.height, 80.0);
    }

    #[test]
    fn save_and_load_round_trip() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("subdir").join("config.json");

        let config = Config {
            polling_interval_ms: 500,
            transparency: 0.5,
            visualization: Visualization::Sparklines,
            overlay_mode: OverlayMode::ClickThrough,
            hotkey: "Alt+P".to_string(),
            window_position: Some(Position { x: 100.0, y: 200.0 }),
            window_size: Size { width: 400.0, height: 300.0 },
            theme: Theme::Dark,
        };

        config.save_to_path(&path).unwrap();
        let loaded = Config::load_from_path(Some(path));
        assert_eq!(config, loaded);
    }

    #[test]
    fn load_clamps_out_of_range_values() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("config.json");
        let json = r#"{
            "polling_interval_ms": 50,
            "transparency": 5.0
        }"#;
        fs::write(&path, json).unwrap();
        let config = Config::load_from_path(Some(path));
        assert_eq!(config.polling_interval_ms, 250);
        assert_eq!(config.transparency, 1.0);
    }

    #[test]
    fn enum_serialization_snake_case() {
        let json = serde_json::to_string(&Visualization::Sparklines).unwrap();
        assert_eq!(json, r#""sparklines""#);
        let json = serde_json::to_string(&OverlayMode::ClickThrough).unwrap();
        assert_eq!(json, r#""click_through""#);
    }

    #[test]
    fn config_path_returns_some() {
        // On any platform with a home directory, this should succeed
        assert!(Config::config_path().is_some());
    }

    #[test]
    fn snapshot_default_config() {
        let config = Config::default();
        let json = serde_json::to_string_pretty(&config).unwrap();
        insta::assert_snapshot!(json);
    }
}
