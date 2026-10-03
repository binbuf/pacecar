#pragma once

// Test-only heap allocation counter.
//
// `aggregator_test.cpp` replaces the global `operator new`/`delete` in the test executable and
// increments a process-wide counter. Tests that assert a hot path performs zero allocations reset
// the counter, run the path, and read it back. The counter is not thread-safe under concurrent
// mutation beyond the atomic increment; tests measure single-threaded windows.

#include <cstddef>

namespace pacecar::test
{
// Total allocations observed since process start (or the last reset).
[[nodiscard]] std::size_t AllocationCount() noexcept;

// Resets the counter to zero.
void ResetAllocationCount() noexcept;
} // namespace pacecar::test