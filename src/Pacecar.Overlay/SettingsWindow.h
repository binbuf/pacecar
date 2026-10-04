#pragma once

// The Settings window (design refs 04-ui-ux.md "Settings window", 07-migration-learnings.md).
//
// A conventional, resizable, non-topmost top-level window built from Win32 common controls. It binds
// to a live `Config` through `SettingsBinding`: every edit applies immediately (via the `onChanged`
// hook) and is persisted by the app's `DebouncedSaver` (no per-keystroke/per-drag writes). The
// window never mutates config directly.
//
// Accessibility: standard controls, dialog-style keyboard navigation (`IsDialogMessage` is driven by
// the app's message loop while this window is active), visible focus, and system High Contrast (the
// window uses no custom colors, so the theme follows the OS).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include "pacecar/config/Config.h"
#include "pacecar/overlay/SettingsBinding.h"

namespace pacecar::overlay
{
// Posted by the key-capture child controls to the Settings window; wParam is the control id.
inline constexpr UINT kWmHotkeyCaptured = WM_APP + 10;

struct SettingsWindowHooks
{
    HINSTANCE instance = nullptr;
    pacecar::Config* config = nullptr;
    // Live-apply + debounced Touch. Called after the binding has mutated and clamped the config.
    std::function<void()> onChanged{};
    // Restarts with-Windows registration; called only when `start_with_windows`/`start_hidden` change.
    std::function<void()> onStartupSettingChanged{};
    // The About tab's diagnostics text (version, providers, helper, PawnIO).
    std::function<std::wstring()> aboutText{};
    // Whether the overlay currently floats always-on-top; shown read-only in the Overlay tab.
    std::function<bool()> overlayOnTop{};
};

class SettingsWindow
{
  public:
    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // Creates the window on first call, then shows and focuses it. Returns false if creation failed.
    bool Open(const SettingsWindowHooks& hooks);
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
    struct Page
    {
        std::wstring title{};
        std::vector<HWND> controls{};
    };

    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    bool Create(HINSTANCE instance);
    void BuildPages();
    void BuildGeneralPage();
    void BuildOverlayPage();
    void BuildTilesPage();
    void BuildSensorsPage();
    void BuildHistoryPage();
    void BuildHotkeysPage();
    void BuildAboutPage();
    void ShowPage(int index);
    void RefreshFromConfig();
    // Enables/disables controls that only apply to some presentation views (e.g. the StatRows value
    // font size). Called after loading and whenever the view changes.
    void UpdateViewDependentControls();
    void OnCommand(int controlId, int notifyCode, HWND control);
    void OnHotkeyCaptured(int controlId);
    void ApplyLive();
    void PersistStartup();

    void AddControl(int page, HWND control);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND tab_ = nullptr;
    SettingsWindowHooks hooks_{};
    std::unique_ptr<SettingsBinding> binding_{};
    std::vector<Page> pages_{};
    int activePage_ = 0;
    bool loading_ = false;
    bool startupDirty_ = false;
};
} // namespace pacecar::overlay