#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include "pacecar/metrics/GpuPdhProvider.h"

#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "pacecar/util/Pdh.h"

#pragma comment(lib, "pdh.lib")

namespace pacecar::metrics
{
namespace
{
// --- parser helpers -------------------------------------------------------

[[nodiscard]] bool IsDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

[[nodiscard]] bool IsHexDigit(char c) noexcept
{
    return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

[[nodiscard]] char ToLowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool ConsumeToken(std::string_view& text, std::string_view token) noexcept
{
    if (text.size() < token.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < token.size(); ++i)
    {
        if (ToLowerAscii(text[i]) != ToLowerAscii(token[i]))
        {
            return false;
        }
    }
    text.remove_prefix(token.size());
    return true;
}

[[nodiscard]] bool ReadUint32(std::string_view& text, std::uint32_t& out) noexcept
{
    std::uint64_t value = 0;
    std::size_t digits = 0;
    while (!text.empty() && IsDigit(text.front()))
    {
        value = value * 10u + static_cast<std::uint64_t>(text.front() - '0');
        if (value > 0xFFFFFFFFull)
        {
            return false;
        }
        text.remove_prefix(1);
        ++digits;
    }
    if (digits == 0)
    {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool ReadHex32(std::string_view& text, std::uint32_t& out) noexcept
{
    if (!ConsumeToken(text, "0x"))
    {
        return false;
    }
    std::uint64_t value = 0;
    std::size_t digits = 0;
    while (!text.empty() && IsHexDigit(text.front()))
    {
        const char c = ToLowerAscii(text.front());
        const std::uint64_t nibble =
            IsDigit(c) ? static_cast<std::uint64_t>(c - '0') : static_cast<std::uint64_t>(c - 'a' + 10);
        value = (value << 4u) | nibble;
        text.remove_prefix(1);
        ++digits;
        if (digits > 8)
        {
            return false;
        }
    }
    if (digits == 0)
    {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

// Normalizes an engine type for aggregation: lowercase, trim, and collapse interior whitespace runs
// to single underscores. `3D` -> `3d`, `high priority 3d` -> `high_priority_3d`, `graphics_1` stays
// `graphics_1`. Empty input yields an empty name (the caller treats that as malformed).
void NormalizeEngineType(std::string_view raw, char (&out)[kEngineTypeCapacity]) noexcept
{
    char buffer[kEngineTypeCapacity] = {};
    std::size_t length = 0;
    bool previousWasSeparator = true; // suppresses leading separators
    for (const char c : raw)
    {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            if (!previousWasSeparator && length + 1 < kEngineTypeCapacity)
            {
                buffer[length++] = '_';
                previousWasSeparator = true;
            }
            continue;
        }
        if (length + 1 >= kEngineTypeCapacity)
        {
            break;
        }
        const char lower = ToLowerAscii(c);
        buffer[length++] = lower;
        previousWasSeparator = (lower == '_');
    }
    while (length > 0 && buffer[length - 1] == '_')
    {
        --length;
    }
    buffer[length] = '\0';
    std::memcpy(out, buffer, length + 1);
}

[[nodiscard]] double ClampPercent(double value) noexcept
{
    if (!(value > 0.0)) // also rejects NaN
    {
        return 0.0;
    }
    return value > 100.0 ? 100.0 : value;
}

// --- real PDH engine source ----------------------------------------------

void CopyWideToNarrow(const wchar_t* source, char* destination, int capacity) noexcept
{
    if (source == nullptr || capacity <= 0)
    {
        return;
    }
    destination[0] = '\0';
    const int written = ::WideCharToMultiByte(CP_UTF8, 0, source, -1, destination, capacity - 1, nullptr,
                                              nullptr);
    if (written <= 0)
    {
        destination[0] = '\0';
    }
    else
    {
        destination[capacity - 1] = '\0';
    }
}

// Reads the wildcard `GPU Engine` counters through the PDH array API. `Utilization Percentage` is the
// primary value; when PDH reports it invalid for an instance, the source falls back to a
// running-time delta measured against the injected clock - the same elapsed-correct math the D3DKMT
// fallback uses, so a variable cadence stays correct.
class PdhEngineSource final : public IGpuEngineSource
{
  public:
    PdhEngineSource() : clock_(MakeQpcClock())
    {
        if (!query_.IsValid())
        {
            return;
        }
        const PDH_STATUS utilStatus = query_.AddEnglishCounter(
            L"\\GPU Engine(*)\\Utilization Percentage", utilCounter_);
        const PDH_STATUS runStatus =
            query_.AddEnglishCounter(L"\\GPU Engine(*)\\Running Time", runCounter_);
        available_ = utilStatus == ERROR_SUCCESS && runStatus == ERROR_SUCCESS &&
                     utilCounter_.IsValid() && runCounter_.IsValid();
    }

    [[nodiscard]] bool IsAvailable() override
    {
        return available_;
    }

    bool Read(std::vector<GpuEngineCounterSample>& out) override
    {
        out.clear();
        if (!available_ || query_.Collect() != ERROR_SUCCESS)
        {
            return false;
        }

        const bool utilOk = ReadFormattedArray(utilCounter_.Handle(), utilBuffer_, utilCount_);
        const bool rawOk = ReadRawArray(runCounter_.Handle(), rawBuffer_, rawCount_);
        if (!utilOk && !rawOk)
        {
            return false;
        }

        const std::uint64_t now = clock_->NowTicks();
        if (!hasBaseline_)
        {
            hasBaseline_ = true;
            previousTicks_ = now;
            CaptureRaw();
            return true;
        }
        const std::uint64_t elapsed = now >= previousTicks_ ? now - previousTicks_ : 0;
        previousTicks_ = now;
        const std::uint64_t ticksPerSecond = clock_->TicksPerSecond();

        const auto* items = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(utilBuffer_.data());
        out.reserve(utilCount_);
        for (std::uint32_t i = 0; i < utilCount_; ++i)
        {
            GpuEngineCounterSample sample{};
            CopyWideToNarrow(items[i].szName, sample.instanceName,
                             static_cast<int>(sizeof(sample.instanceName)));
            const DWORD status = items[i].FmtValue.CStatus;
            if (status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA)
            {
                sample.utilizationPercent = ClampPercent(items[i].FmtValue.doubleValue);
                sample.utilizationValid = true;
            }
            else
            {
                std::uint64_t current = 0;
                if (FindRaw(items[i].szName, current))
                {
                    const std::uint64_t previous = FindPrevious(items[i].szName);
                    const std::uint64_t delta = current >= previous ? current - previous : 0;
                    sample.utilizationPercent =
                        ComputeElapsedCorrectPercent(delta, elapsed, ticksPerSecond);
                    sample.utilizationValid = true;
                }
            }
            out.push_back(sample);
        }

        CaptureRaw(); // this tick's running times become the baseline for the next
        return true;
    }

  private:
    struct RawEntry
    {
        std::wstring name;
        std::uint64_t running = 0;
    };

    static bool ReadFormattedArray(PDH_HCOUNTER counter,
                                   std::vector<std::byte>& buffer,
                                   std::uint32_t& count)
    {
        count = 0;
        DWORD size = 0;
        DWORD items = 0;
        PDH_STATUS status =
            ::PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &items, nullptr);
        if (status != PDH_MORE_DATA && status != ERROR_SUCCESS)
        {
            return false;
        }
        if (items == 0)
        {
            return true;
        }
        buffer.resize(size);
        status = ::PdhGetFormattedCounterArrayW(
            counter, PDH_FMT_DOUBLE, &size, &items,
            reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data()));
        if (status != ERROR_SUCCESS)
        {
            return false;
        }
        count = items;
        return true;
    }

    static bool ReadRawArray(PDH_HCOUNTER counter, std::vector<std::byte>& buffer,
                             std::uint32_t& count)
    {
        count = 0;
        DWORD size = 0;
        DWORD items = 0;
        PDH_STATUS status = ::PdhGetRawCounterArrayW(counter, &size, &items, nullptr);
        if (status != PDH_MORE_DATA && status != ERROR_SUCCESS)
        {
            return false;
        }
        if (items == 0)
        {
            return true;
        }
        buffer.resize(size);
        status = ::PdhGetRawCounterArrayW(counter, &size, &items,
                                          reinterpret_cast<PDH_RAW_COUNTER_ITEM_W*>(buffer.data()));
        if (status != ERROR_SUCCESS)
        {
            return false;
        }
        count = items;
        return true;
    }

    [[nodiscard]] bool FindRaw(const wchar_t* name, std::uint64_t& value) const
    {
        if (name == nullptr || rawBuffer_.empty())
        {
            return false;
        }
        const auto* items = reinterpret_cast<const PDH_RAW_COUNTER_ITEM_W*>(rawBuffer_.data());
        for (std::uint32_t i = 0; i < rawCount_; ++i)
        {
            if (items[i].szName != nullptr && ::_wcsicmp(items[i].szName, name) == 0)
            {
                value = static_cast<std::uint64_t>(items[i].RawValue.FirstValue);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::uint64_t FindPrevious(const wchar_t* name) const
    {
        if (name == nullptr)
        {
            return 0;
        }
        for (const RawEntry& entry : previous_)
        {
            if (entry.name == name)
            {
                return entry.running;
            }
        }
        return 0;
    }

    void CaptureRaw()
    {
        previous_.clear();
        if (rawBuffer_.empty())
        {
            return;
        }
        const auto* items = reinterpret_cast<const PDH_RAW_COUNTER_ITEM_W*>(rawBuffer_.data());
        for (std::uint32_t i = 0; i < rawCount_; ++i)
        {
            if (items[i].szName == nullptr)
            {
                continue;
            }
            RawEntry entry;
            entry.name = items[i].szName;
            entry.running = static_cast<std::uint64_t>(items[i].RawValue.FirstValue);
            previous_.push_back(std::move(entry));
        }
    }

    PdhQuery query_;
    PdhCounter utilCounter_;
    PdhCounter runCounter_;
    std::unique_ptr<IGpuElapsedClock> clock_;
    std::vector<std::byte> utilBuffer_;
    std::vector<std::byte> rawBuffer_;
    std::uint32_t utilCount_ = 0;
    std::uint32_t rawCount_ = 0;
    std::vector<RawEntry> previous_;
    std::uint64_t previousTicks_ = 0;
    bool hasBaseline_ = false;
    bool available_ = false;
};
} // namespace

bool ParseGpuEngineInstance(std::string_view instanceName, GpuEngineInstance& out) noexcept
{
    // Strip a trailing `#<n>` duplicate-index suffix PDH adds for repeated instance names.
    const std::size_t hash = instanceName.rfind('#');
    if (hash != std::string_view::npos && hash + 1 < instanceName.size())
    {
        bool digitsOnly = true;
        for (std::size_t i = hash + 1; i < instanceName.size(); ++i)
        {
            if (!IsDigit(instanceName[i]))
            {
                digitsOnly = false;
                break;
            }
        }
        if (digitsOnly)
        {
            instanceName = instanceName.substr(0, hash);
        }
    }
    // Tolerate an optional `\GPU Engine(...)` wrapper by dropping its closing parenthesis.
    if (!instanceName.empty() && instanceName.back() == ')')
    {
        instanceName.remove_suffix(1);
    }

    // Start at the first `pid_` so the wrapper prefix (if any) is ignored.
    std::size_t start = 0;
    for (std::size_t i = 0; i + 4 <= instanceName.size(); ++i)
    {
        if (ToLowerAscii(instanceName[i]) == 'p' && ToLowerAscii(instanceName[i + 1]) == 'i' &&
            ToLowerAscii(instanceName[i + 2]) == 'd' && instanceName[i + 3] == '_')
        {
            start = i;
            break;
        }
    }
    std::string_view text = instanceName.substr(start);

    GpuEngineInstance parsed{};
    if (!ConsumeToken(text, "pid_") || !ReadUint32(text, parsed.pid))
    {
        return false;
    }
    if (!ConsumeToken(text, "_luid_") || !ReadHex32(text, parsed.luid.high) ||
        !ConsumeToken(text, "_") || !ReadHex32(text, parsed.luid.low))
    {
        return false;
    }
    if (!ConsumeToken(text, "_phys_") || !ReadUint32(text, parsed.physicalAdapter))
    {
        return false;
    }
    if (!ConsumeToken(text, "_eng_") || !ReadUint32(text, parsed.engine))
    {
        return false;
    }
    if (!ConsumeToken(text, "_engtype_") || text.empty())
    {
        return false;
    }

    NormalizeEngineType(text, parsed.engineType);
    if (parsed.engineType[0] == '\0')
    {
        return false;
    }
    out = parsed;
    return true;
}

void AggregateGpuEnginesByType(const std::vector<GpuEngineReading>& readings,
                               std::vector<GpuEngineMetrics>& out)
{
    out.clear();
    for (const GpuEngineReading& reading : readings)
    {
        const char* const type = reading.instance.engineType;
        GpuEngineMetrics* existing = nullptr;
        for (GpuEngineMetrics& engine : out)
        {
            if (std::strcmp(engine.engineType, type) == 0)
            {
                existing = &engine;
                break;
            }
        }
        if (existing == nullptr)
        {
            GpuEngineMetrics entry{};
            std::memcpy(entry.engineType, type, kEngineTypeCapacity);
            entry.utilizationPercent = ClampPercent(reading.utilizationPercent);
            out.push_back(entry);
        }
        else if (reading.utilizationPercent > existing->utilizationPercent)
        {
            // Documented rule: the maximum over matching instances, never a sum.
            existing->utilizationPercent = ClampPercent(reading.utilizationPercent);
        }
    }
}

double PrimaryGpuPercent(const std::vector<GpuEngineMetrics>& engines) noexcept
{
    double best = 0.0;
    for (const GpuEngineMetrics& engine : engines)
    {
        if (std::strcmp(engine.engineType, "3d") == 0)
        {
            // Documented preference: the 3D engine when it exists, even if another type is higher.
            return engine.utilizationPercent;
        }
        if (engine.utilizationPercent > best)
        {
            best = engine.utilizationPercent;
        }
    }
    return best;
}

std::unique_ptr<IGpuEngineSource> MakePdhEngineSource()
{
    return std::make_unique<PdhEngineSource>();
}

GpuPdhProvider::GpuPdhProvider()
    : GpuPdhProvider(MakePdhEngineSource(), MakeDxgiAdapterEnumerator(), MakeD3dKmtSource(),
                     MakeQpcClock(), "auto")
{
}

GpuPdhProvider::GpuPdhProvider(std::unique_ptr<IGpuEngineSource> engineSource,
                               std::unique_ptr<IGpuAdapterEnumerator> adapters,
                               std::unique_ptr<IGpuD3dKmtSource> fallbackSource,
                               std::unique_ptr<IGpuElapsedClock> fallbackClock,
                               std::string selection)
    : engineSource_(std::move(engineSource)), adapters_(std::move(adapters))
{
    EnumerateAndSelect(selection);

    // Build the fallback whenever it is supplied, even if PDH works now: the counter set can
    // disappear mid-run, and the fallback must already be enumerated to take over cleanly.
    if (fallbackSource && fallbackClock)
    {
        fallback_ = std::make_unique<GpuD3dKmtProvider>(std::move(fallbackSource),
                                                        std::move(fallbackClock), selection);
    }

    pdhAvailable_ = engineSource_ && engineSource_->IsAvailable();
    if (pdhAvailable_)
    {
        backend_ = Backend::Pdh;
    }
    else if (fallback_ && fallback_->Available())
    {
        backend_ = Backend::D3dKmt;
    }
}

GpuPdhProvider::~GpuPdhProvider() = default;

void GpuPdhProvider::EnumerateAndSelect(const std::string& selectionText)
{
    adapterList_.clear();
    if (adapters_)
    {
        adapters_->Enumerate(adapterList_);
    }
    const std::size_t index = ResolveGpuAdapter(ParseGpuSelection(selectionText), adapterList_);
    selected_ = index == kNoGpuAdapter ? GpuAdapterInfo{} : adapterList_[index];
}

const char* GpuPdhProvider::Name() const noexcept
{
    return "gpu";
}

std::uint32_t GpuPdhProvider::Domains() const noexcept
{
    return static_cast<std::uint32_t>(MetricDomain::Gpu);
}

std::chrono::milliseconds GpuPdhProvider::Cadence() const noexcept
{
    return std::chrono::milliseconds(1000);
}

HRESULT GpuPdhProvider::Poll(MetricsSnapshot& snapshot)
{
    switch (backend_)
    {
    case Backend::Pdh:
        return PollPdh(snapshot);
    case Backend::D3dKmt:
        return PollFallback(snapshot);
    case Backend::Unavailable:
    default:
        return E_FAIL;
    }
}

HRESULT GpuPdhProvider::PollPdh(MetricsSnapshot& snapshot)
{
    if (!engineSource_ || !engineSource_->IsAvailable() || !engineSource_->Read(samples_))
    {
        // The counter set disappeared or became unreadable: fall back cleanly rather than failing.
        if (fallback_ && fallback_->Available())
        {
            backend_ = Backend::D3dKmt;
            return fallback_->Poll(snapshot);
        }
        return E_FAIL;
    }

    if (pdhFirstRead_)
    {
        // A rate-style counter needs a baseline; the UI shows a placeholder for one tick.
        pdhFirstRead_ = false;
        return E_PENDING;
    }

    readings_.clear();
    for (const GpuEngineCounterSample& sample : samples_)
    {
        if (!sample.utilizationValid)
        {
            continue;
        }
        GpuEngineInstance instance;
        if (!ParseGpuEngineInstance(sample.instanceName, instance))
        {
            continue;
        }
        // Filter to the selected adapter's LUID/instance set; if enumeration failed, keep all.
        if (!adapterList_.empty() && instance.luid != selected_.luid)
        {
            continue;
        }
        if (targetPid_ != 0 && instance.pid != targetPid_)
        {
            continue;
        }
        readings_.push_back(GpuEngineReading{instance, sample.utilizationPercent});
    }

    AggregateGpuEnginesByType(readings_, aggregated_);
    snapshot.gpu.engines.resize(aggregated_.size());
    std::copy(aggregated_.begin(), aggregated_.end(), snapshot.gpu.engines.begin());
    snapshot.gpu.utilizationPercent = PrimaryGpuPercent(aggregated_);
    std::memcpy(snapshot.gpu.name, selected_.name, kGpuNameCapacity);
    return S_OK;
}

HRESULT GpuPdhProvider::PollFallback(MetricsSnapshot& snapshot)
{
    return fallback_ ? fallback_->Poll(snapshot) : E_FAIL;
}

void GpuPdhProvider::Reset() noexcept
{
    pdhFirstRead_ = true;
    if (fallback_)
    {
        fallback_->Reset();
    }
}
} // namespace pacecar::metrics