#pragma once

// Last-snapshot cache for a plausible first frame (design ref 05-performance.md "Startup").
//
// On exit (and periodically) the sampler writes the latest snapshot to
// `%LOCALAPPDATA%\Pacecar\last_snapshot.bin`. At startup the app paints immediately - showing
// placeholders - and loads the cache on a background thread, so a cold start never waits on disk.
// The format is a small, versioned binary blob (not JSON): fixed-width fields, bounded array
// counts, and a magic/version header. Both directions are best-effort and never throw.

#include <filesystem>
#include <cstdint>

#include "pacecar/metrics/MetricsSnapshot.h"

namespace pacecar::metrics
{
inline constexpr std::uint32_t kSnapshotCacheMagic = 0x50434153u; // 'PCAS'
inline constexpr std::uint32_t kSnapshotCacheVersion = 1u;

// `%LOCALAPPDATA%\Pacecar\last_snapshot.bin`, falling back to `%APPDATA%` and then the temp
// directory. Returns an empty path only when no user directory can be resolved.
[[nodiscard]] std::filesystem::path SnapshotCachePath();

// Serializes `snapshot` atomically (temp file + rename) to `path`. Returns false (never throws) on
// an empty path or any I/O failure.
[[nodiscard]] bool SaveSnapshotCache(const MetricsSnapshot& snapshot,
                                     const std::filesystem::path& path) noexcept;

// Loads a previously written cache. Returns false and leaves `out` untouched when the file is
// missing, corrupt, the wrong version, or has out-of-range array counts.
[[nodiscard]] bool LoadSnapshotCache(const std::filesystem::path& path, MetricsSnapshot& out) noexcept;
} // namespace pacecar::metrics