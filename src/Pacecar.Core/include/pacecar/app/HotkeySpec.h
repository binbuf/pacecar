#pragma once

// Global-hotkey binding parsing for the `RegisterHotKey` shell (design ref 04-ui-ux.md "Global
// hotkey").
//
// The config stores hotkeys as human-readable strings ("Ctrl+Shift+P"). This header converts those
// strings to/from a Win32-free `HotkeyBinding` (our own modifier bits plus a virtual-key code), so
// the parse/format rules are unit-testable without a window or `windows.h`. The overlay shell maps
// `HotkeyBinding` to the real `MOD_*` flags and calls `RegisterHotKey`.
//
// Only the keys the Settings capture control can produce are accepted: A-Z, 0-9, F1-F24, and a set
// of named navigation/editing keys. An empty string is an error (it means "no hotkey"); callers
// treat that as "not registered".

#include <cstdint>
#include <string>
#include <string_view>

namespace pacecar::app
{
// Modifier bits stored in `HotkeyBinding::modifiers`. Kept independent of `MOD_CONTROL` etc. so the
// parser has no Win32 dependency.
inline constexpr std::uint32_t kHotkeyControl = 0x1u;
inline constexpr std::uint32_t kHotkeyShift = 0x2u;
inline constexpr std::uint32_t kHotkeyAlt = 0x4u;
inline constexpr std::uint32_t kHotkeyWin = 0x8u;
inline constexpr std::uint32_t kHotkeyModifierMask = kHotkeyControl | kHotkeyShift | kHotkeyAlt |
                                                    kHotkeyWin;

// Virtual-key codes for the named keys we accept (values match the Win32 VK_* constants).
inline constexpr std::uint32_t kHotkeyVkBackspace = 0x08u;
inline constexpr std::uint32_t kHotkeyVkTab = 0x09u;
inline constexpr std::uint32_t kHotkeyVkEnter = 0x0Du;
inline constexpr std::uint32_t kHotkeyVkEscape = 0x1Bu;
inline constexpr std::uint32_t kHotkeyVkSpace = 0x20u;
inline constexpr std::uint32_t kHotkeyVkPageUp = 0x21u;
inline constexpr std::uint32_t kHotkeyVkPageDown = 0x22u;
inline constexpr std::uint32_t kHotkeyVkEnd = 0x23u;
inline constexpr std::uint32_t kHotkeyVkHome = 0x24u;
inline constexpr std::uint32_t kHotkeyVkLeft = 0x25u;
inline constexpr std::uint32_t kHotkeyVkUp = 0x26u;
inline constexpr std::uint32_t kHotkeyVkRight = 0x27u;
inline constexpr std::uint32_t kHotkeyVkDown = 0x28u;
inline constexpr std::uint32_t kHotkeyVkInsert = 0x2Du;
inline constexpr std::uint32_t kHotkeyVkDelete = 0x2Eu;
inline constexpr std::uint32_t kHotkeyVkF1 = 0x70u;

struct HotkeyBinding
{
    std::uint32_t modifiers = 0;
    std::uint32_t virtualKey = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return virtualKey != 0;
    }

    bool operator==(const HotkeyBinding&) const = default;
};

struct HotkeyParseResult
{
    HotkeyBinding binding{};
    std::wstring error{};

    [[nodiscard]] bool ok() const noexcept
    {
        return error.empty() && binding.valid();
    }
};

// Parses "Ctrl+Shift+P". Whitespace around tokens is ignored and modifier order is free. Returns a
// result whose `error` is non-empty on failure (empty input, unknown token, missing key).
[[nodiscard]] HotkeyParseResult ParseHotkey(std::wstring_view text);

// Canonical form, modifier order Ctrl, Shift, Alt, Win then the key ("Ctrl+Shift+P"). Returns an
// empty string for an invalid binding.
[[nodiscard]] std::wstring FormatHotkey(const HotkeyBinding& binding);
} // namespace pacecar::app