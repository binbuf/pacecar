#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "pacecar/util/RingBuffer.h"

namespace
{
using pacecar::RingBuffer;

TEST(RingBuffer, CapacityAndSizeTrackPushes)
{
    RingBuffer<int> buffer(4);
    EXPECT_EQ(buffer.Capacity(), 4u);
    EXPECT_TRUE(buffer.Empty());
    EXPECT_FALSE(buffer.Full());

    for (int i = 0; i < 4; ++i)
    {
        buffer.Push(i);
        EXPECT_EQ(buffer.Size(), static_cast<std::size_t>(i + 1));
    }
    EXPECT_TRUE(buffer.Full());

    buffer.Push(4);
    EXPECT_EQ(buffer.Size(), 4u);
}

TEST(RingBuffer, WrapAroundOrderingIsOldestToNewest)
{
    RingBuffer<int> buffer(3);
    for (int i = 0; i < 5; ++i)
    {
        buffer.Push(i);
    }

    EXPECT_EQ(buffer.Size(), 3u);
    EXPECT_EQ(buffer.Latest(), 4);
    EXPECT_EQ(buffer.Oldest(), 2);
    EXPECT_EQ(buffer.At(0), 2);
    EXPECT_EQ(buffer.At(1), 3);
    EXPECT_EQ(buffer.At(2), 4);
    EXPECT_EQ(buffer.Newest(0), 4);
    EXPECT_EQ(buffer.Newest(2), 2);

    std::vector<int> oldestFirst;
    buffer.ForEachOldestFirst([&](const int& value) { oldestFirst.push_back(value); });
    EXPECT_EQ(oldestFirst, (std::vector<int>{2, 3, 4}));

    std::vector<int> newestFirst;
    buffer.ForEachNewestFirst([&](const int& value) { newestFirst.push_back(value); });
    EXPECT_EQ(newestFirst, (std::vector<int>{4, 3, 2}));
}

TEST(RingBuffer, DownsampleSpansOldestToNewest)
{
    RingBuffer<int> buffer(8);
    for (int i = 0; i < 8; ++i)
    {
        buffer.Push(i);
    }

    int out[4]{};
    const std::size_t written = buffer.Downsample(out);
    EXPECT_EQ(written, 4u);
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[3], 7);
}

TEST(RingBuffer, DownsampleHandlesSmallAndEmptyOutputs)
{
    RingBuffer<int> buffer(8);
    for (int i = 0; i < 8; ++i)
    {
        buffer.Push(i);
    }

    int single[1]{};
    EXPECT_EQ(buffer.Downsample(single), 1u);
    EXPECT_EQ(single[0], 7);

    int big[16]{};
    EXPECT_EQ(buffer.Downsample(big), 8u);

    RingBuffer<int> empty(4);
    EXPECT_EQ(empty.Downsample(big), 0u);
}

TEST(RingBuffer, PushDoesNotAllocateOrResize)
{
    RingBuffer<int> buffer(16);
    const int* storage = buffer.Data();
    const std::size_t capacity = buffer.Capacity();

    for (int i = 0; i < 1000; ++i)
    {
        buffer.Push(i);
        ASSERT_EQ(buffer.Capacity(), capacity);
        ASSERT_EQ(buffer.Data(), storage);
    }
}
} // namespace