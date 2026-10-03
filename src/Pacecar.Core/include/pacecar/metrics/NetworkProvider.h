#pragma once

// Network throughput provider: unprivileged per-interface octet deltas over real elapsed time.
//
// Source (`docs/design/02-metrics.md`): `GetIfTable2` gives each interface's cumulative
// `InOctets`/`OutOctets`; throughput is the delta over the actual measured interval, not an assumed
// cadence. `FreeMibTable` is called afterwards. No elevation and no driver are required.
//
// Selection: `sensors.nic_selection` is a loose string (see `DeviceSelection.h`). "auto"/"all" sums
// every non-loopback interface (the legacy and Task Manager behaviour); an index selects the nth
// non-loopback interface in enumeration order; a name is matched case-insensitively against the
// interface alias or description.
//
// Defensive delta math: the counters are 64-bit and never practically wrap, but a link reset can
// make a counter move backwards. `ComputeByteRate` treats a backwards move as a reset and reports 0
// rather than an absurd spike, then the new value becomes the next baseline.
//
// Testability: `GetIfTable2` sits behind `INetworkSystemSource`, elapsed time behind
// `pacecar::IElapsedClock`, and both the selection/aggregation and the rate math are pure.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pacecar/metrics/DeviceSelection.h"
#include "pacecar/metrics/IMetricProvider.h"
#include "pacecar/util/ElapsedClock.h"

namespace pacecar::metrics
{
// One interface's cumulative counters plus enough metadata to filter and label it.
struct NetworkInterfaceCounters
{
    std::uint64_t luid = 0;
    char name[kNameCapacity] = {};        // friendly alias
    char description[kNameCapacity] = {}; // NDIS description
    bool isLoopback = false;
    bool isUp = false;
    std::uint64_t inOctets = 0;
    std::uint64_t outOctets = 0;
};

// Abstracts the `GetIfTable2` read so the provider can be driven by fakes in tests.
class INetworkSystemSource
{
  public:
    virtual ~INetworkSystemSource() = default;

    // Fills `out` with every row of the interface table. Returns false when the table cannot be
    // read at all.
    virtual bool ReadInterfaces(std::vector<NetworkInterfaceCounters>& out) = 0;
};

[[nodiscard]] std::unique_ptr<INetworkSystemSource> MakeGetIfTable2Source();

// The selected interface set's cumulative totals and display name, produced from one read.
struct NetworkSelectionResult
{
    char name[kNameCapacity] = {};
    std::uint64_t inOctets = 0;
    std::uint64_t outOctets = 0;
    std::size_t interfaceCount = 0;
};

// Filters `interfaces` by `selection` and sums their cumulative counters. Never allocates. `out` is
// zeroed first; `out.name` is the interface alias for a single match, "All interfaces" for an
// aggregate, or empty when nothing matched.
void AggregateNetworkCounters(const std::vector<NetworkInterfaceCounters>& interfaces,
                              const DeviceSelection& selection,
                              NetworkSelectionResult& out) noexcept;

// `(current - previous) / elapsedSeconds`, clamped to zero when the interval is non-positive or the
// counter moved backwards (a reset). Pure and shared with the tests.
[[nodiscard]] double ComputeByteRate(std::uint64_t previous,
                                     std::uint64_t current,
                                     double elapsedSeconds) noexcept;

class NetworkProvider final : public IMetricProvider
{
  public:
    // Uses the real `GetIfTable2` source, `QueryPerformanceCounter`, and the "auto" selection.
    NetworkProvider();

    // Test/embedding seam: callers supply the source, clock, and selection string.
    NetworkProvider(std::unique_ptr<INetworkSystemSource> source,
                    std::unique_ptr<pacecar::IElapsedClock> clock,
                    std::string selection);

    ~NetworkProvider() override;

    NetworkProvider(const NetworkProvider&) = delete;
    NetworkProvider& operator=(const NetworkProvider&) = delete;

    [[nodiscard]] const char* Name() const noexcept override;
    [[nodiscard]] std::uint32_t Domains() const noexcept override;
    [[nodiscard]] std::chrono::milliseconds Cadence() const noexcept override;
    HRESULT Poll(MetricsSnapshot& snapshot) override;
    void Reset() noexcept override;

    // Diagnostics for tests and the sampler.
    [[nodiscard]] const char* SelectedInterfaceName() const noexcept
    {
        return selectedName_;
    }
    [[nodiscard]] std::uint64_t BaselineInOctets() const noexcept
    {
        return previousIn_;
    }
    [[nodiscard]] std::uint64_t BaselineOutOctets() const noexcept
    {
        return previousOut_;
    }

  private:
    std::unique_ptr<INetworkSystemSource> source_;
    std::unique_ptr<pacecar::IElapsedClock> clock_;
    DeviceSelection selection_{};

    std::vector<NetworkInterfaceCounters> interfaces_; // reused across polls
    NetworkSelectionResult current_{};

    std::uint64_t previousIn_ = 0;
    std::uint64_t previousOut_ = 0;
    std::uint64_t previousTicks_ = 0;
    bool hasBaseline_ = false;

    char selectedName_[kNameCapacity] = {};
};
} // namespace pacecar::metrics