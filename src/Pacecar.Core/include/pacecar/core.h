#pragma once

#include <string_view>

namespace pacecar
{
// Version string of the core library. Acts as a minimal symbol so the static library produces a
// linkable artifact and consumers can prove the Core reference resolved.
[[nodiscard]] std::string_view CoreVersion() noexcept;
} // namespace pacecar