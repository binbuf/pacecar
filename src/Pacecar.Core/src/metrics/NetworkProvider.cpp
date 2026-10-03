#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include "pacecar/metrics/NetworkProvider.h"

// `ws2ipdef.h` must precede `netioapi.h` (pulled in by `iphlpapi.h`): without it the
// `_WS2IPDEF_`-guarded `MIB_IF_TABLE2`/`MIB_IF_ROW2` definitions are not visible.
#include <winsock2.h>
#include <ws2ipdef.h>

#include <windows.h>

#include <iphlpapi.h>
#include <netioapi.h>

#include <cstring>

#pragma comment(lib, "iphlpapi.lib")

namespace pacecar::metrics
{
namespace
{
void CopyWideToNarrow(const wchar_t* source, char* destination, int capacity) noexcept
{
    if (destination == nullptr || capacity <= 0)
    {
        return;
    }
    destination[0] = '\0';
    if (source == nullptr)
    {
        return;
    }
    const int written =
        ::WideCharToMultiByte(CP_UTF8, 0, source, -1, destination, capacity - 1, nullptr, nullptr);
    if (written <= 0)
    {
        destination[0] = '\0';
    }
    else
    {
        destination[capacity - 1] = '\0';
    }
}

void CopyName(char (&destination)[kNameCapacity], const char* source) noexcept
{
    if (source == nullptr)
    {
        destination[0] = '\0';
        return;
    }
    const std::size_t length = std::strlen(source);
    const std::size_t count = length < kNameCapacity - 1 ? length : kNameCapacity - 1;
    std::memcpy(destination, source, count);
    destination[count] = '\0';
}

// Real `GetIfTable2` source. The table is freed with `FreeMibTable` as the design requires.
class GetIfTable2Source final : public INetworkSystemSource
{
  public:
    bool ReadInterfaces(std::vector<NetworkInterfaceCounters>& out) override
    {
        out.clear();
        MIB_IF_TABLE2* table = nullptr;
        const NETIO_STATUS status = ::GetIfTable2(&table);
        if (status != NO_ERROR || table == nullptr)
        {
            return false;
        }

        out.reserve(table->NumEntries);
        for (ULONG i = 0; i < table->NumEntries; ++i)
        {
            const MIB_IF_ROW2& row = table->Table[i];
            NetworkInterfaceCounters counters{};
            counters.luid = row.InterfaceLuid.Value;
            CopyWideToNarrow(row.Alias, counters.name, static_cast<int>(sizeof(counters.name)));
            CopyWideToNarrow(row.Description, counters.description,
                             static_cast<int>(sizeof(counters.description)));
            counters.isLoopback = row.Type == IF_TYPE_SOFTWARE_LOOPBACK;
            counters.isUp = row.OperStatus == IfOperStatusUp;
            counters.inOctets = static_cast<std::uint64_t>(row.InOctets);
            counters.outOctets = static_cast<std::uint64_t>(row.OutOctets);
            out.push_back(counters);
        }

        ::FreeMibTable(table);
        return true;
    }
};
} // namespace

std::unique_ptr<INetworkSystemSource> MakeGetIfTable2Source()
{
    return std::make_unique<GetIfTable2Source>();
}

void AggregateNetworkCounters(const std::vector<NetworkInterfaceCounters>& interfaces,
                              const DeviceSelection& selection,
                              NetworkSelectionResult& out) noexcept
{
    out = NetworkSelectionResult{};

    const NetworkInterfaceCounters* first = nullptr;
    std::uint32_t nonLoopbackIndex = 0;
    for (const NetworkInterfaceCounters& iface : interfaces)
    {
        bool match = false;
        switch (selection.kind)
        {
        case DeviceSelectionKind::All:
            match = !iface.isLoopback;
            break;
        case DeviceSelectionKind::Name:
            match = NameContainsInsensitive(iface.name, selection.name) ||
                    (iface.description[0] != '\0' &&
                     NameContainsInsensitive(iface.description, selection.name));
            break;
        case DeviceSelectionKind::Index:
            if (!iface.isLoopback)
            {
                match = nonLoopbackIndex == selection.index;
                ++nonLoopbackIndex;
            }
            break;
        }
        if (!match)
        {
            continue;
        }
        if (first == nullptr)
        {
            first = &iface;
        }
        out.inOctets += iface.inOctets;
        out.outOctets += iface.outOctets;
        ++out.interfaceCount;
    }

    if (out.interfaceCount == 0 || first == nullptr)
    {
        out.name[0] = '\0';
    }
    else if (out.interfaceCount == 1)
    {
        CopyName(out.name, first->name);
    }
    else if (selection.kind == DeviceSelectionKind::All)
    {
        CopyName(out.name, "All interfaces");
    }
    else
    {
        CopyName(out.name, "Multiple interfaces");
    }
}

double ComputeByteRate(std::uint64_t previous, std::uint64_t current, double elapsedSeconds) noexcept
{
    if (!(elapsedSeconds > 0.0))
    {
        return 0.0;
    }
    if (current < previous)
    {
        // A link reset moved the counter backwards; report zero instead of a wrap-sized spike.
        return 0.0;
    }
    return static_cast<double>(current - previous) / elapsedSeconds;
}

NetworkProvider::NetworkProvider()
    : NetworkProvider(MakeGetIfTable2Source(), pacecar::MakeQpcElapsedClock(), "auto")
{
}

NetworkProvider::NetworkProvider(std::unique_ptr<INetworkSystemSource> source,
                                 std::unique_ptr<pacecar::IElapsedClock> clock,
                                 std::string selection)
    : source_(std::move(source)), clock_(std::move(clock)),
      selection_(ParseDeviceSelection(selection))
{
}

NetworkProvider::~NetworkProvider() = default;

const char* NetworkProvider::Name() const noexcept
{
    return "network";
}

std::uint32_t NetworkProvider::Domains() const noexcept
{
    return static_cast<std::uint32_t>(MetricDomain::Network);
}

std::chrono::milliseconds NetworkProvider::Cadence() const noexcept
{
    return std::chrono::milliseconds(1000);
}

HRESULT NetworkProvider::Poll(MetricsSnapshot& snapshot)
{
    if (!source_ || !clock_)
    {
        return E_FAIL;
    }

    if (!source_->ReadInterfaces(interfaces_))
    {
        return E_FAIL;
    }

    AggregateNetworkCounters(interfaces_, selection_, current_);
    if (current_.interfaceCount == 0)
    {
        // The source is present but nothing matched (for example a loopback-only machine with the
        // default "all" selection). Degrade to unavailable rather than publish a meaningless zero.
        return E_FAIL;
    }

    CopyName(selectedName_, current_.name);

    const std::uint64_t now = clock_->NowTicks();
    const std::uint64_t ticksPerSecond = clock_->TicksPerSecond();
    if (ticksPerSecond == 0)
    {
        return E_FAIL;
    }

    if (!hasBaseline_)
    {
        previousIn_ = current_.inOctets;
        previousOut_ = current_.outOctets;
        previousTicks_ = now;
        hasBaseline_ = true;
        // A delta needs a previous sample; the aggregator shows a placeholder for one tick.
        return E_PENDING;
    }

    const std::uint64_t elapsedTicks = now >= previousTicks_ ? now - previousTicks_ : 0;
    const double elapsedSeconds =
        static_cast<double>(elapsedTicks) / static_cast<double>(ticksPerSecond);

    snapshot.network.upBytesPerSecond =
        ComputeByteRate(previousOut_, current_.outOctets, elapsedSeconds);
    snapshot.network.downBytesPerSecond =
        ComputeByteRate(previousIn_, current_.inOctets, elapsedSeconds);
    snapshot.network.totalUpBytes = current_.outOctets;
    snapshot.network.totalDownBytes = current_.inOctets;
    CopyName(snapshot.network.interfaceName, current_.name);

    previousIn_ = current_.inOctets;
    previousOut_ = current_.outOctets;
    previousTicks_ = now;
    return S_OK;
}

void NetworkProvider::Reset() noexcept
{
    previousIn_ = 0;
    previousOut_ = 0;
    previousTicks_ = 0;
    hasBaseline_ = false;
    selectedName_[0] = '\0';
    current_ = NetworkSelectionResult{};
}
} // namespace pacecar::metrics