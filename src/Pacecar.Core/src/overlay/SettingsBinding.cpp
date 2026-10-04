#include "pacecar/overlay/SettingsBinding.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "pacecar/app/HotkeySpec.h"

namespace pacecar::overlay
{
namespace
{
std::wstring Utf8ToWide(std::string_view text)
{
    std::wstring out;
    out.reserve(text.size());
    for (const char c : text)
    {
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string WideToUtf8(std::wstring_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const wchar_t c : text)
    {
        if (c <= 0x7F)
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            // Hotkey strings are ASCII; a non-ASCII captured key is encoded as '?'.
            out.push_back('?');
        }
    }
    return out;
}
} // namespace

std::wstring FormatHotkeyForDisplay(std::string_view utf8Hotkey)
{
    const std::wstring wide = Utf8ToWide(utf8Hotkey);
    if (wide.empty())
    {
        return {};
    }
    const pacecar::app::HotkeyParseResult parsed = pacecar::app::ParseHotkey(wide);
    if (!parsed.ok())
    {
        return wide;
    }
    return pacecar::app::FormatHotkey(parsed.binding);
}

bool CanonicalizeHotkey(std::string_view text, std::string& out)
{
    std::wstring wide = Utf8ToWide(text);
    // Trim surrounding ASCII whitespace so a stray space form the capture control is not an error.
    const auto isSpace = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    while (!wide.empty() && isSpace(wide.front()))
    {
        wide.erase(wide.begin());
    }
    while (!wide.empty() && isSpace(wide.back()))
    {
        wide.pop_back();
    }

    if (wide.empty())
    {
        out.clear();
        return true;
    }
    const pacecar::app::HotkeyParseResult parsed = pacecar::app::ParseHotkey(wide);
    if (!parsed.ok())
    {
        return false;
    }
    out = WideToUtf8(pacecar::app::FormatHotkey(parsed.binding));
    return true;
}

SettingsBinding::SettingsBinding(pacecar::Config& config, ChangeCallback onChanged)
    : config_(config), onChanged_(std::move(onChanged))
{
}

void SettingsBinding::Changed()
{
    config_.Clamp();
    if (onChanged_)
    {
        onChanged_();
    }
}

pacecar::TileConfig* SettingsBinding::TileAt(std::size_t tile) noexcept
{
    switch (tile)
    {
    case 0:
        return &config_.tiles.cpu;
    case 1:
        return &config_.tiles.ram;
    case 2:
        return &config_.tiles.gpu;
    case 3:
        return &config_.tiles.network;
    case 4:
        return &config_.tiles.disk;
    case 5:
        return &config_.tiles.ping;
    default:
        return nullptr;
    }
}

const pacecar::TileConfig* SettingsBinding::TileAt(std::size_t tile) const noexcept
{
    return const_cast<SettingsBinding*>(this)->TileAt(tile);
}

// ---- General ---------------------------------------------------------------------------------

int SettingsBinding::RefreshIndex() const noexcept
{
    return static_cast<int>(OptionIndex(kRefreshOptions, static_cast<int>(config_.general.refresh)));
}

void SettingsBinding::SetRefreshIndex(int index) noexcept
{
    const std::size_t clamped = ClampOptionIndex(index, kRefreshOptions.size());
    config_.general.refresh = static_cast<RefreshRate>(kRefreshOptions[clamped]);
    Changed();
}

double SettingsBinding::Opacity() const noexcept
{
    return config_.general.opacity;
}

void SettingsBinding::SetOpacity(double opacity) noexcept
{
    config_.general.opacity = std::clamp(opacity, 0.1, 1.0);
    Changed();
}

int SettingsBinding::ThemeIndex() const noexcept
{
    return static_cast<int>(OptionIndex(kThemeOptions, config_.general.theme));
}

void SettingsBinding::SetThemeIndex(int index) noexcept
{
    const std::size_t clamped = ClampOptionIndex(index, kThemeOptions.size());
    config_.general.theme = kThemeOptions[clamped];
    Changed();
}

int SettingsBinding::LayoutIndex() const noexcept
{
    return static_cast<int>(OptionIndex(kLayoutOptions, config_.general.layout));
}

void SettingsBinding::SetLayoutIndex(int index) noexcept
{
    const std::size_t clamped = ClampOptionIndex(index, kLayoutOptions.size());
    config_.general.layout = kLayoutOptions[clamped];
    Changed();
}

bool SettingsBinding::StartWithWindows() const noexcept
{
    return config_.general.start_with_windows;
}

void SettingsBinding::SetStartWithWindows(bool enabled)
{
    config_.general.start_with_windows = enabled;
    Changed();
}

bool SettingsBinding::StartHidden() const noexcept
{
    return config_.general.start_hidden;
}

void SettingsBinding::SetStartHidden(bool enabled)
{
    config_.general.start_hidden = enabled;
    Changed();
}

// ---- Overlay ---------------------------------------------------------------------------------

int SettingsBinding::OverlayModeIndex() const noexcept
{
    return static_cast<int>(OptionIndex(kOverlayModeOptions, config_.overlay.mode));
}

void SettingsBinding::SetOverlayModeIndex(int index) noexcept
{
    const std::size_t clamped = ClampOptionIndex(index, kOverlayModeOptions.size());
    config_.overlay.mode = kOverlayModeOptions[clamped];
    Changed();
}

bool SettingsBinding::AlwaysOnTop() const noexcept
{
    return config_.overlay.always_on_top;
}

void SettingsBinding::SetAlwaysOnTop(bool enabled)
{
    config_.overlay.always_on_top = enabled;
    Changed();
}

int SettingsBinding::MonitorIndex() const noexcept
{
    return config_.overlay.monitor_id < 0 ? 0 : config_.overlay.monitor_id;
}

void SettingsBinding::SetMonitorIndex(int index) noexcept
{
    config_.overlay.monitor_id = std::max(index, 0);
    Changed();
}

bool SettingsBinding::CaptureExclusion() const noexcept
{
    return config_.overlay.capture_exclusion;
}

void SettingsBinding::SetCaptureExclusion(bool enabled)
{
    config_.overlay.capture_exclusion = enabled;
    Changed();
}

// ---- Tiles -----------------------------------------------------------------------------------

bool SettingsBinding::TileFlag(std::size_t tile, TileField field) const noexcept
{
    const pacecar::TileConfig* config = TileAt(tile);
    if (config == nullptr)
    {
        return false;
    }
    switch (field)
    {
    case TileField::Visible:
        return config->visible;
    case TileField::Primary:
        return config->show_primary;
    case TileField::Secondary:
        return config->show_secondary;
    case TileField::Tertiary:
        return config->show_tertiary;
    case TileField::Visualization:
        return config->show_visualization;
    case TileField::MiniSparkline:
        return config->mini_sparklines;
    default:
        return false;
    }
}

void SettingsBinding::SetTileFlag(std::size_t tile, TileField field, bool value)
{
    pacecar::TileConfig* config = TileAt(tile);
    if (config == nullptr)
    {
        return;
    }
    switch (field)
    {
    case TileField::Visible:
        config->visible = value;
        break;
    case TileField::Primary:
        config->show_primary = value;
        break;
    case TileField::Secondary:
        config->show_secondary = value;
        break;
    case TileField::Tertiary:
        config->show_tertiary = value;
        break;
    case TileField::Visualization:
        config->show_visualization = value;
        break;
    case TileField::MiniSparkline:
        config->mini_sparklines = value;
        break;
    default:
        return;
    }
    Changed();
}

int SettingsBinding::TileVisualizationIndex(std::size_t tile) const noexcept
{
    const pacecar::TileConfig* config = TileAt(tile);
    if (config == nullptr)
    {
        return 0;
    }
    return static_cast<int>(OptionIndex(kVisualizationOptions, config->visualization));
}

void SettingsBinding::SetTileVisualizationIndex(std::size_t tile, int index) noexcept
{
    pacecar::TileConfig* config = TileAt(tile);
    if (config == nullptr)
    {
        return;
    }
    const std::size_t clamped = ClampOptionIndex(index, kVisualizationOptions.size());
    config->visualization = kVisualizationOptions[clamped];
    Changed();
}

// ---- Sensors ---------------------------------------------------------------------------------

bool SettingsBinding::SensorEnabled(SensorToggle toggle) const noexcept
{
    switch (toggle)
    {
    case SensorToggle::CpuTemperature:
        return config_.sensors.cpu_temperature;
    case SensorToggle::GpuTemperature:
        return config_.sensors.gpu_temperature;
    case SensorToggle::DiskTemperature:
        return config_.sensors.disk_temperature;
    case SensorToggle::FanSpeed:
        return config_.sensors.fan_speed;
    case SensorToggle::RamTemperature:
        return config_.sensors.ram_temperature;
    case SensorToggle::MainboardTemperature:
        return config_.sensors.mainboard_temperature;
    default:
        return false;
    }
}

void SettingsBinding::SetSensorEnabled(SensorToggle toggle, bool enabled)
{
    switch (toggle)
    {
    case SensorToggle::CpuTemperature:
        config_.sensors.cpu_temperature = enabled;
        break;
    case SensorToggle::GpuTemperature:
        config_.sensors.gpu_temperature = enabled;
        break;
    case SensorToggle::DiskTemperature:
        config_.sensors.disk_temperature = enabled;
        break;
    case SensorToggle::FanSpeed:
        config_.sensors.fan_speed = enabled;
        break;
    case SensorToggle::RamTemperature:
        config_.sensors.ram_temperature = enabled;
        break;
    case SensorToggle::MainboardTemperature:
        config_.sensors.mainboard_temperature = enabled;
        break;
    default:
        return;
    }
    Changed();
}

bool SettingsBinding::DeepSensorsEnabled() const noexcept
{
    return config_.sensors.deep_sensors;
}

void SettingsBinding::SetDeepSensorsEnabled(bool enabled)
{
    config_.sensors.deep_sensors = enabled;
    Changed();
}

std::string_view SettingsBinding::Selection(DeviceKind kind) const noexcept
{
    switch (kind)
    {
    case DeviceKind::Gpu:
        return config_.sensors.gpu_selection;
    case DeviceKind::Cpu:
        return config_.sensors.cpu_selection;
    case DeviceKind::Nic:
        return config_.sensors.nic_selection;
    case DeviceKind::Disk:
        return config_.sensors.disk_selection;
    default:
        return {};
    }
}

void SettingsBinding::SetSelection(DeviceKind kind, std::string value)
{
    if (value.empty())
    {
        value = "auto";
    }
    switch (kind)
    {
    case DeviceKind::Gpu:
        config_.sensors.gpu_selection = std::move(value);
        break;
    case DeviceKind::Cpu:
        config_.sensors.cpu_selection = std::move(value);
        break;
    case DeviceKind::Nic:
        config_.sensors.nic_selection = std::move(value);
        break;
    case DeviceKind::Disk:
        config_.sensors.disk_selection = std::move(value);
        break;
    default:
        return;
    }
    Changed();
}

int SettingsBinding::DiskTempModeIndex() const noexcept
{
    return static_cast<int>(OptionIndex(kDiskTempOptions, config_.sensors.disk_temp_mode));
}

void SettingsBinding::SetDiskTempModeIndex(int index) noexcept
{
    const std::size_t clamped = ClampOptionIndex(index, kDiskTempOptions.size());
    config_.sensors.disk_temp_mode = kDiskTempOptions[clamped];
    Changed();
}

int SettingsBinding::FanModeIndex() const noexcept
{
    return static_cast<int>(OptionIndex(kFanModeOptions, config_.sensors.fan_mode));
}

void SettingsBinding::SetFanModeIndex(int index) noexcept
{
    const std::size_t clamped = ClampOptionIndex(index, kFanModeOptions.size());
    config_.sensors.fan_mode = kFanModeOptions[clamped];
    Changed();
}

int SettingsBinding::MainboardModeIndex() const noexcept
{
    return static_cast<int>(OptionIndex(kMainboardModeOptions, config_.sensors.mainboard_mode));
}

void SettingsBinding::SetMainboardModeIndex(int index) noexcept
{
    const std::size_t clamped = ClampOptionIndex(index, kMainboardModeOptions.size());
    config_.sensors.mainboard_mode = kMainboardModeOptions[clamped];
    Changed();
}

std::string_view SettingsBinding::PingTarget() const noexcept
{
    return config_.sensors.ping_target;
}

void SettingsBinding::SetPingTarget(std::string value)
{
    config_.sensors.ping_target = std::move(value);
    Changed();
}

// ---- History ---------------------------------------------------------------------------------

int SettingsBinding::RetentionIndex() const noexcept
{
    return static_cast<int>(OptionIndex(kRetentionOptions, config_.history.retention_minutes));
}

void SettingsBinding::SetRetentionIndex(int index) noexcept
{
    const std::size_t clamped = ClampOptionIndex(index, kRetentionOptions.size());
    config_.history.retention_minutes = kRetentionOptions[clamped];
    Changed();
}

// ---- Hotkeys ---------------------------------------------------------------------------------

std::string_view SettingsBinding::ToggleOverlayHotkey() const noexcept
{
    return config_.hotkeys.toggle_overlay;
}

bool SettingsBinding::SetToggleOverlayHotkey(std::string_view text)
{
    std::string canonical;
    if (!CanonicalizeHotkey(text, canonical) || canonical.empty())
    {
        return false;
    }
    config_.hotkeys.toggle_overlay = std::move(canonical);
    Changed();
    return true;
}

std::string_view SettingsBinding::ToggleClickThroughHotkey() const noexcept
{
    return config_.hotkeys.toggle_click_through;
}

bool SettingsBinding::SetToggleClickThroughHotkey(std::string_view text)
{
    std::string canonical;
    if (!CanonicalizeHotkey(text, canonical))
    {
        return false;
    }
    config_.hotkeys.toggle_click_through = std::move(canonical);
    Changed();
    return true;
}

// ---- Bulk ------------------------------------------------------------------------------------

void SettingsBinding::ResetToDefaults()
{
    config_.Reset();
    Changed();
}
} // namespace pacecar::overlay