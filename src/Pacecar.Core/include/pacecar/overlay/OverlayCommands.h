#pragma once

// The overlay's right-click context menu model (design ref 04-ui-ux.md "Interaction").
//
// Kept Win32-free so the command set and its order are unit-testable; `OverlayWindow` builds the
// real `HMENU` from this list and maps `TrackPopupMenu` results back to `OverlayCommand`.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "pacecar/config/Config.h"

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
    // Cycles the presentation view (Full -> Large visuals -> Small text -> Stat rows -> FPS only).
    // Used by the cycle-view hotkey; the menus use the explicit View* choices below.
    CycleView = 11,
    // Toggles the panel background so the overlay can read as plain text over the desktop/game.
    ToggleBackground = 12,
    // Top-level "View" popup marker. The selectable entries are the View* commands that follow.
    View = 13,
    // Explicit presentation-view choices (radio items in the View submenu).
    ViewFull = 14,
    ViewLargeVisuals = 15,
    ViewSmallText = 16,
    ViewStatRows = 17,
    ViewFpsText = 18,
    ViewFpsOnly = 19,
};

// The View submenu's items, in display order.
inline constexpr std::array<OverlayCommand, 6> kViewMenuCommands{
    OverlayCommand::ViewFull,      OverlayCommand::ViewLargeVisuals,
    OverlayCommand::ViewSmallText, OverlayCommand::ViewStatRows,
    OverlayCommand::ViewFpsText,   OverlayCommand::ViewFpsOnly};

// Maps a View* menu command to the presentation view it selects, or nullopt for other commands.
[[nodiscard]] inline std::optional<pacecar::ViewMode> ViewModeForCommand(
    OverlayCommand command) noexcept
{
    switch (command)
    {
    case OverlayCommand::ViewFull:
        return pacecar::ViewMode::Full;
    case OverlayCommand::ViewLargeVisuals:
        return pacecar::ViewMode::LargeVisuals;
    case OverlayCommand::ViewSmallText:
        return pacecar::ViewMode::SmallText;
    case OverlayCommand::ViewStatRows:
        return pacecar::ViewMode::StatRows;
    case OverlayCommand::ViewFpsText:
        return pacecar::ViewMode::FpsText;
    case OverlayCommand::ViewFpsOnly:
        return pacecar::ViewMode::FpsOnly;
    default:
        return std::nullopt;
    }
}

// The menu command that selects `view`.
[[nodiscard]] inline OverlayCommand CommandForViewMode(pacecar::ViewMode view) noexcept
{
    switch (view)
    {
    case pacecar::ViewMode::Full:
        return OverlayCommand::ViewFull;
    case pacecar::ViewMode::LargeVisuals:
        return OverlayCommand::ViewLargeVisuals;
    case pacecar::ViewMode::SmallText:
        return OverlayCommand::ViewSmallText;
    case pacecar::ViewMode::StatRows:
        return OverlayCommand::ViewStatRows;
    case pacecar::ViewMode::FpsText:
        return OverlayCommand::ViewFpsText;
    case pacecar::ViewMode::FpsOnly:
        return OverlayCommand::ViewFpsOnly;
    }
    return OverlayCommand::ViewFull;
}

// Overlay right-click menu items in display order. The menu command id is the underlying enum
// value.
inline constexpr std::array<OverlayCommand, 9> kContextMenuCommands{
    OverlayCommand::View,
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
    OverlayCommand::View,
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
        return L"Click-through input";
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
        return L"Transparent background";
    case OverlayCommand::View:
        return L"View";
    case OverlayCommand::ViewFull:
        return L"Full panel";
    case OverlayCommand::ViewLargeVisuals:
        return L"Large visuals";
    case OverlayCommand::ViewSmallText:
        return L"Small text";
    case OverlayCommand::ViewStatRows:
        return L"Stat rows";
    case OverlayCommand::ViewFpsText:
        return L"FPS text";
    case OverlayCommand::ViewFpsOnly:
        return L"FPS only";
    case OverlayCommand::None:
    default:
        return L"";
    }
}

// True when `id` is one of the command ids (used to validate WM_COMMAND/TrackPopupMenu).
[[nodiscard]] inline bool IsOverlayCommand(unsigned id) noexcept
{
    return id >= static_cast<unsigned>(OverlayCommand::Mode) &&
           id <= static_cast<unsigned>(OverlayCommand::ViewFpsOnly);
}
} // namespace pacecar::overlay