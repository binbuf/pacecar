// T13 app-shell unit tests (headless): hotkey parsing/formatting, per-user identity names,
// start-with-Windows command building and registry round-trip, tray tooltip formatting, and the tray
// command set. The Win32 shell pieces (tray icon, hotkey registration, single-instance activation)
// are covered by the integration test in lifecycle_test.cpp.

#include <gtest/gtest.h>

#include <windows.h>

#include <array>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "pacecar/app/AppIdentity.h"
#include "pacecar/app/HotkeySpec.h"
#include "pacecar/app/StartWithWindows.h"
#include "pacecar/app/TrayTooltip.h"
#include "pacecar/metrics/MetricsSnapshot.h"
#include "pacecar/overlay/OverlayCommands.h"

namespace
{
using pacecar::app::BuildStartupCommand;
using pacecar::app::FormatHotkey;
using pacecar::app::HotkeyBinding;
using pacecar::app::ParseHotkey;

std::filesystem::path RepoRootFromModule()
{
    wchar_t module[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length == 0)
    {
        return {};
    }
    // out\x64\<Config>\Pacecar.App.Tests.exe -> repo root is three levels up.
    std::filesystem::path path(module);
    return path.parent_path().parent_path().parent_path().parent_path();
}

TEST(HotkeySpec, ParsesDefaultOverlayHotkey)
{
    const auto result = ParseHotkey(L"Ctrl+Shift+P");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.binding.modifiers,
              pacecar::app::kHotkeyControl | pacecar::app::kHotkeyShift);
    EXPECT_EQ(result.binding.virtualKey, static_cast<std::uint32_t>(L'P'));
}

TEST(HotkeySpec, ParsesModifierOrderAndFunctionKeys)
{
    const auto result = ParseHotkey(L"alt + ctrl+F5");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.binding.modifiers,
              pacecar::app::kHotkeyControl | pacecar::app::kHotkeyAlt);
    EXPECT_EQ(result.binding.virtualKey, pacecar::app::kHotkeyVkF1 + 4);
}

TEST(HotkeySpec, ParsesNamedKeysAndWinModifier)
{
    const auto result = ParseHotkey(L"Win+Space");
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.binding.modifiers, pacecar::app::kHotkeyWin);
    EXPECT_EQ(result.binding.virtualKey, pacecar::app::kHotkeyVkSpace);
}

TEST(HotkeySpec, RejectsMalformedInput)
{
    EXPECT_FALSE(ParseHotkey(L"").ok());
    EXPECT_FALSE(ParseHotkey(L"Ctrl+").ok());
    EXPECT_FALSE(ParseHotkey(L"Ctrl+Shift").ok());
    EXPECT_FALSE(ParseHotkey(L"Ctrl+Banana").ok());
    EXPECT_FALSE(ParseHotkey(L"Ctrl+F99").ok());
}

TEST(HotkeySpec, FormatsCanonicallyAndRoundTrips)
{
    const auto parsed = ParseHotkey(L"Shift+Ctrl+P");
    ASSERT_TRUE(parsed.ok()) << parsed.error;
    EXPECT_EQ(FormatHotkey(parsed.binding), L"Ctrl+Shift+P");

    const auto roundTrip = ParseHotkey(FormatHotkey(parsed.binding));
    ASSERT_TRUE(roundTrip.ok());
    EXPECT_EQ(roundTrip.binding, parsed.binding);
    EXPECT_TRUE(FormatHotkey(HotkeyBinding{}).empty());
}

TEST(AppIdentity, BuildsPerUserNames)
{
    EXPECT_EQ(pacecar::app::MakeSingletonName(L"S-1-5-21-1"),
              L"Local\\Pacecar.Singleton.S-1-5-21-1");
    EXPECT_EQ(pacecar::app::MakeActivationMessageName(L"S-1-5-21-1"),
              L"Pacecar.Activate.S-1-5-21-1");
    EXPECT_EQ(pacecar::app::MakeActivationWindowClass(L"S-1-5-21-1"),
              L"Pacecar.SingletonWindow.S-1-5-21-1");
}

TEST(AppIdentity, CurrentUserSidLooksLikeASid)
{
    const std::wstring sid = pacecar::app::CurrentUserSid();
    ASSERT_FALSE(sid.empty());
    EXPECT_EQ(sid.rfind(L"S-", 0), 0u);
}

TEST(StartWithWindows, BuildsQuotedCommand)
{
    const std::wstring command =
        BuildStartupCommand(L"C:\\Program Files\\Pacecar\\Pacecar.Overlay.exe", false);
    EXPECT_EQ(command, L"\"C:\\Program Files\\Pacecar\\Pacecar.Overlay.exe\"");
    EXPECT_EQ(BuildStartupCommand(L"C:\\P.exe", true), L"\"C:\\P.exe\" --start-hidden");
}

TEST(StartWithWindows, RegistryRoundTripOnDisposableKey)
{
    const std::wstring key = L"Software\\Pacecar\\Tests\\StartWithWindows";
    const std::wstring value = L"PacecarT13Test";
    std::wstring error;

    ASSERT_TRUE(pacecar::app::WriteStartupValue(key, value, L"\"C:\\P.exe\" --start-hidden",
                                                 &error))
        << error;

    std::wstring read;
    ASSERT_TRUE(pacecar::app::ReadStartupValue(key, value, read)) << error;
    EXPECT_EQ(read, L"\"C:\\P.exe\" --start-hidden");

    ASSERT_TRUE(pacecar::app::DeleteStartupValue(key, value, &error)) << error;
    EXPECT_FALSE(pacecar::app::ReadStartupValue(key, value, read));
}

TEST(TrayTooltip, SummarisesAvailableMetrics)
{
    pacecar::metrics::MetricsSnapshot snapshot;
    snapshot.cpu.status.available = true;
    snapshot.cpu.totalUtilizationPercent = 42.3;
    snapshot.gpu.status.available = true;
    snapshot.gpu.temperatureStatus.available = true;
    snapshot.gpu.temperatureC = 71.0;
    snapshot.memory.status.available = true;
    snapshot.memory.usedPercent = 63.1;

    EXPECT_EQ(pacecar::app::FormatTrayTooltip(snapshot), L"CPU 42% | GPU 71C | RAM 63%");
}

TEST(TrayTooltip, FallsBackWhenNothingAvailable)
{
    pacecar::metrics::MetricsSnapshot snapshot;
    EXPECT_EQ(pacecar::app::FormatTrayTooltip(snapshot), L"Pacecar");
}

TEST(TrayTooltip, NeverExceedsNotifyIconCapacity)
{
    pacecar::metrics::MetricsSnapshot snapshot;
    snapshot.cpu.status.available = true;
    snapshot.gpu.status.available = true;
    snapshot.gpu.temperatureStatus.available = true;
    snapshot.memory.status.available = true;
    const std::wstring tooltip = pacecar::app::FormatTrayTooltip(snapshot);
    EXPECT_LE(tooltip.size(), pacecar::app::kTrayTooltipCapacity);
}

TEST(OverlayCommands, TrayMenuExposesFullSet)
{
    using pacecar::overlay::IsOverlayCommand;
    using pacecar::overlay::OverlayCommand;

    const std::array<OverlayCommand, 7> expected{
        OverlayCommand::ToggleVisibility, OverlayCommand::Mode, OverlayCommand::Settings,
        OverlayCommand::History,          OverlayCommand::CopySystemInfo, OverlayCommand::About,
        OverlayCommand::Exit};
    EXPECT_EQ(pacecar::overlay::kTrayMenuCommands, expected);
    for (const OverlayCommand command : pacecar::overlay::kTrayMenuCommands)
    {
        EXPECT_GT(std::wcslen(pacecar::overlay::CommandLabel(command)), 0u);
        EXPECT_TRUE(IsOverlayCommand(static_cast<unsigned>(command)));
    }
    EXPECT_TRUE(IsOverlayCommand(static_cast<unsigned>(OverlayCommand::ToggleVisibility)));
    EXPECT_FALSE(IsOverlayCommand(0));
    EXPECT_FALSE(IsOverlayCommand(99));
}

TEST(AppManifest, RequestsAsInvokerElevation)
{
    const std::filesystem::path manifest =
        RepoRootFromModule() / "src" / "Pacecar.Overlay" / "app.manifest";
    ASSERT_TRUE(std::filesystem::exists(manifest)) << manifest.string();

    std::ifstream file(manifest);
    ASSERT_TRUE(file.good());
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("requestedExecutionLevel"), std::string::npos);
    EXPECT_NE(text.find("level=\"asInvoker\""), std::string::npos);
    EXPECT_EQ(text.find("requireAdministrator"), std::string::npos);
}
} // namespace