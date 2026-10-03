#include <gtest/gtest.h>

#include <windows.h>

#include <utility>

#include <wil/result_macros.h>

#include "pacecar/util/Pdh.h"

namespace
{
using pacecar::PdhCounter;
using pacecar::PdhQuery;

TEST(Pdh, QueryOpensAndCloses)
{
    PdhQuery query;
    EXPECT_TRUE(query.IsValid());
    EXPECT_NE(query.Handle(), nullptr);
}

TEST(Pdh, MoveTransfersOwnership)
{
    PdhQuery original;
    ASSERT_TRUE(original.IsValid());
    const PDH_HQUERY handle = original.Handle();

    PdhQuery moved(std::move(original));
    EXPECT_FALSE(original.IsValid());
    EXPECT_TRUE(moved.IsValid());
    EXPECT_EQ(moved.Handle(), handle);
}

TEST(Pdh, InvalidCounterReturnsError)
{
    PdhQuery query;
    ASSERT_TRUE(query.IsValid());

    PdhCounter counter;
    const PDH_STATUS status =
        query.AddEnglishCounter(L"\\Definitely Not A Counter Object\\No Counter", counter);
    EXPECT_NE(status, ERROR_SUCCESS);
    EXPECT_FALSE(counter.IsValid());
}

TEST(Pdh, KnownCounterCollectsAndReads)
{
    PdhQuery query;
    ASSERT_TRUE(query.IsValid());

    PdhCounter counter;
    const PDH_STATUS add =
        query.AddEnglishCounter(L"\\Processor Information(_Total)\\% Processor Time", counter);
    if (add != ERROR_SUCCESS)
    {
        GTEST_SKIP() << "Processor Information counter unavailable on this host";
    }
    ASSERT_TRUE(counter.IsValid());

    EXPECT_EQ(query.Collect(), ERROR_SUCCESS);
    ::Sleep(50);
    EXPECT_EQ(query.Collect(), ERROR_SUCCESS);

    double value = -1.0;
    const PDH_STATUS status = counter.GetDouble(value);
    EXPECT_EQ(status, ERROR_SUCCESS);
    EXPECT_GE(value, 0.0);
    EXPECT_LE(value, 100.0);
}

TEST(Pdh, ThrowIfPdhErrorMapsFailure)
{
    EXPECT_NO_THROW(pacecar::ThrowIfPdhError(ERROR_SUCCESS));
    EXPECT_THROW(pacecar::ThrowIfPdhError(PDH_INVALID_HANDLE), wil::ResultException);
}
} // namespace