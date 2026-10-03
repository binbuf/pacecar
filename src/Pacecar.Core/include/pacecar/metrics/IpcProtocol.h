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