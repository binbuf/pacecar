#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "pacecar/metrics/GpuVendorProvider.h"

namespace
{
using pacecar::metrics::ApplyGpuVendorReading;
using pacecar::metrics::BytesToGibibytes;
using pacecar::metrics::BytesToMebibytes;
using pacecar::metrics::ClassifyGpuVendor;
using pacecar::metrics::GpuAdapterInfo;
using pacecar::metrics::GpuLuid;
using pacecar::metrics::GpuMetrics;
using pacecar::metrics::GpuVendorKind;
using pacecar::metrics::GpuVendorProvider;
using pacecar::metrics::GpuVendorReading;
using pacecar::metrics::IGpuAdapterEnumerator;
using pacecar::metrics::IGpuVendorBackend;
using pacecar::metrics::IVendorModuleLoader;
using pacecar::metrics::kGpuNameCapacity;
using pacecar::metrics::MakeVendorBackends;
using pacecar::metrics::MarkGpuVendorUnavailable;
using pacecar::metrics::MegahertzToGigahertz;
using pacecar::metrics::MetricsSnapshot;
using pacecar::metrics::MilliwattsToWatts;

// --- fakes -----------------------------------------------------------------

class FakeAdapterEnumerator final : public IGpuAdapterEnumerator
{
  public:
    bool Enumerate(std::vector<GpuAdapterInfo>& out) override
    {
        out = adapters;
        return true;
    }

    std::vector<GpuAdapterInfo> adapters;
};

void CopyText(char* destination, std::size_t capacity, const char* source)
{
    const std::size_t length = std::strlen(source);
    const std::size_t count = length < capacity ? length : capacity - 1;
    std::memcpy(destination, source, count);
    destination[count] = '\0';
}

GpuAdapterInfo MakeAdapter(const char* name)
{
    GpuAdapterInfo adapter{};
    CopyText(adapter.name, kGpuNameCapacity, name);
    return adapter;
}

class FakeVendorBackend final : public IGpuVendorBackend
{
  public:
    FakeVendorBackend(GpuVendorKind kind, bool detected, bool available)
        : kind_(kind), detected_(detected), available_(available)
    {
    }

    [[nodiscard]] GpuVendorKind Kind() const noexcept override
    {
        return kind_;
    }
    [[nodiscard]] const char* Name() const noexcept override
    {
        return pacecar::metrics::GpuVendorName(kind_);
    }
    bool Initialize() override
    {
        ++initializeCount;
        return detected_;
    }
    [[nodiscard]] bool Available() const noexcept override
    {
        return available_;
    }
    [[nodiscard]] bool Detected() const noexcept override
    {
        return detected_;
    }
    HRESULT Poll(GpuVendorReading& out) override
    {
        ++pollCount;
        if (failNext)
        {
            failNext = false;
            return E_FAIL;
        }
        out = reading;
        return S_OK;
    }

    GpuVendorReading reading;
    bool failNext = false;
    int initializeCount = 0;
    int pollCount = 0;

  private:
    GpuVendorKind kind_;
    bool detected_;
    bool available_;
};

class FakeVendorModuleLoader final : public IVendorModuleLoader
{
  public:
    struct Module
    {
        std::wstring name;
        void* handle = nullptr;
        std::vector<std::string> symbols;
    };

    void* LoadModule(const wchar_t* libraryName) override
    {
        ++loadCount;
        for (Module& module : modules)
        {
            if (module.name == libraryName)
            {
                return module.handle;
            }
        }
        return nullptr;
    }

    void* ResolveSymbol(void* module, const char* symbolName) override
    {
        for (Module& candidate : modules)
        {
            if (candidate.handle != module)
            {
                continue;
            }
            for (const std::string& symbol : candidate.symbols)
            {
                if (symbol == symbolName)
                {
                    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(1)); // non-null
                }
            }
        }
        return nullptr;
    }

    void UnloadModule(void*) override {}

    void Add(const wchar_t* name, std::initializer_list<const char*> symbols)
    {
        Module module{};
        module.name = name;
        module.handle =
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(modules.size()) + 1);
        for (const char* symbol : symbols)
        {
            module.symbols.emplace_back(symbol);
        }
        modules.push_back(std::move(module));
    }

    std::vector<Module> modules;
    int loadCount = 0;
};

std::vector<std::unique_ptr<IGpuVendorBackend>> OneFakeBackend(FakeVendorBackend*& out,
                                                               GpuVendorKind kind,
                                                               bool detected = true,
                                                               bool available = true)
{
    std::vector<std::unique_ptr<IGpuVendorBackend>> backends;
    auto backend = std::make_unique<FakeVendorBackend>(kind, detected, available);
    out = backend.get();
    backends.push_back(std::move(backend));
    return backends;
}

// --- classification and conversions ----------------------------------------

TEST(GpuVendorClassification, MapsAdapterNamesToVendors)
{
    EXPECT_EQ(ClassifyGpuVendor("NVIDIA GeForce RTX 4080"), GpuVendorKind::Nvidia);
    EXPECT_EQ(ClassifyGpuVendor("AMD Radeon(TM) Graphics"), GpuVendorKind::Amd);
    EXPECT_EQ(ClassifyGpuVendor("Intel(R) Arc(TM) A770 Graphics"), GpuVendorKind::Intel);
    EXPECT_EQ(ClassifyGpuVendor("Intel(R) Iris(R) Xe Graphics"), GpuVendorKind::Intel);
    EXPECT_EQ(ClassifyGpuVendor("Microsoft Basic Render Driver"), GpuVendorKind::None);
    EXPECT_EQ(ClassifyGpuVendor(""), GpuVendorKind::None);
}

TEST(GpuVendorConversions, MilliwattsToWatts)
{
    EXPECT_DOUBLE_EQ(MilliwattsToWatts(0), 0.0);
    EXPECT_DOUBLE_EQ(MilliwattsToWatts(150'000), 150.0);
    EXPECT_DOUBLE_EQ(MilliwattsToWatts(320'500), 320.5);
}

TEST(GpuVendorConversions, BytesToMebibytesAndGibibytes)
{
    EXPECT_DOUBLE_EQ(BytesToMebibytes(0), 0.0);
    EXPECT_DOUBLE_EQ(BytesToMebibytes(1024ull * 1024ull), 1.0);
    EXPECT_DOUBLE_EQ(BytesToMebibytes(3ull * 1024ull * 1024ull), 3.0);
    EXPECT_DOUBLE_EQ(BytesToGibibytes(1024ull * 1024ull * 1024ull), 1.0);
    EXPECT_DOUBLE_EQ(BytesToGibibytes(8ull * 1024ull * 1024ull * 1024ull), 8.0);
    EXPECT_DOUBLE_EQ(BytesToMebibytes(1536ull * 1024ull * 1024ull), 1536.0);
}

TEST(GpuVendorConversions, MegahertzToGigahertz)
{
    EXPECT_DOUBLE_EQ(MegahertzToGigahertz(0.0), 0.0);
    EXPECT_DOUBLE_EQ(MegahertzToGigahertz(2505.0), 2.505);
    EXPECT_DOUBLE_EQ(MegahertzToGigahertz(1000.0), 1.0);
}

// --- applying a reading -----------------------------------------------------

TEST(GpuVendorReadingApplication, AppliesPresentFieldsAndMarksStatuses)
{
    GpuVendorReading reading{};
    reading.hasUtilization = true;
    reading.utilizationPercent = 42.0;
    reading.hasTemperature = true;
    reading.temperatureC = 55.0;
    reading.hasPower = true;
    reading.powerWatts = 150.0;
    reading.hasCoreClock = true;
    reading.coreClockMhz = 2505.0;
    reading.hasMemoryClock = true;
    reading.memoryClockMhz = 10501.0;
    reading.hasFanSpeed = true;
    reading.fanPercent = 47.0;
    reading.hasMemory = true;
    reading.vramUsedBytes = 4ull * 1024ull * 1024ull * 1024ull;
    reading.vramTotalBytes = 16ull * 1024ull * 1024ull * 1024ull;
    CopyText(reading.deviceName, kGpuNameCapacity, "NVIDIA GeForce RTX 4080");

    GpuMetrics gpu{};
    ApplyGpuVendorReading(reading, 7, gpu);

    EXPECT_DOUBLE_EQ(gpu.utilizationPercent, 42.0);
    EXPECT_DOUBLE_EQ(gpu.temperatureC, 55.0);
    EXPECT_TRUE(gpu.temperatureStatus.available);
    EXPECT_FALSE(gpu.temperatureStatus.stale);
    EXPECT_EQ(gpu.temperatureStatus.lastSuccessTick, 7u);
    EXPECT_DOUBLE_EQ(gpu.powerWatts, 150.0);
    EXPECT_TRUE(gpu.powerStatus.available);
    EXPECT_DOUBLE_EQ(gpu.coreClockMhz, 2505.0);
    EXPECT_DOUBLE_EQ(gpu.memoryClockMhz, 10501.0);
    EXPECT_TRUE(gpu.clockStatus.available);
    EXPECT_DOUBLE_EQ(gpu.fanPercent, 47.0);
    EXPECT_TRUE(gpu.fanStatus.available);
    EXPECT_EQ(gpu.vramUsedBytes, 4ull * 1024ull * 1024ull * 1024ull);
    EXPECT_EQ(gpu.vramTotalBytes, 16ull * 1024ull * 1024ull * 1024ull);
    EXPECT_TRUE(gpu.vramStatus.available);
    // The baseline adapter name was empty, so the vendor fills it.
    EXPECT_STREQ(gpu.name, "NVIDIA GeForce RTX 4080");
}

TEST(GpuVendorReadingApplication, MissingFieldsDoNotClobberBaseline)
{
    GpuMetrics gpu{};
    CopyText(gpu.name, kGpuNameCapacity, "AMD Radeon(TM) Graphics");
    gpu.utilizationPercent = 33.0;
    gpu.temperatureC = 61.0;
    gpu.temperatureStatus.available = true;

    GpuVendorReading reading{};
    reading.hasPower = true;
    reading.powerWatts = 90.0;
    reading.deviceName[0] = '\0';
    ApplyGpuVendorReading(reading, 1, gpu);

    // Only power is present; the PDH utilization/temperature/name survive untouched.
    EXPECT_DOUBLE_EQ(gpu.utilizationPercent, 33.0);
    EXPECT_DOUBLE_EQ(gpu.temperatureC, 61.0);
    EXPECT_TRUE(gpu.temperatureStatus.available);
    EXPECT_DOUBLE_EQ(gpu.powerWatts, 90.0);
    EXPECT_TRUE(gpu.powerStatus.available);
    EXPECT_FALSE(gpu.vramStatus.available);
    EXPECT_STREQ(gpu.name, "AMD Radeon(TM) Graphics");
}

TEST(GpuVendorReadingApplication, UtilizationIsClamped)
{
    GpuVendorReading reading{};
    reading.hasUtilization = true;
    reading.utilizationPercent = 250.0;
    GpuMetrics gpu{};
    ApplyGpuVendorReading(reading, 1, gpu);
    EXPECT_DOUBLE_EQ(gpu.utilizationPercent, 100.0);

    reading.utilizationPercent = -5.0;
    ApplyGpuVendorReading(reading, 2, gpu);
    EXPECT_DOUBLE_EQ(gpu.utilizationPercent, 0.0);
}

TEST(GpuVendorReadingApplication, MarkUnavailableKeepsValuesButClearsStatus)
{
    GpuMetrics gpu{};
    gpu.temperatureC = 70.0;
    gpu.powerWatts = 200.0;
    gpu.temperatureStatus.available = true;
    gpu.powerStatus.available = true;

    MarkGpuVendorUnavailable(gpu);

    EXPECT_DOUBLE_EQ(gpu.temperatureC, 70.0);
    EXPECT_DOUBLE_EQ(gpu.powerWatts, 200.0);
    EXPECT_FALSE(gpu.temperatureStatus.available);
    EXPECT_TRUE(gpu.temperatureStatus.stale);
    EXPECT_FALSE(gpu.powerStatus.available);
    EXPECT_FALSE(gpu.fanStatus.available);
}

// --- provider capability detection with faked load results ------------------

TEST(GpuVendorProvider, NoVendorDllIsANoOpAndDoesNotOwnTheDomain)
{
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = {MakeAdapter("NVIDIA GeForce RTX 4080")};
    auto loader = std::make_unique<FakeVendorModuleLoader>(); // serves nothing

    auto backends = MakeVendorBackends(*loader, "auto");
    GpuVendorProvider provider(std::move(loader), std::move(adapters), std::move(backends), "auto");

    EXPECT_EQ(provider.Domains(), 0u); // never marks the GPU domain unavailable/available
    EXPECT_EQ(provider.ActiveVendor(), GpuVendorKind::None);
    EXPECT_EQ(provider.DetectedVendor(), GpuVendorKind::None);

    MetricsSnapshot snapshot;
    snapshot.gpu.temperatureC = 55.0; // PDH baseline value
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(provider.Poll(snapshot), S_OK); // graceful, never throws
    }
    // PDH values are untouched.
    EXPECT_DOUBLE_EQ(snapshot.gpu.temperatureC, 55.0);
}

TEST(GpuVendorProvider, PartialNvmlLoadIsRejectedOnceAndNotRetried)
{
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = {MakeAdapter("NVIDIA GeForce RTX 4080")};
    auto loader = std::make_unique<FakeVendorModuleLoader>();
    // Present DLL, but missing the mandatory device-count/handle entry points.
    loader->Add(L"nvml.dll", {"nvmlInit_v2"});
    FakeVendorModuleLoader* fakeLoader = loader.get();

    auto backends = MakeVendorBackends(*fakeLoader, "auto");
    GpuVendorProvider provider(std::move(loader), std::move(adapters), std::move(backends), "auto");

    EXPECT_EQ(provider.ActiveVendor(), GpuVendorKind::None);
    const int loadsAfterConstruction = fakeLoader->loadCount;
    EXPECT_GE(loadsAfterConstruction, 1);

    MetricsSnapshot snapshot;
    for (int i = 0; i < 4; ++i)
    {
        provider.Poll(snapshot);
    }
    // Capability was probed once; samples never retry the load.
    EXPECT_EQ(fakeLoader->loadCount, loadsAfterConstruction);
}

TEST(GpuVendorProvider, AdlxIsDetectedButReportsNoMetrics)
{
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = {MakeAdapter("AMD Radeon(TM) Graphics")};
    auto loader = std::make_unique<FakeVendorModuleLoader>();
    loader->Add(L"ADLX.dll", {"ADLXInitialize"});

    auto backends = MakeVendorBackends(*loader, "auto");
    GpuVendorProvider provider(std::move(loader), std::move(adapters), std::move(backends), "auto");

    EXPECT_EQ(provider.DetectedVendor(), GpuVendorKind::Amd);
    EXPECT_EQ(provider.ActiveVendor(), GpuVendorKind::None);

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
}

TEST(GpuVendorProvider, IgclIsDetectedButReportsNoMetrics)
{
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = {MakeAdapter("Intel(R) Arc(TM) A770 Graphics")};
    auto loader = std::make_unique<FakeVendorModuleLoader>();
    loader->Add(L"ControlLib.dll", {"ctlInit"});

    auto backends = MakeVendorBackends(*loader, "auto");
    GpuVendorProvider provider(std::move(loader), std::move(adapters), std::move(backends), "auto");

    EXPECT_EQ(provider.DetectedVendor(), GpuVendorKind::Intel);
    EXPECT_EQ(provider.ActiveVendor(), GpuVendorKind::None);
}

// --- provider selection logic with fake backends ----------------------------

TEST(GpuVendorProvider, SelectsTheBackendMatchingTheAdapterVendor)
{
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = {MakeAdapter("AMD Radeon(TM) Graphics")};

    FakeVendorBackend* nvidia = nullptr;
    FakeVendorBackend* amd = nullptr;
    std::vector<std::unique_ptr<IGpuVendorBackend>> backends;
    {
        auto n = std::make_unique<FakeVendorBackend>(GpuVendorKind::Nvidia, true, true);
        nvidia = n.get();
        backends.push_back(std::move(n));
        auto a = std::make_unique<FakeVendorBackend>(GpuVendorKind::Amd, true, true);
        amd = a.get();
        backends.push_back(std::move(a));
    }

    GpuVendorProvider provider(nullptr, std::move(adapters), std::move(backends), "auto");

    EXPECT_EQ(provider.ActiveVendor(), GpuVendorKind::Amd);
    EXPECT_EQ(nvidia->initializeCount, 0); // the wrong vendor is never probed
    EXPECT_EQ(amd->initializeCount, 1);

    GpuVendorReading reading{};
    reading.hasTemperature = true;
    reading.temperatureC = 68.0;
    amd->reading = reading;

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.gpu.temperatureC, 68.0);
    EXPECT_TRUE(snapshot.gpu.temperatureStatus.available);
}

TEST(GpuVendorProvider, KnownVendorDoesNotFallBackToADifferentPhysicalGpu)
{
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = {MakeAdapter("NVIDIA GeForce RTX 4080")};

    FakeVendorBackend* nvidia = nullptr;
    // Only an AMD backend exists (its DLL is present); the NVIDIA DLL is absent.
    auto backends = OneFakeBackend(nvidia, GpuVendorKind::Amd, true, true);
    // Force a preferred Nvidia vendor by inserting a detected-but-unavailable Nvidia backend.
    auto detectedNvidia = std::make_unique<FakeVendorBackend>(GpuVendorKind::Nvidia, true, false);
    FakeVendorBackend* nvidiaDetected = detectedNvidia.get();
    backends.insert(backends.begin(), std::move(detectedNvidia));

    GpuVendorProvider provider(nullptr, std::move(adapters), std::move(backends), "auto");

    // The NVIDIA backend is recognized but has no metrics; we must NOT report the AMD GPU instead.
    EXPECT_EQ(provider.DetectedVendor(), GpuVendorKind::Nvidia);
    EXPECT_EQ(provider.ActiveVendor(), GpuVendorKind::None);
    EXPECT_EQ(nvidiaDetected->initializeCount, 1);

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
}

TEST(GpuVendorProvider, UnknownVendorUsesPriorityOrderFirstUsableBackend)
{
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = {MakeAdapter("Some Experimental Accelerator")};

    FakeVendorBackend* nvidia = nullptr;
    FakeVendorBackend* amd = nullptr;
    std::vector<std::unique_ptr<IGpuVendorBackend>> backends;
    {
        auto a = std::make_unique<FakeVendorBackend>(GpuVendorKind::Amd, true, true);
        amd = a.get();
        backends.push_back(std::move(a));
        auto n = std::make_unique<FakeVendorBackend>(GpuVendorKind::Nvidia, true, true);
        nvidia = n.get();
        backends.push_back(std::move(n));
    }

    GpuVendorProvider provider(nullptr, std::move(adapters), std::move(backends), "auto");
    EXPECT_EQ(provider.ActiveVendor(), GpuVendorKind::Amd);
    EXPECT_EQ(amd->initializeCount, 1);
    EXPECT_EQ(nvidia->initializeCount, 0);
}

// --- mid-run failure behavior ----------------------------------------------

TEST(GpuVendorProvider, MidRunErrorMarksUnavailableAndDoesNotThrowOrRetry)
{
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = {MakeAdapter("NVIDIA GeForce RTX 4080")};

    FakeVendorBackend* backend = nullptr;
    auto backends = OneFakeBackend(backend, GpuVendorKind::Nvidia, true, true);

    GpuVendorProvider provider(nullptr, std::move(adapters), std::move(backends), "auto");
    ASSERT_EQ(provider.ActiveVendor(), GpuVendorKind::Nvidia);

    GpuVendorReading reading{};
    reading.hasTemperature = true;
    reading.temperatureC = 70.0;
    backend->reading = reading;

    MetricsSnapshot snapshot;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_DOUBLE_EQ(snapshot.gpu.temperatureC, 70.0);

    // The vendor call now returns an error mid-run.
    backend->failNext = true;
    EXPECT_EQ(provider.Poll(snapshot), S_OK); // never throws
    EXPECT_EQ(provider.ActiveVendor(), GpuVendorKind::None);
    EXPECT_FALSE(snapshot.gpu.temperatureStatus.available);
    EXPECT_TRUE(snapshot.gpu.temperatureStatus.stale);

    const int pollsAfterFailure = backend->pollCount;
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(provider.Poll(snapshot), S_OK);
    }
    EXPECT_EQ(backend->pollCount, pollsAfterFailure); // no per-sample retry
}

TEST(GpuVendorProvider, PendingBackendProducesNoValues)
{
    auto adapters = std::make_unique<FakeAdapterEnumerator>();
    adapters->adapters = {MakeAdapter("NVIDIA GeForce RTX 4080")};

    FakeVendorBackend* backend = nullptr;
    auto backends = OneFakeBackend(backend, GpuVendorKind::Nvidia, true, true);
    backend->reading = GpuVendorReading{}; // empty

    GpuVendorProvider provider(nullptr, std::move(adapters), std::move(backends), "auto");
    MetricsSnapshot snapshot;
    snapshot.gpu.utilizationPercent = 25.0;
    EXPECT_EQ(provider.Poll(snapshot), S_OK);
    // No vendor utilization flag -> PDH value is retained.
    EXPECT_DOUBLE_EQ(snapshot.gpu.utilizationPercent, 25.0);
}

// --- real-hardware smoke (skipped when no supported vendor is present) ------

TEST(GpuVendorProvider, RealMachineNvmlPublishesPlausibleValuesIfPresent)
{
    GpuVendorProvider provider("auto");
    if (provider.ActiveVendor() != GpuVendorKind::Nvidia)
    {
        GTEST_SKIP() << "no active NVML vendor on this host";
    }

    MetricsSnapshot snapshot;
    ASSERT_EQ(provider.Poll(snapshot), S_OK);
    EXPECT_GE(snapshot.gpu.temperatureC, 0.0);
    EXPECT_LE(snapshot.gpu.utilizationPercent, 100.0);
    EXPECT_GE(snapshot.gpu.powerWatts, 0.0);
    EXPECT_TRUE(snapshot.gpu.temperatureStatus.available);
    EXPECT_FALSE(snapshot.gpu.name[0] == '\0');
}
} // namespace
