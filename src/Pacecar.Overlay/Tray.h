#pragma once

// System tray icon and menu (design ref 04-ui-ux.md "Tray"). The tray runs on the UI thread's single
// message pump: it owns a hidden top-level window that receives the `Shell_NotifyIcon` callback
// message and builds the tray menu. Menu handling therefore stays off the render path.
//
// The tray reports the chosen `OverlayCommand` through a callback; the app decides what each command
// does (toggle visibility, switch click-through, open Settings, quit). Double-clicking the icon
// reports `ToggleVisibility`.

#include <functional>
#include <string>
#include <string_view>

#include <windows.h>

#include "pacecar/overlay/OverlayCommands.h"

namespace pacecar::overlay
{
class Tray
{
  public:
    // Tray callback message posted to the hidden owner window.
    static constexpr UINT WM_APP_TRAY = WM_APP + 3;
    static constexpr UINT kIconId = 1;

    using CommandCallback = std::function<void(OverlayCommand)>;

    Tray() = default;
    ~Tray();

    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // Creates the hidden owner window and adds the notification icon. `iconResourceId` is an
    // `IDI_*` integer (MAKEINTRESOURCE form). Returns false if the icon could not be added.
    bool Create(HINSTANCE instance, int iconResourceId, std::wstring_view tooltip);

    // Removes the icon and destroys the owner window. Safe to call more than once.
    void Destroy();

    void SetTooltip(std::wstring_view tooltip);
    void SetVisibleFlag(bool visible) noexcept;
    void SetClickThroughFlag(bool clickThrough) noexcept;

    void SetCommandCallback(CommandCallback callback)
    {
        commandCallback_ = std::move(callback);
    }

    [[nodiscard]] HWND Hwnd() const noexcept
    {
        return hwnd_;
    }

    [[nodiscard]] bool Created() const noexcept
    {
        return created_;
    }

    // Builds and tracks the tray menu at `screenPoint`, invoking the command callback. Public so it
    // can be reused if the shell sends WM_CONTEXTMENU instead of WM_RBUTTONUP.
    void ShowMenu(POINT screenPoint);

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

  private:
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    bool created_ = false;
    bool visible_ = true;
    bool clickThrough_ = false;
    CommandCallback commandCallback_{};
    NOTIFYICONDATAW iconData_{};
};
} // namespace pacecar::overlay