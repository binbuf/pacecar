#include <gtest/gtest.h>

#include <cmath>

#include "pacecar/util/Smoothing.h"

namespace
{
using pacecar::Ema;

TEST(Ema, FirstSamplePassthrough)
{
    Ema ema(0.25);
    EXPECT_FALSE(ema.HasValue());
    EXPECT_DOUBLE_EQ(ema.Push(42.0), 42.0);
    EXPECT_TRUE(ema.HasValue());
    EXPECT_DOUBLE_EQ(ema.Value(), 42.0);
}

TEST(Ema, AlphaBounds)
{
    EXPECT_DOUBLE_EQ(Ema(2.0).Alpha(), 1.0);
    EXPECT_DOUBLE_EQ(Ema(-1.0).Alpha(), 0.0);
    EXPECT_DOUBLE_EQ(Ema(0.25).Alpha(), 0.25);
    EXPECT_DOUBLE_EQ(Ema(1.0).Alpha(), 1.0);
}

TEST(Ema, FromWindowProducesExpectedAlpha)
{
    EXPECT_NEAR(Ema::FromWindow(3).Alpha(), 0.5, 1e-12);
    EXPECT_NEAR(Ema::FromWindow(6).Alpha(), 2.0 / 7.0, 1e-12);
    EXPECT_NEAR(Ema::FromWindow(0).Alpha(), 1.0, 1e-12);
}

TEST(Ema, ConvergesToConstantInput)
{
    Ema ema(0.3);
    ema.Push(0.0);
    for (int i = 0; i < 200; ++i)
    {
        const double value = ema.Push(10.0);
        EXPECT_GE(value, 0.0);
        EXPECT_LE(value, 10.0);
    }
    EXPECT_NEAR(ema.Value(), 10.0, 1e-9);
}

TEST(Ema, AlphaOneTracksInput)
{
    Ema ema(1.0);
    ema.Push(5.0);
    EXPECT_DOUBLE_EQ(ema.Push(9.0), 9.0);
}

TEST(Ema, ResetClearsState)
{
    Ema ema(0.5);
    ema.Push(3.0);
    ema.Reset();
    EXPECT_FALSE(ema.HasValue());
    EXPECT_DOUBLE_EQ(ema.Push(7.0), 7.0);
}
} // namespace