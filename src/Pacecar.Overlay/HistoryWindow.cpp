#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "targetver.h"

#include "HistoryWindow.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <span>
#include <vector>

#include "pacecar/metrics/HistoryRetention.h"

namespace pacecar::overlay
{
namespace
{
constexpr wchar_t kHistoryClassName[] = L"PacecarHistoryWindow";
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT kRefreshIntervalMs = 1000;
ATOM g_class = 0;
HFONT g_font = nullptr;

bool EnsureClass(HINSTANCE instance)
{
    if (g_class != 0)
    {
        return true;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = HistoryWindow::StaticWndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = kHistoryClassName;
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

std::wstring FormatScaled(double value, const wchar_t* unit)
{
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%.1f %s", value, unit);
    return buffer;
}

struct SeriesView
{
    const wchar_t* title;
    const pacecar::RingBuffer<double>* ring;
    const wchar_t* unit;
    double fixedMax; // > 0 means a fixed 0..fixedMax scale (percentages), else auto-scale
};
} // namespace

HistoryWindow::~HistoryWindow()
{
    Close();
}

bool HistoryWindow::Open(HINSTANCE instance, const pacecar::Config* config, SamplerAccess history,
                         SnapshotAccess snapshot)
{
    config_ = config;
    historyAccess_ = std::move(history);
    snapshotAccess_ = std::move(snapshot);
    if (hwnd_ == nullptr)
    {
        if (!Create(instance))
        {
            return false;
        }
    }
    Refresh();
    if (hwnd_ == nullptr)
    {
        return false;
    }
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    return true;
}

void HistoryWindow::Close()
{
    if (hwnd_ != nullptr)
    {
        HWND hwnd = hwnd_;
        hwnd_ = nullptr;
        DestroyWindow(hwnd);
    }
}

bool HistoryWindow::Create(HINSTANCE instance)
{
    instance_ = instance;
    EnsureFont();
    if (!EnsureClass(instance))
    {
        return false;
    }
    hwnd_ = CreateWindowExW(0, kHistoryClassName, L"Pacecar History",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 620,
                            520, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr)
    {
        return false;
    }
    SetTimer(hwnd_, kRefreshTimerId, kRefreshIntervalMs, nullptr);
    return true;
}

void HistoryWindow::Refresh()
{
    if (historyAccess_)
    {
        static_cast<void>(historyAccess_(history_));
    }
    if (snapshotAccess_)
    {
        snapshot_ = snapshotAccess_();
    }
}

void HistoryWindow::Paint(HDC dc, const RECT& client)
{
    const int width = static_cast<int>(client.right - client.left);
    const int height = static_cast<int>(client.bottom - client.top);

    const std::array<SeriesView, 6> series{{
        {L"CPU %", &history_.cpuTotalUtilization, L"%", 100.0},
        {L"RAM %", &history_.memoryUsedPercent, L"%", 100.0},
        {L"GPU %", &history_.gpuUtilization, L"%", 100.0},
        {L"Network down", &history_.networkDownBytesPerSecond, L"B/s", 0.0},
        {L"Disk read", &history_.diskReadBytesPerSecond, L"B/s", 0.0},
        {L"Ping", &history_.pingRttMs, L"ms", 0.0},
    }};

    const int margin = 10;
    const int columns = 2;
    const int rows = 3;
    const int cellWidth = std::max((width - margin * (columns + 1)) / columns, 40);
    const int cellHeight = std::max((height - margin * (rows + 1)) / rows, 40);

    HGDIOBJ oldFont = SelectObject(dc, g_font);
    SetBkMode(dc, TRANSPARENT);

    std::vector<POINT> points;
    for (std::size_t i = 0; i < series.size(); ++i)
    {
        const int column = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        RECT cell{margin + column * (cellWidth + margin), margin + row * (cellHeight + margin),
                  margin + column * (cellWidth + margin) + cellWidth,
                  margin + row * (cellHeight + margin) + cellHeight};

        DrawTextW(dc, series[i].title, -1, &cell, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

        const pacecar::RingBuffer<double>& ring = *series[i].ring;
        const std::size_t count = ring.Size();

        // Header value: the newest sample in the series (not the smoothed snapshot).
        std::wstring header;
        if (count > 0)
        {
            header = FormatScaled(ring.Latest(), series[i].unit);
        }
        else
        {
            header = L"(no samples)";
        }
        RECT headerRect = cell;
        SetTextColor(dc, GetSysColor(COLOR_GRADIENTACTIVECAPTION));
        DrawTextW(dc, header.c_str(), -1, &headerRect,
                  DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));

        RECT plot = cell;
        plot.top += 20;
        FrameRect(dc, &plot, GetSysColorBrush(COLOR_3DSHADOW));

        const int plotWidth = static_cast<int>(plot.right - plot.left) - 2;
        const int plotHeight = static_cast<int>(plot.bottom - plot.top) - 2;
        if (count < 2 || plotWidth < 2 || plotHeight < 2)
        {
            continue;
        }

        double maxValue = series[i].fixedMax;
        if (maxValue <= 0.0)
        {
            double observed = 1.0;
            ring.ForEachOldestFirst([&observed](double value) { observed = std::max(observed, value); });
            maxValue = observed * 1.1;
            if (std::wcscmp(series[i].unit, L"ms") == 0)
            {
                maxValue = std::max(maxValue, 50.0);
            }
        }

        const std::size_t desired = static_cast<std::size_t>(plotWidth);
        std::vector<double> values(desired);
        const std::size_t produced = ring.Downsample(std::span<double>(values.data(), values.size()));
        points.resize(produced);
        for (std::size_t p = 0; p < produced; ++p)
        {
            const double fraction = std::clamp(values[p] / maxValue, 0.0, 1.0);
            points[p].x = plot.left + 1 + static_cast<LONG>(p);
            points[p].y = plot.bottom - 1 - static_cast<LONG>(fraction * (plotHeight - 1));
        }
        if (points.size() >= 2)
        {
            HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
            SetDCPenColor(dc, GetSysColor(COLOR_HIGHLIGHT));
            Polyline(dc, points.data(), static_cast<int>(points.size()));
            SelectObject(dc, oldPen);
        }
    }

    // Retention caption.
    if (config_ != nullptr)
    {
        wchar_t caption[128] = {};
        const auto retention = pacecar::metrics::PlanHistoryRetention(
            config_->history.retention_minutes,
            std::chrono::milliseconds(static_cast<int>(config_->general.refresh)));
        swprintf_s(caption, L"Retention: %d min (raw %zu samples, stride %zu)", 
                   config_->history.retention_minutes, retention.rawCapacity,
                   retention.downsampleStride);
        RECT captionRect{0, height - 20, width - 8, height};
        DrawTextW(dc, caption, -1, &captionRect,
                  DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (oldFont != nullptr)
    {
        SelectObject(dc, oldFont);
    }
}

LRESULT CALLBACK HistoryWindow::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HistoryWindow* self = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<HistoryWindow*>(create->lpCreateParams);
        if (self != nullptr)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
    }
    else
    {
        self = reinterpret_cast<HistoryWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr)
    {
        return self->HandleMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT HistoryWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TIMER:
        if (wParam == kRefreshTimerId)
        {
            Refresh();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd_, &paint);
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = std::max(static_cast<int>(client.right), 1);
        const int height = std::max(static_cast<int>(client.bottom), 1);

        HDC memoryDc = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
        if (memoryDc != nullptr && bitmap != nullptr)
        {
            HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
            RECT fill{0, 0, width, height};
            FillRect(memoryDc, &fill, GetSysColorBrush(COLOR_WINDOW));
            Paint(memoryDc, client);
            BitBlt(dc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
            SelectObject(memoryDc, oldBitmap);
        }
        if (bitmap != nullptr)
        {
            DeleteObject(bitmap);
        }
        if (memoryDc != nullptr)
        {
            DeleteDC(memoryDc);
        }
        EndPaint(hwnd_, &paint);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd_, kRefreshTimerId);
        hwnd_ = nullptr;
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