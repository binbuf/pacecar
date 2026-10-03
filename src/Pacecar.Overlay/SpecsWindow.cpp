#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "targetver.h"

#include "SpecsWindow.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

#include "pacecar/util/Logger.h"

namespace pacecar::overlay
{
namespace
{
constexpr wchar_t kSpecsClassName[] = L"PacecarSpecsWindow";
ATOM g_class = 0;
HFONT g_font = nullptr;

std::wstring RegString(HKEY root, const wchar_t* subkey, const wchar_t* value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS || key == nullptr)
    {
        return {};
    }
    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LONG result = RegQueryValueExW(key, value, nullptr, &type,
                                         reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ)
    {
        return {};
    }
    return std::wstring(buffer, wcsnlen(buffer, std::size(buffer)));
}

DWORD RegDword(HKEY root, const wchar_t* subkey, const wchar_t* value, DWORD fallback)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS || key == nullptr)
    {
        return fallback;
    }
    DWORD data = fallback;
    DWORD size = sizeof(data);
    const LONG result = RegQueryValueExW(key, value, nullptr, nullptr,
                                         reinterpret_cast<LPBYTE>(&data), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS ? data : fallback;
}

std::vector<std::wstring> GpuNames()
{
    std::vector<std::wstring> names;
    constexpr wchar_t kClassKey[] =
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
    HKEY classKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kClassKey, 0, KEY_READ, &classKey) != ERROR_SUCCESS)
    {
        return names;
    }
    wchar_t subName[64] = {};
    for (DWORD index = 0;; ++index)
    {
        DWORD length = static_cast<DWORD>(std::size(subName));
        const LONG result = RegEnumKeyExW(classKey, index, subName, &length, nullptr, nullptr,
                                          nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS)
        {
            break;
        }
        if (result != ERROR_SUCCESS)
        {
            continue;
        }
        std::wstring full = kClassKey;
        full += L"\\";
        full += subName;
        const std::wstring desc = RegString(HKEY_LOCAL_MACHINE, full.c_str(), L"DriverDesc");
        if (!desc.empty())
        {
            names.push_back(desc);
        }
    }
    RegCloseKey(classKey);
    return names;
}

std::wstring CollectSystemSpecs()
{
    std::wstring text;
    wchar_t line[512] = {};

    constexpr wchar_t kWindowsKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::wstring os = RegString(HKEY_LOCAL_MACHINE, kWindowsKey, L"ProductName");
    const std::wstring displayVersion = RegString(HKEY_LOCAL_MACHINE, kWindowsKey, L"DisplayVersion");
    const DWORD build = RegDword(HKEY_LOCAL_MACHINE, kWindowsKey, L"CurrentBuildNumber", 0);
    if (!displayVersion.empty())
    {
        os += L" " + displayVersion;
    }
    if (build != 0)
    {
        os += L" (build " + std::to_wstring(build) + L")";
    }
    if (os.empty())
    {
        os = L"Windows";
    }
    text += L"Operating system: " + os + L"\n\n";

    const std::wstring cpuName =
        RegString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                  L"ProcessorNameString");
    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);
    swprintf_s(line, L"CPU: %s\n", cpuName.empty() ? L"Unknown" : cpuName.c_str());
    text += line;
    swprintf_s(line, L"  Cores: %lu  Logical processors: %lu\n",
               static_cast<unsigned long>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)),
               static_cast<unsigned long>(systemInfo.dwNumberOfProcessors));
    text += line;
    swprintf_s(line, L"  Architecture: %u\n", static_cast<unsigned>(systemInfo.wProcessorArchitecture));
    text += line;
    text += L"\n";

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) != FALSE)
    {
        const double totalGb = static_cast<double>(memory.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
        swprintf_s(line, L"Physical memory: %.1f GiB (%llu MB)\n\n", totalGb,
                   static_cast<unsigned long long>(memory.ullTotalPhys / (1024ull * 1024ull)));
        text += line;
    }

    text += L"Graphics adapters:\n";
    const std::vector<std::wstring> gpus = GpuNames();
    for (const std::wstring& gpu : gpus)
    {
        text += L"  - " + gpu + L"\n";
    }
    if (gpus.empty())
    {
        text += L"  - (none reported)\n";
    }
    text += L"\n";

    text += L"Monitors:\n";
    const auto enumMonitors = [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL
    {
        auto* output = reinterpret_cast<std::wstring*>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) != FALSE)
        {
            RECT rect = info.rcMonitor;
            wchar_t monitorLine[256] = {};
            swprintf_s(monitorLine, L"  - %s  %ldx%ld\n", info.szDevice,
                       static_cast<long>(rect.right - rect.left),
                       static_cast<long>(rect.bottom - rect.top));
            *output += monitorLine;
        }
        return TRUE;
    };
    std::wstring monitorText;
    EnumDisplayMonitors(nullptr, nullptr, enumMonitors, reinterpret_cast<LPARAM>(&monitorText));
    text += monitorText.empty() ? L"  - (none reported)\n" : monitorText;
    text += L"\n";

    text += L"Storage volumes:\n";
    wchar_t drives[512] = {};
    const DWORD driveLength = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);
    if (driveLength > 0 && driveLength < std::size(drives))
    {
        for (const wchar_t* drive = drives; *drive != L'\0'; drive += wcslen(drive) + 1)
        {
            ULARGE_INTEGER freeBytes{};
            ULARGE_INTEGER totalBytes{};
            if (GetDiskFreeSpaceExW(drive, &freeBytes, &totalBytes, nullptr) != FALSE)
            {
                const double totalGb =
                    static_cast<double>(totalBytes.QuadPart) / (1024.0 * 1024.0 * 1024.0);
                const double freeGb =
                    static_cast<double>(freeBytes.QuadPart) / (1024.0 * 1024.0 * 1024.0);
                wchar_t driveLine[256] = {};
                swprintf_s(driveLine, L"  - %s  %.1f GiB total, %.1f GiB free\n", drive, totalGb,
                           freeGb);
                text += driveLine;
            }
        }
    }
    else
    {
        text += L"  - (none reported)\n";
    }

    return text;
}

bool EnsureClass(HINSTANCE instance)
{
    if (g_class != 0)
    {
        return true;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = SpecsWindow::StaticWndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = kSpecsClassName;
    g_class = RegisterClassExW(&windowClass);
    return g_class != 0;
}

void EnsureFont()
{
    if (g_font != nullptr)
    {
        return;
    }
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE)
    {
        g_font = CreateFontIndirectW(&metrics.lfMessageFont);
    }
    if (g_font == nullptr)
    {
        g_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
}
} // namespace

SpecsWindow::~SpecsWindow()
{
    Close();
}

bool SpecsWindow::Open(HINSTANCE instance)
{
    if (hwnd_ == nullptr)
    {
        if (!Create(instance))
        {
            return false;
        }
    }
    if (hwnd_ == nullptr)
    {
        return false;
    }
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    return true;
}

void SpecsWindow::Close()
{
    if (stale_)
    {
        stale_->store(true);
    }
    if (worker_.joinable())
    {
        worker_.join();
    }
    if (hwnd_ != nullptr)
    {
        HWND hwnd = hwnd_;
        hwnd_ = nullptr;
        DestroyWindow(hwnd);
    }
}

bool SpecsWindow::Create(HINSTANCE instance)
{
    instance_ = instance;
    EnsureFont();
    if (!EnsureClass(instance))
    {
        return false;
    }
    hwnd_ = CreateWindowExW(WS_EX_CONTROLPARENT, kSpecsClassName, L"System Specs",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 460,
                            420, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr)
    {
        return false;
    }

    text_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Gathering system information...",
                            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL |
                                ES_AUTOVSCROLL,
                            0, 0, 100, 100, hwnd_, nullptr, instance, nullptr);
    SendMessageW(text_, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    StartCollect();
    return true;
}

void SpecsWindow::StartCollect()
{
    stale_ = std::make_shared<std::atomic<bool>>(false);
    const HWND target = hwnd_;
    const std::shared_ptr<std::atomic<bool>> stale = stale_;
    worker_ = std::thread(
        [this, target, stale]
        {
            std::wstring text = CollectSystemSpecs();
            if (stale->load())
            {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(contentMutex_);
                content_ = std::move(text);
            }
            if (!stale->load())
            {
                PostMessageW(target, kWmSpecsReady, 0, 0);
            }
        });
}

void SpecsWindow::OnReady()
{
    if (stale_ && stale_->load())
    {
        return;
    }
    std::wstring text;
    {
        std::lock_guard<std::mutex> lock(contentMutex_);
        text = content_;
    }
    if (text.empty())
    {
        return;
    }
    SetWindowTextW(text_, text.c_str());
    FitToContent();
}

void SpecsWindow::FitToContent()
{
    if (text_ == nullptr || hwnd_ == nullptr)
    {
        return;
    }
    HDC dc = GetDC(text_);
    if (dc == nullptr)
    {
        return;
    }
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(text_, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;

    const int maxWidth = 620;
    RECT measured{0, 0, maxWidth, 0};
    const std::wstring text = [this]
    {
        std::lock_guard<std::mutex> lock(contentMutex_);
        return content_;
    }();
    DrawTextW(dc, text.c_str(), -1, &measured, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    if (oldFont != nullptr)
    {
        SelectObject(dc, oldFont);
    }
    ReleaseDC(text_, dc);

    const int clientWidth = std::clamp(static_cast<int>(measured.right) + 24, 320, maxWidth + 24);
    const int clientHeight =
        std::clamp(static_cast<int>(measured.bottom) + 24, 160, 760);

    RECT windowRect{0, 0, clientWidth, clientHeight};
    AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT);
    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;

    RECT current{};
    GetWindowRect(hwnd_, &current);
    SetWindowPos(hwnd_, nullptr, current.left, current.top, windowWidth, windowHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(text_, nullptr, 0, 0, clientWidth, clientHeight, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK SpecsWindow::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    SpecsWindow* self = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SpecsWindow*>(create->lpCreateParams);
        if (self != nullptr)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
    }
    else
    {
        self = reinterpret_cast<SpecsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr)
    {
        return self->HandleMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SpecsWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case kWmSpecsReady:
        OnReady();
        return 0;
    case WM_SIZE:
        if (text_ != nullptr)
        {
            SetWindowPos(text_, nullptr, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER);
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        text_ = nullptr;
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace pacecar::overlay