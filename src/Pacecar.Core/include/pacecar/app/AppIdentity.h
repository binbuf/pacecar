#pragma once

// Per-user application identity helpers shared by the single-instance guard and the helper stub
// (design refs 01-architecture.md "Single instance", 06-security-distribution.md).
//
// The overlay is single-instance per user. Both processes (the first and the secondary that wants to
// activate it) derive the same names from the current user's SID: a `Local\`-namespace mutex, a
// registered window message, and the activation window's class name. Deriving the names from one
// suffix keeps the sender and receiver in lockstep and lets tests inject a unique suffix so they do
// not collide with a real running instance.

#include <string>
#include <string_view>

namespace pacecar::app
{
// Returns "Local\Pacecar.Singleton.<suffix>". The `Local\` namespace scopes the mutex to the logon
// session, and the SID suffix prevents another user from pre-creating it (design ref 06).
[[nodiscard]] std::wstring MakeSingletonName(std::wstring_view suffix);

// "Pacecar.Activate.<suffix>" - passed to `RegisterWindowMessageW`.
[[nodiscard]] std::wstring MakeActivationMessageName(std::wstring_view suffix);

// "Pacecar.SingletonWindow.<suffix>" - the activation window class the primary registers and the
// secondary finds with `FindWindowExW(HWND_MESSAGE, ...)`.
[[nodiscard]] std::wstring MakeActivationWindowClass(std::wstring_view suffix);

// The current process user's SID in string form (e.g. "S-1-5-21-..."), or empty on failure. Used as
// the default suffix.
[[nodiscard]] std::wstring CurrentUserSid();
} // namespace pacecar::app