#include <gtest/gtest.h>

#include <windows.h>

#include <chrono>
#include <string>
#include <string_view>

#include "pacecar/util/Logger.h"

namespace
{
using pacecar::LogLevel;
using pacecar::Logger;

class LoggerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        Logger& logger = Logger::Instance();
        logger.SetDiagnosticsEnabled(false);
        logger.SetMinimumLevel(LogLevel::Trace);
        logger.SetSink(nullptr);
        logger.CloseFileSink();
        logger.SetRateLimit(std::chrono::milliseconds(10'000));
        logger.ResetRateLimit();
    }

    void TearDown() override
    {
        Logger& logger = Logger::Instance();
        logger.SetSink(nullptr);
        logger.CloseFileSink();
        logger.SetDiagnosticsEnabled(false);
        logger.ResetRateLimit();
    }
};

TEST_F(LoggerTest, DisabledWritesNothing)
{
    Logger& logger = Logger::Instance();
    int count = 0;
    logger.SetSink([&](LogLevel, std::wstring_view) { ++count; });

    EXPECT_FALSE(logger.Log(LogLevel::Info, L"hidden"));
    EXPECT_EQ(count, 0);
}

TEST_F(LoggerTest, EnabledWrites)
{
    Logger& logger = Logger::Instance();
    logger.SetDiagnosticsEnabled(true);
    int count = 0;
    logger.SetSink([&](LogLevel, std::wstring_view) { ++count; });

    EXPECT_TRUE(logger.Log(LogLevel::Info, L"shown"));
    EXPECT_EQ(count, 1);
}

TEST_F(LoggerTest, MinimumLevelFilters)
{
    Logger& logger = Logger::Instance();
    logger.SetDiagnosticsEnabled(true);
    logger.SetMinimumLevel(LogLevel::Warn);
    int count = 0;
    logger.SetSink([&](LogLevel, std::wstring_view) { ++count; });

    EXPECT_FALSE(logger.Log(LogLevel::Info, L"info"));
    EXPECT_TRUE(logger.Log(LogLevel::Error, L"error"));
    EXPECT_EQ(count, 1);
}

TEST_F(LoggerTest, RateLimitSuppressesDuplicatesWithinWindow)
{
    Logger& logger = Logger::Instance();
    logger.SetDiagnosticsEnabled(true);
    logger.SetRateLimit(std::chrono::milliseconds(10'000), 1);
    int count = 0;
    logger.SetSink([&](LogLevel, std::wstring_view) { ++count; });

    EXPECT_TRUE(logger.Log(LogLevel::Info, L"duplicate"));
    EXPECT_FALSE(logger.Log(LogLevel::Info, L"duplicate"));
    EXPECT_FALSE(logger.Log(LogLevel::Info, L"duplicate"));
    EXPECT_EQ(count, 1);

    EXPECT_TRUE(logger.Log(LogLevel::Info, L"different"));
    EXPECT_EQ(count, 2);
}

TEST_F(LoggerTest, FileSinkOnlyReceivesWhenEnabled)
{
    Logger& logger = Logger::Instance();

    wchar_t tempDir[MAX_PATH]{};
    ASSERT_NE(GetTempPathW(MAX_PATH, tempDir), 0u);
    wchar_t tempFile[MAX_PATH]{};
    ASSERT_NE(GetTempFileNameW(tempDir, L"pcl", 0, tempFile), 0u);

    logger.SetSink(nullptr);
    ASSERT_TRUE(logger.SetFileSink(tempFile));
    logger.SetMinimumLevel(LogLevel::Trace);
    logger.ResetRateLimit();

    logger.SetDiagnosticsEnabled(false);
    logger.Log(LogLevel::Info, L"disabled");
    logger.SetDiagnosticsEnabled(true);
    logger.Log(LogLevel::Info, L"enabled");
    logger.Flush();
    logger.CloseFileSink();

    const HANDLE file =
        CreateFileW(tempFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(file, INVALID_HANDLE_VALUE);
    LARGE_INTEGER size{};
    EXPECT_TRUE(GetFileSizeEx(file, &size));
    EXPECT_GT(size.QuadPart, 0);
    CloseHandle(file);
    DeleteFileW(tempFile);
}
} // namespace