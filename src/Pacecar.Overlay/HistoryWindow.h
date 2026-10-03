#pragma once

// The History window (design refs 04-ui-ux.md "Settings window"/"History", 05-performance.md).
//
// A conventional top-level window that plots the recent trends from the sampler's fixed-capacity
// ring buffers. It never touches the live rings directly: the app hands it a thread-safe copy
// accessor (`SamplerAccess`), and the window refreshes on a one-second UI timer. Rendering is plain
// GDI on a double-buffered memory DC - the overlay's Direct2D path stays exclusive to the overlay.

#include <functional>
#include <memory>
#include <vector>

#include <windows.h>

#include "pacecar/config/Config.h"
#include "pacecar/metrics/Aggregator.h"
#include "pacecar/metrics/HistoryRetention.h"
#include "pacecar/metrics/MetricsSnapshot.h"

namespace pacecar::overlay
{
class HistoryWindow
{
  public:
    using SamplerAccess = std::function<bool(pacecar::metrics::MetricHistory&)>;
    using SnapshotAccess = std::function<std::shared_ptr<const pacecar::metrics::MetricsSnapshot>()>;

    HistoryWindow() = default;
    ~HistoryWindow();

    HistoryWindow(const HistoryWindow&) = delete;
    HistoryWindow& operator=(const HistoryWindow&) = delete;

    bool Open(HINSTANCE instance, const pacecar::Config* config, SamplerAccess history,
              SnapshotAccess snapshot);
    void Close();

    [[nodiscard]] bool Created() const noexcept
    {
        return hwnd_ != nullptr;
    }
    [[nodiscard]] HWND Hwnd() const noexcept
    {
        return hwnd_;
    }

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

  private:
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    bool Create(HINSTANCE instance);
    void Refresh();
    void Paint(HDC dc, const RECT& client);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    const pacecar::Config* config_ = nullptr;
    SamplerAccess historyAccess_{};
    SnapshotAccess snapshotAccess_{};
    pacecar::metrics::MetricHistory history_{pacecar::metrics::kMaxRawHistorySamples};
    std::shared_ptr<const pacecar::metrics::MetricsSnapshot> snapshot_{};
};
} // namespace pacecar::overlay