#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "pacecar/metrics/GpuVendorProvider.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace pacecar::metrics
{
namespace
{
// --- small string helpers ---------------------------------------------------
[[nodiscard]] char ToLowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle) noexcept
{
    if (needle.empty() || needle.size() > haystack.size())
    {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
    {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j)
        {
            if (ToLowerAscii(haystack[i + j]) != ToLowerAscii(needle[j]))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
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

void CopyNarrow(char* destination, std::size_t capacity, std::string_view source) noexcept
{
    if (destination == nullptr || capacity == 0)
    {
        return;
    }
    const std::size_t count = std::min(source.size(), capacity - 1);
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

// --- SEH-guarded vendor call ------------------------------------------------
// Vendor DLLs are external and may be partial or corrupt. Every call into one goes through this
// guard so an access violation is converted into an error return instead of killing the process.
// The function only ever holds POD by-value parameters, so it requires no C++ object unwinding
// (which MSVC forbids around `__try`). A null or bogus function pointer is caught as well.
//
// C6320 ("exception-filter is the constant EXCEPTION_EXECUTE_HANDLER") is exactly the intent here:
// a vendor DLL is untrusted, so *any* structured exception at the boundary is swallowed and reported
// as a failed call. The analyzer warning is disabled for this one wrapper.
#pragma warning(push)
#pragma warning(disable : 6320)
template <typename... Args>
[[nodiscard]] int GuardedVendorCall(int (*fn)(Args...), Args... args) noexcept
{
    __try
    {
        return fn(args...);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
#pragma warning(pop)

// --- real module loader -----------------------------------------------------
class WindowsVendorModuleLoader final : public IVendorModuleLoader
{
  public:
    void* LoadModule(const wchar_t* libraryName) override
    {
        if (libraryName == nullptr)
        {
            return nullptr;
        }
        return static_cast<void*>(::LoadLibraryW(libraryName));
    }

    void* ResolveSymbol(void* module, const char* symbolName) override
    {
        if (module == nullptr || symbolName == nullptr)
        {
            return nullptr;
        }
        return reinterpret_cast<void*>(
            ::GetProcAddress(static_cast<HMODULE>(module), symbolName));
    }

    void UnloadModule(void* module) override
    {
        if (module != nullptr)
        {
            ::FreeLibrary(static_cast<HMODULE>(module));
        }
    }
};

// Resolves the first symbol name that exists (vendors export `_v2` variants alongside the legacy
// spelling). Returns null when none resolve.
[[nodiscard]] void* ResolveFirst(IVendorModuleLoader& loader, void* module,
                                 const char* const* names, std::size_t count) noexcept
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (void* symbol = loader.ResolveSymbol(module, names[i]))
        {
            return symbol;
        }
    }
    return nullptr;
}

// Selects a device within a multi-GPU vendor: "auto"/empty -> index 0, all-digits -> that index when
// in range, otherwise a case-insensitive substring match against the device names. Unknown names
// fall back to index 0 so a stale config still yields metrics.
[[nodiscard]] std::size_t ResolveVendorDeviceIndex(const std::vector<std::string>& deviceNames,
                                                   std::string_view selection) noexcept
{
    if (deviceNames.empty())
    {
        return 0;
    }
    const std::string lowered = [&selection] {
        std::string text(selection);
        std::transform(text.begin(), text.end(), text.begin(), ToLowerAscii);
        return text;
    }();
    if (lowered.empty() || lowered == "auto")
    {
        return 0;
    }
    if (IsAllDigits(lowered))
    {
        const unsigned long index = std::strtoul(lowered.c_str(), nullptr, 10);
        return index < deviceNames.size() ? static_cast<std::size_t>(index) : 0;
    }
    for (std::size_t i = 0; i < deviceNames.size(); ++i)
    {
        if (ContainsCaseInsensitive(deviceNames[i], selection))
        {
            return i;
        }
    }
    return 0;
}

// --- NVML (NVIDIA) ----------------------------------------------------------
// Windows ABI of the documented, versioned NVML C API. Kept private to this file; no NVML header is
// required or bundled. Struct layouts match the public `nvml.h` (`nvmlUtilization_t`, `nvmlMemory_t`).
constexpr int kNvmlSuccess = 0;
constexpr int kNvmlTemperatureGpu = 0;    // NVML_TEMPERATURE_GPU
constexpr int kNvmlClockGraphics = 0;     // NVML_CLOCK_GRAPHICS
constexpr int kNvmlClockMemory = 2;       // NVML_CLOCK_MEM

struct NvmlUtilization
{
    unsigned int gpu = 0;
    unsigned int memory = 0;
};

struct NvmlMemory
{
    unsigned long long total = 0;
    unsigned long long free = 0;
    unsigned long long used = 0;
};

class NvidiaNvmlBackend final : public IGpuVendorBackend
{
  public:
    NvidiaNvmlBackend(IVendorModuleLoader& loader, std::string_view selection)
        : loader_(loader), selection_(selection)
    {
    }

    ~NvidiaNvmlBackend() override
    {
        if (inited_ && shutdown_ != nullptr)
        {
            static_cast<void>(GuardedVendorCall(shutdown_));
            inited_ = false;
        }
        Unload();
    }

    [[nodiscard]] GpuVendorKind Kind() const noexcept override
    {
        return GpuVendorKind::Nvidia;
    }
    [[nodiscard]] const char* Name() const noexcept override
    {
        return GpuVendorName(GpuVendorKind::Nvidia);
    }
    [[nodiscard]] bool Available() const noexcept override
    {
        return available_;
    }
    [[nodiscard]] bool Detected() const noexcept override
    {
        return detected_;
    }

    bool Initialize() override
    {
        if (initialized_)
        {
            return detected_;
        }
        initialized_ = true;

        module_ = loader_.LoadModule(L"nvml.dll");
        if (module_ == nullptr)
        {
            // The driver's NVSMI copy is not on the search path on every install.
            wchar_t programFiles[MAX_PATH] = {};
            if (::GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH) != 0)
            {
                std::wstring path(programFiles);
                path += L"\\NVIDIA Corporation\\NVSMI\\nvml.dll";
                module_ = loader_.LoadModule(path.c_str());
            }
        }
        if (module_ == nullptr)
        {
            return false;
        }

        const char* const initNames[] = {"nvmlInit_v2", "nvmlInit"};
        const char* const countNames[] = {"nvmlDeviceGetCount_v2", "nvmlDeviceGetCount"};
        const char* const handleNames[] = {"nvmlDeviceGetHandleByIndex_v2",
                                           "nvmlDeviceGetHandleByIndex"};
        init_ = reinterpret_cast<NvmlInitFn>(ResolveFirst(loader_, module_, initNames, 2));
        shutdown_ = reinterpret_cast<NvmlShutdownFn>(
            loader_.ResolveSymbol(module_, "nvmlShutdown"));
        getCount_ = reinterpret_cast<NvmlGetCountFn>(ResolveFirst(loader_, module_, countNames, 2));
        getHandle_ =
            reinterpret_cast<NvmlGetHandleFn>(ResolveFirst(loader_, module_, handleNames, 2));
        getName_ = reinterpret_cast<NvmlGetNameFn>(loader_.ResolveSymbol(module_, "nvmlDeviceGetName"));
        getUtilization_ = reinterpret_cast<NvmlGetUtilizationFn>(
            loader_.ResolveSymbol(module_, "nvmlDeviceGetUtilizationRates"));
        getTemperature_ = reinterpret_cast<NvmlGetTemperatureFn>(
            loader_.ResolveSymbol(module_, "nvmlDeviceGetTemperature"));
        getPower_ = reinterpret_cast<NvmlGetPowerFn>(
            loader_.ResolveSymbol(module_, "nvmlDeviceGetPowerUsage"));
        getClock_ = reinterpret_cast<NvmlGetClockFn>(
            loader_.ResolveSymbol(module_, "nvmlDeviceGetClockInfo"));
        getMemory_ = reinterpret_cast<NvmlGetMemoryFn>(
            loader_.ResolveSymbol(module_, "nvmlDeviceGetMemoryInfo"));
        getFan_ = reinterpret_cast<NvmlGetFanFn>(loader_.ResolveSymbol(module_, "nvmlDeviceGetFanSpeed"));

        // A partial DLL missing the four mandatory entry points is not usable.
        if (init_ == nullptr || getCount_ == nullptr || getHandle_ == nullptr)
        {
            Unload();
            return false;
        }

        if (GuardedVendorCall(init_) != kNvmlSuccess)
        {
            Unload();
            return false;
        }
        inited_ = true;

        unsigned int count = 0;
        if (GuardedVendorCall(getCount_, &count) != kNvmlSuccess || count == 0)
        {
            if (shutdown_ != nullptr)
            {
                static_cast<void>(GuardedVendorCall(shutdown_));
            }
            Unload();
            return false;
        }

        // Enumerate device names so the configured selection picks the right physical GPU.
        std::vector<std::string> names;
        std::vector<void*> handles;
        names.reserve(count);
        handles.reserve(count);
        for (unsigned int i = 0; i < count; ++i)
        {
            void* handle = nullptr;
            if (GuardedVendorCall(getHandle_, i, &handle) != kNvmlSuccess || handle == nullptr)
            {
                continue;
            }
            char nameBuffer[96] = {};
            std::string name;
            if (getName_ != nullptr &&
                GuardedVendorCall(getName_, handle, nameBuffer,
                                  static_cast<unsigned int>(sizeof(nameBuffer))) == kNvmlSuccess)
            {
                name = nameBuffer;
            }
            names.push_back(std::move(name));
            handles.push_back(handle);
        }
        if (handles.empty())
        {
            if (shutdown_ != nullptr)
            {
                static_cast<void>(GuardedVendorCall(shutdown_));
            }
            Unload();
            return false;
        }

        const std::size_t index = ResolveVendorDeviceIndex(names, selection_);
        device_ = handles[index];
        CopyNarrow(deviceName_, sizeof(deviceName_), names[index]);
        detected_ = true;
        available_ = true;
        return true;
    }

    HRESULT Poll(GpuVendorReading& out) override
    {
        if (!available_ || device_ == nullptr)
        {
            return E_FAIL;
        }

        int produced = 0;

        if (getUtilization_ != nullptr)
        {
            NvmlUtilization utilization{};
            if (GuardedVendorCall(getUtilization_, device_, &utilization) == kNvmlSuccess)
            {
                out.hasUtilization = true;
                out.utilizationPercent = static_cast<double>(utilization.gpu);
                ++produced;
            }
        }
        if (getTemperature_ != nullptr)
        {
            unsigned int temperature = 0;
            if (GuardedVendorCall(getTemperature_, device_, kNvmlTemperatureGpu, &temperature) ==
                kNvmlSuccess)
            {
                out.hasTemperature = true;
                out.temperatureC = static_cast<double>(temperature);
                ++produced;
            }
        }
        if (getPower_ != nullptr)
        {
            unsigned int milliwatts = 0;
            if (GuardedVendorCall(getPower_, device_, &milliwatts) == kNvmlSuccess)
            {
                out.hasPower = true;
                out.powerWatts = MilliwattsToWatts(milliwatts);
                ++produced;
            }
        }
        if (getClock_ != nullptr)
        {
            unsigned int graphicsMhz = 0;
            if (GuardedVendorCall(getClock_, device_, kNvmlClockGraphics, &graphicsMhz) ==
                kNvmlSuccess)
            {
                out.hasCoreClock = true;
                out.coreClockMhz = static_cast<double>(graphicsMhz);
                ++produced;
            }
            unsigned int memoryMhz = 0;
            if (GuardedVendorCall(getClock_, device_, kNvmlClockMemory, &memoryMhz) == kNvmlSuccess)
            {
                out.hasMemoryClock = true;
                out.memoryClockMhz = static_cast<double>(memoryMhz);
                ++produced;
            }
        }
        if (getMemory_ != nullptr)
        {
            NvmlMemory memory{};
            if (GuardedVendorCall(getMemory_, device_, &memory) == kNvmlSuccess)
            {
                out.hasMemory = true;
                out.vramTotalBytes = static_cast<std::uint64_t>(memory.total);
                out.vramUsedBytes = static_cast<std::uint64_t>(memory.used);
                ++produced;
            }
        }
        if (getFan_ != nullptr)
        {
            unsigned int fanPercent = 0;
            if (GuardedVendorCall(getFan_, device_, &fanPercent) == kNvmlSuccess)
            {
                out.hasFanSpeed = true;
                out.fanPercent = static_cast<double>(fanPercent);
                ++produced;
            }
        }

        if (produced == 0)
        {
            // Every call failed: treat the backend as gone rather than reporting empty metrics.
            available_ = false;
            return E_FAIL;
        }
        CopyNarrow(out.deviceName, sizeof(out.deviceName), deviceName_);
        return S_OK;
    }

    void Reset() noexcept override
    {
        // Detection and device handle are cached for the provider lifetime; nothing to re-arm.
    }

  private:
    using NvmlInitFn = int (*)();
    using NvmlShutdownFn = int (*)();
    using NvmlGetCountFn = int (*)(unsigned int*);
    using NvmlGetHandleFn = int (*)(unsigned int, void**);
    using NvmlGetNameFn = int (*)(void*, char*, unsigned int);
    using NvmlGetUtilizationFn = int (*)(void*, NvmlUtilization*);
    using NvmlGetTemperatureFn = int (*)(void*, int, unsigned int*);
    using NvmlGetPowerFn = int (*)(void*, unsigned int*);
    using NvmlGetClockFn = int (*)(void*, int, unsigned int*);
    using NvmlGetMemoryFn = int (*)(void*, NvmlMemory*);
    using NvmlGetFanFn = int (*)(void*, unsigned int*);

    void Unload() noexcept
    {
        inited_ = false;
        if (module_ != nullptr)
        {
            loader_.UnloadModule(module_);
            module_ = nullptr;
        }
        init_ = nullptr;
        shutdown_ = nullptr;
        getCount_ = nullptr;
        getHandle_ = nullptr;
        getName_ = nullptr;
        getUtilization_ = nullptr;
        getTemperature_ = nullptr;
        getPower_ = nullptr;
        getClock_ = nullptr;
        getMemory_ = nullptr;
        getFan_ = nullptr;
    }

    IVendorModuleLoader& loader_;
    std::string selection_;
    void* module_ = nullptr;
    void* device_ = nullptr;
    bool initialized_ = false;
    bool detected_ = false;
    bool available_ = false;
    bool inited_ = false;
    char deviceName_[kGpuNameCapacity] = {};

    NvmlInitFn init_ = nullptr;
    NvmlShutdownFn shutdown_ = nullptr;
    NvmlGetCountFn getCount_ = nullptr;
    NvmlGetHandleFn getHandle_ = nullptr;
    NvmlGetNameFn getName_ = nullptr;
    NvmlGetUtilizationFn getUtilization_ = nullptr;
    NvmlGetTemperatureFn getTemperature_ = nullptr;
    NvmlGetPowerFn getPower_ = nullptr;
    NvmlGetClockFn getClock_ = nullptr;
    NvmlGetMemoryFn getMemory_ = nullptr;
    NvmlGetFanFn getFan_ = nullptr;
};

// --- ADLX (AMD) / IGCL (Intel) ----------------------------------------------
// ADLX and IGCL expose COM-style C++ interfaces whose full vtable layout is defined by the vendor
// SDK headers, which are not bundled (see scope: never bundle a vendor SDK). The entry points are
// still probed so the capability is detected and recorded, but no metric call is made: calling an
// unverified vtable would be worse than reporting the metrics unavailable. `docs/design/adr/0020`
// records this and the follow-up. This keeps the selection/fallback architecture complete and safe.
class DetectOnlyBackend final : public IGpuVendorBackend
{
  public:
    DetectOnlyBackend(IVendorModuleLoader& loader, GpuVendorKind kind, const char* displayName,
                      const wchar_t* const* libraries, std::size_t libraryCount,
                      const char* entrySymbol)
        : loader_(loader), kind_(kind), displayName_(displayName), libraries_(libraries),
          libraryCount_(libraryCount), entrySymbol_(entrySymbol)
    {
    }

    ~DetectOnlyBackend() override
    {
        if (module_ != nullptr)
        {
            loader_.UnloadModule(module_);
            module_ = nullptr;
        }
    }

    [[nodiscard]] GpuVendorKind Kind() const noexcept override
    {
        return kind_;
    }
    [[nodiscard]] const char* Name() const noexcept override
    {
        return displayName_;
    }
    [[nodiscard]] bool Available() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool Detected() const noexcept override
    {
        return detected_;
    }

    bool Initialize() override
    {
        if (initialized_)
        {
            return detected_;
        }
        initialized_ = true;
        for (std::size_t i = 0; i < libraryCount_; ++i)
        {
            void* module = loader_.LoadModule(libraries_[i]);
            if (module == nullptr)
            {
                continue;
            }
            if (loader_.ResolveSymbol(module, entrySymbol_) != nullptr)
            {
                module_ = module;
                detected_ = true;
                return true;
            }
            loader_.UnloadModule(module);
        }
        return false;
    }

    HRESULT Poll(GpuVendorReading&) override
    {
        return E_FAIL;
    }

  private:
    IVendorModuleLoader& loader_;
    GpuVendorKind kind_;
    const char* displayName_;
    const wchar_t* const* libraries_;
    std::size_t libraryCount_;
    const char* entrySymbol_;
    void* module_ = nullptr;
    bool initialized_ = false;
    bool detected_ = false;
};
} // namespace

const char* GpuVendorName(GpuVendorKind kind) noexcept
{
    switch (kind)
    {
    case GpuVendorKind::Nvidia:
        return "nvidia-nvml";
    case GpuVendorKind::Amd:
        return "amd-adlx";
    case GpuVendorKind::Intel:
        return "intel-igcl";
    case GpuVendorKind::None:
    default:
        return "none";
    }
}

GpuVendorKind ClassifyGpuVendor(std::string_view adapterName) noexcept
{
    if (ContainsCaseInsensitive(adapterName, "nvidia") ||
        ContainsCaseInsensitive(adapterName, "geforce") ||
        ContainsCaseInsensitive(adapterName, "quadro") ||
        ContainsCaseInsensitive(adapterName, "rtx") || ContainsCaseInsensitive(adapterName, "gtx") ||
        ContainsCaseInsensitive(adapterName, "tesla"))
    {
        return GpuVendorKind::Nvidia;
    }
    if (ContainsCaseInsensitive(adapterName, "amd") || ContainsCaseInsensitive(adapterName, "radeon") ||
        ContainsCaseInsensitive(adapterName, "ati "))
    {
        return GpuVendorKind::Amd;
    }
    if (ContainsCaseInsensitive(adapterName, "intel") || ContainsCaseInsensitive(adapterName, "arc ") ||
        ContainsCaseInsensitive(adapterName, "iris"))
    {
        return GpuVendorKind::Intel;
    }
    return GpuVendorKind::None;
}

void ApplyGpuVendorReading(const GpuVendorReading& reading,
                           std::uint64_t tick,
                           GpuMetrics& gpu) noexcept
{
    const auto mark = [tick](MetricStatus& status) {
        status.available = true;
        status.stale = false;
        status.lastSuccessTick = tick;
    };
    const auto clampPercent = [](double value) {
        if (!(value > 0.0)) // also rejects NaN
        {
            return 0.0;
        }
        return value > 100.0 ? 100.0 : value;
    };

    if (reading.hasUtilization)
    {
        // Vendor whole-GPU utilization overrides the PDH aggregate.
        gpu.utilizationPercent = clampPercent(reading.utilizationPercent);
    }
    if (reading.hasTemperature)
    {
        gpu.temperatureC = reading.temperatureC;
        mark(gpu.temperatureStatus);
    }
    if (reading.hasPower)
    {
        gpu.powerWatts = reading.powerWatts;
        mark(gpu.powerStatus);
    }
    if (reading.hasCoreClock)
    {
        gpu.coreClockMhz = reading.coreClockMhz;
        mark(gpu.clockStatus);
    }
    if (reading.hasMemoryClock)
    {
        gpu.memoryClockMhz = reading.memoryClockMhz;
        mark(gpu.clockStatus);
    }
    if (reading.hasFanSpeed)
    {
        gpu.fanPercent = reading.fanPercent;
        mark(gpu.fanStatus);
    }
    if (reading.hasFanRpm)
    {
        gpu.fanRpm = reading.fanRpm;
        mark(gpu.fanStatus);
    }
    if (reading.hasMemory)
    {
        gpu.vramUsedBytes = reading.vramUsedBytes;
        gpu.vramTotalBytes = reading.vramTotalBytes;
        mark(gpu.vramStatus);
    }
    // Only fill the adapter name when PDH did not (the baseline owns `gpu.name`).
    if (reading.deviceName[0] != '\0' && gpu.name[0] == '\0')
    {
        CopyNarrow(gpu.name, sizeof(gpu.name), reading.deviceName);
    }
}

void MarkGpuVendorUnavailable(GpuMetrics& gpu) noexcept
{
    const auto mark = [](MetricStatus& status) {
        status.available = false;
        status.stale = true;
    };
    mark(gpu.temperatureStatus);
    mark(gpu.powerStatus);
    mark(gpu.clockStatus);
    mark(gpu.fanStatus);
    mark(gpu.vramStatus);
}

std::unique_ptr<IVendorModuleLoader> MakeWindowsVendorModuleLoader()
{
    return std::make_unique<WindowsVendorModuleLoader>();
}

std::vector<std::unique_ptr<IGpuVendorBackend>> MakeVendorBackends(IVendorModuleLoader& loader,
                                                                   std::string_view selection)
{
    std::vector<std::unique_ptr<IGpuVendorBackend>> backends;
    backends.reserve(3);
    backends.push_back(std::make_unique<NvidiaNvmlBackend>(loader, selection));

    static const wchar_t* const kAdlxLibraries[] = {L"ADLX.dll"};
    backends.push_back(std::make_unique<DetectOnlyBackend>(
        loader, GpuVendorKind::Amd, GpuVendorName(GpuVendorKind::Amd), kAdlxLibraries, 1,
        "ADLXInitialize"));

    static const wchar_t* const kIgclLibraries[] = {L"ControlLib.dll", L"igcl.dll"};
    backends.push_back(std::make_unique<DetectOnlyBackend>(
        loader, GpuVendorKind::Intel, GpuVendorName(GpuVendorKind::Intel), kIgclLibraries, 2,
        "ctlInit"));

    return backends;
}

GpuVendorProvider::GpuVendorProvider() : GpuVendorProvider(std::string("auto"))
{
}

GpuVendorProvider::GpuVendorProvider(std::string selection)
    : loader_(MakeWindowsVendorModuleLoader()), adapters_(MakeDxgiAdapterEnumerator())
{
    EnumerateAndSelect(selection);
    if (loader_)
    {
        backends_ = MakeVendorBackends(*loader_, selection);
    }
    backend_ = SelectBackend();
    if (backend_ != nullptr)
    {
        activeKind_ = backend_->Kind();
    }
}

GpuVendorProvider::GpuVendorProvider(std::unique_ptr<IVendorModuleLoader> loader,
                                     std::unique_ptr<IGpuAdapterEnumerator> adapters,
                                     std::vector<std::unique_ptr<IGpuVendorBackend>> backends,
                                     std::string selection)
    : loader_(std::move(loader)), adapters_(std::move(adapters)), backends_(std::move(backends))
{
EnumerateAndSelect(selection);
    backend_ = SelectBackend();
if (backend_ != nullptr)
    {
        activeKind_ = backend_->Kind();
    }
}

GpuVendorProvider::~GpuVendorProvider() = default;

void GpuVendorProvider::EnumerateAndSelect(const std::string& selectionText)
{
    std::vector<GpuAdapterInfo> adapters;
    if (adapters_)
    {
        adapters_->Enumerate(adapters);
    }
    const std::size_t index = ResolveGpuAdapter(ParseGpuSelection(selectionText), adapters);
    selected_ = index == kNoGpuAdapter ? GpuAdapterInfo{} : adapters[index];
}

IGpuVendorBackend* GpuVendorProvider::SelectBackend() noexcept
{
    preferredKind_ = ClassifyGpuVendor(selected_.name);

    if (preferredKind_ != GpuVendorKind::None)
    {
        // A known adapter vendor is authoritative: never report a different physical GPU's metrics.
        for (const auto& backend : backends_)
        {
            if (backend && backend->Kind() == preferredKind_)
            {
                if (backend->Initialize())
                {
                    detectedKind_ = backend->Kind();
                }
                return backend->Available() ? backend.get() : nullptr;
            }
        }
        return nullptr;
    }

    // Unknown vendor: probe each backend once, first usable wins.
    for (const auto& backend : backends_)
    {
        if (!backend)
        {
            continue;
        }
        if (backend->Initialize())
        {
            if (detectedKind_ == GpuVendorKind::None)
            {
                detectedKind_ = backend->Kind();
            }
            if (backend->Available())
            {
                return backend.get();
            }
        }
    }
    return nullptr;
}

const char* GpuVendorProvider::Name() const noexcept
{
    return "gpu-vendor";
}

std::uint32_t GpuVendorProvider::Domains() const noexcept
{
    // Deliberately no domain: the GPU domain's availability is owned by the PDH/D3DKMT baseline, so
    // a missing vendor SDK cannot change whether the GPU metrics are considered available.
    return static_cast<std::uint32_t>(MetricDomain::None);
}

std::chrono::milliseconds GpuVendorProvider::Cadence() const noexcept
{
    return std::chrono::milliseconds(1000);
}

HRESULT GpuVendorProvider::Poll(MetricsSnapshot& snapshot)
{
    if (backend_ == nullptr || activeKind_ == GpuVendorKind::None)
    {
        MarkGpuVendorUnavailable(snapshot.gpu);
        return S_OK;
    }

    GpuVendorReading reading{};
    HRESULT hr = E_FAIL;
    try
    {
        hr = backend_->Poll(reading);
    }
    catch (...)
    {
        hr = E_FAIL;
    }

    if (hr == E_PENDING)
    {
        return S_OK;
    }
    if (FAILED(hr))
    {
        // A vendor call started failing mid-run: stop using the backend and never retry per sample.
        activeKind_ = GpuVendorKind::None;
        backend_ = nullptr;
        MarkGpuVendorUnavailable(snapshot.gpu);
        return S_OK;
    }

    ApplyGpuVendorReading(reading, snapshot.tickIndex + 1, snapshot.gpu);
    return S_OK;
}

void GpuVendorProvider::Reset() noexcept
{
    if (backend_ != nullptr)
    {
        backend_->Reset();
    }
}
} // namespace pacecar::metrics