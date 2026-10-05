using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;

namespace Pacecar.Sensors;

/// <summary>
/// Validates the process on the other end of the pipe before streaming anything (design ref
/// 06-security-distribution.md). The helper is the privilege boundary, so it treats the client as
/// untrusted:
/// <list type="bullet">
/// <item>resolves the client PID with <c>GetNamedPipeClientProcessId</c>,</item>
/// <item>opens the process and records its image path,</item>
/// <item>opens the client's process token and requires its user to match the helper's user (the UI
/// runs as the same user).</item>
/// </list>
/// The token is queried directly rather than with <c>ImpersonateNamedPipeClient</c>: the latter
/// fails with <c>ERROR_CANNOT_IMPERSONATE (1368)</c> whenever the client opened the pipe without an
/// impersonation-level SQOS, which silently rejected every client (no Hello, no capture). Querying
/// the token needs no <c>SeImpersonatePrivilege</c> and works regardless of the client's handle.
/// A same-user attacker could inject into the unsigned UI, so this authenticates the caller's
/// identity; image signature verification (WinVerifyTrust) is a documented follow-up.
/// </summary>
internal static class ClientValidator
{
    public readonly record struct Result(bool Accepted, uint ProcessId, string ImagePath,
                                         string Reason);

    public static Result Validate(nint pipe, string expectedUserSid)
    {
        if (!NativeMethods.GetNamedPipeClientProcessId(pipe, out var pid))
        {
            return new Result(false, 0, string.Empty, "GetNamedPipeClientProcessId failed");
        }

        var imagePath = QueryImagePath(pid);
        if (string.IsNullOrEmpty(imagePath))
        {
            return new Result(false, pid, string.Empty, "could not resolve the client image path");
        }

        var clientSid = QueryUserSid(pid);
        if (string.IsNullOrEmpty(clientSid))
        {
            return new Result(false, pid, imagePath, "could not resolve the client user SID");
        }

        if (!string.Equals(clientSid, expectedUserSid, StringComparison.OrdinalIgnoreCase))
        {
            return new Result(false, pid, imagePath, "client token user does not match the expected user");
        }

        return new Result(true, pid, imagePath, "ok");
    }

    private static string QueryImagePath(uint pid)
    {
        var process = NativeMethods.OpenProcess(NativeMethods.ProcessQueryLimitedInformation,
                                                false, pid);
        if (process == nint.Zero)
        {
            return string.Empty;
        }

        try
        {
            var buffer = new StringBuilder(1024);
            var size = (uint)buffer.Capacity;
            return NativeMethods.QueryFullProcessImageNameW(process, 0, buffer, ref size)
                ? buffer.ToString()
                : string.Empty;
        }
        finally
        {
            _ = NativeMethods.CloseHandle(process);
        }
    }

    private static string QueryUserSid(uint pid)
    {
        var process = NativeMethods.OpenProcess(NativeMethods.ProcessQueryInformation, false, pid);
        if (process == nint.Zero)
        {
            process = NativeMethods.OpenProcess(NativeMethods.ProcessQueryLimitedInformation, false,
                                                pid);
        }
        if (process == nint.Zero)
        {
            return string.Empty;
        }

        try
        {
            if (!NativeMethods.OpenProcessToken(process, NativeMethods.TokenQuery, out var token))
            {
                return string.Empty;
            }
            try
            {
                _ = NativeMethods.GetTokenInformation(token, NativeMethods.TokenUser, nint.Zero, 0,
                                                       out var size);
                if (size <= 0)
                {
                    return string.Empty;
                }
                var buffer = Marshal.AllocHGlobal(size);
                try
                {
                    if (!NativeMethods.GetTokenInformation(token, NativeMethods.TokenUser, buffer,
                                                           size, out _))
                    {
                        return string.Empty;
                    }
                    // TOKEN_USER begins with SID_AND_ATTRIBUTES { PSID Sid; DWORD Attributes; }.
                    var sidPointer = Marshal.ReadIntPtr(buffer);
                    return sidPointer == nint.Zero ? string.Empty : new SecurityIdentifier(sidPointer).Value;
                }
                finally
                {
                    Marshal.FreeHGlobal(buffer);
                }
            }
            finally
            {
                _ = NativeMethods.CloseHandle(token);
            }
        }
        finally
        {
            _ = NativeMethods.CloseHandle(process);
        }
    }
}