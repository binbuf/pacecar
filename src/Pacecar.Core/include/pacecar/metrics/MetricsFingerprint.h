#pragma once

// Cheap "did anything the user can see change?" fingerprint for a metrics snapshot.
//
// The render path skips `BeginDraw`/`Present` when the fingerprint of the latest snapshot matches
// the last painted one, so a steady machine does no compositor work (design ref 05-performance.md
// "Render only when something changed"). Values are quantized to display precision first: a
// hundredth of a percent that never reaches a tile must not force a repaint, while a temperature
// that moves by half a degree does. The function is pure, allocation-free, and headless-testable.

#include <cstdint>

#include "pacecar/metrics/MetricsSnapshot.h"

namespace pacecar::metrics
{
// FNV-1a over the display-quantized utilization, frequency, temperature, throughput, RTT, and FPS
// values plus availability flags. Two snapshots that would format to the same tiles hash equal.
[[nodiscard]] std::uint64_t FingerprintSnapshot(const MetricsSnapshot& snapshot) noexcept;
} // namespace pacecar::metrics