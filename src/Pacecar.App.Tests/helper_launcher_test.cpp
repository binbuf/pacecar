#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "HelperLauncher.h"

using pacecar::overlay::HelperLauncher;
using pacecar::overlay::HelperLaunchOutcome;
using pacecar::overlay::HelperLaunchResult;

TEST(HelperLauncher, ExecutablePathSitsNextToTheApp)
{
    const std::wstring path = HelperLauncher::ExecutablePath();
    ASSERT_FALSE(path.empty());
    EXPECT_NE(path.find(L"Pacecar.Sensors.exe"), std::wstring::npos);
}

TEST(HelperLauncher, ClassifiesStartedLaunch)
{
    HelperLaunchOutcome outcome;
    outcome.started = true;
    const HelperLaunchResult result = HelperLauncher::ClassifyOutcome(outcome);
    EXPECT_TRUE(result.launched);
    EXPECT_FALSE(result.elevationDeclined);
    EXPECT_FALSE(result.message.empty());
}

TEST(HelperLauncher, ClassifiesDeclinedElevation)
{
    HelperLaunchOutcome outcome;
    outcome.declined = true;
    const HelperLaunchResult result = HelperLauncher::ClassifyOutcome(outcome);
    EXPECT_TRUE(result.elevationDeclined);
    EXPECT_FALSE(result.launched);
    EXPECT_FALSE(result.message.empty());
}

TEST(HelperLauncher, ClassifiesGenericFailureWithErrorCode)
{
    HelperLaunchOutcome outcome;
    outcome.error = 5;
    const HelperLaunchResult result = HelperLauncher::ClassifyOutcome(outcome);
    EXPECT_FALSE(result.launched);
    EXPECT_FALSE(result.elevationDeclined);
    EXPECT_NE(result.message.find(L"5"), std::wstring::npos);
}

TEST(HelperLauncher, MissingExecutableIsReportedWithoutLaunching)
{
    bool launchCalled = false;
    HelperLauncher launcher(
        [&launchCalled](const std::wstring&)
        {
            launchCalled = true;
            return HelperLaunchOutcome{};
        });

    const HelperLaunchResult result =
        launcher.EnsureElevated(L"definitely-missing-pacecar-sensors.exe");
    EXPECT_FALSE(result.launched);
    EXPECT_FALSE(result.executableFound);
    EXPECT_FALSE(launchCalled);
    EXPECT_FALSE(launcher.Running());
}

TEST(HelperLauncher, StartedLaunchIsReportedForAnExistingExecutable)
{
    namespace fs = std::filesystem;
    const fs::path temporary = fs::temp_directory_path() / L"PacecarTest.Sensors.exe";
    {
        std::ofstream(temporary) << "stub";
    }

    HelperLauncher launcher(
        [](const std::wstring&)
        {
            HelperLaunchOutcome outcome;
            outcome.started = true; // no real process handle in this test
            return outcome;
        });

    const HelperLaunchResult result = launcher.EnsureElevated(temporary.wstring());
    EXPECT_TRUE(result.launched);
    EXPECT_TRUE(result.executableFound);
    EXPECT_FALSE(result.alreadyRunning);

    fs::remove(temporary);
}