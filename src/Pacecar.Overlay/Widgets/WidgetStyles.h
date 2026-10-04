#pragma once

// The text format ids shared by the widgets. Registered once by the scene against the text
// renderer's stable table, then passed to the draw calls (no per-frame format creation).

#include <cstdint>

namespace pacecar::overlay
{
struct WidgetStyles
{
    std::uint32_t headerTitle = 0;
    std::uint32_t headerStatus = 0;
    std::uint32_t label = 0;
    std::uint32_t primary = 0;
    std::uint32_t secondary = 0;
    // Alternate value sizes so each view can pick a density-appropriate readout without
    // re-registering formats.
    std::uint32_t primarySmall = 0;
    std::uint32_t primaryLarge = 0;
    std::uint32_t primaryXl = 0;
    std::uint32_t secondaryLarge = 0;
    // The StatRows view's icon, label (truncation cue) and value formats. Registering these is
    // driven by the configurable value font size, so their ids change whenever the size changes.
    std::uint32_t statIcon = 0;
    std::uint32_t statLabel = 0;
    std::uint32_t statValue = 0;
};
} // namespace pacecar::overlay