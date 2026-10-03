#pragma once

// Per-user single-instance guard and activation (design refs 01-architecture.md "Single instance",
// 06-security-distribution.md "IPC security").
//
// The primary instance creates a `Local\Pacecar.Singleton.<sid>` mutex and a message-only activation
// window whose class name embeds the same suffix. A secondary launch sees `ERROR_ALREADY_EXISTS`,
// finds the primary's activation window, posts its registered message to make the overlay show
// itself, and exits without creating a second window. No global mutex is used, so another user
// cannot pre-create it.

#include <functional>
#include <string>
#include <string_view>

#include <windows.h>

namespace pacecar::overlay
{
class SingleInstance
{
  public:
    SingleInstance() = default;
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // Creates the named mutex. `alreadyRunning` is true when another instance owns it. Returns false
    // only when the mutex could not be created at all.
    bool Acquire(std::wstring_view suffix, bool& alreadyRunning);

    // Primary only: registers the activation window class and creates the message-only window.
    bool CreateActivationWindow(HINSTANCE instance, std::function<void()> onActivate);

    // Secondary only: posts the activation message to the primary. Returns true when the primary's
    // window was found and the message posted.
    [[nodiscard]] static bool NotifyExisting(std::wstring_view suffix);

    [[nodiscard]] HWND Hwnd() const noexcept
    {
        return hwnd_;
    }

    [[nodiscard]] const std::wstring& InstanceName() const noexcept
    {
        return instanceName_;
    }

    [[nodiscard]] const std::wstring& ActivationMessageName() const noexcept
    {
        return messageName_;
    }

    [[nodiscard]] const std::wstring& ActivationWindowClass() const noexcept
    {
        return windowClass_;
    }

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

  private:
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HANDLE mutex_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT messageId_ = 0;
    std::wstring suffix_{};
    std::wstring instanceName_{};
    std::wstring messageName_{};
    std::wstring windowClass_{};
    std::function<void()> onActivate_{};
};
} // namespace pacecar::overlay