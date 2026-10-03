#pragma once

// The one-shot Specs window (design refs 04-ui-ux.md "Specs view", 07-migration-learnings.md).
//
// A conventional top-level window sized to its content exactly, with no extra chrome beyond a
// caption. The hardware inventory is gathered on a worker thread from native APIs (registry,
// GlobalMemoryStatusEx, monitor enumeration, DXGI-free adapter names) and posted back to the UI
// thread, so opening the window never blocks the UI on the query.

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>

namespace pacecar::overlay
{
// Posted by the inventory worker to the Specs window when the text is ready.
inline constexpr UINT kWmSpecsReady = WM_APP + 20;

class SpecsWindow
{
  public:
    SpecsWindow() = default;
    ~SpecsWindow();

    SpecsWindow(const SpecsWindow&) = delete;
    SpecsWindow& operator=(const SpecsWindow&) = delete;

    bool Open(HINSTANCE instance);
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
    void StartCollect();
    void OnReady();
    void FitToContent();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND text_ = nullptr;
    std::wstring content_;
    std::mutex contentMutex_;
    std::thread worker_;
    std::shared_ptr<std::atomic<bool>> stale_;
};
} // namespace pacecar::overlay