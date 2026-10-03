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
};

// Menu items in display order. The menu command id is the underlying enum value.
inline constexpr std::array<OverlayCommand, 6> kContextMenuCommands{
    OverlayCommand::Mode,     OverlayCommand::Settings, OverlayCommand::History,
    OverlayCommand::Specs,    OverlayCommand::Hide,     OverlayCommand::Exit,
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
    case OverlayCommand::None:
    default:
        return L"";
    }
}

// True when `id` is one of the context menu command ids (used to validate WM_COMMAND/TrackPopupMenu).
[[nodiscard]] inline bool IsOverlayCommand(unsigned id) noexcept
{
    return id >= static_cast<unsigned>(OverlayCommand::Mode) &&
           id <= static_cast<unsigned>(OverlayCommand::Exit);
}
} // namespace pacecar::overlay