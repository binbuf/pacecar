#include "pacecar/metrics/SensorHelperClient.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace pacecar::metrics
{
namespace
{
constexpr std::size_t kMaxReceiveBytes = ipc::kMaxMessageBytes;
constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\Pacecar.Sensors";

HANDLE AsHandle(void* handle) noexcept
{
    return reinterpret_cast<HANDLE>(handle);
}

void* AsVoid(HANDLE handle) noexcept
{
    return reinterpret_cast<void*>(handle);
}
} // namespace

std::wstring SensorHelperClient::DefaultPipeName(const std::wstring& userSid)
{
    if (userSid.empty())
    {
        return kPipePrefix;
    }
    std::wstring name = kPipePrefix;
    name += L".";
    name += userSid;
    return name;
}

SensorHelperClient::SensorHelperClient(std::wstring pipeName) : pipeName_(std::move(pipeName))
{
    rx_.reserve(kMaxReceiveBytes);
    readings_.reserve(ipc::kMaxSensorReadings);
}

SensorHelperClient::~SensorHelperClient()
{
    Disconnect();
}

bool SensorHelperClient::Pump(std::uint64_t nowMs)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!enabled_)
    {
        return false;
    }

    if (pipe_ == nullptr)
    {
        if (nowMs < nextAttemptMs_)
        {
            return false;
        }
        if (!TryConnectInternal())
        {
            ScheduleReconnectInternal(nowMs);
            return false;
        }
    }

    if (!DrainReceiveBuffer())
    {
        ClosePipeInternal();
        ScheduleReconnectInternal(nowMs);
        return false;
    }
    return pipe_ != nullptr;
}

void SensorHelperClient::Disconnect() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    ClosePipeInternal();
    state_ = HelperState::Disconnected;
    haveData_ = false;
    nextAttemptMs_ = 0;
    backoffMs_ = kInitialBackoffMs;
}

void SensorHelperClient::SetEnabled(bool enabled) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (enabled_ == enabled)
    {
        return;
    }
    enabled_ = enabled;
    ClosePipeInternal();
    haveData_ = false;
    backoffMs_ = kInitialBackoffMs;
    nextAttemptMs_ = 0; // Re-enabling connects on the next pump.
}

bool SensorHelperClient::Enabled() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

HelperState SensorHelperClient::State() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool SensorHelperClient::Connected() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == HelperState::Connected;
}

bool SensorHelperClient::HasFreshData() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return haveData_;
}

std::uint64_t SensorHelperClient::NextAttemptMs() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return nextAttemptMs_;
}

std::uint64_t SensorHelperClient::SnapshotCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshotCount_;
}

std::uint32_t SensorHelperClient::Capabilities() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return capabilities_;
}

void SensorHelperClient::ScheduleReconnectInternal(std::uint64_t nowMs) noexcept
{
    const std::uint64_t capped =
        backoffMs_ > kMaxBackoffMs ? kMaxBackoffMs : backoffMs_;
    nextAttemptMs_ = nowMs + capped;
    if (backoffMs_ < kMaxBackoffMs)
    {
        backoffMs_ = backoffMs_ * 2;
        if (backoffMs_ > kMaxBackoffMs)
        {
            backoffMs_ = kMaxBackoffMs;
        }
    }
}

void SensorHelperClient::ClosePipeInternal()
{
    if (pipe_ != nullptr)
    {
        CloseHandle(AsHandle(pipe_));
        pipe_ = nullptr;
    }
    state_ = HelperState::Disconnected;
}

bool SensorHelperClient::TryConnectInternal()
{
    HANDLE pipe = CreateFileW(pipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    pipe_ = AsVoid(pipe);
    state_ = HelperState::Connected;
    haveData_ = false;
    capabilities_ = 0;
    rx_.clear();
    backoffMs_ = kInitialBackoffMs;
    SendHello();
    return pipe_ != nullptr;
}

void SensorHelperClient::SendHello()
{
    ipc::HelloPayload hello{};
    hello.clientPid = static_cast<std::uint32_t>(GetCurrentProcessId());
    hello.reserved = 0;

    std::array<std::uint8_t, ipc::kMaxMessageBytes> buffer{};
    const std::size_t written =
        ipc::EncodeMessage(ipc::MessageKind::Hello, hello, ++helloSequence_, GetTickCount64(),
                           buffer.data(), buffer.size());
    if (written == 0)
    {
        ClosePipeInternal();
        return;
    }

    DWORD bytesWritten = 0;
    if (WriteFile(AsHandle(pipe_), buffer.data(), static_cast<DWORD>(written), &bytesWritten,
                  nullptr) == FALSE ||
        bytesWritten != static_cast<DWORD>(written))
    {
        ClosePipeInternal();
    }
}

bool SensorHelperClient::DrainReceiveBuffer()
{
    for (;;)
    {
        DWORD available = 0;
        if (PeekNamedPipe(AsHandle(pipe_), nullptr, 0, nullptr, &available, nullptr) == FALSE)
        {
            return false;
        }
        if (available == 0)
        {
            break;
        }

        const std::size_t remaining = kMaxReceiveBytes - rx_.size();
        if (remaining == 0)
        {
            // A partial frame already fills the bound; no valid message can be this large.
            return false;
        }
        const std::size_t chunk = std::min<std::size_t>(available, remaining);
        if (!ReadBytesInternal(chunk))
        {
            return false;
        }
        if (!ParseFrames())
        {
            return false;
        }
    }
    return true;
}

bool SensorHelperClient::ReadBytesInternal(std::size_t capacity)
{
    const std::size_t oldSize = rx_.size();
    rx_.resize(oldSize + capacity);
    DWORD bytesRead = 0;
    const BOOL ok = ReadFile(AsHandle(pipe_), rx_.data() + oldSize, static_cast<DWORD>(capacity),
                             &bytesRead, nullptr);
    if (ok == FALSE)
    {
        rx_.resize(oldSize);
        return false;
    }
    rx_.resize(oldSize + bytesRead);
    return bytesRead > 0;
}

bool SensorHelperClient::ParseFrames()
{
    std::size_t offset = 0;
    for (;;)
    {
        if (rx_.size() - offset < sizeof(ipc::MessageHeader))
        {
            break;
        }

        ipc::MessageHeader header{};
        std::memcpy(&header, rx_.data() + offset, sizeof(header));
        if (header.magic != ipc::kMagic || header.version != ipc::kProtocolVersion ||
            header.kind == static_cast<std::uint16_t>(ipc::MessageKind::Invalid) ||
            header.payloadLength > ipc::kMaxPayloadBytes)
        {
            return false;
        }

        const std::size_t expected = sizeof(ipc::MessageHeader) + header.payloadLength;
        if (expected > ipc::kMaxMessageBytes)
        {
            return false;
        }
        if (rx_.size() - offset < expected)
        {
            break; // Partial frame; wait for the rest.
        }

        ipc::DecodedMessage decoded{};
        if (!ipc::DecodeMessage(rx_.data() + offset, expected, decoded))
        {
            return false;
        }
        if (!HandleMessage(decoded))
        {
            return false;
        }
        offset += expected;
    }

    if (offset > 0)
    {
        rx_.erase(rx_.begin(), rx_.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return true;
}

bool SensorHelperClient::HandleMessage(const ipc::DecodedMessage& message)
{
    switch (static_cast<ipc::MessageKind>(message.header.kind))
    {
    case ipc::MessageKind::HelloAck:
    {
        ipc::HelloAckPayload ack{};
        if (ipc::DecodePayload(message, ack))
        {
            capabilities_ = ack.capabilities;
        }
        return true;
    }
    case ipc::MessageKind::SensorSnapshot:
    {
        ipc::SensorSnapshotPayload payload{};
        if (!ipc::DecodePayload(message, payload))
        {
            return true; // Wrong-sized snapshot: ignore rather than trust it.
        }
        std::size_t count = payload.readingCount;
        if (count > ipc::kMaxSensorReadings)
        {
            count = ipc::kMaxSensorReadings;
        }
        readings_.resize(count);
        if (count > 0)
        {
            std::memcpy(readings_.data(), payload.readings, count * sizeof(ipc::SensorReading));
        }
        haveData_ = true;
        ++snapshotCount_;
        return true;
    }
    case ipc::MessageKind::Goodbye:
        return false;
    case ipc::MessageKind::Hello:
    case ipc::MessageKind::Invalid:
    default:
        return true;
    }
}

void SensorHelperClient::ApplyTo(MetricsSnapshot& snapshot) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!haveData_)
    {
        return;
    }

    std::int32_t highestFan = 0;
    std::int64_t fanSum = 0;
    std::uint32_t fanCount = 0;

    for (const ipc::SensorReading& reading : readings_)
    {
        if (reading.available == 0)
        {
            continue;
        }
        switch (static_cast<ipc::SensorId>(reading.id))
        {
        case ipc::SensorId::CpuPackageTemperature:
            snapshot.cpu.packageTemperatureC = reading.value;
            snapshot.cpu.temperatureIsAcpi = false;
            snapshot.cpu.temperatureStatus.available = true;
            snapshot.cpu.temperatureStatus.stale = false;
            snapshot.cpu.temperatureStatus.lastSuccessTick = snapshot.tickIndex;
            break;
        case ipc::SensorId::CpuCoreTemperature:
            if (reading.index >= 0 &&
                static_cast<std::size_t>(reading.index) < snapshot.cpu.cores.size())
            {
                CpuCoreMetrics& core = snapshot.cpu.cores[static_cast<std::size_t>(reading.index)];
                core.temperatureC = reading.value;
                core.temperatureStatus.available = true;
                core.temperatureStatus.stale = false;
                core.temperatureStatus.lastSuccessTick = snapshot.tickIndex;
            }
            break;
        case ipc::SensorId::MainboardTemperature:
            snapshot.board.mainboardTemperatureC = reading.value;
            snapshot.board.mainboardIsAcpi = false;
            snapshot.board.status.available = true;
            snapshot.board.status.stale = false;
            snapshot.board.status.lastSuccessTick = snapshot.tickIndex;
            break;
        case ipc::SensorId::DiskTemperature:
            snapshot.disk.temperatureC = reading.value;
            snapshot.disk.temperatureStatus.available = true;
            snapshot.disk.temperatureStatus.stale = false;
            snapshot.disk.temperatureStatus.lastSuccessTick = snapshot.tickIndex;
            break;
        case ipc::SensorId::GpuTemperature:
            if (!snapshot.gpu.temperatureStatus.available)
            {
                snapshot.gpu.temperatureC = reading.value;
                snapshot.gpu.temperatureStatus.available = true;
                snapshot.gpu.temperatureStatus.stale = false;
                snapshot.gpu.temperatureStatus.lastSuccessTick = snapshot.tickIndex;
            }
            break;
        case ipc::SensorId::FanRpm:
        {
            const auto rpm = static_cast<std::int32_t>(reading.value);
            highestFan = std::max(highestFan, rpm);
            fanSum += rpm;
            ++fanCount;
            break;
        }
        case ipc::SensorId::Unknown:
        case ipc::SensorId::DimmTemperature:
        case ipc::SensorId::Voltage:
        case ipc::SensorId::Power:
        default:
            break;
        }
    }

    if (fanCount > 0)
    {
        snapshot.fan.highestRpm = highestFan;
        snapshot.fan.averageRpm = static_cast<std::int32_t>(fanSum / fanCount);
        snapshot.fan.fanCount = fanCount;
        snapshot.fan.status.available = true;
        snapshot.fan.status.stale = false;
        snapshot.fan.status.lastSuccessTick = snapshot.tickIndex;
    }
}

void SensorHelperClient::MarkUnavailable(MetricsSnapshot& snapshot) const
{
    const auto clear = [&snapshot](MetricStatus& status) {
        status.available = false;
        status.stale = true;
    };
    clear(snapshot.board.status);
    clear(snapshot.fan.status);
    clear(snapshot.disk.temperatureStatus);

    std::lock_guard<std::mutex> lock(mutex_);
    if (pipe_ == nullptr)
    {
        haveData_ = false;
    }
}

std::wstring SensorHelperClient::Status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == HelperState::Connected)
    {
        return haveData_ ? L"connected" : L"connected (awaiting data)";
    }
    return L"unavailable";
}
} // namespace pacecar::metrics