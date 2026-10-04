#include <gtest/gtest.h>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "pacecar/config/Config.h"
#include "pacecar/metrics/FrameTimeProcessor.h"
#include "pacecar/metrics/FrameTimeProvider.h"
#include "pacecar/metrics/IpcProtocol.h"
#include "pacecar/metrics/SensorHelperClient.h"
#include "pacecar/overlay/Layout.h"

namespace
{
using namespace pacecar::ipc;
using pacecar::metrics::FrameCaptureState;
using pacecar::metrics::FrameTimeProvider;
using pacecar::metrics::MetricsSnapshot;
using pacecar::metrics::SensorHelperClient;

constexpr std::uint64_t kFreq = 10'000'000ull;

std::wstring UniquePipeName()
{
    static std::uint32_t counter = 0;
    wchar_t buffer[128] = {};
    swprintf_s(buffer, L"\\\\.\\pipe\\PacecarFrameTest.%lu.%lu",
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

    bool Accept()
    {
        if (ConnectNamedPipe(handle_, nullptr) != FALSE)
        {
            return true;
        }
        return GetLastError() == ERROR_PIPE_CONNECTED;
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

    // Reads one full framed message (header + declared payload).
    bool ReadMessage(MessageHeader& header, std::vector<std::uint8_t>& payload)
    {
        if (!ReadExact(&header, sizeof(header)))
        {
            return false;
        }
        payload.resize(header.payloadLength);
        return payload.empty() || ReadExact(payload.data(), payload.size());
    }

  private:
    bool ReadExact(void* data, std::size_t size)
    {
        auto* bytes = static_cast<std::uint8_t*>(data);
        std::size_t offset = 0;
        while (offset < size)
        {
            DWORD read = 0;
            if (ReadFile(handle_, bytes + offset, static_cast<DWORD>(size - offset), &read,
                         nullptr) == FALSE ||
                read == 0)
            {
                return false;
            }
            offset += read;
        }
        return true;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

void WriteHelloAck(PipeServer& server)
{
    HelloAckPayload ack{};
    ack.helperPid = 4242;
    ack.capabilities = 0;
    ASSERT_TRUE(server.WriteMessage(MessageKind::HelloAck, ack, 1, 1));
}

FrameTimeStatsPayload MakeFrameStats(FrameCaptureState state, std::uint32_t targetPid,
                                     std::size_t count)
{
    FrameTimeStatsPayload payload{};
    payload.sequence = 1;
    payload.timestampMs = 1000;
    payload.qpcFrequency = kFreq;
    payload.targetPid = targetPid;
    payload.captureState = static_cast<std::uint32_t>(state);
    payload.eventCount = static_cast<std::uint32_t>(count);
    return payload;
}

TEST(FrameTimeProvider, ComputesFpsFromStreamedPresents)
{
    const std::wstring name = UniquePipeName();
    PipeServer server(name);
    ASSERT_TRUE(server.Valid());

    auto client = std::make_shared<SensorHelperClient>(name);
    FrameTimeProvider provider(client, [] { return std::uint64_t{0}; });

    ASSERT_TRUE(client->Pump(0));
    ASSERT_TRUE(server.Accept());
    MessageHeader hello{};
    std::vector<std::uint8_t> helloPayload;
    ASSERT_TRUE(server.ReadMessage(hello, helloPayload));
    ASSERT_EQ(hello.kind, static_cast<std::uint16_t>(MessageKind::Hello));
    WriteHelloAck(server);

    FrameTimeStatsPayload stats = MakeFrameStats(FrameCaptureState::Capturing, 100, 3);
    const std::uint64_t step = 166'667; // 16.6667 ms
    for (int i = 0; i < 3; ++i)
    {
        stats.events[i].qpcTicks = step * static_cast<std::uint64_t>(i);
        stats.events[i].pid = 100;
        stats.events[i].kind = 1;
        stats.events[i].cpuTicks = -1;
        stats.events[i].gpuTicks = -1;
    }
    ASSERT_TRUE(server.WriteMessage(MessageKind::FrameTimeStats, stats, 2, 2));

    MetricsSnapshot snapshot{};
    const HRESULT hr = provider.Poll(snapshot);
    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(provider.CaptureState(), FrameCaptureState::Capturing);
    EXPECT_NEAR(snapshot.frame.fps, 60.0, 0.1);
    EXPECT_NEAR(snapshot.frame.frameTimeMs, 16.6667, 0.01);
}

TEST(FrameTimeProvider, ConflictStateIsExplainedAndNotActive)
{
    const std::wstring name = UniquePipeName();
    PipeServer server(name);
    ASSERT_TRUE(server.Valid());

    auto client = std::make_shared<SensorHelperClient>(name);
    FrameTimeProvider provider(client, [] { return std::uint64_t{0}; });

    ASSERT_TRUE(client->Pump(0));
    ASSERT_TRUE(server.Accept());
    MessageHeader hello{};
    std::vector<std::uint8_t> helloPayload;
    ASSERT_TRUE(server.ReadMessage(hello, helloPayload));
    WriteHelloAck(server);

    FrameTimeStatsPayload stats = MakeFrameStats(FrameCaptureState::SessionBusy, 0, 0);
    ASSERT_TRUE(server.WriteMessage(MessageKind::FrameTimeStats, stats, 2, 2));

    MetricsSnapshot snapshot{};
    EXPECT_EQ(provider.Poll(snapshot), E_FAIL);
    EXPECT_EQ(provider.CaptureState(), FrameCaptureState::SessionBusy);
    EXPECT_NE(provider.StatusLine().find(L"another ETW session"), std::wstring::npos);
}

TEST(FrameTimeProvider, ClientSendsStartAndStopCaptureCommands)
{
    const std::wstring name = UniquePipeName();
    PipeServer server(name);
    ASSERT_TRUE(server.Valid());

    auto client = std::make_shared<SensorHelperClient>(name);
    client->SetCaptureTarget(4242, true);

    ASSERT_TRUE(client->Pump(0));
    ASSERT_TRUE(server.Accept());
    MessageHeader hello{};
    std::vector<std::uint8_t> helloPayload;
    ASSERT_TRUE(server.ReadMessage(hello, helloPayload));
    ASSERT_EQ(hello.kind, static_cast<std::uint16_t>(MessageKind::Hello));

    MessageHeader command{};
    std::vector<std::uint8_t> commandPayload;
    ASSERT_TRUE(server.ReadMessage(command, commandPayload));
    ASSERT_EQ(command.kind, static_cast<std::uint16_t>(MessageKind::FrameCaptureCommand));
    FrameCaptureCommandPayload start{};
    ASSERT_TRUE(DecodePayload(
        DecodedMessage{command, commandPayload.data(), commandPayload.size()}, start));
    EXPECT_EQ(start.command, static_cast<std::uint32_t>(FrameCaptureCommand::Start));
    EXPECT_EQ(start.targetPid, 4242u);

    client->SetCaptureTarget(0, false);
    ASSERT_TRUE(client->Pump(1));
    ASSERT_TRUE(server.ReadMessage(command, commandPayload));
    FrameCaptureCommandPayload stop{};
    ASSERT_TRUE(DecodePayload(
        DecodedMessage{command, commandPayload.data(), commandPayload.size()}, stop));
    EXPECT_EQ(stop.command, static_cast<std::uint32_t>(FrameCaptureCommand::Stop));
}

TEST(FrameTimeProvider, FpsTileFollowsCaptureOptIn)
{
    const auto hasFps = [](const pacecar::overlay::LayoutSettings& s)
    {
        const pacecar::overlay::LayoutResult layout =
            pacecar::overlay::ComputeLayout(320.0f, 240.0f, s);
        for (std::size_t i = 0; i < layout.count; ++i)
        {
            if (layout.tiles[i].id == pacecar::overlay::TileId::Fps)
            {
                return true;
            }
        }
        return false;
    };

    // Default config leaves FPS capture off, so the FPS tile stays hidden.
    pacecar::Config config;
    EXPECT_FALSE(
        pacecar::overlay::LayoutSettingsFromConfig(config)
            .tiles[static_cast<std::size_t>(pacecar::overlay::TileId::Fps)]
            .visible);

    // Enabling capture (the explicit opt-in) shows the tile even before frames flow.
    config.sensors.fps_capture = true;
    EXPECT_TRUE(hasFps(pacecar::overlay::LayoutSettingsFromConfig(config)));

    config.sensors.fps_capture = false;
    EXPECT_FALSE(hasFps(pacecar::overlay::LayoutSettingsFromConfig(config)));
}
} // namespace