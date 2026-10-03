using System.Runtime.InteropServices;

namespace Pacecar.Sensors;

/// <summary>
/// Builds the explicit named-pipe DACL (design ref 06-security-distribution.md). Mirrors the native
/// <c>pacecar::metrics::BuildPipeSddl</c> so the shape is identical and unit-tested on the native
/// side:
/// <code>D:P(D;;GA;;;AN)(D;;GA;;;NU)(A;;GA;;;&lt;userSid&gt;)(A;;GA;;;BA)</code>
/// A protected DACL that denies Anonymous and Network explicitly and grants only the intended user
/// and Builtin Administrators. A NULL/default DACL is never used.
/// </summary>
internal static class PipeSecurity
{
    public static string BuildSddl(string userSid)
    {
        if (string.IsNullOrEmpty(userSid) || !userSid.StartsWith("S-1-", StringComparison.Ordinal))
        {
            throw new ArgumentException("A valid user SID is required for the pipe DACL.", nameof(userSid));
        }

        return $"D:P(D;;GA;;;AN)(D;;GA;;;NU)(A;;GA;;;{userSid})(A;;GA;;;BA)";
    }

    /// <summary>Parses <paramref name="sddl"/> into a self-relative security descriptor.</summary>
    public static IntPtr CreateDescriptor(string sddl)
    {
        if (!NativeMethods.ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, 1, out var descriptor,
                out _))
        {
            throw new InvalidOperationException(
                $"Failed to build the pipe security descriptor (Win32 error {Marshal.GetLastWin32Error()}).");
        }

        return descriptor;
    }

    public static void FreeDescriptor(IntPtr descriptor)
    {
        if (descriptor != IntPtr.Zero)
        {
            _ = NativeMethods.LocalFree(descriptor);
        }
    }
}