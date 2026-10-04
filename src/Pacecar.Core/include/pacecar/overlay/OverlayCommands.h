#pragma once

// The overlay's right-click context menu model (design ref 04-ui-ux.md "Interaction").
//
// Kept Win32-free so the command set and its order are unit-testable; `OverlayWindow` builds the
// real `HMENU` from this list and maps `TrackPopupMenu` results back to `OverlayCommand`.

#include <array>
#include <cstddef>
#include <cstdint>

namespace pacecar::overlay
{
enum class OverlayCommand : std::uint16_t
{
    None = 0,
    Mode = 1,
    Settings = 2,
    History = 3,
    Specs = 4,
    Hide = 5,
    Exit = 6,
    // Tray-only commands (design ref 04-ui-ux.md "Tray"). `ToggleVisibility` is Show/Hide as one
    // item; `Hide` stays in the overlay context menu.
    ToggleVisibility = 7,
    CopySystemInfo = 8,
    About = 9,
    // Opt-in FPS / frame-time capture (task T17). Tray/context action; never runs by default.
    ToggleFrameCapture = 10,
    // Cycles the presentation view (Full -> Large visuals -> Small text -> FPS only).
    CycleView = 11,
    // Toggles the panel background so the overlay can read as plain text over the desktop/game.
    ToggleBackground = 12,
};

// Overlay right-click menu items in display order. The menu command id is the underlying enum
// value.
inline constexpr std::array<OverlayCommand, 9> kContextMenuCommands{
    OverlayCommand::CycleView,
    OverlayCommand::ToggleBackground,
    OverlayCommand::Mode,
    OverlayCommand::Settings,
    OverlayCommand::History,
    OverlayCommand::Specs,
    OverlayCommand::ToggleFrameCapture,
    OverlayCommand::Hide,
    OverlayCommand::Exit,
};

// Tray menu items in display order: Show/Hide, Cycle view, Toggle background, Mode, Settings,
// History, FPS capture, Copy system info, About, Exit (design ref 04-ui-ux.md "Tray").
inline constexpr std::array<OverlayCommand, 10> kTrayMenuCommands{
    OverlayCommand::ToggleVisibility,
    OverlayCommand::CycleView,
    OverlayCommand::ToggleBackground,
    OverlayCommand::Mode,
    OverlayCommand::Settings,
    OverlayCommand::History,
    OverlayCommand::ToggleFrameCapture,
    OverlayCommand::CopySystemInfo,
    OverlayCommand::About,
    OverlayCommand::Exit,
};

// The display label for a command (stable static storage).
[[nodiscard]] inline const wchar_t* CommandLabel(OverlayCommand command) noexcept
{
    switch (command)
    {
    case OverlayCommand::Mode:
        return L"Mode";
    case OverlayCommand::Settings:
        return L"Settings";
    case OverlayCommand::History:
        return L"History";
    case OverlayCommand::Specs:
        return L"Specs";
    case OverlayCommand::Hide:
        return L"Hide";
    case OverlayCommand::Exit:
        return L"Exit";
    case OverlayCommand::ToggleVisibility:
        return L"Show/Hide";
    case OverlayCommand::CopySystemInfo:
        return L"Copy system info";
    case OverlayCommand::About:
        return L"About";
    case OverlayCommand::ToggleFrameCapture:
        return L"FPS capture";
    case OverlayCommand::CycleView:
        return L"Cycle view";
    case OverlayCommand::ToggleBackground:
        return L"Background";
    case OverlayCommand::None:
    default:
        return L"";
    }
}

// True when `id` is one of the command ids (used to validate WM_COMMAND/TrackPopupMenu).
[[nodiscard]] inline bool IsOverlayCommand(unsigned id) noexcept
{
    return id >= static_cast<unsigned>(OverlayCommand::Mode) &&
           id <= static_cast<unsigned>(OverlayCommand::ToggleBackground);
}
} // namespace pacecar::overlay