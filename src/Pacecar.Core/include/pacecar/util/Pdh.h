#pragma once

// Thin RAII wrappers over the PDH query/counter handles used by the CPU, GPU, and disk providers.
//
// Low-level methods return the raw PDH_STATUS so callers can decide whether an error is fatal;
// ThrowIfPdhError converts one to an exception at a boundary. The query owns its counters and is
// closed on destruction; a PdhCounter does not close anything on its own because PDH closes every
// counter when its parent query closes.

#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>

#include <string>
#include <string_view>

namespace pacecar
{
class PdhCounter
{
  public:
    PdhCounter() noexcept = default;
    explicit PdhCounter(PDH_HCOUNTER handle) noexcept : handle_(handle) {}

    [[nodiscard]] bool IsValid() const noexcept
    {
        return handle_ != nullptr;
    }
    [[nodiscard]] PDH_HCOUNTER Handle() const noexcept
    {
        return handle_;
    }

    // Reads the most recent collected value as a double. Returns the PDH status.
    [[nodiscard]] PDH_STATUS GetDouble(double& out) const noexcept
    {
        if (handle_ == nullptr)
        {
            return static_cast<PDH_STATUS>(PDH_INVALID_HANDLE);
        }
        PDH_FMT_COUNTERVALUE value{};
        const PDH_STATUS status =
            PdhGetFormattedCounterValue(handle_, PDH_FMT_DOUBLE, nullptr, &value);
        if (status == ERROR_SUCCESS)
        {
            out = value.doubleValue;
        }
        return status;
    }

  private:
    PDH_HCOUNTER handle_ = nullptr;
};

class PdhQuery
{
  public:
    PdhQuery() noexcept
    {
        query_ = nullptr;
        PdhOpenQueryW(nullptr, 0, &query_);
    }

    ~PdhQuery() noexcept
    {
        Close();
    }

    PdhQuery(PdhQuery&& other) noexcept : query_(other.query_)
    {
        other.query_ = nullptr;
    }

    PdhQuery& operator=(PdhQuery&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            query_ = other.query_;
            other.query_ = nullptr;
        }
        return *this;
    }

    PdhQuery(const PdhQuery&) = delete;
    PdhQuery& operator=(const PdhQuery&) = delete;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return query_ != nullptr;
    }
    [[nodiscard]] PDH_HQUERY Handle() const noexcept
    {
        return query_;
    }

    void Close() noexcept
    {
        if (query_ != nullptr)
        {
            PdhCloseQuery(query_);
            query_ = nullptr;
        }
    }

    // Adds a counter using the localized counter/object names.
    [[nodiscard]] PDH_STATUS AddCounter(std::wstring_view path, PdhCounter& counter) noexcept
    {
        return AddCounterImpl(path, counter, false);
    }

    // Adds a counter using the invariant English counter/object names.
    [[nodiscard]] PDH_STATUS AddEnglishCounter(std::wstring_view path, PdhCounter& counter) noexcept
    {
        return AddCounterImpl(path, counter, true);
    }

    [[nodiscard]] PDH_STATUS Collect() noexcept
    {
        if (query_ == nullptr)
        {
            return static_cast<PDH_STATUS>(PDH_INVALID_HANDLE);
        }
        return PdhCollectQueryData(query_);
    }

  private:
    [[nodiscard]] PDH_STATUS AddCounterImpl(std::wstring_view path, PdhCounter& counter,
                                            bool english) noexcept
    {
        counter = PdhCounter{};
        if (query_ == nullptr)
        {
            return static_cast<PDH_STATUS>(PDH_INVALID_HANDLE);
        }
        const std::wstring widePath(path);
        PDH_HCOUNTER handle = nullptr;
        const PDH_STATUS status =
            english ? PdhAddEnglishCounterW(query_, widePath.c_str(), 0, &handle)
                    : PdhAddCounterW(query_, widePath.c_str(), 0, &handle);
        if (status == ERROR_SUCCESS)
        {
            counter = PdhCounter(handle);
        }
        return status;
    }

    PDH_HQUERY query_ = nullptr;
};

// Converts a failing PDH status to an exception. PDH_STATUS values are HRESULT-shaped, so the
// exception carries the original code. No-op on ERROR_SUCCESS.
void ThrowIfPdhError(PDH_STATUS status);
} // namespace pacecar