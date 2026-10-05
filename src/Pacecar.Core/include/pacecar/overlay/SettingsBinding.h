#pragma once

// The Settings window's Win32-free binding layer (design refs 04-ui-ux.md "Settings window",
// 07-migration-learnings.md "Debounced saves").
//
// The Settings window owns common controls; this class owns the mapping between those controls and
// the `Config` tree. Keeping the mapping here (no HWND, no comctl32) makes every control group
// testable without a window: ranges/clamping, enum <-> combo-index conversion, per-tile/per-sensor
// toggles, reset-to-defaults, and the "one write per burst of edits" contract when the binding's
// `onChanged` callback drives a `pacecar::DebouncedSaver`.
//
// Every setter clamps first and then invokes `onChanged`, so "changes apply live" and "values are
// repaired on edit" are the same code path the UI uses. The window never writes config directly.

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "pacecar/config/Config.h"

namespace pacecar::overlay
{
// Option tables shared by the combo boxes and the tests. The order is the display order.
inline constexpr std::array<int, 5> kRefreshOptions{250, 500, 1000, 2000, 5000};
// Allowed value font size range (DIPs) for the text views; the slider and the binding share it.
inline constexpr int kStatTextSizeMin = 8;
inline constexpr int kStatTextSizeMax = 28;
inline constexpr std::array<int, 7> kRetentionOptions{1, 5, 10, 15, 30, 60, 120};
inline constexpr std::array<Theme, 3> kThemeOptions{Theme::Dark, Theme::Light, Theme::HighContrast};
inline constexpr std::array<LayoutPreset, 4> kLayoutOptions{
    LayoutPreset::Compact3x3, LayoutPreset::Vertical1x6, LayoutPreset::AutoFit,
    LayoutPreset::Custom};
inline constexpr std::array<ViewMode, 6> kViewOptions{
    ViewMode::Full,      ViewMode::LargeVisuals, ViewMode::SmallText,
    ViewMode::StatRows,  ViewMode::FpsText,      ViewMode::FpsOnly};
inline constexpr std::array<OverlayMode, 2> kOverlayModeOptions{OverlayMode::Interactive,
                                                                OverlayMode::ClickThrough};
inline constexpr std::array<Visualization, 2> kVisualizationOptions{Visualization::Gauges,
                                                                    Visualization::Sparklines};
inline constexpr std::array<DiskTempMode, 3> kDiskTempOptions{
    DiskTempMode::SelectedDisk, DiskTempMode::Highest, DiskTempMode::Average};
inline constexpr std::array<FanSpeedMode, 2> kFanModeOptions{FanSpeedMode::Highest,
                                                             FanSpeedMode::Average};
inline constexpr std::array<MainboardTempMode, 2> kMainboardModeOptions{MainboardTempMode::Highest,
                                                                        MainboardTempMode::Average};

// Number of tiles the UI binds (matches `TilesConfig`).
inline constexpr std::size_t kSettingsTileCount = 6;

// The per-tile toggles, in display order.
enum class TileField : int
{
    Visible = 0,
    Primary,
    Secondary,
    Tertiary,
    Visualization,
    MiniSparkline,
};

enum class SensorToggle : int
{
    CpuTemperature = 0,
    GpuTemperature,
    DiskTemperature,
    FanSpeed,
    RamTemperature,
    MainboardTemperature,
};

enum class DeviceKind : int
{
    Gpu = 0,
    Cpu,
    Nic,
    Disk,
};

// Index of `value` in `options`, or 0 when it is absent (so a control always has a selection).
template <typename T, std::size_t N>
[[nodiscard]] inline std::size_t OptionIndex(const std::array<T, N>& options, T value) noexcept
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (options[i] == value)
        {
            return i;
        }
    }
    return 0;
}

// The next view in the cycle (wraps around). Used by the "View" menu/hotkey action.
[[nodiscard]] inline ViewMode NextViewMode(ViewMode view) noexcept
{
    const std::size_t index = OptionIndex(kViewOptions, view);
    return kViewOptions[(index + 1) % kViewOptions.size()];
}

// Clamps a combo index into `[0, count)`.
[[nodiscard]] inline std::size_t ClampOptionIndex(int index, std::size_t count) noexcept
{
    if (count == 0 || index < 0)
    {
        return 0;
    }
    const std::size_t candidate = static_cast<std::size_t>(index);
    return candidate < count ? candidate : count - 1;
}

// Formats a hotkey string for display, falling back to the raw text when it does not parse.
[[nodiscard]] std::wstring FormatHotkeyForDisplay(std::string_view utf8Hotkey);

// Parses a captured hotkey into canonical text. Returns false (leaving `out` untouched) for an
// invalid binding. An empty `text` is valid and clears the binding (used by the optional
// click-through hotkey).
[[nodiscard]] bool CanonicalizeHotkey(std::string_view text, std::string& out);

class SettingsBinding
{
  public:
    using ChangeCallback = std::function<void()>;

    SettingsBinding(pacecar::Config& config, ChangeCallback onChanged);

    [[nodiscard]] const pacecar::Config& Current() const noexcept
    {
        return config_;
    }

    // ---- General ------------------------------------------------------------------------------
    [[nodiscard]] int RefreshIndex() const noexcept;
    void SetRefreshIndex(int index) noexcept;
    [[nodiscard]] double Opacity() const noexcept;
    void SetOpacity(double opacity) noexcept;
    [[nodiscard]] int ThemeIndex() const noexcept;
    void SetThemeIndex(int index) noexcept;
    [[nodiscard]] int LayoutIndex() const noexcept;
    void SetLayoutIndex(int index) noexcept;
    [[nodiscard]] int ViewIndex() const noexcept;
    void SetViewIndex(int index) noexcept;
    // Value font size (DIPs) for the text views. Repaired to the allowed range on set.
    [[nodiscard]] int StatTextSize() const noexcept;
    void SetStatTextSize(int size) noexcept;
    [[nodiscard]] bool TransparentBackground() const noexcept;
    void SetTransparentBackground(bool enabled);
    [[nodiscard]] bool StartWithWindows() const noexcept;
    void SetStartWithWindows(bool enabled);
    [[nodiscard]] bool StartHidden() const noexcept;
    void SetStartHidden(bool enabled);

    // ---- Overlay ------------------------------------------------------------------------------
    [[nodiscard]] int OverlayModeIndex() const noexcept;
    void SetOverlayModeIndex(int index) noexcept;
    [[nodiscard]] bool AlwaysOnTop() const noexcept;
    void SetAlwaysOnTop(bool enabled);
    [[nodiscard]] int MonitorIndex() const noexcept;
    void SetMonitorIndex(int index) noexcept;
    [[nodiscard]] bool CaptureExclusion() const noexcept;
    void SetCaptureExclusion(bool enabled);

    // ---- Tiles --------------------------------------------------------------------------------
    [[nodiscard]] bool TileFlag(std::size_t tile, TileField field) const noexcept;
    void SetTileFlag(std::size_t tile, TileField field, bool value);
    [[nodiscard]] int TileVisualizationIndex(std::size_t tile) const noexcept;
    void SetTileVisualizationIndex(std::size_t tile, int index) noexcept;

    // ---- Sensors ------------------------------------------------------------------------------
    [[nodiscard]] bool SensorEnabled(SensorToggle toggle) const noexcept;
    void SetSensorEnabled(SensorToggle toggle, bool enabled);
    // The master deep-sensor switch. Enabling also prompts for elevation via the helper launcher
    // (the window/app layer performs the launch; the binding only maps the config flag).
    [[nodiscard]] bool DeepSensorsEnabled() const noexcept;
    void SetDeepSensorsEnabled(bool enabled);
    // Show the frame-time ("ms") line on the FPS tile. Off by default.
    [[nodiscard]] bool FpsShowFrameTime() const noexcept;
    void SetFpsShowFrameTime(bool enabled);
    [[nodiscard]] std::string_view Selection(DeviceKind kind) const noexcept;
    void SetSelection(DeviceKind kind, std::string value);
    [[nodiscard]] int DiskTempModeIndex() const noexcept;
    void SetDiskTempModeIndex(int index) noexcept;
    [[nodiscard]] int FanModeIndex() const noexcept;
    void SetFanModeIndex(int index) noexcept;
    [[nodiscard]] int MainboardModeIndex() const noexcept;
    void SetMainboardModeIndex(int index) noexcept;
    [[nodiscard]] std::string_view PingTarget() const noexcept;
    void SetPingTarget(std::string value);

    // ---- History ------------------------------------------------------------------------------
    [[nodiscard]] int RetentionIndex() const noexcept;
    void SetRetentionIndex(int index) noexcept;

    // ---- Hotkeys ------------------------------------------------------------------------------
    [[nodiscard]] std::string_view ToggleOverlayHotkey() const noexcept;
    [[nodiscard]] bool SetToggleOverlayHotkey(std::string_view text);
    [[nodiscard]] std::string_view ToggleClickThroughHotkey() const noexcept;
    [[nodiscard]] bool SetToggleClickThroughHotkey(std::string_view text);
    [[nodiscard]] std::string_view CycleViewHotkey() const noexcept;
    [[nodiscard]] bool SetCycleViewHotkey(std::string_view text);
    [[nodiscard]] std::string_view ToggleBackgroundHotkey() const noexcept;
    [[nodiscard]] bool SetToggleBackgroundHotkey(std::string_view text);
    [[nodiscard]] std::string_view ToggleFpsCaptureHotkey() const noexcept;
    [[nodiscard]] bool SetToggleFpsCaptureHotkey(std::string_view text);

    // ---- Bulk ---------------------------------------------------------------------------------
    // Restores the built-in defaults and reports the change exactly once.
    void ResetToDefaults();

  private:
    [[nodiscard]] pacecar::TileConfig* TileAt(std::size_t tile) noexcept;
    [[nodiscard]] const pacecar::TileConfig* TileAt(std::size_t tile) const noexcept;

    void Changed();

    pacecar::Config& config_;
    ChangeCallback onChanged_;
};
} // namespace pacecar::overlay