#pragma once

// User configuration for Pacecar.
//
// The config is a plain, cheap-to-copy aggregate tree serialized as JSON at
// `%APPDATA%\Pacecar\config.json`. `Config::Load` never throws for expected failure modes: a
// missing file, unreadable file, malformed JSON, or a non-object root all fall back to defaults
// (malformed content is logged as a warning first). Missing keys keep their defaults and every
// bounded value is repaired by `Clamp()` after parsing. `Save()` creates parent directories and
// replaces the file atomically (write a temp file next to the target, then rename over it).
//
// `DebouncedSaver` coalesces rapid mutations (settings drags, keystrokes) into a single write some
// `delay` after the last `Touch()`, which is the design's "save 500 ms after the last change" rule.
//
// This header has no UI or hardware dependency and links only against the standard library plus the
// Win32 path lookup in the .cpp.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace pacecar
{
// Sampling cadence. The viewer only accepts the discrete rates the Settings window offers; invalid
// values snap to the nearest allowed rate (default 1000 ms).
enum class RefreshRate : int
{
    Ms250 = 250,
    Ms500 = 500,
    Ms1000 = 1000,
    Ms2000 = 2000,
    Ms5000 = 5000,
};

enum class Theme
{
    Dark,
    Light,
    HighContrast,
};

enum class LayoutPreset
{
    Compact3x3,
    Vertical1x6,
    AutoFit,
    Custom,
};

// The overlay presentation "view" the user cycles through at runtime. This is independent of
// `LayoutPreset`: `LayoutPreset` refines the arrangement inside the two dense views, while
// `ViewMode` selects how much is shown and how each tile is presented.
//
//   Full         - the full panel: header plus the configured tiles and their visuals.
//   LargeVisuals - fewer, larger tiles; values are placed inside/around their gauge or graph.
//   SmallText    - a compact text-only readout (label + value per metric, no visuals).
//   StatRows     - the simplest readout: one plain text line per visible stat, stacked top to
//                  bottom, with no tile chrome or visuals. The value font size is user-adjustable.
//   FpsText      - a StatRows-style plain text list restricted to the FPS / frame-time readout.
//                  Selecting it arms FPS capture so the readout appears as soon as frames flow.
//   FpsOnly      - just the FPS / frame-time text readout.
//
// The text views (`SmallText`, `StatRows`, `FpsText`, `FpsOnly`) look best with the panel background
// turned off, so the overlay reads as plain text over the desktop/game.
enum class ViewMode
{
    Full = 0,
    LargeVisuals,
    SmallText,
    StatRows,
    FpsText,
    FpsOnly,
};

enum class OverlayMode
{
    Interactive,
    ClickThrough,
};

enum class Visualization
{
    Gauges,
    Sparklines,
};

enum class DiskTempMode
{
    SelectedDisk,
    Highest,
    Average,
};

enum class FanSpeedMode
{
    Highest,
    Average,
};

enum class MainboardTempMode
{
    Highest,
    Average,
};

// Saved overlay rectangle for one monitor. `valid` is false until the overlay has been placed, so a
// fresh config does not pin the window to a stale position. Task T09 owns per-monitor validation.
struct MonitorRect
{
    int monitor_id = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

// Per-tile visibility and presentation. `show_primary`/`show_secondary`/`show_tertiary` toggle the
// individual value lines; `show_visualization` toggles the graph/percentage block; `visualization`
// chooses gauges vs sparklines; `mini_sparklines` adds the compact history strip.
struct TileConfig
{
    bool visible = true;
    bool show_primary = true;
    bool show_secondary = true;
    bool show_tertiary = true;
    bool show_visualization = true;
    Visualization visualization = Visualization::Gauges;
    bool mini_sparklines = false;
};

// Rate-based tiles (network/disk/ping) have no percentage to show in a gauge, so they default to a
// text-only presentation instead of an empty arc.
[[nodiscard]] inline TileConfig TextOnlyTile()
{
    TileConfig tile{};
    tile.show_visualization = false;
    return tile;
}

struct TilesConfig
{
    TileConfig cpu{};
    TileConfig ram{};
    TileConfig gpu{};
    TileConfig network = TextOnlyTile();
    TileConfig disk = TextOnlyTile();
    TileConfig ping = TextOnlyTile();
    // Opt-in FPS / frame-time tile (task T17). Hidden by default and shown by the overlay only
    // while a capture is active.
    TileConfig fps{false};
};

// One tile's geometry for the Custom layout preset, in DIPs relative to the panel content origin
// (after the panel padding and the header). `tile` is the canonical tile key ("cpu", "ram", "gpu",
// "network", "disk", "ping", or a future "fans"/"mainboard"); unknown keys are ignored by the
// layout engine. `valid` is false until the user drags/resizes the tile.
struct CustomTilePlacement
{
    std::string tile = "cpu";
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool valid = false;
};

struct LayoutConfig
{
    std::vector<CustomTilePlacement> custom_tiles{};
};

struct GeneralConfig
{
    RefreshRate refresh = RefreshRate::Ms1000;
    double opacity = 0.65;
    Theme theme = Theme::Dark;
    LayoutPreset layout = LayoutPreset::Compact3x3;
    // The active presentation view (cycled at runtime via menu/hotkey). Defaults to the plain
    // StatRows text list so a fresh install reads as a minimal, background-free readout.
    ViewMode view = ViewMode::StatRows;
    // Value font size for the text views, in device-independent pixels. Small by default so the
    // StatRows view reads as a discreet list; adjustable live from Settings. Clamped by `Clamp()`.
    int stat_text_size = 9;
    // When true the panel/header background is not drawn, so the overlay is just text (and, in the
    // visual views, the accent drawings) floating over the desktop/game.
    bool transparent_background = false;
    bool start_with_windows = false;
    bool start_hidden = false;
};

struct OverlayConfig
{
    OverlayMode mode = OverlayMode::Interactive;
    bool always_on_top = true;
    int monitor_id = 0;
    // Reserved for the deferred uiAccess path; parsed but a no-op in the MVP.
    bool enhanced_fullscreen = false;
    bool capture_exclusion = true;
    std::vector<MonitorRect> monitor_rects{};
};

struct SensorsConfig
{
    // Selection strings are intentionally loose ("auto", a name, or an index as text) so provider
    // tasks (T05-T08) can interpret them without another config migration.
    std::string gpu_selection = "auto";
    std::string cpu_selection = "auto";
    std::string nic_selection = "auto";
    std::string disk_selection = "auto";
    bool cpu_temperature = true;
    bool gpu_temperature = true;
    bool disk_temperature = true;
    bool fan_speed = true;
    bool ram_temperature = true;
    bool mainboard_temperature = true;
    // Opt-in deep sensors: when true the UI attempts to connect to the elevated Pacecar.Sensors
    // helper for package/board/DIMM/fan sensors. Default off; the UI stays unelevated regardless.
    bool deep_sensors = false;
    // Opt-in FPS / frame-time capture via the helper's ETW session (task T17). Default off; capture
    // never runs unless explicitly enabled and is shown only while active.
    bool fps_capture = false;
    DiskTempMode disk_temp_mode = DiskTempMode::SelectedDisk;
    FanSpeedMode fan_mode = FanSpeedMode::Highest;
    MainboardTempMode mainboard_mode = MainboardTempMode::Highest;
    std::string ping_target = "8.8.8.8";
};

struct HistoryConfig
{
    // Minutes of sample history kept for sparklines. Only the preset set is valid.
    int retention_minutes = 30;
};

struct HotkeysConfig
{
    std::string toggle_overlay = "Ctrl+Shift+P";
    std::string toggle_click_through = "";
    // Cycles Full -> LargeVisuals -> SmallText -> StatRows -> FpsOnly. Empty disables the binding.
    std::string cycle_view = "Alt+F12";
    // Toggles the panel background on/off. Empty disables the binding.
    std::string toggle_background = "Ctrl+Shift+B";
    // Arms/disarms FPS + frame-time capture (launches the elevated helper on first use). Empty
    // disables the binding.
    std::string toggle_fps_capture = "Alt+F11";
};

struct Config
{
    int schema_version = 1;
    GeneralConfig general{};
    OverlayConfig overlay{};
    TilesConfig tiles{};
    LayoutConfig layout{};
    SensorsConfig sensors{};
    HistoryConfig history{};
    HotkeysConfig hotkeys{};

    // Returns the built-in defaults. Config{} already equals this; the function exists so callers
    // read intent (and so tests can spell it out).
    [[nodiscard]] static Config Defaults();

    // Resets every field to the built-in defaults.
    void Reset();

    // Repairs out-of-range values in place. Idempotent.
    void Clamp();

    // `%APPDATA%\Pacecar\config.json`. Returns an empty path if %APPDATA% is unavailable. A
    // package-aware variant is noted for a future MSIX build.
    [[nodiscard]] static std::filesystem::path DefaultPath();

    // Loads the default path. Never throws.
    [[nodiscard]] static Config Load();

    // Loads from an explicit path: missing/unreadable/malformed -> defaults (malformed logged).
    [[nodiscard]] static Config Load(const std::filesystem::path& path);

    // Saves to the default path. Creates parent directories and writes atomically. Returns false
    // (and logs a warning) on failure.
    [[nodiscard]] bool Save() const;

    [[nodiscard]] bool Save(const std::filesystem::path& path) const;
};

// Canonical (deterministic, key-sorted) JSON. `indent > 0` pretty-prints.
[[nodiscard]] std::string ConfigToJsonString(const Config& config, int indent = 2);

// Parses JSON into `out`; returns false without touching `out` when the text is not a JSON object.
[[nodiscard]] bool ConfigFromJsonString(std::string_view text, Config& out);

// Fires `save` once per burst of mutations: each `Touch()` postpones the write to `delay` after the
// last change. The destructor flushes any pending write and stops the worker thread.
class DebouncedSaver
{
  public:
    using SaveFunction = std::function<void()>;

    explicit DebouncedSaver(SaveFunction save,
                            std::chrono::milliseconds delay = std::chrono::milliseconds(500));
    ~DebouncedSaver();

    DebouncedSaver(const DebouncedSaver&) = delete;
    DebouncedSaver& operator=(const DebouncedSaver&) = delete;
    DebouncedSaver(DebouncedSaver&&) = delete;
    DebouncedSaver& operator=(DebouncedSaver&&) = delete;

    // Marks the config dirty and (re)arms the timer.
    void Touch() noexcept;

    // Performs a pending write immediately and blocks until it completes.
    void Flush();

    // Discards any pending write without saving.
    void Cancel() noexcept;

    [[nodiscard]] bool Pending() const noexcept;

    // Number of writes performed so far (for tests and diagnostics).
    [[nodiscard]] std::uint64_t SaveCount() const noexcept;

  private:
    void Worker();

    SaveFunction save_;
    std::chrono::milliseconds delay_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    bool dirty_ = false;
    bool stop_ = false;
    std::chrono::steady_clock::time_point deadline_{};
    std::uint64_t saveCount_ = 0;
};
} // namespace pacecar