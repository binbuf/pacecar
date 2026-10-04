#include "Tray.h"

#include <algorithm>
#include <cstddef>

#include <shellapi.h>

#include "pacecar/util/Logger.h"

namespace pacecar::overlay
{
namespace
{
constexpr wchar_t kTrayWindowClass[] = L"PacecarTrayWindow";
constexpr std::size_t kTooltipCapacity = 127;

ATOM g_trayClass = 0;

// Appends the "View" popup with a radio check on the active view. Returns false on failure.
bool AppendViewSubmenu(HMENU menu, pacecar::ViewMode current)
{
    HMENU sub = CreatePopupMenu();
    if (sub == nullptr)
    {
        return false;
    }
    for (const OverlayCommand command : kViewMenuCommands)
    {
        if (AppendMenuW(sub, MF_STRING, static_cast<UINT_PTR>(command), CommandLabel(command)) ==
            FALSE)
        {
            DestroyMenu(sub);
            return false;
        }
    }
    UINT radioIndex = 0;
    const OverlayCommand active = CommandForViewMode(current);
    for (std::size_t i = 0; i < kViewMenuCommands.size(); ++i)
    {
        if (kViewMenuCommands[i] == active)
        {
            radioIndex = static_cast<UINT>(i);
            break;
        }
    }
    CheckMenuRadioItem(sub, 0, static_cast<UINT>(kViewMenuCommands.size() - 1), radioIndex,
                       MF_BYPOSITION);
    if (AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"View") == FALSE)
    {
        DestroyMenu(sub);
        return false;
    }
    return true;
}

bool EnsureTrayClass(HINSTANCE instance)
{
    if (g_trayClass != 0)
    {
        return true;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = Tray::StaticWndProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kTrayWindowClass;
    g_trayClass = RegisterClassExW(&windowClass);
    return g_trayClass != 0;
}
} // namespace

Tray::~Tray()
{
    Destroy();
}

bool Tray::Create(HINSTANCE instance, int iconResourceId, std::wstring_view tooltip)
{
    if (created_)
    {
        return true;
    }
    if (!EnsureTrayClass(instance))
    {
        LogWarn(L"tray: failed to register the tray window class");
        return false;
    }

    hwnd_ = CreateWindowExW(0, kTrayWindowClass, L"Pacecar tray", WS_POPUP, 0, 0, 0, 0, nullptr,
                            nullptr, instance, this);
    if (hwnd_ == nullptr)
    {
        LogWarn(L"tray: failed to create the tray owner window");
        return false;
    }

    iconData_ = {};
    iconData_.cbSize = sizeof(iconData_);
    iconData_.hWnd = hwnd_;
    iconData_.uID = kIconId;
    iconData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    iconData_.uCallbackMessage = WM_APP_TRAY;
    iconData_.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(iconResourceId));
    const std::wstring tip(tooltip.substr(0, std::min(tooltip.size(), kTooltipCapacity)));
    wcsncpy_s(iconData_.szTip, tip.c_str(), _TRUNCATE);

    if (Shell_NotifyIconW(NIM_ADD, &iconData_) == FALSE)
    {
        LogWarn(L"tray: Shell_NotifyIcon(NIM_ADD) failed");
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    created_ = true;
    return true;
}

void Tray::Destroy()
{
    if (created_)
    {
        Shell_NotifyIconW(NIM_DELETE, &iconData_);
        created_ = false;
    }
    if (hwnd_ != nullptr)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void Tray::SetTooltip(std::wstring_view tooltip)
{
    if (!created_)
    {
        return;
    }
    const std::wstring tip(tooltip.substr(0, std::min(tooltip.size(), kTooltipCapacity)));
    wcsncpy_s(iconData_.szTip, tip.c_str(), _TRUNCATE);
    iconData_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &iconData_);
    iconData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

void Tray::SetVisibleFlag(bool visible) noexcept
{
    visible_ = visible;
}

void Tray::SetClickThroughFlag(bool clickThrough) noexcept
{
    clickThrough_ = clickThrough;
}

void Tray::SetBackgroundFlag(bool transparentBackground) noexcept
{
    transparentBackground_ = transparentBackground;
}

void Tray::ShowMenu(POINT screenPoint)
{
    if (hwnd_ == nullptr)
    {
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        return;
    }

    for (const OverlayCommand command : kTrayMenuCommands)
    {
        // Group the menu: show/hide, view controls, windows, capture/output, lifecycle.
        if (command == OverlayCommand::View || command == OverlayCommand::Settings ||
            command == OverlayCommand::CopySystemInfo || command == OverlayCommand::Exit)
        {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        if (command == OverlayCommand::View)
        {
            if (!AppendViewSubmenu(menu, view_))
            {
                DestroyMenu(menu);
                return;
            }
            continue;
        }
        UINT flags = MF_STRING;
        if (command == OverlayCommand::Mode && clickThrough_)
        {
            flags |= MF_CHECKED;
        }
        if (command == OverlayCommand::ToggleBackground && transparentBackground_)
        {
            flags |= MF_CHECKED;
        }
        const wchar_t* label = CommandLabel(command);
        if (command == OverlayCommand::ToggleVisibility)
        {
            label = visible_ ? L"Hide" : L"Show";
        }
        if (AppendMenuW(menu, flags, static_cast<UINT_PTR>(command), label) == FALSE)
        {
            DestroyMenu(menu);
            return;
        }
    }

    // Required so the menu dismisses correctly when the user clicks elsewhere.
    SetForegroundWindow(hwnd_);
    const UINT chosen =
        static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                         screenPoint.x, screenPoint.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    if (chosen != 0 && IsOverlayCommand(chosen))
    {
        if (commandCallback_)
        {
            commandCallback_(static_cast<OverlayCommand>(chosen));
        }
    }
}

LRESULT CALLBACK Tray::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Tray* self = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Tray*>(create->lpCreateParams);
        if (self != nullptr)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
    }
    else
    {
        self = reinterpret_cast<Tray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr)
    {
        return self->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Tray::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_APP_TRAY:
        switch (LOWORD(lParam))
        {
        case WM_LBUTTONDBLCLK:
            if (commandCallback_)
            {
                commandCallback_(OverlayCommand::ToggleVisibility);
            }
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT point{};
            GetCursorPos(&point);
            ShowMenu(point);
            return 0;
        }
        default:
            break;
        }
        return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == 0 && IsOverlayCommand(LOWORD(wParam)))
        {
            if (commandCallback_)
            {
                commandCallback_(static_cast<OverlayCommand>(LOWORD(wParam)));
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
} // namespace pacecar::overlay