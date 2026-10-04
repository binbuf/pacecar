#pragma once

// Global hotkey registration for the overlay (`RegisterHotKey` + `WM_HOTKEY`; design ref
// 04-ui-ux.md "Global hotkey"). No low-level keyboard hook is used.
//
// The manager owns at most a handful of bindings keyed by a stable integer id. Re-registering an id
// unregisters the old binding first, so a settings change takes effect without a restart. A failed
// registration never throws: the id is left unregistered, `LastError` describes why, and the app
// keeps running (the caller surfaces the message to the user).

#include <functional>
#include <map>
#include <string>

#include <windows.h>

#include "pacecar/app/HotkeySpec.h"

namespace pacecar::overlay
{
class HotkeyManager
{
  public:
    // WM_HOTKEY ids used by the app.
    static constexpr int kToggleOverlayId = 1;
    static constexpr int kToggleClickThroughId = 2;
    static constexpr int kCycleViewId = 3;
    static constexpr int kToggleBackgroundId = 4;

    using Callback = std::function<void(int hotkeyId)>;

    HotkeyManager() = default;
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    // Attaches the manager to the window that receives `WM_HOTKEY`. Clears any prior bindings.
    void Attach(HWND hwnd);

    // Registers (or replaces) `id`. Returns false and sets LastError when the binding is invalid or
    // the system reports the combination is already taken.
    bool Register(int id, const pacecar::app::HotkeyBinding& binding);

    // Removes `id` if present. Safe to call for an unregistered id.
    void Unregister(int id);

    // Removes and frees every binding.
    void UnregisterAll();

    [[nodiscard]] bool IsRegistered(int id) const noexcept;
    [[nodiscard]] const std::wstring& LastError() const noexcept
    {
        return lastError_;
    }

    void SetCallback(Callback callback)
    {
        callback_ = std::move(callback);
    }

    // Invokes the callback for a `WM_HOTKEY` id. Returns true when the id was handled.
    bool HandleHotkey(int id) const;

  private:
    struct Entry
    {
        pacecar::app::HotkeyBinding binding{};
        std::wstring text{};
    };

    HWND hwnd_ = nullptr;
    std::map<int, Entry> entries_{};
    std::wstring lastError_{};
    Callback callback_{};
};

// Translates the core modifier bits and virtual key into `RegisterHotKey` arguments, always adding
// `MOD_NOREPEAT`. Exposed for tests.
[[nodiscard]] UINT HotkeyToWin32Modifiers(std::uint32_t modifiers) noexcept;
} // namespace pacecar::overlay