using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Pacecar.Sensors;

/// <summary>
/// Secured named-pipe server for the UI client. One instance at a time: it creates the pipe with an
/// explicit restrictive DACL and <c>PIPE_REJECT_REMOTE_CLIENTS</c>, validates the connecting process,
/// answers the Hello handshake, and streams fixed-layout snapshots on a timer. All messages come
/// from <see cref="IpcProtocol"/> and are size-bounded by construction.
/// </summary>
internal sealed class PipeServer
{
    private readonly string _pipeName;
    private readonly string _userSid;
    private uint _sequence;

    public PipeServer(string pipeName, string userSid)
    {
        _pipeName = pipeName;
        _userSid = userSid;
    }

    public static string BuildPipeName(string userSid)
    {
        return string.IsNullOrEmpty(userSid)
            ? @"\\.\pipe\Pacecar.Sensors"
            : @"\\.\pipe\Pacecar.Sensors." + userSid;
    }

    public void Run(Func<IReadOnlyList<IpcProtocol.Reading>> readingsFactory, TimeSpan interval,
                    CancellationToken token)
    {
        var sddl = PipeSecurity.BuildSddl(_userSid);

        while (!token.IsCancellationRequested)
        {
            var descriptor = PipeSecurity.CreateDescriptor(sddl);
            nint handle;
            try
            {
                var attributes = NativeMethods.ToSecurityAttributes(descriptor);
                handle = NativeMethods.CreateNamedPipeW(
                    _pipeName,
                    NativeMethods.PipeAccessDuplex,
                    NativeMethods.PipeTypeByte | NativeMethods.PipeReadmodeByte | NativeMethods.PipeWait |
                    NativeMethods.PipeRejectRemoteClients,
                    1, 65536, 65536, 0, ref attributes);
            }
            finally
            {
                PipeSecurity.FreeDescriptor(descriptor);
            }

            if (handle == new nint(-1))
            {
                throw new InvalidOperationException(
                    $"CreateNamedPipeW failed (Win32 error {Marshal.GetLastWin32Error()}).");
            }

            try
            {
                if (!NativeMethods.ConnectNamedPipe(handle, nint.Zero))
                {
                    var error = Marshal.GetLastWin32Error();
                    if (error != NativeMethods.ErrorPipeConnected)
                    {
                        continue;
                    }
                }

                var client = ClientValidator.Validate(handle, _userSid);
                Console.WriteLine(client.Accepted
                    ? $"[pipe] accepted pid={client.ProcessId} image={client.ImagePath}"
                    : $"[pipe] rejected pid={client.ProcessId}: {client.Reason}");

                if (!client.Accepted)
                {
                    continue;
                }

                using var stream = new FileStream(new SafeFileHandle(handle, ownsHandle: false),
                                                  FileAccess.ReadWrite, 65536, isAsync: false);
                if (!ReadHello(stream))
                {
                    continue;
                }

                var capabilities = CapabilitiesOf(readingsFactory());
                stream.Write(IpcProtocol.EncodeHelloAck((uint)Environment.ProcessId, capabilities,
                                                        NextSequence(), NowMs()));
                stream.Flush();

                StreamSnapshots(stream, readingsFactory, interval, token);
            }
            catch (IOException ex)
            {
                Console.WriteLine($"[pipe] client disconnected: {ex.Message}");
            }
            finally
            {
                _ = NativeMethods.DisconnectNamedPipe(handle);
                _ = NativeMethods.CloseHandle(handle);
            }
        }
    }

    private void StreamSnapshots(FileStream stream,
                                 Func<IReadOnlyList<IpcProtocol.Reading>> readingsFactory,
                                 TimeSpan interval, CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            var message = IpcProtocol.EncodeSnapshot(readingsFactory(), NextSequence(), NowMs());
            stream.Write(message, 0, message.Length);
            stream.Flush();

            if (token.WaitHandle.WaitOne(interval))
            {
                return;
            }
        }
    }

    private static bool ReadHello(FileStream stream)
    {
        var header = new byte[IpcProtocol.HeaderSize];
        if (!ReadExact(stream, header, header.Length))
        {
            return false;
        }

        var magic = BitConverter.ToUInt32(header, 0);
        var version = BitConverter.ToUInt16(header, 4);
        var kind = BitConverter.ToUInt16(header, 6);
        var payloadLength = BitConverter.ToUInt32(header, 8);

        if (magic != IpcProtocol.Magic || version != IpcProtocol.ProtocolVersion ||
            kind != IpcProtocol.MessageKindHello || payloadLength > IpcProtocol.MaxPayloadBytes)
        {
            Console.WriteLine("[pipe] rejected a malformed Hello header");
            return false;
        }

        var payload = new byte[payloadLength];
        return payloadLength == 0 || ReadExact(stream, payload, payload.Length);
    }

    private static bool ReadExact(Stream stream, byte[] buffer, int count)
    {
        var offset = 0;
        while (offset < count)
        {
            var read = stream.Read(buffer, offset, count - offset);
            if (read <= 0)
            {
                return false;
            }

            offset += read;
        }

        return true;
    }

    private static IpcProtocol.Capability CapabilitiesOf(IReadOnlyList<IpcProtocol.Reading> readings)
    {
        var capabilities = IpcProtocol.Capability.None;
        foreach (var reading in readings)
        {
            capabilities |= reading.Id switch
            {
                IpcProtocol.SensorIdCpuPackageTemperature => IpcProtocol.Capability.CpuPackageTemp,
                IpcProtocol.SensorIdCpuCoreTemperature => IpcProtocol.Capability.CpuCoreTemp,
                IpcProtocol.SensorIdMainboardTemperature => IpcProtocol.Capability.MainboardTemp,
                IpcProtocol.SensorIdDimmTemperature => IpcProtocol.Capability.DimmTemp,
                IpcProtocol.SensorIdDiskTemperature => IpcProtocol.Capability.DiskTemp,
                IpcProtocol.SensorIdFanRpm => IpcProtocol.Capability.FanRpm,
                IpcProtocol.SensorIdVoltage => IpcProtocol.Capability.Voltage,
                IpcProtocol.SensorIdPower => IpcProtocol.Capability.Power,
                _ => IpcProtocol.Capability.None,
            };
        }

        return capabilities;
    }

    private uint NextSequence()
    {
        return ++_sequence;
    }

    private static ulong NowMs()
    {
        return (ulong)Environment.TickCount64;
    }
}