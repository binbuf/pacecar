#pragma once

// CPU metric provider: unprivileged per-core/total utilization, dynamic frequency, and a
// best-effort ACPI thermal-zone temperature that is labeled distinctly from a package sensor.
//
// Sources (all require no elevation and never open a driver, per `docs/design/02-metrics.md`):
//   - Utilization: `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` delta across
//     logical processors. When that path is unavailable the provider falls back to a
//     `GetSystemTimes` delta for total utilization only (per-core tiles are left unavailable).
//   - Frequency: a persistent PDH query over `\Processor Information(_Total)\% Processor
//     Performance` and `\Processor Information(_Total)\Processor Frequency`, with a
//     `CallNtPowerInformation` current-MHz fallback and a registry base-clock fallback.
//   - Temperature: `CallNtPowerInformation(ThermalInformation)` ACPI thermal zone, when exposed.
//     It is always flagged `temperatureIsAcpi` so the UI never presents it as a package temp.
//
// Testability: the OS queries sit behind `ICpuSystemSource`, and the frequency fallback order is a
// pure function (`SelectFrequency`) so both can be exercised without hardware.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "pacecar/metrics/CpuTimes.h"
#include "pacecar/metrics/IMetricProvider.h"
#include "pacecar/util/Pdh.h"

namespace pacecar::metrics
{
// Which path produced the current utilization sample. Exposed for tests and diagnostics.
enum class CpuSampleSource
{
    None,
    NtPerCore,
    SystemTimesFallback,
};

// Abstracts the unprivileged OS time queries so the provider can be driven by fakes in tests.
class ICpuSystemSource
{
  public:
    virtual ~ICpuSystemSource() = default;

    // Number of active logical processors. Queried on every read so a hot-plugged core is picked up.
    [[nodiscard]] virtual std::size_t CoreCount() = 0;

    // Reads one cumulative counter set per logical processor. Returns false when the NT path is
    // unavailable, in which case the provider falls back to `ReadSystemTotal`.
    virtual bool ReadPerCore(std::vector<CpuTimeCounters>& out) = 0;

    // Reads one system-wide cumulative counter set via `GetSystemTimes`.
    virtual bool ReadSystemTotal(CpuTimeCounters& out) = 0;
};

// Best-effort ACPI thermal-zone reader. Returns false when the machine exposes no zone (common on
// desktops), which the provider reports as "unavailable", never as a package temperature.
class IAcpiTemperatureSource
{
  public:
    virtual ~IAcpiTemperatureSource() = default;

    virtual bool ReadCelsius(double& celsius) = 0;
};

// Frequency source selection, isolated from the PDH/registry plumbing so the fallback order can be
// unit-tested with injected availability.
enum class CpuFrequencySource
{
    Unavailable,
    PdhPair,
    PowerInformation,
    BaseClock,
};

struct CpuFrequencyInputs
{
    bool pdhPairAvailable = false;
    double pdhPercentPerformance = 0.0;
    double pdhProcessorFrequencyMhz = 0.0;
    bool powerInformationAvailable = false;
    double powerInformationMhz = 0.0;
    bool baseClockAvailable = false;
    double baseClockMhz = 0.0;
};

struct CpuFrequencyResult
{
    CpuFrequencySource source = CpuFrequencySource::Unavailable;
    double mhz = 0.0;
};

// The PDH pair: `% Processor Performance` is relative to the base clock, so dynamic MHz is the
// processor-frequency value scaled by the performance percentage.
[[nodiscard]] double ComputeDynamicFrequencyMhz(double percentPerformance,
                                                double processorFrequencyMhz) noexcept;

// Chooses the highest-confidence available frequency source: PDH pair, then
// `CallNtPowerInformation`, then the registry base clock.
[[nodiscard]] CpuFrequencyResult SelectFrequency(const CpuFrequencyInputs& inputs) noexcept;

class CpuProvider final : public IMetricProvider
{
  public:
    // Uses the real NT/GetSystemTimes, PDH, power-information, registry, and ACPI sources.
    CpuProvider();

    // Test/embedding seam: callers supply the system and (optionally) temperature sources.
    CpuProvider(std::unique_ptr<ICpuSystemSource> system,
                std::unique_ptr<IAcpiTemperatureSource> temperature);

    ~CpuProvider() override;

    CpuProvider(const CpuProvider&) = delete;
    CpuProvider& operator=(const CpuProvider&) = delete;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] std::uint32_t Domains() const noexcept override;
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override;
    HRESULT Poll(MetricsSnapshot& snapshot) override;
    void Reset() noexcept override;

    [[nodiscard]] CpuSampleSource SampleSource() const noexcept
    {
        return sampleSource_;
    }
    [[nodiscard]] CpuFrequencySource FrequencySource() const noexcept
    {
        return frequencySource_;
    }
    [[nodiscard]] std::size_t CoreCount() const noexcept
    {
        return coreCount_;
    }

  private:
    struct ProcessorPowerInformation
    {
        std::uint32_t number = 0;
        std::uint32_t maxMhz = 0;
        std::uint32_t currentMhz = 0;
        std::uint32_t mhzLimit = 0;
        std::uint32_t maxIdleState = 0;
        std::uint32_t currentIdleState = 0;
    };

    void SetupPdh() noexcept;
    void UpdateFrequency(MetricsSnapshot& snapshot) noexcept;
    void UpdateTemperature(MetricsSnapshot& snapshot) noexcept;
    [[nodiscard]] bool ReadProcessorPowerMhz(double& mhz) noexcept;
    [[nodiscard]] double BaseClockMhz() noexcept;

    std::unique_ptr<ICpuSystemSource> system_;
    std::unique_ptr<IAcpiTemperatureSource> temperature_;

    PdhQuery pdhQuery_;
    PdhCounter percentCounter_;
    PdhCounter frequencyCounter_;
    bool pdhReady_ = false;
    bool pdhFirstCollect_ = false;

    std::vector<CpuTimeCounters> previousCore_;
    CpuTimeCounters previousTotal_{};
    bool hasBaseline_ = false;

    std::vector<ProcessorPowerInformation> powerBuffer_;
    bool baseClockKnown_ = false;
    double baseClockMhz_ = 0.0;

    bool temperatureSampled_ = false;
    std::chrono::steady_clock::time_point lastTemperature_{};

    CpuSampleSource sampleSource_ = CpuSampleSource::None;
    CpuFrequencySource frequencySource_ = CpuFrequencySource::Unavailable;
    std::size_t coreCount_ = 0;
};
} // namespace pacecar::metrics