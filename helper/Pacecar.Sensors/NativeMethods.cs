using System.Runtime.InteropServices;
using System.Text;

namespace Pacecar.Sensors;

/// <summary>
/// Win32 interop used by the secured pipe server. Kept tiny and centralized so the security-relevant
/// calls (explicit DACL, <c>PIPE_REJECT_REMOTE_CLIENTS</c>, client validation, DLL search path) are
/// easy to audit.
/// </summary>
internal static class NativeMethods
{
    public const uint PipeAccessDuplex = 0x00000003;
    public const uint PipeTypeByte = 0x00000000;
    public const uint PipeReadmodeByte = 0x00000000;
    public const uint PipeWait = 0x00000000;
    public const uint PipeRejectRemoteClients = 0x00000008;
    public const int ErrorPipeConnected = 535;
    public const int ErrorBrokenPipe = 109;

    public const uint ProcessQueryLimitedInformation = 0x1000;
    public const uint LoadLibrarySearchSystem32 = 0x00000800;

    [StructLayout(LayoutKind.Sequential)]
    public struct SecurityAttributes
    {
        public int nLength;
        public IntPtr lpSecurityDescriptor;
        public int bInheritHandle;
    }

    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ConvertStringSecurityDescriptorToSecurityDescriptorW(
        string stringSecurityDescriptor, uint stringSecurityDescriptorRevision,
        out IntPtr securityDescriptor, out uint securityDescriptorSize);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateNamedPipeW(
        string name, uint openMode, uint pipeMode, uint maxInstances, uint outBufferSize,
        uint inBufferSize, uint defaultTimeout, ref SecurityAttributes securityAttributes);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateNamedPipeW(
        string name, uint openMode, uint pipeMode, uint maxInstances, uint outBufferSize,
        uint inBufferSize, uint defaultTimeout, IntPtr securityAttributes);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ConnectNamedPipe(IntPtr pipe, IntPtr overlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool DisconnectNamedPipe(IntPtr pipe);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetNamedPipeClientProcessId(IntPtr pipe, out uint clientProcessId);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(uint desiredAccess,
                                            [MarshalAs(UnmanagedType.Bool)] bool inheritHandle,
                                            uint processId);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool QueryFullProcessImageNameW(IntPtr process, uint flags,
                                                         StringBuilder exeName, ref uint size);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool CloseHandle(IntPtr handle);

    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ImpersonateNamedPipeClient(IntPtr pipe);

    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool RevertToSelf();

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetDefaultDllDirectories(uint directoryFlags);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr LocalFree(IntPtr memory);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PeekNamedPipe(IntPtr pipe, IntPtr buffer, uint bufferSize,
                                            IntPtr bytesRead, out uint bytesAvailable,
                                            IntPtr bytesLeftThisMessage);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ReadFile(IntPtr file, byte[] buffer, uint bytesToRead,
                                       out uint bytesRead, IntPtr overlapped);

    public const uint GenericRead = 0x80000000;
    public const uint GenericWrite = 0x40000000;
    public const uint OpenExisting = 3;

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateFileW(string name, uint desiredAccess, uint shareMode,
                                            IntPtr securityAttributes, uint creationDisposition,
                                            uint flagsAndAttributes, IntPtr templateFile);

    public static SecurityAttributes ToSecurityAttributes(IntPtr descriptor)
    {
        return new SecurityAttributes
        {
            nLength = Marshal.SizeOf<SecurityAttributes>(),
            lpSecurityDescriptor = descriptor,
            bInheritHandle = 0,
        };
    }
}