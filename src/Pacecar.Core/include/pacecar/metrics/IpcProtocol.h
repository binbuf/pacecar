#pragma once

// Sensor-helper IPC protocol: the versioned, fixed-layout, language-neutral message schema shared
// between the native UI (client) and `Pacecar.Sensors` (server, task T16).
//
// This header is deliberately self-contained: it includes only the C++ standard library and uses
// fixed-width integer types, `#pragma pack(1)` structs, and `memcpy` so the exact same byte layout
// can be produced by the managed helper. It must never pull in UI or hardware headers.
//
// Wire format (all little-endian on the supported platforms):
//
//   +--------------------- MessageHeader (fixed size) ---------------------+
//   | magic | version | kind | payloadLength | sequence | timestampMs       |
//   +-----------------------------------------------------------------------+
//   | payload (payloadLength bytes)                                          |
//   +------------------------------------------------------------------------+
//
// Bounds: `payloadLength` must not exceed `kMaxPayloadBytes`, and a full message must not exceed
// `kMaxMessageBytes`. Decoding rejects a bad magic, an unknown/unsupported version, an invalid kind,
// an over-length payload, and truncated buffers.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pacecar::ipc
{
// "PCAR" stored little-endian.
inline constexpr std::uint32_t kMagic = 0x50434152u;
inline constexpr std::uint16_t kProtocolVersion = 1u;
inline constexpr std::size_t kMaxPayloadBytes = 4096u;
// Header (24 bytes) plus the maximum payload, with headroom for a future larger header.
inline constexpr std::size_t kMaxMessageBytes = kMaxPayloadBytes + 64u;

#pragma pack(push, 1)

enum class MessageKind : std::uint16_t
{
    Invalid = 0,
    Hello = 1,
    HelloAck = 2,
    SensorSnapshot = 3,
    Goodbye = 4,
    // Frame-time (FPS) extension (task T17). The helper streams decoded present events; the native
    // side derives intervals/FPS. `FrameCaptureCommand` is the client -> helper control message.
    FrameTimeStats = 5,
    FrameCaptureCommand = 6,
};

struct MessageHeader
{
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t kind;
    std::uint32_t payloadLength;
    std::uint32_t sequence;
    std::uint64_t timestampMs;
};

static_assert(sizeof(MessageHeader) == 24, "MessageHeader layout must stay fixed");

enum class SensorId : std::uint16_t
{
    Unknown = 0,
    CpuPackageTemperature = 1,
    CpuCoreTemperature = 2,
    MainboardTemperature = 3,
    DimmTemperature = 4,
    DiskTemperature = 5,
    GpuTemperature = 6,
    FanRpm = 7,
    Voltage = 8,
    Power = 9,
};

struct SensorReading
{
    std::uint16_t id;
    std::uint8_t available;
    std::uint8_t reserved;
    std::int32_t index;
    double value;
};

static_assert(sizeof(SensorReading) == 16, "SensorReading layout must stay fixed");

inline constexpr std::size_t kMaxSensorReadings = 128;

struct SensorSnapshotPayload
{
    std::uint64_t sequence;
    std::uint64_t timestampMs;
    std::uint32_t readingCount;
    std::uint32_t reserved;
    SensorReading readings[kMaxSensorReadings];
};

static_assert(sizeof(SensorSnapshotPayload) <= kMaxPayloadBytes,
              "SensorSnapshotPayload must fit the payload bound");

// Handshake payloads. `Hello` is sent by the UI client on (re)connect; the helper replies with
// `HelloAck`. The header already carries the protocol version, so these carry process identity and
// a capability bitmask the UI can use to explain what the helper can read.
struct HelloPayload
{
    std::uint32_t clientPid;
    std::uint32_t reserved;
};

static_assert(sizeof(HelloPayload) == 8, "HelloPayload layout must stay fixed");

// Sensor families the helper reports it can provide. Values are bit flags.
enum class SensorCapability : std::uint32_t
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
};

struct HelloAckPayload
{
    std::uint32_t helperPid;
    std::uint32_t capabilities;
};

static_assert(sizeof(HelloAckPayload) == 8, "HelloAckPayload layout must stay fixed");

// ---------------------------------------------------------------------------------------------
// Frame-time capture extension (task T17).
//
// `captureState` carries the numeric value of `pacecar::metrics::FrameCaptureState`
// (0 NotCapturing, 1 Capturing, 2 SessionBusy, 3 AccessDenied, 4 ProviderUnavailable, 5 NoTarget,
// 6 Error). The values are duplicated on the managed side; changing them is a wire change.
//
// The helper sends `FrameTimeStats` roughly once per second with the present events decoded since
// the previous message (incremental, bounded by `kMaxFrameEvents`). The native `FrameTimeProcessor`
// turns consecutive presents into intervals and derives FPS/frame-time/percentiles. `event.kind` is
// 1 for a decoded present; CPU/GPU durations are `-1` when unknown.
// ---------------------------------------------------------------------------------------------

inline constexpr std::size_t kMaxFrameEvents = 120;

struct FrameEventPayload
{
    std::uint64_t qpcTicks; // present timestamp in `qpcFrequency` ticks
    std::int64_t cpuTicks;  // CPU frame duration; -1 when unknown
    std::int64_t gpuTicks;  // GPU frame duration; -1 when unknown
    std::uint32_t pid;
    std::uint16_t kind; // 1 = present
    std::uint16_t reserved;
};

static_assert(sizeof(FrameEventPayload) == 32, "FrameEventPayload layout must stay fixed");

struct FrameTimeStatsPayload
{
    std::uint64_t sequence;
    std::uint64_t timestampMs;
    std::uint64_t qpcFrequency; // ticks per second (10,000,000 for ETW 100 ns timestamps)
    std::uint32_t targetPid;
    std::uint32_t captureState;
    std::uint32_t eventCount; // number of valid entries in `events`
    std::uint32_t reserved;
    FrameEventPayload events[kMaxFrameEvents];
};

static_assert(sizeof(FrameTimeStatsPayload) <= kMaxPayloadBytes,
              "FrameTimeStatsPayload must fit the payload bound");

// Client -> helper capture control. `command` is 0 (stop) or 1 (start); `targetPid` is the process
// to capture (ignored on stop). The helper answers through `captureState` in the next stats frame.
enum class FrameCaptureCommand : std::uint32_t
{
    Stop = 0,
    Start = 1,
};

struct FrameCaptureCommandPayload
{
    std::uint32_t command;
    std::uint32_t targetPid;
};

static_assert(sizeof(FrameCaptureCommandPayload) == 8,
              "FrameCaptureCommandPayload layout must stay fixed");

#pragma pack(pop)

struct DecodedMessage
{
    MessageHeader header{};
    const std::uint8_t* payload = nullptr;
    std::size_t payloadSize = 0;
};

// Serializes `header` + `payload` into `out`. Returns the number of bytes written, or 0 when the
// buffer is too small (nothing is written in that case).
template <typename T>
inline std::size_t EncodeMessage(MessageKind kind,
                                 const T& payload,
                                 std::uint32_t sequence,
                                 std::uint64_t timestampMs,
                                 std::uint8_t* out,
                                 std::size_t outCapacity) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>, "IPC payloads must be trivially copyable");
    const std::size_t total = sizeof(MessageHeader) + sizeof(T);
    if (out == nullptr || outCapacity < total)
    {
        return 0;
    }
    MessageHeader header{};
    header.magic = kMagic;
    header.version = kProtocolVersion;
    header.kind = static_cast<std::uint16_t>(kind);
    header.payloadLength = static_cast<std::uint32_t>(sizeof(T));
    header.sequence = sequence;
    header.timestampMs = timestampMs;
    std::memcpy(out, &header, sizeof(header));
    std::memcpy(out + sizeof(header), &payload, sizeof(T));
    return total;
}

// Validates and splits a received buffer into header + payload view. Returns false (and leaves
// `out` unspecified) for malformed input.
inline bool DecodeMessage(const std::uint8_t* data, std::size_t size, DecodedMessage& out) noexcept
{
    if (data == nullptr || size < sizeof(MessageHeader))
    {
        return false;
    }
    MessageHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != kMagic)
    {
        return false;
    }
    if (header.version != kProtocolVersion)
    {
        return false;
    }
    if (header.kind == static_cast<std::uint16_t>(MessageKind::Invalid))
    {
        return false;
    }
    if (header.payloadLength > kMaxPayloadBytes)
    {
        return false;
    }
    const std::size_t expected = sizeof(MessageHeader) + header.payloadLength;
    if (expected > kMaxMessageBytes || size < expected)
    {
        return false;
    }
    out.header = header;
    out.payload = data + sizeof(MessageHeader);
    out.payloadSize = header.payloadLength;
    return true;
}

// Reinterprets the decoded payload as `T`. Fails when the payload length does not match `sizeof(T)`
// exactly.
template <typename T>
inline bool DecodePayload(const DecodedMessage& message, T& out) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>, "IPC payloads must be trivially copyable");
    if (message.payload == nullptr || message.payloadSize != sizeof(T))
    {
        return false;
    }
    std::memcpy(&out, message.payload, sizeof(T));
    return true;
}
} // namespace pacecar::ipc