#include "pacecar/config/Config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "pacecar/util/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace pacecar
{
namespace
{
using Json = nlohmann::ordered_json;

constexpr std::array<int, 5> kAllowedRefresh{250, 500, 1000, 2000, 5000};
constexpr std::array<int, 7> kAllowedRetention{1, 5, 10, 15, 30, 60, 120};

// Normalizes an enum token for case-insensitive comparison: lowercases and drops separators so
// "ClickThrough", "click_through", and "click-through" all compare equal.
std::string NormalizeToken(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        if (c == ' ' || c == '_' || c == '-' || c == '\t' || c == '\r' || c == '\n')
        {
            continue;
        }
        out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return out;
}

int NearestAllowed(int value, std::span<const int> allowed, int fallback)
{
    if (allowed.empty())
    {
        return fallback;
    }
    int best = allowed[0];
    int bestDelta = std::abs(value - best);
    for (const int candidate : allowed)
    {
        const int delta = std::abs(value - candidate);
        if (delta < bestDelta)
        {
            best = candidate;
            bestDelta = delta;
        }
    }
    return best;
}

int GetInt(const Json& j, const char* key, int fallback)
{
    const auto it = j.find(key);
    if (it == j.end())
    {
        return fallback;
    }
    if (it->is_number_integer())
    {
        return static_cast<int>(it->get<long long>());
    }
    if (it->is_number_float())
    {
        return static_cast<int>(it->get<double>());
    }
    if (it->is_string())
    {
        try
        {
            return std::stoi(it->get<std::string>());
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }
    return fallback;
}

double GetDouble(const Json& j, const char* key, double fallback)
{
    const auto it = j.find(key);
    if (it == j.end())
    {
        return fallback;
    }
    if (it->is_number())
    {
        return it->get<double>();
    }
    if (it->is_string())
    {
        try
        {
            return std::stod(it->get<std::string>());
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }
    return fallback;
}

bool GetBool(const Json& j, const char* key, bool fallback)
{
    const auto it = j.find(key);
    if (it == j.end())
    {
        return fallback;
    }
    if (it->is_boolean())
    {
        return it->get<bool>();
    }
    if (it->is_number_integer())
    {
        return it->get<long long>() != 0;
    }
    return fallback;
}

std::string GetString(const Json& j, const char* key, const std::string& fallback)
{
    const auto it = j.find(key);
    if (it != j.end() && it->is_string())
    {
        return it->get<std::string>();
    }
    return fallback;
}

template <std::size_t N>
int ParseEnumIndex(const Json& j, const char* key,
                   const std::array<std::pair<const char*, int>, N>& table, int fallback)
{
    const auto it = j.find(key);
    if (it == j.end())
    {
        return fallback;
    }
    std::string raw;
    if (it->is_string())
    {
        raw = it->get<std::string>();
    }
    else if (it->is_number_integer())
    {
        raw = std::to_string(it->get<long long>());
    }
    else
    {
        return fallback;
    }
    const std::string norm = NormalizeToken(raw);
    for (const auto& [name, value] : table)
    {
        if (NormalizeToken(name) == norm)
        {
            return value;
        }
    }
    return fallback;
}

constexpr auto kThemeNames =
    std::array<std::pair<const char*, int>, 3>{{{"dark", 0}, {"light", 1}, {"high_contrast", 2}}};
constexpr auto kLayoutNames = std::array<std::pair<const char*, int>, 4>{
    {{"compact_3x3", 0}, {"vertical_1x6", 1}, {"auto_fit", 2}, {"custom", 3}}};
constexpr auto kOverlayModeNames =
    std::array<std::pair<const char*, int>, 2>{{{"interactive", 0}, {"click_through", 1}}};
constexpr auto kVisualizationNames =
    std::array<std::pair<const char*, int>, 2>{{{"gauges", 0}, {"sparklines", 1}}};
constexpr auto kDiskTempNames = std::array<std::pair<const char*, int>, 3>{
    {{"selected_disk", 0}, {"highest", 1}, {"average", 2}}};
constexpr auto kFanModeNames =
    std::array<std::pair<const char*, int>, 2>{{{"highest", 0}, {"average", 1}}};

const char* ThemeToString(Theme theme)
{
    switch (theme)
    {
    case Theme::Light:
        return "light";
    case Theme::HighContrast:
        return "high_contrast";
    case Theme::Dark:
    default:
        return "dark";
    }
}

const char* LayoutToString(LayoutPreset layout)
{
    switch (layout)
    {
    case LayoutPreset::Vertical1x6:
        return "vertical_1x6";
    case LayoutPreset::AutoFit:
        return "auto_fit";
    case LayoutPreset::Custom:
        return "custom";
    case LayoutPreset::Compact3x3:
    default:
        return "compact_3x3";
    }
}

const char* OverlayModeToString(OverlayMode mode)
{
    return mode == OverlayMode::ClickThrough ? "click_through" : "interactive";
}

const char* VisualizationToString(Visualization visualization)
{
    return visualization == Visualization::Sparklines ? "sparklines" : "gauges";
}

const char* DiskTempToString(DiskTempMode mode)
{
    switch (mode)
    {
    case DiskTempMode::Highest:
        return "highest";
    case DiskTempMode::Average:
        return "average";
    case DiskTempMode::SelectedDisk:
    default:
        return "selected_disk";
    }
}

const char* FanModeToString(FanSpeedMode mode)
{
    return mode == FanSpeedMode::Average ? "average" : "highest";
}

const char* MainboardModeToString(MainboardTempMode mode)
{
    return mode == MainboardTempMode::Average ? "average" : "highest";
}

} // namespace

// ---- JSON mapping -----------------------------------------------------------------------------

void to_json(Json& j, const MonitorRect& rect)
{
    j = Json{{"monitor_id", rect.monitor_id},
             {"x", rect.x},
             {"y", rect.y},
             {"width", rect.width},
             {"height", rect.height},
             {"valid", rect.valid}};
}

void from_json(const Json& j, MonitorRect& rect)
{
    const MonitorRect defaults{};
    rect.monitor_id = GetInt(j, "monitor_id", defaults.monitor_id);
    rect.x = GetInt(j, "x", defaults.x);
    rect.y = GetInt(j, "y", defaults.y);
    rect.width = GetInt(j, "width", defaults.width);
    rect.height = GetInt(j, "height", defaults.height);
    rect.valid = GetBool(j, "valid", defaults.valid);
}

void to_json(Json& j, const TileConfig& tile)
{
    j = Json{{"visible", tile.visible},
             {"show_primary", tile.show_primary},
             {"show_secondary", tile.show_secondary},
             {"show_tertiary", tile.show_tertiary},
             {"visualization", VisualizationToString(tile.visualization)},
             {"mini_sparklines", tile.mini_sparklines}};
}

void from_json(const Json& j, TileConfig& tile)
{
    const TileConfig defaults{};
    tile.visible = GetBool(j, "visible", defaults.visible);
    tile.show_primary = GetBool(j, "show_primary", defaults.show_primary);
    tile.show_secondary = GetBool(j, "show_secondary", defaults.show_secondary);
    tile.show_tertiary = GetBool(j, "show_tertiary", defaults.show_tertiary);
    tile.visualization = static_cast<Visualization>(
        ParseEnumIndex(j, "visualization", kVisualizationNames, static_cast<int>(defaults.visualization)));
    tile.mini_sparklines = GetBool(j, "mini_sparklines", defaults.mini_sparklines);
}

void to_json(Json& j, const TilesConfig& tiles)
{
    j = Json{{"cpu", tiles.cpu},       {"ram", tiles.ram},       {"gpu", tiles.gpu},
             {"network", tiles.network}, {"disk", tiles.disk},    {"ping", tiles.ping}};
}

void from_json(const Json& j, TilesConfig& tiles)
{
    const auto sub = [&j](const char* key) -> Json
    {
        const auto it = j.find(key);
        return (it != j.end() && it->is_object()) ? *it : Json::object();
    };
    tiles.cpu = sub("cpu").get<TileConfig>();
    tiles.ram = sub("ram").get<TileConfig>();
    tiles.gpu = sub("gpu").get<TileConfig>();
    tiles.network = sub("network").get<TileConfig>();
    tiles.disk = sub("disk").get<TileConfig>();
    tiles.ping = sub("ping").get<TileConfig>();
}

void to_json(Json& j, const GeneralConfig& general)
{
    j = Json{{"refresh_ms", static_cast<int>(general.refresh)},
             {"opacity", general.opacity},
             {"theme", ThemeToString(general.theme)},
             {"layout_preset", LayoutToString(general.layout)},
             {"start_with_windows", general.start_with_windows},
             {"start_hidden", general.start_hidden}};
}

void from_json(const Json& j, GeneralConfig& general)
{
    const GeneralConfig defaults{};
    const int refresh = GetInt(j, "refresh_ms", static_cast<int>(defaults.refresh));
    general.refresh = static_cast<RefreshRate>(NearestAllowed(refresh, kAllowedRefresh,
                                                              static_cast<int>(defaults.refresh)));
    general.opacity = GetDouble(j, "opacity", defaults.opacity);
    general.theme = static_cast<Theme>(
        ParseEnumIndex(j, "theme", kThemeNames, static_cast<int>(defaults.theme)));
    general.layout = static_cast<LayoutPreset>(
        ParseEnumIndex(j, "layout_preset", kLayoutNames, static_cast<int>(defaults.layout)));
    general.start_with_windows = GetBool(j, "start_with_windows", defaults.start_with_windows);
    general.start_hidden = GetBool(j, "start_hidden", defaults.start_hidden);
}

void to_json(Json& j, const OverlayConfig& overlay)
{
    j = Json{{"mode", OverlayModeToString(overlay.mode)},
             {"always_on_top", overlay.always_on_top},
             {"monitor_id", overlay.monitor_id},
             {"enhanced_fullscreen", overlay.enhanced_fullscreen},
             {"capture_exclusion", overlay.capture_exclusion},
             {"monitor_rects", overlay.monitor_rects}};
}

void from_json(const Json& j, OverlayConfig& overlay)
{
    const OverlayConfig defaults{};
    overlay.mode = static_cast<OverlayMode>(
        ParseEnumIndex(j, "mode", kOverlayModeNames, static_cast<int>(defaults.mode)));
    overlay.always_on_top = GetBool(j, "always_on_top", defaults.always_on_top);
    overlay.monitor_id = GetInt(j, "monitor_id", defaults.monitor_id);
    overlay.enhanced_fullscreen = GetBool(j, "enhanced_fullscreen", defaults.enhanced_fullscreen);
    overlay.capture_exclusion = GetBool(j, "capture_exclusion", defaults.capture_exclusion);
    overlay.monitor_rects.clear();
    const auto it = j.find("monitor_rects");
    if (it != j.end() && it->is_array())
    {
        for (const auto& entry : *it)
        {
            if (entry.is_object())
            {
                overlay.monitor_rects.push_back(entry.get<MonitorRect>());
            }
        }
    }
}

void to_json(Json& j, const SensorsConfig& sensors)
{
    j = Json{{"gpu_selection", sensors.gpu_selection},
             {"cpu_selection", sensors.cpu_selection},
             {"nic_selection", sensors.nic_selection},
             {"disk_selection", sensors.disk_selection},
             {"cpu_temperature", sensors.cpu_temperature},
             {"gpu_temperature", sensors.gpu_temperature},
             {"disk_temperature", sensors.disk_temperature},
             {"fan_speed", sensors.fan_speed},
             {"ram_temperature", sensors.ram_temperature},
             {"mainboard_temperature", sensors.mainboard_temperature},
             {"disk_temp_mode", DiskTempToString(sensors.disk_temp_mode)},
             {"fan_mode", FanModeToString(sensors.fan_mode)},
             {"mainboard_mode", MainboardModeToString(sensors.mainboard_mode)},
             {"ping_target", sensors.ping_target}};
}

void from_json(const Json& j, SensorsConfig& sensors)
{
    const SensorsConfig defaults{};
    sensors.gpu_selection = GetString(j, "gpu_selection", defaults.gpu_selection);
    sensors.cpu_selection = GetString(j, "cpu_selection", defaults.cpu_selection);
    sensors.nic_selection = GetString(j, "nic_selection", defaults.nic_selection);
    sensors.disk_selection = GetString(j, "disk_selection", defaults.disk_selection);
    sensors.cpu_temperature = GetBool(j, "cpu_temperature", defaults.cpu_temperature);
    sensors.gpu_temperature = GetBool(j, "gpu_temperature", defaults.gpu_temperature);
    sensors.disk_temperature = GetBool(j, "disk_temperature", defaults.disk_temperature);
    sensors.fan_speed = GetBool(j, "fan_speed", defaults.fan_speed);
    sensors.ram_temperature = GetBool(j, "ram_temperature", defaults.ram_temperature);
    sensors.mainboard_temperature = GetBool(j, "mainboard_temperature", defaults.mainboard_temperature);
    sensors.disk_temp_mode = static_cast<DiskTempMode>(
        ParseEnumIndex(j, "disk_temp_mode", kDiskTempNames, static_cast<int>(defaults.disk_temp_mode)));
    sensors.fan_mode = static_cast<FanSpeedMode>(
        ParseEnumIndex(j, "fan_mode", kFanModeNames, static_cast<int>(defaults.fan_mode)));
    sensors.mainboard_mode = static_cast<MainboardTempMode>(
        ParseEnumIndex(j, "mainboard_mode", kFanModeNames, static_cast<int>(defaults.mainboard_mode)));
    sensors.ping_target = GetString(j, "ping_target", defaults.ping_target);
}

void to_json(Json& j, const HistoryConfig& history)
{
    j = Json{{"retention_minutes", history.retention_minutes}};
}

void from_json(const Json& j, HistoryConfig& history)
{
    const HistoryConfig defaults{};
    const int retention = GetInt(j, "retention_minutes", defaults.retention_minutes);
    history.retention_minutes =
        NearestAllowed(retention, kAllowedRetention, defaults.retention_minutes);
}

void to_json(Json& j, const HotkeysConfig& hotkeys)
{
    j = Json{{"toggle_overlay", hotkeys.toggle_overlay},
             {"toggle_click_through", hotkeys.toggle_click_through}};
}

void from_json(const Json& j, HotkeysConfig& hotkeys)
{
    const HotkeysConfig defaults{};
    hotkeys.toggle_overlay = GetString(j, "toggle_overlay", defaults.toggle_overlay);
    hotkeys.toggle_click_through = GetString(j, "toggle_click_through", defaults.toggle_click_through);
}

void to_json(Json& j, const Config& config)
{
    j = Json{{"schema_version", config.schema_version},
             {"general", config.general},
             {"overlay", config.overlay},
             {"tiles", config.tiles},
             {"sensors", config.sensors},
             {"history", config.history},
             {"hotkeys", config.hotkeys}};
}

void from_json(const Json& j, Config& config)
{
    const Config defaults{};
    config.schema_version = GetInt(j, "schema_version", defaults.schema_version);

    const auto sub = [&j](const char* key) -> Json
    {
        const auto it = j.find(key);
        return (it != j.end() && it->is_object()) ? *it : Json::object();
    };
    config.general = sub("general").get<GeneralConfig>();
    config.overlay = sub("overlay").get<OverlayConfig>();
    config.tiles = sub("tiles").get<TilesConfig>();
    config.sensors = sub("sensors").get<SensorsConfig>();
    config.history = sub("history").get<HistoryConfig>();
    config.hotkeys = sub("hotkeys").get<HotkeysConfig>();
}

// ---- Config -----------------------------------------------------------------------------------

Config Config::Defaults()
{
    return Config{};
}

void Config::Reset()
{
    *this = Config{};
}

void Config::Clamp()
{
    if (schema_version < 1)
    {
        schema_version = 1;
    }

    general.refresh = static_cast<RefreshRate>(
        NearestAllowed(static_cast<int>(general.refresh), kAllowedRefresh,
                       static_cast<int>(RefreshRate::Ms1000)));

    if (!std::isfinite(general.opacity))
    {
        general.opacity = 0.65;
    }
    general.opacity = std::clamp(general.opacity, 0.1, 1.0);

    history.retention_minutes =
        NearestAllowed(history.retention_minutes, kAllowedRetention, 30);

    if (overlay.monitor_id < 0)
    {
        overlay.monitor_id = 0;
    }
    for (auto& rect : overlay.monitor_rects)
    {
        if (rect.monitor_id < 0)
        {
            rect.monitor_id = 0;
        }
        if (rect.width < 0)
        {
            rect.width = 0;
        }
        if (rect.height < 0)
        {
            rect.height = 0;
        }
    }
}

std::filesystem::path Config::DefaultPath()
{
    constexpr DWORD kBufferLength = 32767;
    std::wstring buffer(kBufferLength, L'\0');
    const DWORD length =
        ::GetEnvironmentVariableW(L"APPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        // A future MSIX build would fall back to the package's ApplicationData path here.
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer) / L"Pacecar" / L"config.json";
}

Config Config::Load()
{
    return Load(DefaultPath());
}

Config Config::Load(const std::filesystem::path& path)
{
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec))
    {
        return Config::Defaults();
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        LogWarn(L"config: failed to open file, using defaults");
        return Config::Defaults();
    }

    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Config config;
    if (!ConfigFromJsonString(text, config))
    {
        LogWarn(L"config: malformed JSON, using defaults");
        return Config::Defaults();
    }
    return config;
}

bool Config::Save() const
{
    return Save(DefaultPath());
}

bool Config::Save(const std::filesystem::path& path) const
{
    if (path.empty())
    {
        LogWarn(L"config: no config path available, save skipped");
        return false;
    }

    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            LogWarn(L"config: failed to create config directory");
            return false;
        }
    }

    std::filesystem::path temp = path;
    temp += L".tmp" + std::to_wstring(::GetCurrentProcessId());

    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            LogWarn(L"config: failed to open temp file for writing");
            return false;
        }
        const std::string text = ConfigToJsonString(*this, 2);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out)
        {
            out.close();
            std::filesystem::remove(temp, ec);
            LogWarn(L"config: failed to write temp file");
            return false;
        }
    }

    std::filesystem::rename(temp, path, ec);
    if (ec)
    {
        std::error_code cleanupError;
        std::filesystem::remove(temp, cleanupError);
        LogWarn(L"config: failed to replace config file");
        return false;
    }
    return true;
}

std::string ConfigToJsonString(const Config& config, int indent)
{
    const Json j = config;
    return indent > 0 ? j.dump(indent) : j.dump();
}

bool ConfigFromJsonString(std::string_view text, Config& out)
{
    try
    {
        const Json j = Json::parse(text.begin(), text.end());
        if (!j.is_object())
        {
            return false;
        }
        Config parsed = j.get<Config>();
        parsed.Clamp();
        out = std::move(parsed);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// ---- DebouncedSaver ---------------------------------------------------------------------------

DebouncedSaver::DebouncedSaver(SaveFunction save, std::chrono::milliseconds delay)
    : save_(std::move(save)), delay_(delay)
{
    worker_ = std::thread([this] { Worker(); });
}

DebouncedSaver::~DebouncedSaver()
{
    Flush();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void DebouncedSaver::Touch() noexcept
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        dirty_ = true;
        deadline_ = std::chrono::steady_clock::now() + delay_;
    }
    cv_.notify_all();
}

void DebouncedSaver::Flush()
{
    SaveFunction fn;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (!dirty_)
        {
            return;
        }
        dirty_ = false;
        fn = save_;
    }
    if (fn)
    {
        fn();
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ++saveCount_;
    }
}

void DebouncedSaver::Cancel() noexcept
{
    const std::lock_guard<std::mutex> lock(mutex_);
    dirty_ = false;
}

bool DebouncedSaver::Pending() const noexcept
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return dirty_;
}

std::uint64_t DebouncedSaver::SaveCount() const noexcept
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return saveCount_;
}

void DebouncedSaver::Worker()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (true)
    {
        cv_.wait(lock, [this] { return dirty_ || stop_; });
        if (stop_)
        {
            break;
        }

        const auto deadline = deadline_;
        cv_.wait_until(lock, deadline,
                       [this, deadline] { return stop_ || deadline_ != deadline; });
        if (stop_)
        {
            break;
        }
        if (deadline_ != deadline)
        {
            continue; // Re-touched: wait out the new deadline.
        }
        if (!dirty_)
        {
            continue;
        }

        dirty_ = false;
        SaveFunction fn = save_;
        lock.unlock();
        if (fn)
        {
            fn();
        }
        lock.lock();
        ++saveCount_;
    }
}
} // namespace pacecar