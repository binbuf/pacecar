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
/// <item>impersonates the client and requires the resulting token's user to match the helper's user
/// (the UI runs as the same user),</item>
/// </list>
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

        if (!NativeMethods.ImpersonateNamedPipeClient(pipe))
        {
            return new Result(false, pid, imagePath, "ImpersonateNamedPipeClient failed");
        }

        try
        {
            using var identity = WindowsIdentity.GetCurrent();
            var clientSid = identity.User?.Value;
            if (clientSid is null || !string.Equals(clientSid, expectedUserSid, StringComparison.OrdinalIgnoreCase))
            {
                return new Result(false, pid, imagePath, "client token user does not match the expected user");
            }
        }
        finally
        {
            _ = NativeMethods.RevertToSelf();
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
}