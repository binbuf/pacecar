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

#include <cstdio>
#include <cwchar>
#include <optional>
#include <string>

#include <shellapi.h>
#include <objbase.h>

#include "Diagnostics.h"
#include "Hdr.h"
#include "OverlayWindow.h"
#include "pacecar/config/Config.h"
#include "pacecar/core.h"
#include "pacecar/util/Logger.h"

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

    pacecar::overlay::ProcessUsage baseline = pacecar::overlay::QueryProcessUsage();

    const auto emitReport = [&](const pacecar::overlay::ProcessUsage& finalUsage)
    {
        std::wstring report;
        report += L"diagnostics: " + overlay.Diagnostics() + L"\n";
        report += L"overhead:    " + pacecar::overlay::FormatProcessUsage(finalUsage) + L"\n";
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
        if (message.message == WM_TIMER && message.hwnd == overlay.Hwnd())
        {
            KillTimer(overlay.Hwnd(), 1);
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (commandLine.measure)
    {
        emitReport(pacecar::overlay::QueryProcessUsage());
    }

    if (comInitialized)
    {
        CoUninitialize();
    }
    return 0;
}