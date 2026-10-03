#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <random>
#include <string>
#include <string_view>

#include "pacecar/util/Format.h"

namespace
{
using pacecar::DecimalSeparator;
using pacecar::DecimalStyle;

TEST(Format, PercentRoundTripsWithinPrecision)
{
    std::mt19937_64 rng(0xC0FFEE);
    std::uniform_real_distribution<double> dist(-1000.0, 1000.0);
    wchar_t buffer[64];

    for (int i = 0; i < 1000; ++i)
    {
        const double value = dist(rng);
        const std::wstring_view text = pacecar::FormatPercent(buffer, value, 3);
        ASSERT_FALSE(text.empty());
        ASSERT_EQ(text.back(), L'%');
        const std::wstring numeric(text.substr(0, text.size() - 1));
        const double parsed = std::wcstod(numeric.c_str(), nullptr);
        EXPECT_NEAR(parsed, value, 1e-3 + std::abs(value) * 1e-9);
    }
}

TEST(Format, PercentEdgeCases)
{
    wchar_t buffer[64];
    EXPECT_EQ(std::wstring(pacecar::FormatPercent(buffer, 0.0, 1)), L"0.0%");
    EXPECT_EQ(std::wstring(pacecar::FormatPercent(buffer, 100.0, 0)), L"100%");
    EXPECT_EQ(std::wstring(pacecar::FormatPercent(buffer, -1.5, 1)), L"-1.5%");
}

TEST(Format, BytesEdgeCases)
{
    wchar_t buffer[64];
    EXPECT_EQ(std::wstring(pacecar::FormatBytes(buffer, 0)), L"0 B");
    EXPECT_EQ(std::wstring(pacecar::FormatBytes(buffer, 1)), L"1 B");
    EXPECT_EQ(std::wstring(pacecar::FormatBytes(buffer, 1023)), L"1023 B");
    EXPECT_EQ(std::wstring(pacecar::FormatBytes(buffer, 1024)), L"1.0 KiB");
    EXPECT_EQ(std::wstring(pacecar::FormatBytes(buffer, 1024ull * 1024)), L"1.0 MiB");
    EXPECT_EQ(std::wstring(pacecar::FormatBytes(buffer, 1024ull * 1024 * 1024)), L"1.0 GiB");
    EXPECT_EQ(std::wstring(pacecar::FormatBytes(buffer, 1024ull * 1024 * 1024 * 1024)),
              L"1.0 TiB");
    EXPECT_EQ(std::wstring(pacecar::FormatBytes(buffer, 1536)), L"1.5 KiB");
}

TEST(Format, BytesRoundTrip)
{
    wchar_t buffer[64];
    const std::uint64_t values[] = {0, 1, 512, 1024, 4096, 1u << 20, (1ull << 40) + 12345};
    for (const std::uint64_t value : values)
    {
        const std::wstring_view text = pacecar::FormatBytes(buffer, value, 2);
        ASSERT_FALSE(text.empty());
        const double parsed = std::wcstod(std::wstring(text).c_str(), nullptr);
        const bool hasKiB = text.find(L"KiB") != std::wstring_view::npos;
        const bool hasMiB = text.find(L"MiB") != std::wstring_view::npos;
        const bool hasGiB = text.find(L"GiB") != std::wstring_view::npos;
        const bool hasTiB = text.find(L"TiB") != std::wstring_view::npos;
        double scale = 1.0;
        if (hasKiB)
        {
            scale = 1024.0;
        }
        else if (hasMiB)
        {
            scale = 1024.0 * 1024.0;
        }
        else if (hasGiB)
        {
            scale = 1024.0 * 1024.0 * 1024.0;
        }
        else if (hasTiB)
        {
            scale = 1024.0 * 1024.0 * 1024.0 * 1024.0;
        }
        const double reconstructed = parsed * scale;
        EXPECT_NEAR(reconstructed, static_cast<double>(value),
                    std::max(1.0, static_cast<double>(value) * 1e-3));
    }
}

TEST(Format, Rate)
{
    wchar_t buffer[64];
    EXPECT_EQ(std::wstring(pacecar::FormatRate(buffer, 0.0)), L"0 B/s");
    EXPECT_EQ(std::wstring(pacecar::FormatRate(buffer, 1024.0)), L"1.0 KiB/s");
    EXPECT_EQ(std::wstring(pacecar::FormatRate(buffer, -512.0)), L"-512 B/s");
}

TEST(Format, Frequency)
{
    wchar_t buffer[64];
    EXPECT_EQ(std::wstring(pacecar::FormatFrequency(buffer, 3.6e9, 2)), L"3.60 GHz");
    EXPECT_EQ(std::wstring(pacecar::FormatFrequency(buffer, 0.0, 2)), L"0.00 GHz");
    EXPECT_EQ(std::wstring(pacecar::FormatFrequency(buffer, 1.5e9, 1)), L"1.5 GHz");
    EXPECT_EQ(std::wstring(pacecar::FormatFrequency(buffer, -1.0e9, 1)), L"-1.0 GHz");
}

TEST(Format, Temperature)
{
    wchar_t buffer[64];
    EXPECT_EQ(std::wstring(pacecar::FormatTemperature(buffer, 0.0, 1)), L"0.0\u00B0C");
    EXPECT_EQ(std::wstring(pacecar::FormatTemperature(buffer, 55.0, 1)), L"55.0\u00B0C");
    EXPECT_EQ(std::wstring(pacecar::FormatTemperature(buffer, -10.5, 1)), L"-10.5\u00B0C");
}

TEST(Format, Duration)
{
    wchar_t buffer[64];
    EXPECT_EQ(std::wstring(pacecar::FormatDuration(buffer, 0.0, 1)), L"0.0s");
    EXPECT_EQ(std::wstring(pacecar::FormatDuration(buffer, 12.3, 1)), L"12.3s");
    EXPECT_EQ(std::wstring(pacecar::FormatDuration(buffer, 90.0)), L"1m 30s");
    EXPECT_EQ(std::wstring(pacecar::FormatDuration(buffer, 3661.0)), L"1h 01m");
    EXPECT_EQ(std::wstring(pacecar::FormatDuration(buffer, -5.0, 1)), L"-5.0s");
}

TEST(Format, TruncatesWithoutOverrun)
{
    wchar_t small[4];
    const std::wstring_view text = pacecar::FormatPercent(small, 12.34, 2);
    EXPECT_EQ(text.size(), 3u);
    EXPECT_EQ(small[3], L'\0');
}

TEST(Format, DecimalSeparatorInvariant)
{
    EXPECT_EQ(DecimalSeparator(DecimalStyle::Invariant), L'.');
    EXPECT_NE(DecimalSeparator(DecimalStyle::System), L'\0');
}
} // namespace