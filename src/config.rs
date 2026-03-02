use directories::ProjectDirs;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

/// Which GPU to monitor.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum GpuSelection {
    /// Auto-detect the best available GPU (NVML first, then D3DKMT).
    Auto,
    /// Select GPU by adapter index.
    ByIndex(u32),
    /// Select GPU by (substring of) adapter name.
    ByName(String),
}

impl Default for GpuSelection {
    fn default() -> Self {
        Self::Auto
    }
}

/// Which CPU metric to display.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CpuSelection {
    /// Aggregate usage across all cores.
    Aggregate,
    /// Usage for a specific core index.
    Core(usize),
}

impl Default for CpuSelection {
    fn default() -> Self {
        Self::Aggregate
    }
}

/// How to aggregate disk temperatures when multiple drives are present.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DiskTempMode {
    /// Show temp of the disk selected by disk_device filter (falls back to Highest when All).
    SelectedDisk,
    /// Show the hottest disk's temperature.
    Highest,
    /// Show the average temperature across all disks.
    Average,
}

impl Default for DiskTempMode {
    fn default() -> Self {
        Self::SelectedDisk
    }
}

/// How to aggregate fan speeds when multiple fans are present.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FanSpeedMode {
    /// Show the highest fan RPM.
    Highest,
    /// Show the average RPM across all fans.
    Average,
}

impl Default for FanSpeedMode {
    fn default() -> Self {
        Self::Highest
    }
}

/// How to aggregate mainboard temperatures when multiple sensors are present.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum MainboardTempMode {
    /// Show the highest mainboard temperature.
    Highest,
    /// Show the average temperature across all sensors.
    Average,
}

impl Default for MainboardTempMode {
    fn default() -> Self {
        Self::Highest
    }
}

/// Filter for network interfaces or disk devices.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DeviceFilter {
    /// Aggregate across all devices.
    All,
    /// Filter to a single named device.
    Named(String),
}

impl Default for DeviceFilter {
    fn default() -> Self {
        Self::All
    }
}

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

/// Layout preset controlling column count and window size.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum LayoutPreset {
    /// Auto-detect columns from window width.
    Auto,
    /// Wide 4-column layout (~520x240).
    Wide,
    /// Skinny single-column layout (~130x800).
    Skinny,
}

impl Default for LayoutPreset {
    fn default() -> Self {
        Self::Auto
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
            width: 130.0,
            height: 800.0,
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
    pub gpu_selection: GpuSelection,
    pub cpu_selection: CpuSelection,
    pub network_interface: DeviceFilter,
    pub disk_device: DeviceFilter,
    pub ping_target: String,
    pub show_cpu_temperature: bool,
    pub show_disk_temperature: bool,
    pub disk_temp_mode: DiskTempMode,
    pub show_fan_speed: bool,
    pub fan_speed_mode: FanSpeedMode,
    pub show_ram_temperature: bool,
    pub show_cpu_fan_speed: bool,
    pub show_gpu_fan_speed: bool,
    pub show_mainboard_temp: bool,
    pub mainboard_temp_mode: MainboardTempMode,
    // Per-tile visibility
    pub show_cpu: bool,
    pub show_ram: bool,
    pub show_gpu: bool,
    pub show_network: bool,
    pub show_disk: bool,
    pub show_ping: bool,
    // Per-tile display options
    pub show_graphs: bool,
    pub show_percentage: bool,
    pub show_secondary: bool,
    pub show_tertiary: bool,
    // Layout preset
    pub layout_preset: LayoutPreset,
    // History
    pub show_mini_sparklines: bool,
    pub history_retention_minutes: u32,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            polling_interval_ms: 1000,
            transparency: 0.65,
            visualization: Visualization::default(),
            overlay_mode: OverlayMode::default(),
            hotkey: "Ctrl+Shift+P".to_string(),
            window_position: None,
            window_size: Size::default(),
            theme: Theme::default(),
            gpu_selection: GpuSelection::default(),
            cpu_selection: CpuSelection::default(),
            network_interface: DeviceFilter::default(),
            disk_device: DeviceFilter::default(),
            ping_target: "8.8.8.8".to_string(),
            show_cpu_temperature: true,
            show_disk_temperature: true,
            disk_temp_mode: DiskTempMode::default(),
            show_fan_speed: true,
            fan_speed_mode: FanSpeedMode::default(),
            show_ram_temperature: true,
            show_cpu_fan_speed: true,
            show_gpu_fan_speed: true,
            show_mainboard_temp: true,
            mainboard_temp_mode: MainboardTempMode::default(),
            show_cpu: true,
            show_ram: true,
            show_gpu: true,
            show_network: true,
            show_disk: true,
            show_ping: true,
            show_graphs: false,
            show_percentage: true,
            show_secondary: true,
            show_tertiary: true,
            layout_preset: LayoutPreset::Skinny,
            show_mini_sparklines: true,
            history_retention_minutes: 30,
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
        // Snap to nearest valid retention preset
        const RETENTION_PRESETS: &[u32] = &[1, 5, 10, 15, 30, 60, 120];
        if !RETENTION_PRESETS.contains(&self.history_retention_minutes) {
            self.history_retention_minutes = 5;
        }
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
        assert_eq!(config.transparency, 0.65);
        assert_eq!(config.visualization, Visualization::Gauges);
        assert_eq!(config.overlay_mode, OverlayMode::Interactive);
        assert_eq!(config.hotkey, "Ctrl+Shift+P");
        assert_eq!(config.window_position, None);
        assert_eq!(config.window_size, Size { width: 130.0, height: 800.0 });
        assert_eq!(config.theme, Theme::Dark);
        assert_eq!(config.gpu_selection, GpuSelection::Auto);
        assert_eq!(config.cpu_selection, CpuSelection::Aggregate);
        assert_eq!(config.network_interface, DeviceFilter::All);
        assert_eq!(config.disk_device, DeviceFilter::All);
        assert_eq!(config.ping_target, "8.8.8.8");
        assert_eq!(config.show_cpu_temperature, true);
        assert_eq!(config.show_disk_temperature, true);
        assert_eq!(config.disk_temp_mode, DiskTempMode::SelectedDisk);
        assert_eq!(config.show_fan_speed, true);
        assert_eq!(config.fan_speed_mode, FanSpeedMode::Highest);
        assert_eq!(config.show_ram_temperature, true);
        assert_eq!(config.show_cpu_fan_speed, true);
        assert_eq!(config.show_gpu_fan_speed, true);
        assert_eq!(config.show_mainboard_temp, true);
        assert_eq!(config.mainboard_temp_mode, MainboardTempMode::Highest);
        assert_eq!(config.show_cpu, true);
        assert_eq!(config.show_ram, true);
        assert_eq!(config.show_gpu, true);
        assert_eq!(config.show_network, true);
        assert_eq!(config.show_disk, true);
        assert_eq!(config.show_ping, true);
        assert_eq!(config.show_graphs, false);
        assert_eq!(config.show_percentage, true);
        assert_eq!(config.show_secondary, true);
        assert_eq!(config.show_tertiary, true);
        assert_eq!(config.layout_preset, LayoutPreset::Skinny);
        assert_eq!(config.show_mini_sparklines, true);
        assert_eq!(config.history_retention_minutes, 30);
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
            gpu_selection: GpuSelection::ByIndex(1),
            cpu_selection: CpuSelection::Core(2),
            network_interface: DeviceFilter::Named("eth0".to_string()),
            disk_device: DeviceFilter::Named("C:\\".to_string()),
            ping_target: "1.1.1.1".to_string(),
            show_cpu_temperature: false,
            show_disk_temperature: true,
            disk_temp_mode: DiskTempMode::Highest,
            show_fan_speed: true,
            fan_speed_mode: FanSpeedMode::Average,
            show_ram_temperature: true,
            show_cpu_fan_speed: true,
            show_gpu_fan_speed: true,
            show_mainboard_temp: true,
            mainboard_temp_mode: MainboardTempMode::Average,
            show_cpu: true,
            show_ram: true,
            show_gpu: false,
            show_network: true,
            show_disk: true,
            show_ping: false,
            show_graphs: false,
            show_percentage: true,
            show_secondary: false,
            show_tertiary: true,
            layout_preset: LayoutPreset::Wide,
            show_mini_sparklines: true,
            history_retention_minutes: 30,
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
    fn clamp_history_retention_invalid_resets() {
        let mut config = Config::default();
        config.history_retention_minutes = 7; // not a valid preset
        config.clamp();
        assert_eq!(config.history_retention_minutes, 5);
    }

    #[test]
    fn clamp_history_retention_valid_unchanged() {
        let mut config = Config::default();
        for &mins in &[1u32, 5, 10, 15, 30, 60, 120] {
            config.history_retention_minutes = mins;
            config.clamp();
            assert_eq!(config.history_retention_minutes, mins);
        }
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
