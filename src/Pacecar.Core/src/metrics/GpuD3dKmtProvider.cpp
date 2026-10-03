#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include "pacecar/metrics/GpuD3dKmtProvider.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3dkmthk.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")

namespace pacecar::metrics
{
namespace
{
// ASCII-only lowercase; adapter names are compared case-insensitively but never localized.
[[nodiscard]] std::string ToLowerAscii(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept
{
    const auto notSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) == 0; };
    while (!text.empty() && !notSpace(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && !notSpace(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool IsAllDigits(std::string_view text) noexcept
{
    if (text.empty())
    {
        return false;
    }
    for (const char c : text)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
    }
    return true;
}

void CopyName(char* dst, const std::string& src) noexcept
{
    const std::size_t count = std::min(src.size(), kGpuNameCapacity - 1);
    if (count != 0)
    {
        std::memcpy(dst, src.data(), count);
    }
    dst[count] = '\0';
}

[[nodiscard]] std::size_t FirstUsableAdapter(const std::vector<GpuAdapterInfo>& adapters) noexcept
{
    if (adapters.empty())
    {
        return kNoGpuAdapter;
    }
    for (std::size_t i = 0; i < adapters.size(); ++i)
    {
        if (!adapters[i].isSoftware)
        {
            return i;
        }
    }
    return 0;
}

// DXGI-backed enumerator. Creates a factory per call (enumeration happens only at construction).
class DxgiAdapterEnumerator final : public IGpuAdapterEnumerator
{
  public:
    bool Enumerate(std::vector<GpuAdapterInfo>& out) override
    {
        out.clear();
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
        {
            return false;
        }

        for (UINT index = 0;; ++index)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(index, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            if (!adapter)
            {
                break;
            }

            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc)))
            {
                continue;
            }

            GpuAdapterInfo info{};
            info.luid.high = static_cast<std::uint32_t>(desc.AdapterLuid.HighPart);
            info.luid.low = static_cast<std::uint32_t>(desc.AdapterLuid.LowPart);
            info.isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            const int written = ::WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, info.name,
                                                      static_cast<int>(kGpuNameCapacity - 1), nullptr,
                                                      nullptr);
            if (written <= 0)
            {
                info.name[0] = '\0';
            }
            else
            {
                info.name[kGpuNameCapacity - 1] = '\0';
            }
            out.push_back(info);
        }
        return true;
    }
};

// Real `QueryPerformanceCounter` clock.
class QpcClock final : public IGpuElapsedClock
{
  public:
    QpcClock() noexcept
    {
        LARGE_INTEGER frequency{};
        if (::QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
        {
            ticksPerSecond_ = static_cast<std::uint64_t>(frequency.QuadPart);
        }
    }

    [[nodiscard]] std::uint64_t NowTicks() const noexcept override
    {
        LARGE_INTEGER counter{};
        if (!::QueryPerformanceCounter(&counter))
        {
            return 0;
        }
        return static_cast<std::uint64_t>(counter.QuadPart);
    }

    [[nodiscard]] std::uint64_t TicksPerSecond() const noexcept override
    {
        return ticksPerSecond_ != 0 ? ticksPerSecond_ : kGpuRunningTimeTicksPerSecond;
    }

  private:
    std::uint64_t ticksPerSecond_ = 0;
};

// Real D3DKMT source. Adapter names/LUIDs come from DXGI; node counts and node running times come
// from `D3DKMTQueryStatistics` using the `d3dkmthk.h` structs (no byte offsets). Failures are
// tolerated: a query that returns a failing NTSTATUS simply contributes nothing.
class D3dKmtSystemSource final : public IGpuD3dKmtSource
{
  public:
    D3dKmtSystemSource() : dxgi_(MakeDxgiAdapterEnumerator())
    {
        std::vector<GpuAdapterInfo> adapters;
        available_ = dxgi_ && dxgi_->Enumerate(adapters) && !adapters.empty();
    }

    [[nodiscard]] bool IsAvailable() override
    {
        return available_;
    }

    bool Enumerate(std::vector<GpuAdapterInfo>& out) override
    {
        out.clear();
        if (!dxgi_ || !dxgi_->Enumerate(out))
        {
            return false;
        }
        for (GpuAdapterInfo& adapter : out)
        {
            adapter.nodeCount = QueryNodeCount(adapter.luid);
        }
        // Keep only render-capable, non-software adapters; the fallback delegates selection to the
        // provider which expects a filtered list.
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [](const GpuAdapterInfo& adapter) {
                                     return adapter.isSoftware || adapter.nodeCount == 0;
                                 }),
                  out.end());
        return true;
    }

    bool QueryRunningTime(const GpuAdapterInfo& adapter, std::uint64_t& runningTime) override
    {
        std::uint64_t total = 0;
        for (std::uint32_t node = 0; node < adapter.nodeCount; ++node)
        {
            D3DKMT_QUERYSTATISTICS query{};
            query.Type = D3DKMT_QUERYSTATISTICS_NODE;
            query.AdapterLuid.HighPart = static_cast<LONG>(adapter.luid.high);
            query.AdapterLuid.LowPart = adapter.luid.low;
            query.QueryNode.NodeId = node;

            const NTSTATUS status = ::D3DKMTQueryStatistics(&query);
            if (status < 0)
            {
                // STATUS_INVALID_PARAMETER and friends: skip this node rather than failing the read.
                continue;
            }
            const LONGLONG running = query.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
            if (running > 0)
            {
                total += static_cast<std::uint64_t>(running);
            }
        }
        runningTime = total;
        return true;
    }

  private:
    [[nodiscard]] static std::uint32_t QueryNodeCount(const GpuLuid& luid) noexcept
    {
        D3DKMT_QUERYSTATISTICS query{};
        query.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
        query.AdapterLuid.HighPart = static_cast<LONG>(luid.high);
        query.AdapterLuid.LowPart = luid.low;
        if (::D3DKMTQueryStatistics(&query) < 0)
        {
            return 0;
        }
        return static_cast<std::uint32_t>(query.QueryResult.AdapterInformation.NodeCount);
    }

    std::unique_ptr<IGpuAdapterEnumerator> dxgi_;
    bool available_ = false;
};
} // namespace

std::string FormatGpuLuid(const GpuLuid& luid)
{
    char buffer[24] = {};
    // Fixed 8-digit lowercase hex, high word first, matching the PDH instance spelling.
    char highText[9] = {};
    char lowText[9] = {};
    std::uint32_t high = luid.high;
    std::uint32_t low = luid.low;
    for (int i = 7; i >= 0; --i)
    {
        const char digit = static_cast<char>(high & 0xFu);
        highText[i] = static_cast<char>(digit < 10 ? ('0' + digit) : ('a' + (digit - 10)));
        high >>= 4u;
    }
    for (int i = 7; i >= 0; --i)
    {
        const char digit = static_cast<char>(low & 0xFu);
        lowText[i] = static_cast<char>(digit < 10 ? ('0' + digit) : ('a' + (digit - 10)));
        low >>= 4u;
    }
    const int written = std::snprintf(buffer, sizeof(buffer), "0x%s_0x%s", highText, lowText);
    if (written <= 0)
    {
        return {};
    }
    return std::string(buffer, static_cast<std::size_t>(written));
}

GpuSelection ParseGpuSelection(std::string_view text) noexcept
{
    GpuSelection selection{};
    const std::string_view trimmed = Trim(text);
    if (trimmed.empty() || ToLowerAscii(trimmed) == "auto")
    {
        selection.kind = GpuSelectionKind::Auto;
        return selection;
    }
    if (IsAllDigits(trimmed) && trimmed.size() <= 9)
    {
        std::uint32_t value = 0;
        for (const char c : trimmed)
        {
            value = value * 10u + static_cast<std::uint32_t>(c - '0');
        }
        selection.kind = GpuSelectionKind::Index;
        selection.index = value;
        return selection;
    }
    selection.kind = GpuSelectionKind::Name;
    CopyName(selection.name, ToLowerAscii(trimmed));
    return selection;
}

std::size_t ResolveGpuAdapter(const GpuSelection& selection,
                              const std::vector<GpuAdapterInfo>& adapters) noexcept
{
    if (adapters.empty())
    {
        return kNoGpuAdapter;
    }
    switch (selection.kind)
    {
    case GpuSelectionKind::Index:
        if (selection.index < adapters.size())
        {
            return selection.index;
        }
        return FirstUsableAdapter(adapters);
    case GpuSelectionKind::Name:
    {
        const std::string needle = ToLowerAscii(selection.name);
        if (!needle.empty())
        {
            for (std::size_t i = 0; i < adapters.size(); ++i)
            {
                const std::string candidate = ToLowerAscii(adapters[i].name);
                if (candidate.find(needle) != std::string::npos)
                {
                    return i;
                }
            }
        }
        return FirstUsableAdapter(adapters);
    }
    case GpuSelectionKind::Auto:
    default:
        return FirstUsableAdapter(adapters);
    }
}

std::unique_ptr<IGpuAdapterEnumerator> MakeDxgiAdapterEnumerator()
{
    return std::make_unique<DxgiAdapterEnumerator>();
}

std::unique_ptr<IGpuElapsedClock> MakeQpcClock()
{
    return std::make_unique<QpcClock>();
}

double ComputeElapsedCorrectPercent(std::uint64_t runningDelta,
                                    std::uint64_t elapsedTicks,
                                    std::uint64_t ticksPerSecond) noexcept
{
    if (elapsedTicks == 0 || ticksPerSecond == 0)
    {
        return 0.0;
    }
    const double elapsedSeconds =
        static_cast<double>(elapsedTicks) / static_cast<double>(ticksPerSecond);
    const double runningSeconds =
        static_cast<double>(runningDelta) / static_cast<double>(kGpuRunningTimeTicksPerSecond);
    if (!(elapsedSeconds > 0.0) || !(runningSeconds > 0.0))
    {
        return 0.0;
    }
    const double percent = (runningSeconds / elapsedSeconds) * 100.0;
    if (!(percent > 0.0)) // also rejects NaN
    {
        return 0.0;
    }
    return percent > 100.0 ? 100.0 : percent;
}

std::unique_ptr<IGpuD3dKmtSource> MakeD3dKmtSource()
{
    return std::make_unique<D3dKmtSystemSource>();
}

GpuD3dKmtProvider::GpuD3dKmtProvider()
    : GpuD3dKmtProvider(MakeD3dKmtSource(), MakeQpcClock(), "auto")
{
}

GpuD3dKmtProvider::GpuD3dKmtProvider(std::unique_ptr<IGpuD3dKmtSource> source,
                                     std::unique_ptr<IGpuElapsedClock> clock,
                                     std::string selection)
    : source_(std::move(source)), clock_(std::move(clock))
{
    if (!source_ || !clock_ || !source_->IsAvailable())
    {
        available_ = false;
        return;
    }

    std::vector<GpuAdapterInfo> adapters;
    if (!source_->Enumerate(adapters))
    {
        available_ = false;
        return;
    }
    const std::size_t selected = ResolveGpuAdapter(ParseGpuSelection(selection), adapters);
    if (selected == kNoGpuAdapter)
    {
        available_ = false;
        return;
    }
    selected_ = adapters[selected];
    available_ = true;
}

GpuD3dKmtProvider::~GpuD3dKmtProvider() = default;

const char* GpuD3dKmtProvider::Name() const noexcept
{
    return "gpu-d3d-kmt";
}

std::uint32_t GpuD3dKmtProvider::Domains() const noexcept
{
    return static_cast<std::uint32_t>(MetricDomain::Gpu);
}

std::chrono::milliseconds GpuD3dKmtProvider::Cadence() const noexcept
{
    return std::chrono::milliseconds(1000);
}

HRESULT GpuD3dKmtProvider::Poll(MetricsSnapshot& snapshot)
{
    if (!available_)
    {
        return E_FAIL;
    }

    const std::uint64_t now = clock_->NowTicks();
    std::uint64_t running = 0;
    if (!source_->QueryRunningTime(selected_, running))
    {
        return E_FAIL;
    }

    if (!hasBaseline_)
    {
        previousRunningTime_ = running;
        previousTicks_ = now;
        hasBaseline_ = true;
        return E_PENDING;
    }

    const std::uint64_t elapsed = now >= previousTicks_ ? now - previousTicks_ : 0;
    previousTicks_ = now;
    const std::uint64_t delta = running >= previousRunningTime_ ? running - previousRunningTime_ : 0;
    previousRunningTime_ = running;

    snapshot.gpu.utilizationPercent =
        ComputeElapsedCorrectPercent(delta, elapsed, clock_->TicksPerSecond());
    std::memcpy(snapshot.gpu.name, selected_.name, kGpuNameCapacity);
    // D3DKMT node statistics cannot be classified by engine type without extra node-metadata
    // queries; leave the per-engine list empty and publish the whole-adapter value only.
    snapshot.gpu.engines.clear();
    return S_OK;
}

void GpuD3dKmtProvider::Reset() noexcept
{
    previousRunningTime_ = 0;
    previousTicks_ = 0;
    hasBaseline_ = false;
}
} // namespace pacecar::metrics