#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include "pacecar/metrics/CpuProvider.h"

#include <windows.h>

#include <poclass.h>
#include <powrprof.h>

#include <algorithm>
#include <cstdint>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "PowrProf.lib")

namespace pacecar::metrics
{
namespace
{
using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

constexpr LONG kStatusSuccess = 0;
constexpr ULONG kStatusInfoLengthMismatch = 0xC0000004u;

// Real per-core source. Resolves the undocumented-but-stable `NtQuerySystemInformation` at runtime
// so the provider degrades to `GetSystemTimes` when it is absent, and sizes its buffer from the
// live processor count (with slack, retrying on STATUS_INFO_LENGTH_MISMATCH) rather than a fixed cap.
class NtCpuSystemSource final : public ICpuSystemSource
{
  public:
    NtCpuSystemSource() noexcept
    {
        if (HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll"))
        {
            query_ = reinterpret_cast<NtQuerySystemInformationFn>(
                ::GetProcAddress(ntdll, "NtQuerySystemInformation"));
        }
    }

    [[nodiscard]] std::size_t CoreCount() override
    {
        const DWORD count = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        return count == 0 ? 1u : static_cast<std::size_t>(count);
    }

    bool ReadPerCore(std::vector<CpuTimeCounters>& out) override
    {
        if (query_ == nullptr)
        {
            return false;
        }

        const std::size_t cores = CoreCount();
        std::size_t records = cores + 32; // slack for processors added since the last query
        records = std::max<std::size_t>(records, 64);
        buffer_.resize(records * sizeof(ProcessorPerformanceInfo));

        ULONG required = 0;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const LONG status =
                query_(kSystemProcessorPerformanceInformation, buffer_.data(),
                       static_cast<ULONG>(buffer_.size()), &required);
            if (status == kStatusSuccess)
            {
                const std::size_t parsed = ParseProcessorPerformance(
                    buffer_.data(), buffer_.size(), records, parsed_);
                if (parsed == 0)
                {
                    return false;
                }
                out.resize(parsed);
                for (std::size_t i = 0; i < parsed; ++i)
                {
                    out[i] = ToCounters(parsed_[i]);
                }
                return true;
            }
            if (static_cast<ULONG>(status) != kStatusInfoLengthMismatch)
            {
                return false;
            }
            ULONG nextSize = required;
            if (nextSize <= buffer_.size())
            {
                nextSize = static_cast<ULONG>(buffer_.size() + sizeof(ProcessorPerformanceInfo));
            }
            buffer_.resize(nextSize);
            records = buffer_.size() / sizeof(ProcessorPerformanceInfo);
        }
        return false;
    }

    bool ReadSystemTotal(CpuTimeCounters& out) override
    {
        FILETIME idle{};
        FILETIME kernel{};
        FILETIME user{};
        if (!::GetSystemTimes(&idle, &kernel, &user))
        {
            return false;
        }
        out = CpuTimeCounters{};
        out.idle = FileTimeToUint64(idle);
        out.kernel = FileTimeToUint64(kernel);
        out.user = FileTimeToUint64(user);
        return true;
    }

  private:
    [[nodiscard]] static std::uint64_t FileTimeToUint64(const FILETIME& value) noexcept
    {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    }

    NtQuerySystemInformationFn query_ = nullptr;
    std::vector<std::byte> buffer_;
    std::vector<ProcessorPerformanceInfo> parsed_;
};

// Real ACPI thermal-zone source. `ThermalInformation` is an undocumented `CallNtPowerInformation`
// level; when the machine exposes no zone (common on desktops) the call fails or returns zero and
// the provider reports the temperature unavailable.
class AcpiThermalZoneSource final : public IAcpiTemperatureSource
{
  public:
    bool ReadCelsius(double& celsius) override
    {
        THERMAL_INFORMATION info{};
        constexpr auto kThermalInformation = static_cast<POWER_INFORMATION_LEVEL>(26);
        const LONG status = ::CallNtPowerInformation(kThermalInformation, nullptr, 0, &info, sizeof(info));
        if (status != kStatusSuccess || info.CurrentTemperature == 0)
        {
            return false;
        }
        // `CurrentTemperature` is in tenths of a degree Kelvin.
        const double value = static_cast<double>(info.CurrentTemperature) / 10.0 - 273.15;
        if (value < -40.0 || value > 150.0)
        {
            return false;
        }
        celsius = value;
        return true;
    }
};
} // namespace

double ComputeDynamicFrequencyMhz(double percentPerformance, double processorFrequencyMhz) noexcept
{
    if (!(percentPerformance > 0.0) || !(processorFrequencyMhz > 0.0))
    {
        return 0.0;
    }
    return processorFrequencyMhz * (percentPerformance / 100.0);
}

CpuFrequencyResult SelectFrequency(const CpuFrequencyInputs& inputs) noexcept
{
    if (inputs.pdhPairAvailable)
    {
        const double mhz =
            ComputeDynamicFrequencyMhz(inputs.pdhPercentPerformance, inputs.pdhProcessorFrequencyMhz);
        if (mhz > 0.0)
        {
            return CpuFrequencyResult{CpuFrequencySource::PdhPair, mhz};
        }
    }
    if (inputs.powerInformationAvailable && inputs.powerInformationMhz > 0.0)
    {
        return CpuFrequencyResult{CpuFrequencySource::PowerInformation, inputs.powerInformationMhz};
    }
    if (inputs.baseClockAvailable && inputs.baseClockMhz > 0.0)
    {
        return CpuFrequencyResult{CpuFrequencySource::BaseClock, inputs.baseClockMhz};
    }
    return CpuFrequencyResult{};
}

CpuProvider::CpuProvider()
    : CpuProvider(std::make_unique<NtCpuSystemSource>(), std::make_unique<AcpiThermalZoneSource>())
{
}

CpuProvider::CpuProvider(std::unique_ptr<ICpuSystemSource> system,
                         std::unique_ptr<IAcpiTemperatureSource> temperature)
    : system_(std::move(system)), temperature_(std::move(temperature))
{
    SetupPdh();
}

CpuProvider::~CpuProvider() = default;

const char* CpuProvider::Name() const noexcept
{
    return "cpu";
}

std::uint32_t CpuProvider::Domains() const noexcept
{
    return static_cast<std::uint32_t>(MetricDomain::Cpu);
}

std::chrono::milliseconds CpuProvider::Cadence() const noexcept
{
    return std::chrono::milliseconds(1000);
}

void CpuProvider::SetupPdh() noexcept
{
    if (!pdhQuery_.IsValid())
    {
        return;
    }
    const PDH_STATUS percentStatus = pdhQuery_.AddEnglishCounter(
        L"\\Processor Information(_Total)\\% Processor Performance", percentCounter_);
    const PDH_STATUS frequencyStatus = pdhQuery_.AddEnglishCounter(
        L"\\Processor Information(_Total)\\Processor Frequency", frequencyCounter_);
    pdhReady_ = percentStatus == ERROR_SUCCESS && frequencyStatus == ERROR_SUCCESS &&
                percentCounter_.IsValid() && frequencyCounter_.IsValid();
    pdhFirstCollect_ = false;
}

void CpuProvider::UpdateFrequency(MetricsSnapshot& snapshot) noexcept
{
    CpuFrequencyInputs inputs;

    if (pdhReady_ && pdhQuery_.Collect() == ERROR_SUCCESS)
    {
        if (!pdhFirstCollect_)
        {
            // A rate counter needs two collections; discard the first sample.
            pdhFirstCollect_ = true;
        }
        else
        {
            double percent = 0.0;
            double mhz = 0.0;
            if (percentCounter_.GetDouble(percent) == ERROR_SUCCESS &&
                frequencyCounter_.GetDouble(mhz) == ERROR_SUCCESS)
            {
                inputs.pdhPairAvailable = true;
                inputs.pdhPercentPerformance = percent;
                inputs.pdhProcessorFrequencyMhz = mhz;
            }
        }
    }

    double powerMhz = 0.0;
    if (ReadProcessorPowerMhz(powerMhz))
    {
        inputs.powerInformationAvailable = true;
        inputs.powerInformationMhz = powerMhz;
    }

    const double baseMhz = BaseClockMhz();
    if (baseMhz > 0.0)
    {
        inputs.baseClockAvailable = true;
        inputs.baseClockMhz = baseMhz;
    }

    const CpuFrequencyResult result = SelectFrequency(inputs);
    frequencySource_ = result.source;
    snapshot.cpu.totalFrequencyMhz = result.mhz;
}

bool CpuProvider::ReadProcessorPowerMhz(double& mhz) noexcept
{
    const DWORD count = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (count == 0)
    {
        return false;
    }
    powerBuffer_.resize(count);
    const LONG status =
        ::CallNtPowerInformation(ProcessorInformation, nullptr, 0, powerBuffer_.data(),
                                 static_cast<ULONG>(powerBuffer_.size() *
                                                    sizeof(ProcessorPowerInformation)));
    if (status != kStatusSuccess)
    {
        return false;
    }

    std::uint64_t sum = 0;
    std::size_t used = 0;
    for (const ProcessorPowerInformation& info : powerBuffer_)
    {
        if (info.currentMhz != 0)
        {
            sum += info.currentMhz;
            ++used;
        }
    }
    if (used == 0)
    {
        return false;
    }
    mhz = static_cast<double>(sum) / static_cast<double>(used);
    return true;
}

double CpuProvider::BaseClockMhz() noexcept
{
    if (baseClockKnown_)
    {
        return baseClockMhz_;
    }
    baseClockKnown_ = true;

    DWORD mhz = 0;
    DWORD size = sizeof(mhz);
    const LSTATUS status = ::RegGetValueW(
        HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz",
        RRF_RT_REG_DWORD, nullptr, &mhz, &size);
    if (status == ERROR_SUCCESS && mhz > 0)
    {
        baseClockMhz_ = static_cast<double>(mhz);
    }
    return baseClockMhz_;
}

void CpuProvider::UpdateTemperature(MetricsSnapshot& snapshot) noexcept
{
    if (!temperature_)
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    constexpr auto kTemperatureInterval = std::chrono::seconds(5);
    if (temperatureSampled_ && now - lastTemperature_ < kTemperatureInterval)
    {
        return; // keep the last reading; the ACPI query is comparatively expensive
    }
    lastTemperature_ = now;
    temperatureSampled_ = true;

    double celsius = 0.0;
    if (temperature_->ReadCelsius(celsius))
    {
        snapshot.cpu.packageTemperatureC = celsius;
        // The ACPI label is the lower-confidence marker; the UI must never present this as a
        // package temperature (see the deferred helper path, T16).
        snapshot.cpu.temperatureIsAcpi = true;
        snapshot.cpu.temperatureStatus.available = true;
        snapshot.cpu.temperatureStatus.stale = false;
    }
    else
    {
        snapshot.cpu.packageTemperatureC = 0.0;
        snapshot.cpu.temperatureIsAcpi = false;
        snapshot.cpu.temperatureStatus.available = false;
        snapshot.cpu.temperatureStatus.stale = true;
    }
}

HRESULT CpuProvider::Poll(MetricsSnapshot& snapshot)
{
    std::vector<CpuTimeCounters> perCore;
    CpuTimeCounters systemTotal;
    CpuSampleSource source = CpuSampleSource::None;

    if (system_ && system_->ReadPerCore(perCore))
    {
        source = CpuSampleSource::NtPerCore;
    }
    else if (system_ && system_->ReadSystemTotal(systemTotal))
    {
        source = CpuSampleSource::SystemTimesFallback;
    }
    else
    {
        sampleSource_ = CpuSampleSource::None;
        return E_FAIL;
    }
    sampleSource_ = source;

    UpdateFrequency(snapshot);
    UpdateTemperature(snapshot);

    if (!hasBaseline_)
    {
        if (source == CpuSampleSource::NtPerCore)
        {
            previousCore_ = perCore;
            previousTotal_ = SumCounters(perCore);
        }
        else
        {
            previousCore_.clear();
            previousTotal_ = systemTotal;
        }
        hasBaseline_ = true;
        // No previous sample to delta against: report "incomplete" so the UI shows a placeholder.
        // The aggregator marks the CPU domain stale for this tick; the next poll publishes values.
        return E_PENDING;
    }

    if (source == CpuSampleSource::NtPerCore)
    {
        const std::size_t count = perCore.size();
        coreCount_ = count;
        if (snapshot.cpu.cores.size() != count)
        {
            snapshot.cpu.cores.resize(count);
        }
        const bool sameShape = previousCore_.size() == count;
        for (std::size_t i = 0; i < count; ++i)
        {
            snapshot.cpu.cores[i].utilizationPercent = sameShape
                ? ComputeBusyPercent(previousCore_[i], perCore[i])
                : 0.0;
        }
        const CpuTimeCounters total = SumCounters(perCore);
        snapshot.cpu.totalUtilizationPercent = ComputeBusyPercent(previousTotal_, total);
    }
    else
    {
        // GetSystemTimes is a single system-wide total and cannot drive per-core tiles reliably.
        coreCount_ = 0;
        snapshot.cpu.cores.clear();
        snapshot.cpu.totalUtilizationPercent = ComputeBusyPercent(previousTotal_, systemTotal);
    }

    previousCore_ = source == CpuSampleSource::NtPerCore ? perCore : std::vector<CpuTimeCounters>{};
    previousTotal_ = source == CpuSampleSource::NtPerCore ? SumCounters(perCore) : systemTotal;
    return S_OK;
}

void CpuProvider::Reset() noexcept
{
    previousCore_.clear();
    previousTotal_ = CpuTimeCounters{};
    hasBaseline_ = false;
    sampleSource_ = CpuSampleSource::None;
    frequencySource_ = CpuFrequencySource::Unavailable;
    coreCount_ = 0;
    pdhFirstCollect_ = false;
    baseClockKnown_ = false;
    baseClockMhz_ = 0.0;
    temperatureSampled_ = false;
}
} // namespace pacecar::metrics