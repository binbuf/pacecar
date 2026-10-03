#pragma once

// Named-pipe access control for the helper IPC (design ref 06-security-distribution.md).
//
// The helper's named pipe must never be created with a NULL/default DACL. This builds an explicit,
// protected SDDL that grants access only to the intended user and Builtin Administrators and
// explicitly denies `NT AUTHORITY\NETWORK` and Anonymous. The server additionally sets
// `PIPE_REJECT_REMOTE_CLIENTS`. The .NET helper builds the equivalent SDDL from its own SID probe;
// this native helper exists so the security contract is unit-testable and documents the required
// shape.

#include <string>

namespace pacecar::metrics
{
// Returns the SDDL for the pipe DACL, or an empty string when `userSid` is empty/unusable.
//
// Shape: `D:P(D;;GA;;;AN)(D;;GA;;;NU)(A;;GA;;;<userSid>)(A;;GA;;;BA)`
//   D:P  = a protected (no inherited ACEs) discretionary ACL
//   AN   = NT AUTHORITY\ANONYMOUS LOGON (explicit deny)
//   NU   = NT AUTHORITY\NETWORK (explicit deny)
//   BA   = BUILTIN\Administrators (allow)
[[nodiscard]] std::wstring BuildPipeSddl(const std::wstring& userSid);
} // namespace pacecar::metrics