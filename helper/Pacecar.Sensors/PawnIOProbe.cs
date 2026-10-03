using Microsoft.Win32;

namespace Pacecar.Sensors;

/// <summary>
/// Detects the signed PawnIO driver (design ref 06-security-distribution.md). Pacecar never bundles
/// a kernel driver and never ships or uses WinRing0. If PawnIO is absent, deep sensors are explained
/// rather than silently failing.
/// </summary>
internal static class PawnIOProbe
{
    private const string DevicePath = @"\\.\GLOBALROOT\Device\PawnIO";
    private const string UninstallKey64 =
        @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO";
    private const string UninstallKeyWow =
        @"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO";

    public static bool IsPresent()
    {
        if (DevicePresent())
        {
            return true;
        }

        return RegistryKeyExists(RegistryView.Registry64, UninstallKey64) ||
               RegistryKeyExists(RegistryView.Registry32, UninstallKeyWow);
    }

    public static string Guidance(bool present)
    {
        return present
            ? "PawnIO detected; deep sensors are available."
            : "PawnIO not installed; install it from pawnio.eu for package/board/fan sensors.";
    }

    private static bool DevicePresent()
    {
        var handle = NativeMethods.CreateFileW(DevicePath,
            NativeMethods.GenericRead | NativeMethods.GenericWrite, 0, IntPtr.Zero,
            NativeMethods.OpenExisting, 0, IntPtr.Zero);
        if (handle == new IntPtr(-1))
        {
            // Access-denied beyond elevation still proves the device exists.
            var error = System.Runtime.InteropServices.Marshal.GetLastWin32Error();
            return error == 5 /* ERROR_ACCESS_DENIED */ || error == 32 /* ERROR_SHARING_VIOLATION */;
        }

        _ = NativeMethods.CloseHandle(handle);
        return true;
    }

    private static bool RegistryKeyExists(RegistryView view, string subKey)
    {
        try
        {
            using var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
            using var key = baseKey.OpenSubKey(subKey);
            return key is not null;
        }
        catch (Exception)
        {
            return false;
        }
    }
}