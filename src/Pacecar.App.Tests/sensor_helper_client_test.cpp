#include <gtest/gtest.h>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

#include "pacecar/metrics/IpcProtocol.h"
#include "pacecar/metrics/SensorHelperClient.h"
#include "pacecar/metrics/SensorHelperProvider.h"

namespace
{
using namespace pacecar::ipc;
using pacecar::metrics::HelperState;
using pacecar::metrics::SensorHelperClient;

std::wstring UniquePipeName()
{
    static std::uint32_t counter = 0;
    wchar_t buffer[128] = {};
    swprintf_s(buffer, L"\\\\.\\pipe\\PacecarTest.%lu.%lu",
               static_cast<unsigned long>(GetCurrentProcessId()),
               static_cast<unsigned long>(++counter));
    return buffer;
}

class PipeServer
{
  public:
    explicit PipeServer(const std::wstring& name)
    {
        handle_ = CreateNamedPipeW(
            name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536,
            65536, 0, nullptr);
    }

    ~PipeServer()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }
    }

    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    [[nodiscard]] bool Valid() const noexcept
    {
        return handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE Get() const noexcept
    {
        return handle_;
    }

    bool Accept()
    {
        if (ConnectNamedPipe(handle_, nullptr) != FALSE)
        {
            return true;
        }
        return GetLastError() == ERROR_PIPE_CONNECTED;
    }

    bool ReadSome(std::uint8_t* out, std::size_t capacity, DWORD& read)
    {
        return ReadFile(handle_, out, static_cast<DWORD>(capacity), &read, nullptr) != FALSE;
    }

    bool Write(const void* data, std::size_t size)
    {
        DWORD written = 0;
        return WriteFile(handle_, data, static_cast<DWORD>(size), &written, nullptr) != FALSE &&
               written == static_cast<DWORD>(size);
    }

    template <typename T>
    bool WriteMessage(MessageKind kind, const T& payload, std::uint32_t sequence,
                      std::uint64_t timestamp)
    {
        std::array<std::uint8_t, kMaxMessageBytes> buffer{};
        const std::size_t size =
            EncodeMessage(kind, payload, sequence, timestamp, buffer.data(), buffer.size());
        return size > 0 && Write(buffer.data(), size);
    }

  private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

TEST(SensorHelperClient, BackoffAndUnavailableTransitions)
{
    SensorHelperClient client(L"\\\\.\\pipe\\PacecarTest.DoesNotExist");
    EXPECT_EQ(client.State(), HelperState::Disconnected);
    EXPECT_FALSE(client.HasFreshData());
    EXPECT_EQ(client.NextAttemptMs(), 0u);

    EXPECT_FALSE(client.Pump(0));
    EXPECT_EQ(client.NextAttemptMs(), SensorHelperClient::kInitialBackoffMs);

    // Before the backoff elapses, no attempt is made and the deadline is unchanged.
    EXPECT_FALSE(client.Pump(500));
    EXPECT_EQ(client.NextAttemptMs(), SensorHelperClient::kInitialBackoffMs);

    // Attempting at the deadline doubles the backoff.
    EXPECT_FALSE(client.Pump(SensorHelperClient::kInitialBackoffMs));
    EXPECT_EQ(client.NextAttemptMs(),
              SensorHelperClient::kInitialBackoffMs + 2 * SensorHelperClient::kInitialBackoffMs);

    // Backoff is capped at kMaxBackoffMs.
    client.Disconnect();
    std::uint64_t now = 0;
    EXPECT_FALSE(client.Pump(now));
    for (int i = 0; i < 12; ++i)
    {
        now = client.NextAttemptMs();
        EXPECT_FALSE(client.Pump(now));
        EXPECT_LE(client.NextAttemptMs() - now, SensorHelperClient::kMaxBackoffMs);
    }
    EXPECT_EQ(client.NextAttemptMs() - now, SensorHelperClient::kMaxBackoffMs);
    EXPECT_EQ(client.State(), HelperState::Disconnected);
}

TEST(SensorHelperClient, DisabledClientNeverConnectsAndReconnectsWhenEnabled)
{
    SensorHelperClient client(L"\\\\.\\pipe\\PacecarTest.DoesNotExist");
    EXPECT_TRUE(client.Enabled());

    client.SetEnabled(false);
    EXPECT_FALSE(client.Enabled());
    // Disabled: `Pump` is a no-op and does not even schedule a reconnect.
    EXPECT_FALSE(client.Pump(0));
    EXPECT_EQ(client.NextAttemptMs(), 0u);
    EXPECT_FALSE(client.Pump(1000000));
    EXPECT_EQ(client.State(), HelperState::Disconnected);

    // Re-enabling arms an immediate attempt.
    client.SetEnabled(true);
    EXPECT_TRUE(client.Enabled());
    EXPECT_EQ(client.NextAttemptMs(), 0u);
    EXPECT_FALSE(client.Pump(0));
    EXPECT_EQ(client.NextAttemptMs(), SensorHelperClient::kInitialBackoffMs);
}

TEST(SensorHelperClient, RoundTripsHandshakeAndSnapshot)
{
    const std::wstring name = UniquePipeName();
    PipeServer server(name);
    ASSERT_TRUE(server.Valid());

    SensorHelperClient client(name);
    ASSERT_TRUE(client.Pump(0));
    ASSERT_TRUE(server.Accept());

    // Consume the client's Hello.
    std::array<std::uint8_t, kMaxMessageBytes> hello{};
    DWORD helloBytes = 0;
    ASSERT_TRUE(server.ReadSome(hello.data(), hello.size(), helloBytes));
    ASSERT_GE(helloBytes, sizeof(MessageHeader));

    HelloAckPayload ack{};
    ack.helperPid = 4242;
    ack.capabilities = static_cast<std::uint32_t>(SensorCapability::CpuPackageTemp) |
                       static_cast<std::uint32_t>(SensorCapability::FanRpm);
    ASSERT_TRUE(server.WriteMessage(MessageKind::HelloAck, ack, 1, 10));

    SensorSnapshotPayload snapshot{};
    snapshot.sequence = 9;
    snapshot.timestampMs = 1000;
    snapshot.readingCount = 3;
    snapshot.readings[0].id = static_cast<std::uint16_t>(SensorId::CpuPackageTemperature);
    snapshot.readings[0].available = 1;
    snapshot.readings[0].value = 61.5;
    snapshot.readings[1].id = static_cast<std::uint16_t>(SensorId::MainboardTemperature);
    snapshot.readings[1].available = 1;
    snapshot.readings[1].value = 34.0;
    snapshot.readings[2].id = static_cast<std::uint16_t>(SensorId::FanRpm);
    snapshot.readings[2].available = 1;
    snapshot.readings[2].value = 1200.0;
    ASSERT_TRUE(server.WriteMessage(MessageKind::SensorSnapshot, snapshot, 9, 1000));

    ASSERT_TRUE(client.Pump(1));
    EXPECT_EQ(client.State(), HelperState::Connected);
    EXPECT_TRUE(client.HasFreshData());
    EXPECT_EQ(client.SnapshotCount(), 1u);
    EXPECT_EQ(client.Capabilities(), ack.capabilities);

    pacecar::metrics::MetricsSnapshot out{};
    out.tickIndex = 7;
    client.ApplyTo(out);
    EXPECT_DOUBLE_EQ(out.cpu.packageTemperatureC, 61.5);
    EXPECT_TRUE(out.cpu.temperatureStatus.available);
    EXPECT_FALSE(out.cpu.temperatureIsAcpi);
    EXPECT_DOUBLE_EQ(out.board.mainboardTemperatureC, 34.0);
    EXPECT_TRUE(out.board.status.available);
    EXPECT_EQ(out.fan.highestRpm, 1200);
    EXPECT_EQ(out.fan.fanCount, 1u);
}

TEST(SensorHelperClient, FanAggregationPicksHighestAndAverage)
{
    const std::wstring name = UniquePipeName();
    PipeServer server(name);
    ASSERT_TRUE(server.Valid());

    SensorHelperClient client(name);
    ASSERT_TRUE(client.Pump(0));
    ASSERT_TRUE(server.Accept());
    std::array<std::uint8_t, kMaxMessageBytes> hello{};
    DWORD helloBytes = 0;
    ASSERT_TRUE(server.ReadSome(hello.data(), hello.size(), helloBytes));

    SensorSnapshotPayload snapshot{};
    snapshot.readingCount = 3;
    for (std::size_t i = 0; i < 3; ++i)
    {
        snapshot.readings[i].id = static_cast<std::uint16_t>(SensorId::FanRpm);
        snapshot.readings[i].available = 1;
    }
    snapshot.readings[0].value = 1000.0;
    snapshot.readings[1].value = 2050.0;
    snapshot.readings[2].value = 1450.0;
    ASSERT_TRUE(server.WriteMessage(MessageKind::SensorSnapshot, snapshot, 1, 1));
    ASSERT_TRUE(client.Pump(1));

    pacecar::metrics::MetricsSnapshot out{};
    client.ApplyTo(out);
    EXPECT_EQ(out.fan.highestRpm, 2050);
    EXPECT_EQ(out.fan.averageRpm, 1500);
    EXPECT_EQ(out.fan.fanCount, 3u);
    EXPECT_TRUE(out.fan.status.available);
}

TEST(SensorHelperClient, ReconnectAfterServerDisconnect)
{
    const std::wstring name = UniquePipeName();
    SensorHelperClient client(name);
    {
        PipeServer server(name);
        ASSERT_TRUE(server.Valid());
        ASSERT_TRUE(client.Pump(0));
        ASSERT_TRUE(server.Accept());
        std::array<std::uint8_t, kMaxMessageBytes> hello{};
        DWORD helloBytes = 0;
        ASSERT_TRUE(server.ReadSome(hello.data(), hello.size(), helloBytes));
        ASSERT_TRUE(client.Pump(1));
    } // server closes here

    // The next pump observes the broken pipe and schedules a reconnect.
    EXPECT_FALSE(client.Pump(2));
    EXPECT_EQ(client.State(), HelperState::Disconnected);
    EXPECT_GT(client.NextAttemptMs(), 2u);
}

TEST(SensorHelperClient, RejectsMalformedHeaderAndSchedulesReconnect)
{
    const std::wstring name = UniquePipeName();
    PipeServer server(name);
    ASSERT_TRUE(server.Valid());

    SensorHelperClient client(name);
    ASSERT_TRUE(client.Pump(0));
    ASSERT_TRUE(server.Accept());
    std::array<std::uint8_t, kMaxMessageBytes> hello{};
    DWORD helloBytes = 0;
    ASSERT_TRUE(server.ReadSome(hello.data(), hello.size(), helloBytes));

    MessageHeader header{};
    header.magic = kMagic ^ 0x1u; // wrong magic
    header.version = kProtocolVersion;
    header.kind = static_cast<std::uint16_t>(MessageKind::SensorSnapshot);
    header.payloadLength = 0;
    ASSERT_TRUE(server.Write(&header, sizeof(header)));

    EXPECT_FALSE(client.Pump(1));
    EXPECT_EQ(client.State(), HelperState::Disconnected);
    EXPECT_GT(client.NextAttemptMs(), 1u);
}

TEST(SensorHelperClient, RejectsOversizedPayloadAndSchedulesReconnect)
{
    const std::wstring name = UniquePipeName();
    PipeServer server(name);
    ASSERT_TRUE(server.Valid());

    SensorHelperClient client(name);
    ASSERT_TRUE(client.Pump(0));
    ASSERT_TRUE(server.Accept());
    std::array<std::uint8_t, kMaxMessageBytes> hello{};
    DWORD helloBytes = 0;
    ASSERT_TRUE(server.ReadSome(hello.data(), hello.size(), helloBytes));

    MessageHeader header{};
    header.magic = kMagic;
    header.version = kProtocolVersion;
    header.kind = static_cast<std::uint16_t>(MessageKind::SensorSnapshot);
    header.payloadLength = static_cast<std::uint32_t>(kMaxPayloadBytes + 1);
    ASSERT_TRUE(server.Write(&header, sizeof(header)));

    EXPECT_FALSE(client.Pump(1));
    EXPECT_EQ(client.State(), HelperState::Disconnected);
}

TEST(SensorHelperClient, ReassemblesTruncatedFrameAcrossPumps)
{
    const std::wstring name = UniquePipeName();
    PipeServer server(name);
    ASSERT_TRUE(server.Valid());

    SensorHelperClient client(name);
    ASSERT_TRUE(client.Pump(0));
    ASSERT_TRUE(server.Accept());
    std::array<std::uint8_t, kMaxMessageBytes> hello{};
    DWORD helloBytes = 0;
    ASSERT_TRUE(server.ReadSome(hello.data(), hello.size(), helloBytes));

    SensorSnapshotPayload payload{};
    payload.readingCount = 1;
    payload.readings[0].id = static_cast<std::uint16_t>(SensorId::CpuPackageTemperature);
    payload.readings[0].available = 1;
    payload.readings[0].value = 55.0;

    std::array<std::uint8_t, kMaxMessageBytes> buffer{};
    const std::size_t total =
        EncodeMessage(MessageKind::SensorSnapshot, payload, 1, 1, buffer.data(), buffer.size());
    ASSERT_GT(total, sizeof(MessageHeader));

    const std::size_t split = sizeof(MessageHeader) + sizeof(SensorReading);
    ASSERT_TRUE(server.Write(buffer.data(), split));
    // A partial frame must not be treated as data nor as a malformed frame.
    EXPECT_TRUE(client.Pump(1));
    EXPECT_FALSE(client.HasFreshData());

    ASSERT_TRUE(server.Write(buffer.data() + split, total - split));
    ASSERT_TRUE(client.Pump(2));
    EXPECT_TRUE(client.HasFreshData());
    EXPECT_EQ(client.SnapshotCount(), 1u);
}

TEST(SensorHelperClient, ProviderMarksDeepSensorsUnavailableWithoutHelper)
{
    auto client = std::make_shared<SensorHelperClient>(L"\\\\.\\pipe\\PacecarTest.DoesNotExist2");
    pacecar::metrics::SensorHelperProvider provider(client, [] { return std::uint64_t{0}; });

    pacecar::metrics::MetricsSnapshot snapshot{};
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
    EXPECT_FALSE(snapshot.board.status.available);
    EXPECT_FALSE(snapshot.fan.status.available);
    EXPECT_FALSE(snapshot.disk.temperatureStatus.available);
    EXPECT_EQ(provider.Domains(),
              static_cast<std::uint32_t>(pacecar::metrics::MetricDomain::DeepSensors));
}
} // namespace