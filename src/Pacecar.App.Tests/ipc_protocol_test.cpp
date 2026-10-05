#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "pacecar/metrics/IpcProtocol.h"

namespace
{
using namespace pacecar::ipc;

TEST(IpcProtocol, FixedLayoutIsStable)
{
    EXPECT_EQ(kMagic, 0x50434152u);
    EXPECT_EQ(kProtocolVersion, 2u);
    EXPECT_EQ(sizeof(MessageHeader), 24u);
    EXPECT_EQ(sizeof(SensorReading), 16u);
    EXPECT_LE(sizeof(SensorSnapshotPayload), kMaxPayloadBytes);
    // Frame-time v2: the event gained the swap-chain identity and the stats gained health counters.
    EXPECT_EQ(sizeof(FrameEventPayload), 40u);
    EXPECT_EQ(sizeof(FrameTimeStatsPayload), 48u + (40u * kMaxFrameEvents));
    EXPECT_LE(sizeof(FrameTimeStatsPayload), kMaxPayloadBytes);
}

TEST(IpcProtocol, RoundTripsSensorSnapshot)
{
    SensorSnapshotPayload payload{};
    payload.sequence = 7;
    payload.timestampMs = 123456;
    payload.readingCount = 2;
    payload.readings[0].id = static_cast<std::uint16_t>(SensorId::CpuPackageTemperature);
    payload.readings[0].index = 0;
    payload.readings[0].available = 1;
    payload.readings[0].value = 61.5;
    payload.readings[1].id = static_cast<std::uint16_t>(SensorId::FanRpm);
    payload.readings[1].index = 0;
    payload.readings[1].available = 1;
    payload.readings[1].value = 1450.0;

    std::array<std::uint8_t, kMaxMessageBytes> buffer{};
    const std::size_t written =
        EncodeMessage(MessageKind::SensorSnapshot, payload, 42, 123456, buffer.data(), buffer.size());
    ASSERT_EQ(written, sizeof(MessageHeader) + sizeof(SensorSnapshotPayload));

    DecodedMessage decoded{};
    ASSERT_TRUE(DecodeMessage(buffer.data(), written, decoded));
    EXPECT_EQ(decoded.header.magic, kMagic);
    EXPECT_EQ(decoded.header.version, kProtocolVersion);
    EXPECT_EQ(decoded.header.kind, static_cast<std::uint16_t>(MessageKind::SensorSnapshot));
    EXPECT_EQ(decoded.header.sequence, 42u);
    EXPECT_EQ(decoded.header.timestampMs, 123456u);
    EXPECT_EQ(decoded.payloadSize, sizeof(SensorSnapshotPayload));

    SensorSnapshotPayload roundTripped{};
    ASSERT_TRUE(DecodePayload(decoded, roundTripped));
    EXPECT_EQ(roundTripped.sequence, 7u);
    EXPECT_EQ(roundTripped.readingCount, 2u);
    EXPECT_DOUBLE_EQ(roundTripped.readings[0].value, 61.5);
    EXPECT_DOUBLE_EQ(roundTripped.readings[1].value, 1450.0);
}

TEST(IpcProtocol, RejectsBadMagic)
{
    SensorSnapshotPayload payload{};
    std::array<std::uint8_t, kMaxMessageBytes> buffer{};
    const std::size_t written =
        EncodeMessage(MessageKind::SensorSnapshot, payload, 1, 1, buffer.data(), buffer.size());
    ASSERT_GT(written, 0u);

    buffer[0] ^= 0xFF;
    DecodedMessage decoded{};
    EXPECT_FALSE(DecodeMessage(buffer.data(), written, decoded));
}

TEST(IpcProtocol, RejectsBadVersion)
{
    SensorSnapshotPayload payload{};
    std::array<std::uint8_t, kMaxMessageBytes> buffer{};
    const std::size_t written =
        EncodeMessage(MessageKind::SensorSnapshot, payload, 1, 1, buffer.data(), buffer.size());
    ASSERT_GT(written, 0u);

    MessageHeader header{};
    std::memcpy(&header, buffer.data(), sizeof(header));
    header.version = static_cast<std::uint16_t>(kProtocolVersion + 1);
    std::memcpy(buffer.data(), &header, sizeof(header));

    DecodedMessage decoded{};
    EXPECT_FALSE(DecodeMessage(buffer.data(), written, decoded));
}

TEST(IpcProtocol, RejectsInvalidKind)
{
    SensorSnapshotPayload payload{};
    std::array<std::uint8_t, kMaxMessageBytes> buffer{};
    const std::size_t written =
        EncodeMessage(MessageKind::SensorSnapshot, payload, 1, 1, buffer.data(), buffer.size());
    ASSERT_GT(written, 0u);

    MessageHeader header{};
    std::memcpy(&header, buffer.data(), sizeof(header));
    header.kind = static_cast<std::uint16_t>(MessageKind::Invalid);
    std::memcpy(buffer.data(), &header, sizeof(header));

    DecodedMessage decoded{};
    EXPECT_FALSE(DecodeMessage(buffer.data(), written, decoded));
}

TEST(IpcProtocol, RejectsOverlengthPayload)
{
    SensorSnapshotPayload payload{};
    std::array<std::uint8_t, kMaxMessageBytes> buffer{};
    const std::size_t written =
        EncodeMessage(MessageKind::SensorSnapshot, payload, 1, 1, buffer.data(), buffer.size());
    ASSERT_GT(written, 0u);

    MessageHeader header{};
    std::memcpy(&header, buffer.data(), sizeof(header));
    header.payloadLength = static_cast<std::uint32_t>(kMaxPayloadBytes + 1);
    std::memcpy(buffer.data(), &header, sizeof(header));

    DecodedMessage decoded{};
    EXPECT_FALSE(DecodeMessage(buffer.data(), written, decoded));
}

TEST(IpcProtocol, RejectsTruncatedBuffer)
{
    SensorSnapshotPayload payload{};
    std::array<std::uint8_t, kMaxMessageBytes> buffer{};
    const std::size_t written =
        EncodeMessage(MessageKind::SensorSnapshot, payload, 1, 1, buffer.data(), buffer.size());
    ASSERT_GT(written, 0u);

    DecodedMessage decoded{};
    EXPECT_FALSE(DecodeMessage(buffer.data(), sizeof(MessageHeader) - 1, decoded));
    EXPECT_FALSE(DecodeMessage(buffer.data(), written - 1, decoded));
}

TEST(IpcProtocol, EncodeFailsWhenBufferTooSmall)
{
    SensorSnapshotPayload payload{};
    std::array<std::uint8_t, sizeof(MessageHeader)> tiny{};
    EXPECT_EQ(EncodeMessage(MessageKind::SensorSnapshot, payload, 1, 1, tiny.data(), tiny.size()),
              0u);
    EXPECT_EQ(EncodeMessage(MessageKind::SensorSnapshot, payload, 1, 1, nullptr, 0), 0u);
}
} // namespace