#include <gtest/gtest.h>

#include <string_view>

#include "pacecar/core.h"

namespace
{
TEST(CoreSmoke, CoreVersionIsSet)
{
    const std::string_view version = pacecar::CoreVersion();
    EXPECT_FALSE(version.empty());
}
} // namespace