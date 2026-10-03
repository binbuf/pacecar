// pacecar.cpp : Overlay host process.
//
// Phase 1 keeps the overlay minimal: it creates the window/renderer, wires placement persistence,
// and (when asked) reports Phase 0 measurements. Real widgets (T10), layout (T11), sampling (T12),
// and tray/hotkey (T13) build on the `IRenderer`/`OverlayWindow` boundary established here.
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

#include "framework.h"
#include "pacecar.h"

#include <chrono>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <shellapi.h>
#include <objbase.h>

#include "Diagnostics.h"
#include "Hdr.h"
#include "OverlayWindow.h"
#include "Sampler.h"
#include "pacecar/config/Config.h"
#include "pacecar/core.h"
#include "pacecar/metrics/DisplayFrame.h"
#include "pacecar/metrics/RenderGate.h"
#include "pacecar/metrics/SnapshotCache.h"
#include "pacecar/util/Logger.h"
#include "pacecar/util/TimerResolution.h"

namespace
{
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
        else if (arg.starts_with(L"--out="))
        {
            options.outputFile = arg.substr(6);
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

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                         COINIT_DISABLE_OLE1DDE);
    const bool comInitialized = SUCCEEDED(comResult);

    CommandLineOptions commandLine = ParseCommandLine();
    if (commandLine.measure)
    {
        EnableConsole();
    }

    [[maybe_unused]] const std::string_view coreVersion = pacecar::CoreVersion();

    pacecar::Config config = pacecar::Config::Load();

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

    overlay.SetPositionChangedCallback(
        [&config](const pacecar::MonitorRect& rect)
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
            static_cast<void>(config.Save());
        });

    overlay.Show(nCmdShow);
    overlay.Invalidate();
    const double firstPaintMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime)
            .count();

    pacecar::overlay::ProcessUsage baseline = pacecar::overlay::QueryProcessUsage();

    pacecar::overlay::Sampler sampler;

    const auto emitReport = [&](const pacecar::overlay::ProcessUsage& finalUsage)
    {
        std::wstring report;
        report += L"diagnostics: " + overlay.Diagnostics() + L"\n";
        report += L"sampler:     " + sampler.Diagnostics() + L"\n";
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

    pacecar::metrics::RenderGate renderGate(
        std::chrono::milliseconds(static_cast<int>(config.general.refresh)));
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
        SetTimer(overlay.Hwnd(), 1, static_cast<UINT>(commandLine.measureSeconds > 0
                                                          ? commandLine.measureSeconds
                                                          : 1) *
                                         1000u,
                 nullptr);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.hwnd == overlay.Hwnd() && message.message == WM_TIMER && message.wParam == 1)
        {
            KillTimer(overlay.Hwnd(), 1);
            break;
        }
        if (message.hwnd == overlay.Hwnd() && message.message == pacecar::overlay::WM_APP_METRICS_UPDATED)
        {
            applyFrame(sampler.LatestFrame());
            continue;
        }
        if (message.hwnd == overlay.Hwnd() && message.message == pacecar::overlay::WM_APP_CACHE_READY)
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
        TranslateMessage(&message);
        DispatchMessageW(&message);
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