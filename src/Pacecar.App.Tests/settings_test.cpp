// T14 Settings-window binding tests (headless): control-group -> config mapping, clamp-on-edit,
// reset-to-defaults, hotkey canonicalization, and the debounced-save contract. No HWND is created:
// the mapping layer (`SettingsBinding`) is deliberately Win32-free.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>

#include "pacecar/config/Config.h"
#include "pacecar/metrics/Aggregator.h"
#include "pacecar/metrics/HistoryRetention.h"
#include "pacecar/metrics/MetricsSnapshot.h"
#include "pacecar/overlay/SettingsBinding.h"

namespace
{
using namespace std::chrono_literals;

using pacecar::Config;
using pacecar::DebouncedSaver;
using pacecar::overlay::DeviceKind;
using pacecar::overlay::kFanModeOptions;
using pacecar::overlay::kLayoutOptions;
using pacecar::overlay::kOverlayModeOptions;
using pacecar::overlay::kRefreshOptions;
using pacecar::overlay::kRetentionOptions;
using pacecar::overlay::kThemeOptions;
using pacecar::overlay::SensorToggle;
using pacecar::overlay::SettingsBinding;
using pacecar::overlay::TileField;

Config MakeConfig()
{
    return Config::Defaults();
}

TEST(SettingsBinding, MapsGeneralControlsToConfig)
{
    Config config = MakeConfig();
    SettingsBinding binding(config, [] {});

    binding.SetRefreshIndex(static_cast<int>(kRefreshOptions.size()) - 1);
    EXPECT_EQ(static_cast<int>(config.general.refresh), kRefreshOptions.back());

    binding.SetOpacity(0.42);
    EXPECT_DOUBLE_EQ(config.general.opacity, 0.42);

    binding.SetThemeIndex(2);
    EXPECT_EQ(config.general.theme, kThemeOptions[2]);

    binding.SetLayoutIndex(1);
    EXPECT_EQ(config.general.layout, kLayoutOptions[1]);

    binding.SetStartWithWindows(true);
    binding.SetStartHidden(true);
    EXPECT_TRUE(config.general.start_with_windows);
    EXPECT_TRUE(config.general.start_hidden);

    // Round-trips back out of the binding.
    EXPECT_EQ(binding.RefreshIndex(), static_cast<int>(kRefreshOptions.size()) - 1);
    EXPECT_DOUBLE_EQ(binding.Opacity(), 0.42);
    EXPECT_EQ(binding.ThemeIndex(), 2);
    EXPECT_EQ(binding.LayoutIndex(), 1);
}

TEST(SettingsBinding, MapsOverlayControlsToConfig)
{
    Config config = MakeConfig();
    SettingsBinding binding(config, [] {});

    binding.SetOverlayModeIndex(1);
    EXPECT_EQ(config.overlay.mode, kOverlayModeOptions[1]);

    binding.SetAlwaysOnTop(false);
    EXPECT_FALSE(config.overlay.always_on_top);

    binding.SetMonitorIndex(3);
    EXPECT_EQ(config.overlay.monitor_id, 3);

    binding.SetCaptureExclusion(false);
    EXPECT_FALSE(config.overlay.capture_exclusion);

    // enhanced_fullscreen is reserved and must never be exposed by the binding.
    EXPECT_FALSE(config.overlay.enhanced_fullscreen);
}

TEST(SettingsBinding, MapsTileTogglesAndVisualization)
{
    Config config = MakeConfig();
    SettingsBinding binding(config, [] {});

    binding.SetTileFlag(1, TileField::Visible, false);
    EXPECT_FALSE(config.tiles.ram.visible);

    binding.SetTileFlag(2, TileField::Secondary, false);
    EXPECT_FALSE(config.tiles.gpu.show_secondary);

    binding.SetTileFlag(3, TileField::MiniSparkline, true);
    EXPECT_TRUE(config.tiles.network.mini_sparklines);

    binding.SetTileVisualizationIndex(4, 1);
    EXPECT_EQ(config.tiles.disk.visualization, pacecar::Visualization::Sparklines);

    // Reading the flags back gives the same values.
    EXPECT_FALSE(binding.TileFlag(1, TileField::Visible));
    EXPECT_FALSE(binding.TileFlag(2, TileField::Secondary));
    EXPECT_TRUE(binding.TileFlag(3, TileField::MiniSparkline));
    EXPECT_EQ(binding.TileVisualizationIndex(4), 1);
}

TEST(SettingsBinding, MapsSensorControlsAndHistory)
{
    Config config = MakeConfig();
    SettingsBinding binding(config, [] {});

    binding.SetSensorEnabled(SensorToggle::GpuTemperature, false);
    EXPECT_FALSE(config.sensors.gpu_temperature);

    binding.SetSensorEnabled(SensorToggle::FanSpeed, false);
    EXPECT_FALSE(config.sensors.fan_speed);

    binding.SetSelection(DeviceKind::Nic, "Ethernet 2");
    EXPECT_EQ(config.sensors.nic_selection, "Ethernet 2");

    binding.SetDiskTempModeIndex(2);
    EXPECT_EQ(config.sensors.disk_temp_mode, pacecar::DiskTempMode::Average);

    binding.SetFanModeIndex(1);
    EXPECT_EQ(config.sensors.fan_mode, pacecar::FanSpeedMode::Average);

    binding.SetMainboardModeIndex(1);
    EXPECT_EQ(config.sensors.mainboard_mode, pacecar::MainboardTempMode::Average);

    binding.SetPingTarget("1.1.1.1");
    EXPECT_EQ(config.sensors.ping_target, "1.1.1.1");

    binding.SetRetentionIndex(static_cast<int>(kRetentionOptions.size()) - 1);
    EXPECT_EQ(config.history.retention_minutes, kRetentionOptions.back());
}

TEST(SettingsBinding, MapsDeepSensorMasterToggle)
{
    Config config = MakeConfig();
    int changes = 0;
    SettingsBinding binding(config, [&changes] { ++changes; });

    EXPECT_FALSE(binding.DeepSensorsEnabled());
    EXPECT_FALSE(config.sensors.deep_sensors);

    binding.SetDeepSensorsEnabled(true);
    EXPECT_TRUE(config.sensors.deep_sensors);
    EXPECT_TRUE(binding.DeepSensorsEnabled());
    EXPECT_EQ(changes, 1);

    binding.SetDeepSensorsEnabled(false);
    EXPECT_FALSE(config.sensors.deep_sensors);
    EXPECT_EQ(changes, 2);
}

TEST(SettingsBinding, ClampsOnEdit)
{
    Config config = MakeConfig();
    SettingsBinding binding(config, [] {});

binding.SetOpacity(5.0);
    EXPECT_DOUBLE_EQ(config.general.opacity, 1.0);
    binding.SetOpacity(-2.0);
    EXPECT_DOUBLE_EQ(config.general.opacity, 0.1);

    binding.SetRefreshIndex(-5);
    binding.SetThemeIndex(99);
    binding.SetRetentionIndex(999);

    EXPECT_EQ(config.general.refresh, pacecar::RefreshRate::Ms250);
    EXPECT_EQ(config.general.theme, kThemeOptions.back());
    EXPECT_EQ(config.history.retention_minutes, kRetentionOptions.back());

    // A NaN opacity is repaired by Clamp().
    binding.SetOpacity(std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(config.general.opacity, 0.65);
}

TEST(SettingsBinding, ResetRestoresDefaultsAndNotifiesOnce)
{
    Config config = MakeConfig();
    int changes = 0;
    SettingsBinding binding(config, [&changes] { ++changes; });

    binding.SetOpacity(0.9);
    binding.SetTileFlag(0, TileField::Visible, false);
    binding.SetRetentionIndex(6);
    const int before = changes;

    binding.ResetToDefaults();
    EXPECT_EQ(changes, before + 1);

    const Config defaults = Config::Defaults();
    EXPECT_DOUBLE_EQ(config.general.opacity, defaults.general.opacity);
    EXPECT_TRUE(config.tiles.cpu.visible);
    EXPECT_EQ(config.history.retention_minutes, defaults.history.retention_minutes);
    EXPECT_EQ(pacecar::ConfigToJsonString(config), pacecar::ConfigToJsonString(defaults));
}

TEST(SettingsBinding, CanonicalizesHotkeys)
{
    Config config = MakeConfig();
    SettingsBinding binding(config, [] {});

    ASSERT_TRUE(binding.SetToggleOverlayHotkey("shift+ctrl+p"));
    EXPECT_EQ(config.hotkeys.toggle_overlay, "Ctrl+Shift+P");

    // Invalid input is rejected without mutating the config.
    EXPECT_FALSE(binding.SetToggleOverlayHotkey("Ctrl+Banana"));
    EXPECT_EQ(config.hotkeys.toggle_overlay, "Ctrl+Shift+P");

    // An empty click-through hotkey is valid and clears the binding.
    ASSERT_TRUE(binding.SetToggleClickThroughHotkey(""));
    EXPECT_TRUE(config.hotkeys.toggle_click_through.empty());
}

TEST(SettingsBinding, RapidEditsProduceASingleDebouncedSave)
{
    Config config = MakeConfig();
    std::uint64_t saves = 0;
    DebouncedSaver saver([&saves] { ++saves; }, 80ms);
    SettingsBinding binding(config, [&saver] { saver.Touch(); });

    for (int i = 0; i < 12; ++i)
    {
        binding.SetOpacity(0.2 + 0.05 * i);
    }

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (saves == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(10ms);
    }

    EXPECT_EQ(saves, 1u);
    EXPECT_EQ(saver.SaveCount(), 1u);
}

TEST(SettingsBinding, HistoryCopyDoesNotAliasTheLiveRing)
{
    // The History window folds a *copy* of the rings; confirm the copy is independent so the UI can
    // never race the sampler.
    pacecar::metrics::MetricHistory source(8);
    pacecar::metrics::MetricHistory copy(8);
    pacecar::metrics::MetricsSnapshot snapshot;
    snapshot.cpu.totalUtilizationPercent = 5.0;
    source.Push(snapshot);

    copy = source;
    source.cpuTotalUtilization.Push(99.0);
    EXPECT_EQ(copy.cpuTotalUtilization.Size(), 1u);
    EXPECT_DOUBLE_EQ(copy.cpuTotalUtilization.Latest(), 5.0);
}
} // namespace