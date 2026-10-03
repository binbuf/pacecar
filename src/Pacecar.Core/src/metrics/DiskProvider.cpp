#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include "pacecar/metrics/DiskProvider.h"

#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>

#include <cstdint>
#include <cstring>

#include "pacecar/util/Pdh.h"

#pragma comment(lib, "pdh.lib")

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

[[nodiscard]] bool IsTotalInstance(const char* name) noexcept
{
    return name != nullptr && std::strcmp(name, "_Total") == 0;
}

// Real PDH source over the two `PhysicalDisk` rate counters. Both counters come from one persistent
// wildcard query; the caller discards the first sample because a rate counter needs two collections.
class PdhDiskSource final : public IDiskSource
{
  public:
    PdhDiskSource()
    {
        if (!query_.IsValid())
        {
            return;
        }
        const PDH_STATUS readStatus = query_.AddEnglishCounter(
            L"\\PhysicalDisk(*)\\Disk Read Bytes/sec", readCounter_);
        const PDH_STATUS writeStatus = query_.AddEnglishCounter(
            L"\\PhysicalDisk(*)\\Disk Write Bytes/sec", writeCounter_);
        available_ = readStatus == ERROR_SUCCESS && writeStatus == ERROR_SUCCESS &&
                     readCounter_.IsValid() && writeCounter_.IsValid();
    }

    [[nodiscard]] bool IsAvailable() override
    {
        return available_;
    }

    bool Read(std::vector<DiskCounterSample>& out) override
    {
        out.clear();
        if (!available_ || query_.Collect() != ERROR_SUCCESS)
        {
            return false;
        }
        if (!primed_)
        {
            // A rate counter needs two collections: the first is the baseline and its formatted
            // values are `PDH_INVALID_DATA`. Report "no data yet" rather than a hard failure so the
            // provider can return E_PENDING for exactly one tick.
            primed_ = true;
            return true;
        }

        const bool readOk = ReadArray(readCounter_.Handle(), readBuffer_, readCount_);
        const bool writeOk = ReadArray(writeCounter_.Handle(), writeBuffer_, writeCount_);
        if (!readOk && !writeOk)
        {
            return false;
        }

        if (readOk)
        {
            const auto* items = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(readBuffer_.data());
            out.reserve(readCount_);
            for (std::uint32_t i = 0; i < readCount_; ++i)
            {
                DiskCounterSample sample{};
                CopyWideToNarrow(items[i].szName, sample.instanceName,
                                 static_cast<int>(sizeof(sample.instanceName)));
                const DWORD status = items[i].FmtValue.CStatus;
                if (status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA)
                {
                    sample.readBytesPerSecond = items[i].FmtValue.doubleValue;
                    sample.readValid = true;
                }
                out.push_back(sample);
            }
        }

        if (writeOk)
        {
            const auto* items = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(writeBuffer_.data());
            for (std::uint32_t i = 0; i < writeCount_; ++i)
            {
                char name[128] = {};
                CopyWideToNarrow(items[i].szName, name, static_cast<int>(sizeof(name)));
                const DWORD status = items[i].FmtValue.CStatus;
                const bool valid = status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA;

                DiskCounterSample* target = nullptr;
                for (DiskCounterSample& sample : out)
                {
                    if (sample.instanceName[0] != '\0' && std::strcmp(sample.instanceName, name) == 0)
                    {
                        target = &sample;
                        break;
                    }
                }
                if (target == nullptr)
                {
                    DiskCounterSample sample{};
                    CopyWideToNarrow(items[i].szName, sample.instanceName,
                                     static_cast<int>(sizeof(sample.instanceName)));
                    if (valid)
                    {
                        sample.writeBytesPerSecond = items[i].FmtValue.doubleValue;
                        sample.writeValid = true;
                    }
                    out.push_back(sample);
                }
                else if (valid)
                {
                    target->writeBytesPerSecond = items[i].FmtValue.doubleValue;
                    target->writeValid = true;
                }
            }
        }

        return true;
    }

  private:
    static bool ReadArray(PDH_HCOUNTER counter,
                          std::vector<std::byte>& buffer,
                          std::uint32_t& count) noexcept
    {
        count = 0;
        if (counter == nullptr)
        {
            return false;
        }
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

    PdhQuery query_;
    PdhCounter readCounter_;
    PdhCounter writeCounter_;
    std::vector<std::byte> readBuffer_;
    std::vector<std::byte> writeBuffer_;
    std::uint32_t readCount_ = 0;
    std::uint32_t writeCount_ = 0;
    bool available_ = false;
    bool primed_ = false;
};
} // namespace

std::unique_ptr<IDiskSource> MakePdhDiskSource()
{
    return std::make_unique<PdhDiskSource>();
}

void AggregateDiskRates(const std::vector<DiskCounterSample>& samples,
                        const DeviceSelection& selection,
                        DiskRate& out) noexcept
{
    out = DiskRate{};

    const DiskCounterSample* first = nullptr;
    const DiskCounterSample* total = nullptr;
    std::uint32_t nonTotalIndex = 0;
    for (const DiskCounterSample& sample : samples)
    {
        const bool isTotal = IsTotalInstance(sample.instanceName);
        if (isTotal)
        {
            total = &sample;
        }

        bool match = false;
        switch (selection.kind)
        {
        case DeviceSelectionKind::All:
            match = !isTotal;
            break;
        case DeviceSelectionKind::Name:
            match = NameContainsInsensitive(sample.instanceName, selection.name);
            break;
        case DeviceSelectionKind::Index:
            if (!isTotal)
            {
                match = nonTotalIndex == selection.index;
                ++nonTotalIndex;
            }
            break;
        }
        if (!match)
        {
            continue;
        }

        if (first == nullptr)
        {
            first = &sample;
        }
        if (sample.readValid)
        {
            out.readBytesPerSecond += sample.readBytesPerSecond;
        }
        if (sample.writeValid)
        {
            out.writeBytesPerSecond += sample.writeBytesPerSecond;
        }
        ++out.instanceCount;
    }

    // A machine that only exposes `_Total` still gets an aggregate rather than unavailable.
    if (selection.kind == DeviceSelectionKind::All && out.instanceCount == 0 && total != nullptr)
    {
        first = total;
        if (total->readValid)
        {
            out.readBytesPerSecond += total->readBytesPerSecond;
        }
        if (total->writeValid)
        {
            out.writeBytesPerSecond += total->writeBytesPerSecond;
        }
        out.instanceCount = 1;
    }

    if (out.instanceCount == 0 || first == nullptr)
    {
        out.name[0] = '\0';
    }
    else if (out.instanceCount == 1)
    {
        CopyName(out.name, first->instanceName);
    }
    else if (selection.kind == DeviceSelectionKind::All)
    {
        CopyName(out.name, "All disks");
    }
    else
    {
        CopyName(out.name, "Multiple disks");
    }
}

DiskProvider::DiskProvider() : DiskProvider(MakePdhDiskSource(), "auto")
{
}

DiskProvider::DiskProvider(std::unique_ptr<IDiskSource> source, std::string selection)
    : source_(std::move(source)), selection_(ParseDeviceSelection(selection))
{
}

DiskProvider::~DiskProvider() = default;

const char* DiskProvider::Name() const noexcept
{
    return "disk";
}

std::uint32_t DiskProvider::Domains() const noexcept
{
    return static_cast<std::uint32_t>(MetricDomain::Disk);
}

std::chrono::milliseconds DiskProvider::Cadence() const noexcept
{
    return std::chrono::milliseconds(1000);
}

HRESULT DiskProvider::Poll(MetricsSnapshot& snapshot)
{
    if (!source_ || !source_->IsAvailable())
    {
        return E_FAIL;
    }
    if (!source_->Read(samples_))
    {
        return E_FAIL;
    }
    if (firstRead_)
    {
        // A rate counter needs two collections; the UI shows a placeholder for one tick.
        firstRead_ = false;
        return E_PENDING;
    }

    AggregateDiskRates(samples_, selection_, current_);
    if (current_.instanceCount == 0)
    {
        return E_FAIL;
    }

    snapshot.disk.readBytesPerSecond = current_.readBytesPerSecond;
    snapshot.disk.writeBytesPerSecond = current_.writeBytesPerSecond;
    CopyName(snapshot.disk.name, current_.name);
    return S_OK;
}

void DiskProvider::Reset() noexcept
{
    firstRead_ = true;
    current_ = DiskRate{};
}
} // namespace pacecar::metrics