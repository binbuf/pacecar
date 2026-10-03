// pacecar.cpp : Overlay host process.
//
// The app shell: command-line parse, config load, per-user single-instance guard, overlay window,
// tray icon, global hotkeys, sampler, helper stub, and the full shutdown path (debounced config
// save on exit, WM_ENDSESSION, and console Ctrl+C).
//
// Command line:
//   --recipe=a|b        layered (default) or DirectComposition prototype
//   --interactive       start in interactive mode (input is NOT passed through)
//   --click-through     force click-through mode regardless of config
//   --hit-test          Recipe B only: use WM_NCHITTEST/HTTRANSPARENT instead of WS_EX_TRANSPARENT
//   --no-capture        do not request capture exclusion
//   --measure[=SECONDS] run visible for SECONDS (default 5), print overhead, then exit
//   --diagnostics       print the renderer/HDR/capture findings and exit immediately
//   --position=X,Y,W,H  force the window rectangle (physical pixels) instead of the saved/config
//                       placement; used by the cross-process click-through probe
//   --config=PATH       load/save config at PATH instead of %APPDATA%\Pacecar\config.json
//   --instance=SUFFIX   override the single-instance suffix (tests use a unique value)
//   --start-hidden      start hidden (tray only)
//   --no-tray           do not create the tray icon (tests / measure)
//   --no-hotkey         do not register global hotkeys (tests / measure)
//   --open-window=NAME  open a conventional window at startup (settings|specs|history)
//   --exit-after=MS     cleanly exit MS milliseconds after startup (integration tests)
//   --lifecycle-log=PATH  append lifecycle events (started/activation/shutdown) to PATH

#include "framework.h"
#include "pacecar.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <shellapi.h>
#include <objbase.h>

#include "Diagnostics.h"
#include "HelperClient.h"
#include "Hdr.h"
#include "HistoryWindow.h"
#include "Hotkey.h"
#include "OverlayWindow.h"
#include "Sampler.h"
#include "SettingsWindow.h"
#include "SingleInstance.h"
#include "SpecsWindow.h"
#include "Tray.h"
#include "pacecar/app/AppIdentity.h"
#include "pacecar/app/HotkeySpec.h"
#include "pacecar/app/StartWithWindows.h"
#include "pacecar/app/TrayTooltip.h"
#include "pacecar/config/Config.h"
#include "pacecar/core.h"
#include "pacecar/metrics/DisplayFrame.h"
#include "pacecar/metrics/RenderGate.h"
#include "pacecar/metrics/SnapshotCache.h"
#include "pacecar/util/Logger.h"
#include "pacecar/util/TimerResolution.h"

namespace
{
// Posted by the console control handler to ask the UI thread to persist config and quit.
constexpr UINT kSaveAndQuitMessage = WM_APP + 4;
constexpr UINT_PTR kMeasureTimerId = 1;
constexpr UINT_PTR kExitTimerId = 2;

HWND g_signalWindow = nullptr;

BOOL WINAPI SaveAndQuitHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_signalWindow != nullptr)
        {
            PostMessageW(g_signalWindow, kSaveAndQuitMessage, 0, 0);
            return TRUE;
        }
        return FALSE;
    default:
        return FALSE;
    }
}

struct CommandLineOptions
{
    pacecar::overlay::OverlayRecipe recipe = pacecar::overlay::OverlayRecipe::Layered;
    // -1 = defer to config, 0 = force interactive, 1 = force click-through.
    int clickThroughOverride = -1;
    bool hitTest = false;
    bool captureExclusion = true;
    bool measure = false;
    int measureSeconds = 5;
    bool diagnosticsOnly = false;
    bool assertTimerResolution = false;
    std::wstring outputFile{};
    std::optional<pacecar::MonitorRect> position{};
    std::filesystem::path configPath{};
    std::wstring instanceSuffix{};
    bool startHidden = false;
    bool noTray = false;
    bool noHotkey = false;
    std::wstring openWindow{};
    std::optional<unsigned> exitAfterMs{};
    std::wstring lifecycleLog{};
};

void EnableConsole()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE)
    {
        AllocConsole();
    }
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
}

std::wstring Utf8ToWide(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
    {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

void AppendLifecycleLog(const std::wstring& path, std::wstring_view event)
{
    if (path.empty())
    {
        return;
    }
    std::ofstream file(path, std::ios::app | std::ios::binary);
    if (!file)
    {
        return;
    }
    file << "event=";
    for (const wchar_t c : event)
    {
        file << static_cast<char>(c);
    }
    file << "\n";
}

std::uint32_t ForegroundProcessId()
{
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr)
    {
        return 0;
    }
    DWORD pid = 0;
    static_cast<void>(GetWindowThreadProcessId(foreground, &pid));
    return pid;
}

void CopyTextToClipboard(HWND owner, const std::wstring& text)
{
    if (OpenClipboard(owner) == FALSE)
    {
        return;
    }
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr)
    {
        void* destination = GlobalLock(memory);
        if (destination != nullptr)
        {
            memcpy(destination, text.c_str(), bytes);
            GlobalUnlock(memory);
            if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
            {
                GlobalFree(memory);
            }
        }
        else
        {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

void SyncStartWithWindows(const pacecar::Config& config)
{
    wchar_t executable[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0)
    {
        return;
    }
    std::wstring existing;
    const bool present = pacecar::app::ReadStartupValue(
        pacecar::app::kStartupRunKey, pacecar::app::kStartupValueName, existing);
    if (config.general.start_with_windows)
    {
        const std::wstring desired =
            pacecar::app::BuildStartupCommand(executable, config.general.start_hidden);
        if (!present || existing != desired)
        {
            std::wstring error;
            static_cast<void>(pacecar::app::WriteStartupValue(
                pacecar::app::kStartupRunKey, pacecar::app::kStartupValueName, desired, &error));
        }
    }
    else if (present)
    {
        std::wstring error;
        static_cast<void>(pacecar::app::DeleteStartupValue(
            pacecar::app::kStartupRunKey, pacecar::app::kStartupValueName, &error));
    }
}

CommandLineOptions ParseCommandLine()
{
    CommandLineOptions options{};
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
    {
        return options;
    }
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring_view arg = argv[i];
        if (arg == L"--recipe=b" || arg == L"--composition")
        {
            options.recipe = pacecar::overlay::OverlayRecipe::Composition;
        }
        else if (arg == L"--recipe=a" || arg == L"--layered")
        {
            options.recipe = pacecar::overlay::OverlayRecipe::Layered;
        }
        else if (arg == L"--interactive")
        {
            options.clickThroughOverride = 0;
        }
        else if (arg == L"--click-through")
        {
            options.clickThroughOverride = 1;
        }
        else if (arg == L"--hit-test")
        {
            options.hitTest = true;
        }
        else if (arg == L"--no-capture")
        {
            options.captureExclusion = false;
        }
        else if (arg == L"--diagnostics")
        {
            options.diagnosticsOnly = true;
            options.measure = true;
            options.measureSeconds = 0;
        }
        else if (arg == L"--assert-timer-resolution")
        {
            options.assertTimerResolution = true;
        }
        else if (arg == L"--start-hidden")
        {
            options.startHidden = true;
        }
        else if (arg == L"--no-tray")
        {
            options.noTray = true;
        }
        else if (arg == L"--no-hotkey")
        {
            options.noHotkey = true;
        }
        else if (arg.starts_with(L"--open-window="))
        {
            options.openWindow = arg.substr(14);
        }
        else if (arg == L"--measure")
        {
            options.measure = true;
        }
        else if (arg.starts_with(L"--measure="))
        {
            options.measure = true;
            try
            {
                options.measureSeconds = std::stoi(std::wstring(arg.substr(10)));
            }
            catch (...)
            {
                options.measureSeconds = 5;
            }
        }
        else if (arg.starts_with(L"--exit-after="))
        {
            try
            {
                options.exitAfterMs =
                    static_cast<unsigned>(std::stoul(std::wstring(arg.substr(13))));
            }
            catch (...)
            {
                options.exitAfterMs = 0;
            }
        }
        else if (arg.starts_with(L"--out="))
        {
            options.outputFile = arg.substr(6);
        }
        else if (arg.starts_with(L"--config="))
        {
            options.configPath = std::filesystem::path(std::wstring(arg.substr(9)));
        }
        else if (arg.starts_with(L"--instance="))
        {
            options.instanceSuffix = arg.substr(11);
        }
        else if (arg.starts_with(L"--lifecycle-log="))
        {
            options.lifecycleLog = arg.substr(16);
        }
        else if (arg.starts_with(L"--position="))
        {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            if (swscanf_s(std::wstring(arg.substr(11)).c_str(), L"%d,%d,%d,%d", &x, &y, &width,
                          &height) == 4 &&
                width > 0 && height > 0)
            {
                pacecar::MonitorRect rect{};
                rect.x = x;
                rect.y = y;
                rect.width = width;
                rect.height = height;
                rect.valid = true;
                options.position = rect;
            }
        }
    }
    LocalFree(argv);
    return options;
}
} // namespace

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool comInitialized = SUCCEEDED(comResult);

    CommandLineOptions commandLine = ParseCommandLine();
    if (commandLine.measure)
    {
        EnableConsole();
    }

    [[maybe_unused]] const std::string_view coreVersion = pacecar::CoreVersion();

    pacecar::Config config = commandLine.configPath.empty()
                                 ? pacecar::Config::Load()
                                 : pacecar::Config::Load(commandLine.configPath);

    const bool shellMode = !commandLine.measure && !commandLine.diagnosticsOnly;
    // Tests inject a unique instance suffix; skip the HKCU Run sync so they never touch the real
    // per-user startup entry.
    const bool testMode = !commandLine.instanceSuffix.empty();
    const auto logEvent = [&commandLine](std::wstring_view event)
    { AppendLifecycleLog(commandLine.lifecycleLog, event); };

    std::wstring instanceSuffix = commandLine.instanceSuffix;
    if (instanceSuffix.empty())
    {
        instanceSuffix = pacecar::app::CurrentUserSid();
    }
    if (instanceSuffix.empty())
    {
        instanceSuffix = L"default";
    }

    pacecar::overlay::SingleInstance singleInstance;
    if (shellMode)
    {
        bool alreadyRunning = false;
        if (singleInstance.Acquire(instanceSuffix, alreadyRunning) && alreadyRunning)
        {
            static_cast<void>(pacecar::overlay::SingleInstance::NotifyExisting(instanceSuffix));
            logEvent(L"secondary-exit");
            if (comInitialized)
            {
                CoUninitialize();
            }
            return 0;
        }
    }

    pacecar::overlay::HelperClient helper;
    static_cast<void>(helper.TryConnect());

    std::optional<pacecar::MonitorRect> savedRect;
    for (const pacecar::MonitorRect& rect : config.overlay.monitor_rects)
    {
        if (rect.monitor_id == config.overlay.monitor_id)
        {
            savedRect = rect;
            break;
        }
    }
    if (commandLine.position.has_value())
    {
        savedRect = commandLine.position;
    }

    pacecar::overlay::OverlayOptions options;
    options.recipe = commandLine.recipe;
    options.clickThrough = commandLine.clickThroughOverride >= 0
                               ? commandLine.clickThroughOverride == 1
                               : config.overlay.mode == pacecar::OverlayMode::ClickThrough;
    options.captureExclusion = commandLine.captureExclusion && config.overlay.capture_exclusion;
    options.alwaysOnTop = config.overlay.always_on_top;
    options.compositionClickThrough = commandLine.hitTest
                                          ? pacecar::overlay::CompositionClickThrough::
                                                HitTestTransparent
                                          : pacecar::overlay::CompositionClickThrough::
                                                TransparentExStyle;
    options.panelOpacity = config.general.opacity;
    options.theme = config.general.theme;

    const auto startTime = std::chrono::steady_clock::now();
    const pacecar::TimerResolution timerStart = pacecar::QueryTimerResolution();

    pacecar::overlay::OverlayWindow overlay;
    if (!overlay.Create(hInstance, options, savedRect))
    {
        if (commandLine.measure)
        {
            wprintf(L"pacecar: failed to create the overlay window\n");
        }
        if (comInitialized)
        {
            CoUninitialize();
        }
        return 1;
    }

    // Apply the config-driven theme and layout to the live renderer (preset + per-tile/per-field
    // toggles) without recreating the window.
    overlay.ApplyConfig(config);

    const auto saveConfig = [&config, &commandLine]
    {
        if (commandLine.configPath.empty())
        {
            static_cast<void>(config.Save());
        }
        else
        {
            static_cast<void>(config.Save(commandLine.configPath));
        }
    };
    pacecar::DebouncedSaver saver(saveConfig);

    overlay.SetPositionChangedCallback(
        [&config, &saver](const pacecar::MonitorRect& rect)
        {
            bool replaced = false;
            for (pacecar::MonitorRect& existing : config.overlay.monitor_rects)
            {
                if (existing.monitor_id == rect.monitor_id)
                {
                    existing = rect;
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
            {
                config.overlay.monitor_rects.push_back(rect);
            }
            config.overlay.monitor_id = rect.monitor_id;
            saver.Touch();
        });
    overlay.SetSessionEndingCallback([&saveConfig] { saveConfig(); });

    pacecar::overlay::Sampler sampler;

    // The repaint gate is created here (not with the frame pipeline below) so the Settings window's
    // live-apply path can retune its interval without recreating it.
    pacecar::metrics::RenderGate renderGate(
        std::chrono::milliseconds(static_cast<int>(config.general.refresh)));

    // Global shell: single-instance activation, tray, hotkeys. Measure runs keep the minimal
    // behavior the probes rely on.
    pacecar::overlay::Tray tray;
    bool trayCreated = false;
    pacecar::overlay::HotkeyManager hotkeys;

    // Conventional windows are created lazily on first open and destroyed at process exit. Declared
    // outside the shell block so the message loop can route keyboard navigation to Settings.
    pacecar::overlay::SettingsWindow settingsWindow;
    pacecar::overlay::SpecsWindow specsWindow;
    pacecar::overlay::HistoryWindow historyWindow;

    const auto refreshShellFlags = [&overlay, &tray, trayCreated]
    {
        if (trayCreated)
        {
            tray.SetVisibleFlag(overlay.IsVisible());
            tray.SetClickThroughFlag(overlay.ClickThrough());
        }
    };

    if (shellMode)
    {
        static_cast<void>(singleInstance.CreateActivationWindow(
            hInstance,
            [&overlay, &logEvent, &refreshShellFlags]
            {
                overlay.SetVisible(true);
                refreshShellFlags();
                logEvent(L"activation");
            }));

        if (!testMode)
        {
            SyncStartWithWindows(config);
        }

        const auto aboutText = [&overlay, &sampler, &helper]
        {
            std::wstring text = L"Version: " + Utf8ToWide(pacecar::CoreVersion()) + L"\n";
            text += L"Renderer: " + overlay.Diagnostics() + L"\n";
            text += L"Sampler: " + sampler.Diagnostics() + L"\n";
            text += L"Helper: " + helper.Status() + L"\n";
            text += L"PawnIO: not installed (deep sensors unavailable)\n";
            return text;
        };

        // Live-apply every Settings edit: overlay appearance/behavior, sampler cadence, hotkey
        // re-registration, tray flags, and the debounced config write. No per-keystroke/per-drag
        // write happens here - `saver.Touch()` only arms the 500 ms debounce.
        const auto applyConfigChanges = [&]
        {
            overlay.ApplyConfig(config);
            const auto refreshMs =
                std::chrono::milliseconds(static_cast<int>(config.general.refresh));
            sampler.SetInterval(refreshMs);
            renderGate.SetInterval(refreshMs);
            if (!commandLine.noHotkey)
            {
                const std::wstring overlayHotkey = Utf8ToWide(config.hotkeys.toggle_overlay);
                const pacecar::app::HotkeyParseResult overlayParse =
                    pacecar::app::ParseHotkey(overlayHotkey);
                if (overlayParse.ok())
                {
                    static_cast<void>(hotkeys.Register(
                        pacecar::overlay::HotkeyManager::kToggleOverlayId, overlayParse.binding));
                }
                hotkeys.Unregister(pacecar::overlay::HotkeyManager::kToggleClickThroughId);
                const std::wstring clickHotkey = Utf8ToWide(config.hotkeys.toggle_click_through);
                if (!clickHotkey.empty())
                {
                    const pacecar::app::HotkeyParseResult clickParse =
                        pacecar::app::ParseHotkey(clickHotkey);
                    if (clickParse.ok())
                    {
                        static_cast<void>(hotkeys.Register(
                            pacecar::overlay::HotkeyManager::kToggleClickThroughId,
                            clickParse.binding));
                    }
                }
            }
            refreshShellFlags();
            saver.Touch();
        };

        pacecar::overlay::SettingsWindowHooks settingsHooks;
        settingsHooks.instance = hInstance;
        settingsHooks.config = &config;
        settingsHooks.onChanged = applyConfigChanges;
        settingsHooks.onStartupSettingChanged = [&config, testMode]
        {
            if (!testMode)
            {
                SyncStartWithWindows(config);
            }
        };
        settingsHooks.aboutText = aboutText;

        const pacecar::overlay::HistoryWindow::SamplerAccess historyAccess =
            [&sampler](pacecar::metrics::MetricHistory& out) { return sampler.CopyHistory(out); };
        const pacecar::overlay::HistoryWindow::SnapshotAccess snapshotAccess =
            [&sampler]() { return sampler.LatestSnapshot(); };

        const auto handleCommand = [&](pacecar::overlay::OverlayCommand command)
        {
            switch (command)
            {
            case pacecar::overlay::OverlayCommand::ToggleVisibility:
                overlay.ToggleVisibility();
                refreshShellFlags();
                break;
            case pacecar::overlay::OverlayCommand::Mode:
                overlay.ToggleClickThrough();
                refreshShellFlags();
                break;
            case pacecar::overlay::OverlayCommand::Hide:
                overlay.SetVisible(false);
                refreshShellFlags();
                break;
            case pacecar::overlay::OverlayCommand::Exit:
                overlay.Quit();
                break;
            case pacecar::overlay::OverlayCommand::CopySystemInfo:
            {
                std::wstring info = L"Pacecar " + Utf8ToWide(pacecar::CoreVersion()) + L"\n";
                info += overlay.Diagnostics() + L"\n";
                info += sampler.Diagnostics() + L"\n";
                info += L"helper: " + helper.Status() + L"\n";
                CopyTextToClipboard(overlay.Hwnd(), info);
                break;
            }
            case pacecar::overlay::OverlayCommand::About:
                MessageBoxW(overlay.Hwnd(), L"Pacecar 2.0\nSystem metrics overlay",
                            L"About Pacecar", MB_OK | MB_ICONINFORMATION);
                break;
            case pacecar::overlay::OverlayCommand::Settings:
                if (!settingsWindow.Open(settingsHooks))
                {
                    pacecar::LogWarn(L"overlay: failed to open the Settings window");
                }
                break;
            case pacecar::overlay::OverlayCommand::Specs:
                if (!specsWindow.Open(hInstance))
                {
                    pacecar::LogWarn(L"overlay: failed to open the Specs window");
                }
                break;
            case pacecar::overlay::OverlayCommand::History:
                if (!historyWindow.Open(hInstance, &config, historyAccess, snapshotAccess))
                {
                    pacecar::LogWarn(L"overlay: failed to open the History window");
                }
                break;
            default:
                break;
            }
        };

        if (!commandLine.noTray)
        {
            trayCreated = tray.Create(hInstance, IDI_PACECAR, L"Pacecar");
            if (trayCreated)
            {
                tray.SetCommandCallback(handleCommand);
                tray.SetVisibleFlag(true);
                tray.SetClickThroughFlag(overlay.ClickThrough());
            }
        }

        if (!commandLine.noHotkey)
        {
            hotkeys.Attach(overlay.Hwnd());
            hotkeys.SetCallback(
                [&](int hotkeyId)
                {
                    if (hotkeyId == pacecar::overlay::HotkeyManager::kToggleOverlayId)
                    {
                        overlay.ToggleVisibility();
                    }
                    else if (hotkeyId == pacecar::overlay::HotkeyManager::kToggleClickThroughId)
                    {
                        overlay.ToggleClickThrough();
                    }
                    refreshShellFlags();
                });

            const std::wstring overlayHotkey = Utf8ToWide(config.hotkeys.toggle_overlay);
            const pacecar::app::HotkeyParseResult overlayParse =
                pacecar::app::ParseHotkey(overlayHotkey);
            if (overlayParse.ok())
            {
                static_cast<void>(hotkeys.Register(
                    pacecar::overlay::HotkeyManager::kToggleOverlayId, overlayParse.binding));
            }
            else
            {
                pacecar::LogWarn(L"hotkey: invalid overlay hotkey in config: " + overlayHotkey);
            }

            const std::wstring clickThroughHotkey =
                Utf8ToWide(config.hotkeys.toggle_click_through);
            if (!clickThroughHotkey.empty())
            {
                const pacecar::app::HotkeyParseResult clickParse =
                    pacecar::app::ParseHotkey(clickThroughHotkey);
                if (clickParse.ok())
                {
                    static_cast<void>(hotkeys.Register(
                        pacecar::overlay::HotkeyManager::kToggleClickThroughId,
                        clickParse.binding));
                }
                else
                {
                    pacecar::LogWarn(L"hotkey: invalid click-through hotkey in config: " +
                                     clickThroughHotkey);
                }
            }
        }

        g_signalWindow = overlay.Hwnd();
        SetConsoleCtrlHandler(SaveAndQuitHandler, TRUE);

        // Test/automation hook: open a conventional window at startup (no user gesture available).
        if (commandLine.openWindow == L"settings")
        {
            handleCommand(pacecar::overlay::OverlayCommand::Settings);
        }
        else if (commandLine.openWindow == L"specs")
        {
            handleCommand(pacecar::overlay::OverlayCommand::Specs);
        }
        else if (commandLine.openWindow == L"history")
        {
            handleCommand(pacecar::overlay::OverlayCommand::History);
        }
    }

    overlay.Show((config.general.start_hidden || commandLine.startHidden) ? SW_HIDE : nCmdShow);
    overlay.Invalidate();
    const double firstPaintMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime)
            .count();

    const pacecar::overlay::ProcessUsage baseline = pacecar::overlay::QueryProcessUsage();

    const auto emitReport = [&](const pacecar::overlay::ProcessUsage& finalUsage)
    {
        std::wstring report;
        report += L"diagnostics: " + overlay.Diagnostics() + L"\n";
        report += L"sampler:     " + sampler.Diagnostics() + L"\n";
        report += L"helper:      " + helper.Status() + L"\n";
        report += L"overhead:    " + pacecar::overlay::FormatProcessUsage(finalUsage) + L"\n";

        wchar_t firstPaintLine[96] = {};
        swprintf_s(firstPaintLine, L"firstPaint:  %.1f ms\n", firstPaintMs);
        report += firstPaintLine;

        const pacecar::TimerResolution timerEnd = pacecar::QueryTimerResolution();
        wchar_t timerLine[160] = {};
        swprintf_s(timerLine, L"timerResolution: current=%.3f ms max=%.3f ms default=%s\n",
                   timerEnd.CurrentMs(), timerEnd.MaximumMs(),
                   pacecar::IsDefaultTimerResolution(timerEnd) ? L"yes" : L"no");
        report += timerLine;

        if (commandLine.measureSeconds > 0)
        {
            const double windowSeconds = static_cast<double>(commandLine.measureSeconds);
            const double cpuSeconds =
                static_cast<double>(finalUsage.cpuTime100ns - baseline.cpuTime100ns) / 1.0e7;
            wchar_t cpuLine[96] = {};
            swprintf_s(cpuLine, L"idle cpu:    %.2f%% over %d s\n",
                       100.0 * cpuSeconds / windowSeconds, commandLine.measureSeconds);
            report += cpuLine;
        }
        fputws(report.c_str(), stdout);
        if (!commandLine.outputFile.empty())
        {
            FILE* file = nullptr;
            if (_wfopen_s(&file, commandLine.outputFile.c_str(), L"w") == 0 && file != nullptr)
            {
                fputws(report.c_str(), file);
                fclose(file);
            }
        }
    };

    if (commandLine.diagnosticsOnly)
    {
        emitReport(pacecar::overlay::QueryProcessUsage());
        if (comInitialized)
        {
            CoUninitialize();
        }
        return 0;
    }

    // Sampling runs entirely off the UI thread; it wakes this window only while it is effectively
    // visible (shown and not occluded/locked).
    static_cast<void>(sampler.Start(config, overlay.Hwnd()));
    overlay.SetVisibilityChangedCallback(
        [&sampler](bool visible) { sampler.SetVisible(visible); });
    sampler.SetVisible(overlay.EffectivelyVisible());

    const auto elapsedMs = [&startTime]() -> std::uint64_t
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                  startTime)
                .count());
    };

    const auto applyFrame = [&](std::shared_ptr<const pacecar::metrics::DisplayFrame> frame)
    {
        if (!frame)
        {
            return;
        }
        if (renderGate.ShouldRepaintFingerprint(frame->fingerprint, elapsedMs()))
        {
            if (frame->snapshot)
            {
                if (trayCreated)
                {
                    tray.SetTooltip(pacecar::app::FormatTrayTooltip(*frame->snapshot));
                }
                sampler.SetForegroundPid(ForegroundProcessId());
            }
            overlay.SetFrame(std::move(frame));
            overlay.Invalidate();
        }
    };

    // Load the cached last snapshot on a background thread so a cold start paints placeholders
    // immediately and never waits on disk (design ref 05 "Startup").
    std::mutex cacheMutex;
    std::shared_ptr<pacecar::metrics::MetricsSnapshot> cachedSnapshot;
    std::thread cacheThread(
        [&overlay, &cacheMutex, &cachedSnapshot]
        {
            auto loaded = std::make_shared<pacecar::metrics::MetricsSnapshot>();
            if (pacecar::metrics::LoadSnapshotCache(pacecar::metrics::SnapshotCachePath(), *loaded))
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                cachedSnapshot = std::move(loaded);
            }
            PostMessageW(overlay.Hwnd(), pacecar::overlay::WM_APP_CACHE_READY, 0, 0);
        });

    if (commandLine.measure)
    {
        SetTimer(overlay.Hwnd(), kMeasureTimerId,
                 static_cast<UINT>(commandLine.measureSeconds > 0 ? commandLine.measureSeconds : 1) *
                     1000u,
                 nullptr);
    }
    else if (commandLine.exitAfterMs.has_value())
    {
        SetTimer(overlay.Hwnd(), kExitTimerId, *commandLine.exitAfterMs, nullptr);
    }

    logEvent(L"started");

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.hwnd == overlay.Hwnd())
        {
            if (message.message == WM_TIMER && message.wParam == kMeasureTimerId)
            {
                KillTimer(overlay.Hwnd(), kMeasureTimerId);
                break;
            }
            if (message.message == WM_TIMER && message.wParam == kExitTimerId)
            {
                KillTimer(overlay.Hwnd(), kExitTimerId);
                saveConfig();
                overlay.Quit();
                continue;
            }
            if (message.message == pacecar::overlay::WM_APP_METRICS_UPDATED)
            {
                applyFrame(sampler.LatestFrame());
                continue;
            }
            if (message.message == pacecar::overlay::WM_APP_CACHE_READY)
            {
                std::shared_ptr<pacecar::metrics::MetricsSnapshot> snap;
                {
                    std::lock_guard<std::mutex> lock(cacheMutex);
                    snap = cachedSnapshot;
                }
                if (snap)
                {
                    static pacecar::metrics::MetricHistory emptyHistory(0);
                    auto frame = std::make_shared<pacecar::metrics::DisplayFrame>(
                        pacecar::metrics::BuildDisplayFrame(std::move(snap), emptyHistory));
                    applyFrame(std::move(frame));
                }
                continue;
            }
            if (message.message == WM_HOTKEY)
            {
                static_cast<void>(hotkeys.HandleHotkey(static_cast<int>(message.wParam)));
                continue;
            }
            if (message.message == kSaveAndQuitMessage)
            {
                saveConfig();
                overlay.Quit();
                continue;
            }
        }
        // Route keyboard navigation (Tab/arrows/Enter/Escape) to the Settings dialog while it is
        // active; this gives the conventional window dialog-style focus behavior and visible focus.
        if (shellMode && settingsWindow.Created() &&
            IsWindowVisible(settingsWindow.Hwnd()) != FALSE &&
            (message.hwnd == settingsWindow.Hwnd() ||
             IsChild(settingsWindow.Hwnd(), message.hwnd) != FALSE))
        {
            if (IsDialogMessageW(settingsWindow.Hwnd(), &message) != FALSE)
            {
                continue;
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (shellMode)
    {
        saveConfig();
    }
    sampler.Stop();
    if (const auto latest = sampler.LatestSnapshot())
    {
        static_cast<void>(
            pacecar::metrics::SaveSnapshotCache(*latest, pacecar::metrics::SnapshotCachePath()));
    }
    if (cacheThread.joinable())
    {
        cacheThread.join();
    }

    if (shellMode)
    {
        g_signalWindow = nullptr;
        hotkeys.UnregisterAll();
        tray.Destroy();
        helper.Disconnect();
        logEvent(L"shutdown");
    }

    const pacecar::TimerResolution timerEnd = pacecar::QueryTimerResolution();
    const bool timerUnchanged = pacecar::SameTimerResolution(timerStart, timerEnd);

    int exitCode = 0;
    if (commandLine.assertTimerResolution && !timerUnchanged)
    {
        std::wstring warning =
            L"pacecar: timer resolution was raised during the run (no timeBeginPeriod expected)\n";
        fputws(warning.c_str(), stderr);
        exitCode = 2;
    }

    if (commandLine.measure)
    {
        emitReport(pacecar::overlay::QueryProcessUsage());
    }

    if (comInitialized)
    {
        CoUninitialize();
    }
    return exitCode;
}