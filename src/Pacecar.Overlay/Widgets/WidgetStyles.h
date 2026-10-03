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
};
} // namespace pacecar::overlay