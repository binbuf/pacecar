using System.Buffers.Binary;

namespace Pacecar.Sensors;

/// <summary>
/// Managed counterpart of <c>Pacecar.Core/Metrics/IpcProtocol.h</c>. The byte layout is fixed and
/// versioned: every field is little-endian and the header/reading sizes are asserted by the native
/// tests. Any wire change must bump <see cref="ProtocolVersion"/> on both sides.
/// </summary>
internal static class IpcProtocol
{
    public const uint Magic = 0x50434152u; // "PCAR"
    public const ushort ProtocolVersion = 2;
    public const int HeaderSize = 24;
    public const int MaxPayloadBytes = 4096;
    public const int MaxMessageBytes = MaxPayloadBytes + 64;
    public const int MaxSensorReadings = 128;
    public const int SensorReadingSize = 16;
    public const int SnapshotPayloadSize = 8 + 8 + 4 + 4 + (SensorReadingSize * MaxSensorReadings);
    public const int HelloPayloadSize = 8;
    public const int HelloAckPayloadSize = 8;

    public const int MessageKindInvalid = 0;
    public const int MessageKindHello = 1;
    public const int MessageKindHelloAck = 2;
    public const int MessageKindSensorSnapshot = 3;
    public const int MessageKindGoodbye = 4;
    public const int MessageKindFrameTimeStats = 5;
    public const int MessageKindFrameCaptureCommand = 6;

    // Frame-time extension (task T17). Must match `Pacecar.Core/Metrics/IpcProtocol.h`.
    public const int MaxFrameEvents = 100;
    public const int FrameEventSize = 40;
    public const int FrameTimeStatsFixedSize = 8 + 8 + 8 + 4 + 4 + 4 + 4 + 4 + 4;
    public const int FrameTimeStatsPayloadSize = FrameTimeStatsFixedSize +
                                                (FrameEventSize * MaxFrameEvents);
    public const int FrameCaptureCommandPayloadSize = 8;

    public const int FrameEventKindPresent = 1;

    // Matches `pacecar::metrics::FrameCaptureState`.
    public const uint CaptureStateNotCapturing = 0;
    public const uint CaptureStateCapturing = 1;
    public const uint CaptureStateSessionBusy = 2;
    public const uint CaptureStateAccessDenied = 3;
    public const uint CaptureStateProviderUnavailable = 4;
    public const uint CaptureStateNoTarget = 5;
    public const uint CaptureStateError = 6;

    public const uint FrameCaptureCommandStop = 0;
    public const uint FrameCaptureCommandStart = 1;

    public const ushort SensorIdUnknown = 0;
    public const ushort SensorIdCpuPackageTemperature = 1;
    public const ushort SensorIdCpuCoreTemperature = 2;
    public const ushort SensorIdMainboardTemperature = 3;
    public const ushort SensorIdDimmTemperature = 4;
    public const ushort SensorIdDiskTemperature = 5;
    public const ushort SensorIdGpuTemperature = 6;
    public const ushort SensorIdFanRpm = 7;
    public const ushort SensorIdVoltage = 8;
    public const ushort SensorIdPower = 9;

    [Flags]
    public enum Capability : uint
    {
        None = 0,
        CpuPackageTemp = 1u << 0,
        CpuCoreTemp = 1u << 1,
        MainboardTemp = 1u << 2,
        DimmTemp = 1u << 3,
        DiskTemp = 1u << 4,
        FanRpm = 1u << 5,
        Voltage = 1u << 6,
        Power = 1u << 7,
    }

    public readonly record struct Reading(ushort Id, bool Available, int Index, double Value);

    /// <summary>One decoded present event (task T17). Durations are -1 when unknown.</summary>
    public readonly record struct FrameEvent(ulong QpcTicks, long CpuTicks, long GpuTicks,
                                             ulong SwapChain, uint Pid, ushort Kind);

    private static void WriteHeader(Span<byte> buffer, int kind, int payloadLength, uint sequence,
                                    ulong timestampMs)
    {
        BinaryPrimitives.WriteUInt32LittleEndian(buffer[0..], Magic);
        BinaryPrimitives.WriteUInt16LittleEndian(buffer[4..], ProtocolVersion);
        BinaryPrimitives.WriteUInt16LittleEndian(buffer[6..], (ushort)kind);
        BinaryPrimitives.WriteUInt32LittleEndian(buffer[8..], (uint)payloadLength);
        BinaryPrimitives.WriteUInt32LittleEndian(buffer[12..], sequence);
        BinaryPrimitives.WriteUInt64LittleEndian(buffer[16..], timestampMs);
    }

    public static byte[] EncodeHello(uint clientPid, uint sequence, ulong timestampMs)
    {
        var message = new byte[HeaderSize + HelloPayloadSize];
        WriteHeader(message, MessageKindHello, HelloPayloadSize, sequence, timestampMs);
        BinaryPrimitives.WriteUInt32LittleEndian(message.AsSpan(HeaderSize), clientPid);
        BinaryPrimitives.WriteUInt32LittleEndian(message.AsSpan(HeaderSize + 4), 0);
        return message;
    }

    public static byte[] EncodeHelloAck(uint helperPid, Capability capabilities, uint sequence,
                                        ulong timestampMs)
    {
        var message = new byte[HeaderSize + HelloAckPayloadSize];
        WriteHeader(message, MessageKindHelloAck, HelloAckPayloadSize, sequence, timestampMs);
        BinaryPrimitives.WriteUInt32LittleEndian(message.AsSpan(HeaderSize), helperPid);
        BinaryPrimitives.WriteUInt32LittleEndian(message.AsSpan(HeaderSize + 4), (uint)capabilities);
        return message;
    }

    /// <summary>
    /// Encodes a fixed-layout snapshot. Up to <see cref="MaxSensorReadings"/> readings are packed from
    /// the front; unused slots stay zero and <c>readingCount</c> records the real count.
    /// </summary>
    public static byte[] EncodeSnapshot(IReadOnlyList<Reading> readings, uint sequence,
                                        ulong timestampMs)
    {
        var message = new byte[HeaderSize + SnapshotPayloadSize];
        WriteHeader(message, MessageKindSensorSnapshot, SnapshotPayloadSize, sequence, timestampMs);

        var payload = message.AsSpan(HeaderSize);
        BinaryPrimitives.WriteUInt64LittleEndian(payload[0..], sequence);
        BinaryPrimitives.WriteUInt64LittleEndian(payload[8..], timestampMs);

        var count = (uint)Math.Min(readings.Count, MaxSensorReadings);
        BinaryPrimitives.WriteUInt32LittleEndian(payload[16..], count);
        BinaryPrimitives.WriteUInt32LittleEndian(payload[20..], 0);

        for (var i = 0; i < count; i++)
        {
            var offset = 24 + (i * SensorReadingSize);
            var reading = readings[i];
            BinaryPrimitives.WriteUInt16LittleEndian(payload[offset..], reading.Id);
            payload[offset + 2] = reading.Available ? (byte)1 : (byte)0;
            payload[offset + 3] = 0;
            BinaryPrimitives.WriteInt32LittleEndian(payload[(offset + 4)..], reading.Index);
            BinaryPrimitives.WriteUInt64LittleEndian(payload[(offset + 8)..],
                BitConverter.DoubleToUInt64Bits(reading.Value));
        }

        return message;
    }

    /// <summary>
    /// Encodes an incremental batch of present events plus the current capture state. The native
    /// side derives intervals/FPS from consecutive events, so the helper never sends the same event
    /// twice.
    /// </summary>
    public static byte[] EncodeFrameTimeStats(IReadOnlyList<FrameEvent> events, uint captureState,
                                              uint targetPid, ulong qpcFrequency, uint eventsLost,
                                              uint queueDrops, uint sequence, ulong timestampMs)
    {
        var message = new byte[HeaderSize + FrameTimeStatsPayloadSize];
        WriteHeader(message, MessageKindFrameTimeStats, FrameTimeStatsPayloadSize, sequence,
                    timestampMs);

        var payload = message.AsSpan(HeaderSize);
        BinaryPrimitives.WriteUInt64LittleEndian(payload[0..], sequence);
        BinaryPrimitives.WriteUInt64LittleEndian(payload[8..], timestampMs);
        BinaryPrimitives.WriteUInt64LittleEndian(payload[16..], qpcFrequency);
        BinaryPrimitives.WriteUInt32LittleEndian(payload[24..], targetPid);
        BinaryPrimitives.WriteUInt32LittleEndian(payload[28..], captureState);

        var count = (uint)Math.Min(events.Count, MaxFrameEvents);
        BinaryPrimitives.WriteUInt32LittleEndian(payload[32..], count);
        BinaryPrimitives.WriteUInt32LittleEndian(payload[36..], eventsLost);
        BinaryPrimitives.WriteUInt32LittleEndian(payload[40..], queueDrops);
        BinaryPrimitives.WriteUInt32LittleEndian(payload[44..], 0);

        for (var i = 0; i < count; i++)
        {
            var offset = FrameTimeStatsFixedSize + (i * FrameEventSize);
            var ev = events[i];
            BinaryPrimitives.WriteUInt64LittleEndian(payload[offset..], ev.QpcTicks);
            BinaryPrimitives.WriteInt64LittleEndian(payload[(offset + 8)..], ev.CpuTicks);
            BinaryPrimitives.WriteInt64LittleEndian(payload[(offset + 16)..], ev.GpuTicks);
            BinaryPrimitives.WriteUInt64LittleEndian(payload[(offset + 24)..], ev.SwapChain);
            BinaryPrimitives.WriteUInt32LittleEndian(payload[(offset + 32)..], ev.Pid);
            BinaryPrimitives.WriteUInt16LittleEndian(payload[(offset + 36)..], ev.Kind);
            BinaryPrimitives.WriteUInt16LittleEndian(payload[(offset + 38)..], 0);
        }

        return message;
    }

    /// <summary>Decodes a client capture command; returns false for a malformed payload.</summary>
    public static bool TryDecodeCaptureCommand(ReadOnlySpan<byte> payload, out uint command,
                                               out uint targetPid)
    {
        command = 0;
        targetPid = 0;
        if (payload.Length != FrameCaptureCommandPayloadSize)
        {
            return false;
        }
        command = BinaryPrimitives.ReadUInt32LittleEndian(payload[0..]);
        targetPid = BinaryPrimitives.ReadUInt32LittleEndian(payload[4..]);
        return true;
    }
}