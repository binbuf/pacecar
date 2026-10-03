#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include "pacecar/util/ElapsedClock.h"

#include <windows.h>

#include <chrono>

namespace pacecar
{
namespace
{
class QpcElapsedClock final : public IElapsedClock
{
  public:
    QpcElapsedClock() noexcept
    {
        LARGE_INTEGER frequency{};
        if (::QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
        {
            ticksPerSecond_ = static_cast<std::uint64_t>(frequency.QuadPart);
            useQpc_ = true;
        }
    }

    [[nodiscard]] std::uint64_t NowTicks() const noexcept override
    {
        if (useQpc_)
        {
            LARGE_INTEGER counter{};
            if (::QueryPerformanceCounter(&counter))
            {
                return static_cast<std::uint64_t>(counter.QuadPart);
            }
        }
        // Fallback: `steady_clock` ticked at a fixed 10 MHz so rate math keeps working.
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        return static_cast<std::uint64_t>(nanos) * 10'000ull / 1'000'000ull;
    }

    [[nodiscard]] std::uint64_t TicksPerSecond() const noexcept override
    {
        return ticksPerSecond_;
    }

  private:
    std::uint64_t ticksPerSecond_ = 10'000'000ull;
    bool useQpc_ = false;
};
} // namespace

std::unique_ptr<IElapsedClock> MakeQpcElapsedClock()
{
    return std::make_unique<QpcElapsedClock>();
}
} // namespace pacecar