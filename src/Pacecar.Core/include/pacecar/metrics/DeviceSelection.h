#pragma once

// Shared device-selection parsing for the network and disk providers.
//
// `Config::SensorsConfig` carries loose selection strings (`nic_selection`, `disk_selection`) so a
// future settings UI can offer "all", an index, or a name without a config migration. This is the
// same convention the GPU provider uses (`sensors.gpu_selection`): the string is parsed into a
// small tagged value and the provider decides how to match it against enumerated devices.
//
// Parsing is pure so the mapping from config to device set is unit-testable without hardware.

#include <cstdint>
#include <cstddef>
#include <string_view>

#include "pacecar/metrics/MetricsSnapshot.h"

namespace pacecar::metrics
{
enum class DeviceSelectionKind
{
    All,
    Index,
    Name,
};

struct DeviceSelection
{
    DeviceSelectionKind kind = DeviceSelectionKind::All;
    std::uint32_t index = 0;
    char name[kNameCapacity] = {};
};

// Parses a selection string:
//   - empty, "auto", or "all" (case-insensitive) -> All.
//   - an all-digit string -> Index (out of `uint32_t` range falls back to All).
//   - anything else -> Name, lowercased and trimmed of surrounding whitespace.
[[nodiscard]] DeviceSelection ParseDeviceSelection(std::string_view text) noexcept;

// Case-insensitive substring test used for name matching. An empty `needle` matches everything.
[[nodiscard]] bool NameContainsInsensitive(std::string_view haystack, std::string_view needle) noexcept;
} // namespace pacecar::metrics