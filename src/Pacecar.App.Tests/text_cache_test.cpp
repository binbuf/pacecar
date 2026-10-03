#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

#include "allocation_probe.h"
#include "pacecar/overlay/TextCache.h"

namespace
{
TEST(TextCache, ReusesValueForSameKey)
{
    pacecar::overlay::TextLayoutCache<int> cache;
    int created = 0;
    const auto factory = [&created]() { return ++created; };

    const int& first = cache.Get(1, L"42", factory);
    const int& second = cache.Get(1, L"42", factory);

    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 1);
    EXPECT_EQ(created, 1);
    EXPECT_EQ(cache.Hits(), 1u);
    EXPECT_EQ(cache.Misses(), 1u);
}

TEST(TextCache, DistinguishesByString)
{
    pacecar::overlay::TextLayoutCache<int> cache;
    int created = 0;
    const auto factory = [&created]() { return ++created; };

    EXPECT_EQ(cache.Get(1, L"1", factory), 1);
    EXPECT_EQ(cache.Get(1, L"2", factory), 2);
    EXPECT_EQ(cache.Size(), 2u);
}

TEST(TextCache, DistinguishesByFormat)
{
    pacecar::overlay::TextLayoutCache<int> cache;
    int created = 0;
    const auto factory = [&created]() { return ++created; };

    EXPECT_EQ(cache.Get(1, L"42", factory), 1);
    EXPECT_EQ(cache.Get(2, L"42", factory), 2);
    EXPECT_EQ(cache.FormatCount(), 2u);
    EXPECT_EQ(cache.Size(), 2u);
}

TEST(TextCache, ContainsOnlyCachedKeys)
{
    pacecar::overlay::TextLayoutCache<int> cache;
    static_cast<void>(cache.Get(7, L"value", []() { return 0; }));
    EXPECT_TRUE(cache.Contains(7, L"value"));
    EXPECT_FALSE(cache.Contains(7, L"other"));
    EXPECT_FALSE(cache.Contains(8, L"value"));
}

TEST(TextCache, EvictsOldestWhenFull)
{
    pacecar::overlay::TextLayoutCache<int> cache(/*maxEntriesPerFormat=*/2);
    int created = 0;
    const auto factory = [&created]() { return ++created; };

    static_cast<void>(cache.Get(1, L"a", factory));
    static_cast<void>(cache.Get(1, L"b", factory));
    static_cast<void>(cache.Get(1, L"c", factory));

    EXPECT_EQ(cache.Size(), 2u);
    EXPECT_EQ(cache.Evictions(), 1u);
    EXPECT_FALSE(cache.Contains(1, L"a"));
    EXPECT_TRUE(cache.Contains(1, L"b"));
    EXPECT_TRUE(cache.Contains(1, L"c"));
}

TEST(TextCache, EvictionIsLru)
{
    pacecar::overlay::TextLayoutCache<int> cache(2);
    static_cast<void>(cache.Get(1, L"a", []() { return 0; }));
    static_cast<void>(cache.Get(1, L"b", []() { return 0; }));
    static_cast<void>(cache.Get(1, L"a", []() { return 0; })); // touch a
    static_cast<void>(cache.Get(1, L"c", []() { return 0; }));

    EXPECT_TRUE(cache.Contains(1, L"a"));
    EXPECT_FALSE(cache.Contains(1, L"b"));
    EXPECT_TRUE(cache.Contains(1, L"c"));
}

TEST(TextCache, EvictionIsPerFormat)
{
    pacecar::overlay::TextLayoutCache<int> cache(1);
    static_cast<void>(cache.Get(1, L"a", []() { return 0; }));
    static_cast<void>(cache.Get(2, L"a", []() { return 0; }));
    EXPECT_TRUE(cache.Contains(1, L"a"));
    EXPECT_TRUE(cache.Contains(2, L"a"));
    EXPECT_EQ(cache.Evictions(), 0u);
}

TEST(TextCache, ClearResetsContentsAndStats)
{
    pacecar::overlay::TextLayoutCache<int> cache;
    static_cast<void>(cache.Get(1, L"a", []() { return 0; }));
    cache.Clear();
    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_EQ(cache.Hits(), 0u);
    EXPECT_EQ(cache.Misses(), 0u);
}

TEST(TextCache, HitPathPerformsZeroHeapAllocations)
{
    pacecar::overlay::TextLayoutCache<int> cache;
    std::array<std::wstring, 32> keys{};
    for (int i = 0; i < 32; ++i)
    {
        keys[static_cast<std::size_t>(i)] = std::to_wstring(i);
        static_cast<void>(cache.Get(1, keys[static_cast<std::size_t>(i)], [i]() { return i; }));
    }

    pacecar::test::ResetAllocationCount();
    std::int64_t sink = 0;
    for (int i = 0; i < 10000; ++i)
    {
        const std::wstring& key = keys[static_cast<std::size_t>(i) % keys.size()];
        sink += cache.Get(1, key, [i]() { return i; });
    }

    EXPECT_GT(sink, 0);
    EXPECT_EQ(pacecar::test::AllocationCount(), 0u);
}
} // namespace