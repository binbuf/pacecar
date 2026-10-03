#pragma once

// Per-user "start with Windows" registration via `HKCU\...\Run` (design refs 04-ui-ux.md "Settings
// window", 06-security-distribution.md "Elevation model": per-user auto-start needs no elevation).
//
// Only the current user's hive is touched, so the overlay never prompts for elevation to auto-start.
// The pure command builder is unit-tested; the registry read/write/delete helpers accept an explicit
// subkey so tests can round-trip against a disposable key instead of the real Run key.

#include <string>
#include <string_view>

namespace pacecar::app
{
// The value lives under HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
inline constexpr wchar_t kStartupRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kStartupValueName[] = L"Pacecar";

// Builds the Run command line: the quoted executable path plus an optional `--start-hidden` switch.
[[nodiscard]] std::wstring BuildStartupCommand(std::wstring_view executablePath, bool startHidden);

// HKCU registry helpers. `subKey` is relative to HKEY_CURRENT_USER. `error` receives a message on
// failure (unchanged on success).
[[nodiscard]] bool ReadStartupValue(std::wstring_view subKey, std::wstring_view valueName,
                                    std::wstring& command);
[[nodiscard]] bool WriteStartupValue(std::wstring_view subKey, std::wstring_view valueName,
                                     std::wstring_view command, std::wstring* error);
[[nodiscard]] bool DeleteStartupValue(std::wstring_view subKey, std::wstring_view valueName,
                                      std::wstring* error);

// Convenience over the Run key. `SetStartWithWindows(exe, false, ...)` removes the value.
[[nodiscard]] bool SetStartWithWindows(std::wstring_view executablePath, bool enabled,
                                       bool startHidden, std::wstring* error);
[[nodiscard]] bool IsStartWithWindowsEnabled(std::wstring* commandOut, std::wstring* error);
} // namespace pacecar::app