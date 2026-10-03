#pragma once

// Optional vendor GPU enrichment: NVML (NVIDIA), ADLX (AMD), and IGCL (Intel).
//
// `docs/design/02-metrics.md` chooses PDH "GPU Engine" as the cross-vendor baseline and lists the
// vendor SDKs as the source of the metrics PDH cannot provide: temperature, power, clocks, VRAM,
// fan, and a whole-GPU utilization. This provider layers those values on top of the `GpuPdhProvider`
// snapshot output. **It does not own the GPU domain status**: its `Domains()` is `None`, so the
// aggregator's availability never depends on a vendor being present. If no vendor SDK is installed
// the provider is a no-op and the app runs exactly as with PDH/D3DKMT alone.
//
// Loading rules (the reason this file exists):
//   - Never bundle or hard-link a vendor SDK. Each backend resolves its DLL with
//     `LoadLibrary` + `GetProcAddress` through `IVendorModuleLoader` and keeps the handle alive.
//   - Capability is detected **once**, at construction. A failed load is remembered and never
//     retried on every sample. A vendor call that starts failing mid-run marks the backend
//     unavailable and stops; the PDH baseline (registered first) remains in the snapshot.
//   - Every vendor boundary call is SEH-guarded (`__try`/`__except`) so a partial/corrupt DLL
//     cannot take down the process. This is why the raw vendor structs/ABIs stay in the .cpp and
//     never reach `MetricsSnapshot`.
//
// Testability: the loader, the adapter enumerator, and the backends are all seams. Capability and
// selection logic can be exercised with faked load results (present / absent / partial), and the
// pure unit conversions and the reading-application step are unit-tested with no hardware.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pacecar/metrics/GpuD3dKmtProvider.h"
#include "pacecar/metrics/IMetricProvider.h"

namespace pacecar::metrics
{
enum class GpuVendorKind
{
    None,
    Nvidia,
    Amd,
    Intel,
};

[[nodiscard]] const char* GpuVendorName(GpuVendorKind kind) noexcept;

// Maps a DXGI adapter description to a vendor. Case-insensitive substring match; unknown names
// return `None`, which means "try every backend in priority order" rather than "no vendor".
[[nodiscard]] GpuVendorKind ClassifyGpuVendor(std::string_view adapterName) noexcept;

// --- unit conversions (pure, unit-tested) -----------------------------------
inline constexpr double kMilliwattsPerWatt = 1000.0;
inline constexpr std::uint64_t kBytesPerMebibyte = 1024ull * 1024ull;
inline constexpr std::uint64_t kBytesPerGibibyte = 1024ull * 1024ull * 1024ull;
inline constexpr double kMegahertzPerGigahertz = 1000.0;

// NVML reports power in milliwatts; the snapshot stores watts.
[[nodiscard]] inline double MilliwattsToWatts(std::uint64_t milliwatts) noexcept
{
    return static_cast<double>(milliwatts) / kMilliwattsPerWatt;
}

[[nodiscard]] inline double BytesToMebibytes(std::uint64_t bytes) noexcept
{
    return static_cast<double>(bytes) / static_cast<double>(kBytesPerMebibyte);
}

[[nodiscard]] inline double BytesToGibibytes(std::uint64_t bytes) noexcept
{
    return static_cast<double>(bytes) / static_cast<double>(kBytesPerGibibyte);
}

// Clocks are stored in MHz; this is the display conversion for diagnostics/formatting.
[[nodiscard]] inline double MegahertzToGigahertz(double megahertz) noexcept
{
    return megahertz / kMegahertzPerGigahertz;
}

// The vendor-agnostic reading a backend produces. Every field is optional: a backend that cannot
// read one metric simply leaves its `has*` flag false, so the snapshot falls back to the PDH value
// or "unavailable" per metric.
struct GpuVendorReading
{
    bool hasUtilization = false;
    double utilizationPercent = 0.0;
    bool hasTemperature = false;
    double temperatureC = 0.0;
    bool hasPower = false;
    double powerWatts = 0.0;
    bool hasCoreClock = false;
    double coreClockMhz = 0.0;
    bool hasMemoryClock = false;
    double memoryClockMhz = 0.0;
    bool hasFanSpeed = false;
    double fanPercent = 0.0;
    bool hasFanRpm = false;
    std::int32_t fanRpm = 0;
    bool hasMemory = false;
    std::uint64_t vramUsedBytes = 0;
    std::uint64_t vramTotalBytes = 0;
    char deviceName[kGpuNameCapacity] = {};
};

// Applies a reading to the snapshot: values are written only when present (so the PDH baseline is
// never clobbered with a zero), sub-statuses are marked available, and the whole-GPU utilization is
// overridden when the vendor provides it (the design's "vendor overrides/enriches PDH" rule). Pure
// function, unit-tested.
void ApplyGpuVendorReading(const GpuVendorReading& reading,
                           std::uint64_t tick,
                           GpuMetrics& gpu) noexcept;

// Marks every vendor-owned sub-status unavailable without touching values, so a backend that
// disappears mid-run leaves the last PDH values visible. Pure function, unit-tested.
void MarkGpuVendorUnavailable(GpuMetrics& gpu) noexcept;

// --- dynamic-loading seam ---------------------------------------------------
// Returns an opaque module handle or null. None of these throw; a failed load is a null handle.
class IVendorModuleLoader
{
  public:
    virtual ~IVendorModuleLoader() = default;

    virtual void* LoadModule(const wchar_t* libraryName) = 0;
    virtual void* ResolveSymbol(void* module, const char* symbolName) = 0;
    virtual void UnloadModule(void* module) = 0;
};

[[nodiscard]] std::unique_ptr<IVendorModuleLoader> MakeWindowsVendorModuleLoader();

// --- backend seam -----------------------------------------------------------
// One vendor's implementation. `Initialize` performs the one-time capability detection (load the
// DLL, resolve the entry points) and returns true when the module was recognized. `Available` is
// true only when metrics can actually be read; ADLX/IGCL currently recognize their module but
// report no metrics (see the ADR), so they are `Detected` but not `Available`.
class IGpuVendorBackend
{
  public:
    virtual ~IGpuVendorBackend() = default;

    [[nodiscard]] virtual GpuVendorKind Kind() const noexcept = 0;
    [[nodiscard]] virtual const char* Name() const noexcept = 0;

    // Idempotent capability detection. Must not throw and must not leave a dangling module handle.
    // Called at most once per provider lifetime (and not per sample).
    virtual bool Initialize() = 0;

    [[nodiscard]] virtual bool Available() const noexcept = 0;
    [[nodiscard]] virtual bool Detected() const noexcept
    {
        return Available();
    }

    // Reads metrics into `out`. Returns `S_OK` when at least one metric was produced. `E_PENDING`
    // means "nothing yet, try again" and is not an error. Any failing HRESULT / exception tells the
    // provider to mark this backend unavailable and stop polling it.
    virtual HRESULT Poll(GpuVendorReading& out) = 0;

    virtual void Reset() noexcept {}
};

// Builds the real NVML/ADLX/IGCL backends around `loader`. `selection` is `sensors.gpu_selection`,
// used to pick the right device within a vendor that exposes several.
[[nodiscard]] std::vector<std::unique_ptr<IGpuVendorBackend>> MakeVendorBackends(
    IVendorModuleLoader& loader,
    std::string_view selection);

// The GPU vendor enrichment provider. Register it *after* `GpuPdhProvider` in the aggregator so its
// overrides win within a tick.
class GpuVendorProvider final : public IMetricProvider
{
  public:
    // Uses the real module loader, DXGI enumeration, and all three backends.
    GpuVendorProvider();

    // Same, but with the configured `sensors.gpu_selection` so the device within a multi-GPU vendor
    // resolves to the same adapter the PDH baseline selected.
    explicit GpuVendorProvider(std::string selection);

    // Test/embedding seam: callers supply the loader, adapter enumerator, and backends.
    GpuVendorProvider(std::unique_ptr<IVendorModuleLoader> loader,
                      std::unique_ptr<IGpuAdapterEnumerator> adapters,
                      std::vector<std::unique_ptr<IGpuVendorBackend>> backends,
                      std::string selection);

    ~GpuVendorProvider() override;

    GpuVendorProvider(const GpuVendorProvider&) = delete;
    GpuVendorProvider& operator=(const GpuVendorProvider&) = delete;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] std::uint32_t Domains() const noexcept override;
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override;
    HRESULT Poll(MetricsSnapshot& snapshot) override;
    void Reset() noexcept override;

    // `None` when no vendor backend is active (missing SDK, partial/corrupt DLL, or a mid-run
    // failure). `ActiveVendor` is the backend currently enriching the snapshot.
    [[nodiscard]] GpuVendorKind ActiveVendor() const noexcept
    {
        return activeKind_;
    }
    // The backend that was recognized even if it cannot read metrics (for the diagnostics window).
    [[nodiscard]] GpuVendorKind DetectedVendor() const noexcept
    {
        return detectedKind_;
    }
    [[nodiscard]] const char* ActiveVendorName() const noexcept
    {
        return GpuVendorName(activeKind_);
    }
    [[nodiscard]] const char* SelectedAdapterName() const noexcept
    {
        return selected_.name;
    }

  private:
    void EnumerateAndSelect(const std::string& selectionText);
    IGpuVendorBackend* SelectBackend() noexcept;

    std::unique_ptr<IVendorModuleLoader> loader_;
    std::unique_ptr<IGpuAdapterEnumerator> adapters_;
    std::vector<std::unique_ptr<IGpuVendorBackend>> backends_;
    GpuAdapterInfo selected_{};
    IGpuVendorBackend* backend_ = nullptr;
    GpuVendorKind preferredKind_ = GpuVendorKind::None;
    GpuVendorKind activeKind_ = GpuVendorKind::None;
    GpuVendorKind detectedKind_ = GpuVendorKind::None;
};
} // namespace pacecar::metrics