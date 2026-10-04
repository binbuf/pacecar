#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "pacecar/config/Config.h"
#include "pacecar/util/Logger.h"

namespace
{
using namespace std::chrono_literals;
using pacecar::Config;
using pacecar::ConfigFromJsonString;
using pacecar::ConfigToJsonString;
using pacecar::DebouncedSaver;
using pacecar::DiskTempMode;
using pacecar::LayoutPreset;
using pacecar::LogLevel;
using pacecar::Logger;
using pacecar::OverlayMode;
using pacecar::RefreshRate;
using pacecar::Theme;
using pacecar::Visualization;

std::atomic<int> g_tempCounter{0};

std::filesystem::path MakeTempDir()
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("pacecar_cfg_" + std::to_string(::GetCurrentProcessId()) + "_" +
         std::to_string(g_tempCounter.fetch_add(1)));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

bool WaitFor(const std::function<bool()>& predicate, std::chrono::milliseconds timeout)
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end)
    {
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return predicate();
}

class ConfigFileTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        dir_ = MakeTempDir();
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    std::filesystem::path dir_;
};

class ConfigLoggerTest : public ConfigFileTest
{
  protected:
    void SetUp() override
    {
        ConfigFileTest::SetUp();
        Logger& logger = Logger::Instance();
        logger.SetDiagnosticsEnabled(false);
        logger.SetMinimumLevel(LogLevel::Trace);
        logger.SetSink(nullptr);
        logger.SetRateLimit(10'000ms);
        logger.ResetRateLimit();
    }

    void TearDown() override
    {
        Logger& logger = Logger::Instance();
        logger.SetSink(nullptr);
        logger.SetDiagnosticsEnabled(false);
        logger.ResetRateLimit();
        ConfigFileTest::TearDown();
    }
};

const char* kDefaultConfigJson = R"JSON({
  "schema_version": 1,
  "general": {
    "refresh_ms": 1000,
    "opacity": 0.65,
    "theme": "dark",
    "layout_preset": "compact_3x3",
    "start_with_windows": false,
    "start_hidden": false
  },
  "overlay": {
    "mode": "interactive",
    "always_on_top": true,
    "monitor_id": 0,
    "enhanced_fullscreen": false,
    "capture_exclusion": true,
    "monitor_rects": []
  },
  "tiles": {
    "cpu": {
      "visible": true,
      "show_primary": true,
      "show_secondary": true,
      "show_tertiary": true,
      "show_visualization": true,
      "visualization": "gauges",
      "mini_sparklines": false
    },
    "ram": {
      "visible": true,
      "show_primary": true,
      "show_secondary": true,
      "show_tertiary": true,
      "show_visualization": true,
      "visualization": "gauges",
      "mini_sparklines": false
    },
    "gpu": {
      "visible": true,
      "show_primary": true,
      "show_secondary": true,
      "show_tertiary": true,
      "show_visualization": true,
      "visualization": "gauges",
      "mini_sparklines": false
    },
    "network": {
      "visible": true,
      "show_primary": true,
      "show_secondary": true,
      "show_tertiary": true,
      "show_visualization": true,
      "visualization": "gauges",
      "mini_sparklines": false
    },
    "disk": {
      "visible": true,
      "show_primary": true,
      "show_secondary": true,
      "show_tertiary": true,
      "show_visualization": true,
      "visualization": "gauges",
      "mini_sparklines": false
    },
    "ping": {
      "visible": true,
      "show_primary": true,
      "show_secondary": true,
      "show_tertiary": true,
      "show_visualization": true,
      "visualization": "gauges",
      "mini_sparklines": false
    },
    "fps": {
      "visible": false,
      "show_primary": true,
      "show_secondary": true,
      "show_tertiary": true,
      "show_visualization": true,
      "visualization": "gauges",
      "mini_sparklines": false
    }
  },
  "layout": {
    "custom_tiles": []
  },
  "sensors": {
    "gpu_selection": "auto",
    "cpu_selection": "auto",
    "nic_selection": "auto",
    "disk_selection": "auto",
    "cpu_temperature": true,
    "gpu_temperature": true,
    "disk_temperature": true,
    "fan_speed": true,
    "ram_temperature": true,
    "mainboard_temperature": true,
    "deep_sensors": false,
    "fps_capture": false,
    "disk_temp_mode": "selected_disk",
    "fan_mode": "highest",
    "mainboard_mode": "highest",
    "ping_target": "8.8.8.8"
  },
  "history": {
    "retention_minutes": 30
  },
  "hotkeys": {
    "toggle_overlay": "Ctrl+Shift+P",
    "toggle_click_through": ""
  }
})JSON";

TEST(ConfigDefaults, MatchesSpec)
{
    const Config config = Config::Defaults();
    EXPECT_EQ(config.schema_version, 1);
    EXPECT_EQ(config.general.refresh, RefreshRate::Ms1000);
    EXPECT_DOUBLE_EQ(config.general.opacity, 0.65);
    EXPECT_EQ(config.general.theme, Theme::Dark);
    EXPECT_EQ(config.general.layout, LayoutPreset::Compact3x3);
    EXPECT_FALSE(config.general.start_with_windows);
    EXPECT_FALSE(config.general.start_hidden);
    EXPECT_EQ(config.overlay.mode, OverlayMode::Interactive);
    EXPECT_TRUE(config.overlay.always_on_top);
    EXPECT_FALSE(config.overlay.enhanced_fullscreen);
    EXPECT_TRUE(config.tiles.cpu.visible);
    EXPECT_EQ(config.tiles.gpu.visualization, Visualization::Gauges);
    EXPECT_EQ(config.sensors.disk_temp_mode, DiskTempMode::SelectedDisk);
    EXPECT_EQ(config.sensors.ping_target, "8.8.8.8");
    EXPECT_EQ(config.history.retention_minutes, 30);
    EXPECT_EQ(config.hotkeys.toggle_overlay, "Ctrl+Shift+P");
}

TEST(ConfigSerialization, GoldenSnapshotOfDefault)
{
    EXPECT_EQ(ConfigToJsonString(Config::Defaults()), std::string(kDefaultConfigJson));
}

TEST(ConfigSerialization, RoundTripIsStable)
{
    const std::string first = ConfigToJsonString(Config::Defaults());
    Config loaded;
    ASSERT_TRUE(ConfigFromJsonString(first, loaded));
    EXPECT_EQ(ConfigToJsonString(loaded), first);
}

TEST(ConfigSerialization, MissingKeysUseDefaults)
{
    Config config;
    ASSERT_TRUE(ConfigFromJsonString(R"({"general":{"opacity":0.5}})", config));
    EXPECT_DOUBLE_EQ(config.general.opacity, 0.5);
    EXPECT_EQ(config.general.refresh, RefreshRate::Ms1000);
    EXPECT_EQ(config.overlay.mode, OverlayMode::Interactive);
    EXPECT_EQ(config.sensors.ping_target, "8.8.8.8");
    EXPECT_EQ(ConfigToJsonString(config),
              [] {
                  Config merged = Config::Defaults();
                  merged.general.opacity = 0.5;
                  return ConfigToJsonString(merged);
              }());
}

TEST(ConfigSerialization, MalformedJsonReturnsFalse)
{
    Config config;
    EXPECT_FALSE(ConfigFromJsonString("{ not valid json", config));
    EXPECT_FALSE(ConfigFromJsonString("[]", config));
    EXPECT_FALSE(ConfigFromJsonString("", config));
}

TEST(ConfigSerialization, EnumsParseCaseInsensitively)
{
    Config config;
    ASSERT_TRUE(ConfigFromJsonString(
        R"({
            "general":{"theme":"HIGH CONTRAST","layout_preset":"Vertical 1x6"},
            "overlay":{"mode":"clickthrough"},
            "tiles":{"cpu":{"visualization":"Sparklines"}},
            "sensors":{"disk_temp_mode":"AVERAGE","fan_mode":"Average","mainboard_mode":"average"}
        })",
        config));
    EXPECT_EQ(config.general.theme, Theme::HighContrast);
    EXPECT_EQ(config.general.layout, LayoutPreset::Vertical1x6);
    EXPECT_EQ(config.overlay.mode, OverlayMode::ClickThrough);
    EXPECT_EQ(config.tiles.cpu.visualization, Visualization::Sparklines);
    EXPECT_EQ(config.sensors.disk_temp_mode, DiskTempMode::Average);
    EXPECT_EQ(config.sensors.fan_mode, pacecar::FanSpeedMode::Average);
    EXPECT_EQ(config.sensors.mainboard_mode, pacecar::MainboardTempMode::Average);
}

TEST(ConfigSerialization, UnknownEnumsFallBack)
{
    Config config;
    ASSERT_TRUE(ConfigFromJsonString(
        R"({"general":{"theme":"rainbow"},"overlay":{"mode":"banana"},
            "tiles":{"ram":{"visualization":"pie"}}})",
        config));
    EXPECT_EQ(config.general.theme, Theme::Dark);
    EXPECT_EQ(config.overlay.mode, OverlayMode::Interactive);
    EXPECT_EQ(config.tiles.ram.visualization, Visualization::Gauges);
}

TEST(ConfigSerialization, CustomLayoutGeometryRoundTrips)
{
    Config config = Config::Defaults();
    config.general.layout = LayoutPreset::Custom;
    config.layout.custom_tiles.push_back(
        pacecar::CustomTilePlacement{"cpu", 1.0, 2.0, 120.0, 80.0, true});
    config.layout.custom_tiles.push_back(
        pacecar::CustomTilePlacement{"gpu", 130.0, 2.0, 120.0, 80.0, true});

    Config loaded;
    ASSERT_TRUE(ConfigFromJsonString(ConfigToJsonString(config), loaded));
    EXPECT_EQ(loaded.general.layout, LayoutPreset::Custom);
    ASSERT_EQ(loaded.layout.custom_tiles.size(), 2u);
    EXPECT_EQ(loaded.layout.custom_tiles[0].tile, "cpu");
    EXPECT_DOUBLE_EQ(loaded.layout.custom_tiles[0].x, 1.0);
    EXPECT_DOUBLE_EQ(loaded.layout.custom_tiles[1].width, 120.0);
    EXPECT_TRUE(loaded.layout.custom_tiles[1].valid);
    EXPECT_EQ(ConfigToJsonString(loaded), ConfigToJsonString(config));
}

TEST(ConfigSerialization, VisualizationToggleParses)
{
    Config config;
    ASSERT_TRUE(ConfigFromJsonString(R"({"tiles":{"cpu":{"show_visualization":false}}})", config));
    EXPECT_FALSE(config.tiles.cpu.show_visualization);
}

TEST(ConfigClamp, CustomGeometryRepaired)
{
    Config config;
    ASSERT_TRUE(ConfigFromJsonString(
        R"({"layout":{"custom_tiles":[{"tile":"cpu","x":0,"y":0,"width":-5,"height":-9,"valid":true}]}})",
        config));
    ASSERT_EQ(config.layout.custom_tiles.size(), 1u);
    EXPECT_DOUBLE_EQ(config.layout.custom_tiles[0].width, 0.0);
    EXPECT_DOUBLE_EQ(config.layout.custom_tiles[0].height, 0.0);
}

TEST(ConfigClamp, Opacity)
{
    Config config;
    ASSERT_TRUE(ConfigFromJsonString(R"({"general":{"opacity":5.0}})", config));
    EXPECT_DOUBLE_EQ(config.general.opacity, 1.0);

    ASSERT_TRUE(ConfigFromJsonString(R"({"general":{"opacity":-1.0}})", config));
    EXPECT_DOUBLE_EQ(config.general.opacity, 0.1);
}

TEST(ConfigClamp, RefreshSnapsToAllowedSet)
{
    Config config;
    ASSERT_TRUE(ConfigFromJsonString(R"({"general":{"refresh_ms":100}})", config));
    EXPECT_EQ(config.general.refresh, RefreshRate::Ms250);

    ASSERT_TRUE(ConfigFromJsonString(R"({"general":{"refresh_ms":4000}})", config));
    EXPECT_EQ(config.general.refresh, RefreshRate::Ms5000);

    ASSERT_TRUE(ConfigFromJsonString(R"({"general":{"refresh_ms":900}})", config));
    EXPECT_EQ(config.general.refresh, RefreshRate::Ms1000);
}

TEST(ConfigClamp, RetentionSnapsToAllowedSet)
{
    Config config;
    ASSERT_TRUE(ConfigFromJsonString(R"({"history":{"retention_minutes":7}})", config));
    EXPECT_EQ(config.history.retention_minutes, 5);

    ASSERT_TRUE(ConfigFromJsonString(R"({"history":{"retention_minutes":1000}})", config));
    EXPECT_EQ(config.history.retention_minutes, 120);

    ASSERT_TRUE(ConfigFromJsonString(R"({"history":{"retention_minutes":0}})", config));
    EXPECT_EQ(config.history.retention_minutes, 1);
}

TEST(ConfigClamp, NegativeMonitorAndSchemaVersionRepaired)
{
    Config config;
    ASSERT_TRUE(ConfigFromJsonString(
        R"({"schema_version":-3,"overlay":{"monitor_id":-5,"monitor_rects":[
              {"monitor_id":-1,"width":-10,"height":-20}]}})",
        config));
    EXPECT_EQ(config.schema_version, 1);
    EXPECT_EQ(config.overlay.monitor_id, 0);
    ASSERT_EQ(config.overlay.monitor_rects.size(), 1u);
    EXPECT_EQ(config.overlay.monitor_rects[0].monitor_id, 0);
    EXPECT_EQ(config.overlay.monitor_rects[0].width, 0);
    EXPECT_EQ(config.overlay.monitor_rects[0].height, 0);
}

TEST_F(ConfigFileTest, MissingFileReturnsDefaults)
{
    const Config config = Config::Load(dir_ / "does_not_exist.json");
    EXPECT_EQ(ConfigToJsonString(config), ConfigToJsonString(Config::Defaults()));
}

TEST_F(ConfigLoggerTest, MalformedFileLogsWarningAndFallsBack)
{
    Logger& logger = Logger::Instance();
    logger.SetDiagnosticsEnabled(true);
    int warnings = 0;
    logger.SetSink(
        [&](LogLevel level, std::wstring_view)
        {
            if (level == LogLevel::Warn)
            {
                ++warnings;
            }
        });

    const std::filesystem::path path = dir_ / "config.json";
    {
        std::ofstream out(path, std::ios::binary);
        out << "{ this is not json";
    }

    const Config config = Config::Load(path);
    EXPECT_EQ(ConfigToJsonString(config), ConfigToJsonString(Config::Defaults()));
    EXPECT_GE(warnings, 1);
}

TEST_F(ConfigFileTest, SaveCreatesParentsAndReloads)
{
    const std::filesystem::path path = dir_ / "nested" / "deeper" / "config.json";
    Config config = Config::Defaults();
    config.general.opacity = 0.4;
    config.general.refresh = RefreshRate::Ms250;
    config.sensors.ping_target = "1.1.1.1";
    config.hotkeys.toggle_overlay = "Ctrl+Alt+O";

    ASSERT_TRUE(config.Save(path));
    ASSERT_TRUE(std::filesystem::exists(path));

    const Config loaded = Config::Load(path);
    EXPECT_EQ(ConfigToJsonString(loaded), ConfigToJsonString(config));
}

TEST_F(ConfigFileTest, SaveReplacesExistingFile)
{
    const std::filesystem::path path = dir_ / "config.json";

    Config first = Config::Defaults();
    first.general.theme = Theme::Light;
    ASSERT_TRUE(first.Save(path));

    Config second = Config::Defaults();
    second.general.theme = Theme::HighContrast;
    second.overlay.mode = OverlayMode::ClickThrough;
    ASSERT_TRUE(second.Save(path));

    const Config loaded = Config::Load(path);
    EXPECT_EQ(ConfigToJsonString(loaded), ConfigToJsonString(second));
}

TEST(ConfigDebounce, CoalescesRapidMutations)
{
    std::atomic<int> saves{0};
    {
        DebouncedSaver saver([&] { saves.fetch_add(1); }, 40ms);
        for (int i = 0; i < 100; ++i)
        {
            saver.Touch();
        }
        ASSERT_TRUE(WaitFor([&] { return saver.SaveCount() == 1; }, 3s));
        EXPECT_EQ(saves.load(), 1);

        for (int i = 0; i < 50; ++i)
        {
            saver.Touch();
        }
        ASSERT_TRUE(WaitFor([&] { return saver.SaveCount() == 2; }, 3s));
        EXPECT_EQ(saves.load(), 2);
    }
}

TEST(ConfigDebounce, DestructorFlushesPendingWrite)
{
    std::atomic<int> saves{0};
    {
        DebouncedSaver saver([&] { saves.fetch_add(1); }, 5s);
        saver.Touch();
        ASSERT_TRUE(saver.Pending());
    }
    EXPECT_EQ(saves.load(), 1);
}

TEST(ConfigDebounce, CancelDiscardsPendingWrite)
{
    std::atomic<int> saves{0};
    {
        DebouncedSaver saver([&] { saves.fetch_add(1); }, 40ms);
        saver.Touch();
        saver.Cancel();
        EXPECT_FALSE(saver.Pending());
        std::this_thread::sleep_for(100ms);
    }
    EXPECT_EQ(saves.load(), 0);
}
} // namespace