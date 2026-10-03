#include <gtest/gtest.h>

#include "pacecar/metrics/PawnIODetection.h"

namespace
{
class FakePawnIOSource final : public pacecar::metrics::IPawnIOSystemSource
{
  public:
    FakePawnIOSource(bool device, bool uninstall) : device_(device), uninstall_(uninstall) {}

    bool DevicePresent() override
    {
        return device_;
    }

    bool UninstallKeyPresent() override
    {
        return uninstall_;
    }

  private:
    bool device_;
    bool uninstall_;
};
} // namespace

TEST(PawnIODetection, AbsentWhenNoProbeMatches)
{
    FakePawnIOSource source(false, false);
    EXPECT_EQ(pacecar::metrics::DetectPawnIO(source), pacecar::metrics::PawnIOStatus::Absent);
}

TEST(PawnIODetection, InstalledWhenDevicePresent)
{
    FakePawnIOSource source(true, false);
    EXPECT_EQ(pacecar::metrics::DetectPawnIO(source), pacecar::metrics::PawnIOStatus::Installed);
}

TEST(PawnIODetection, InstalledWhenUninstallKeyPresent)
{
    FakePawnIOSource source(false, true);
    EXPECT_EQ(pacecar::metrics::DetectPawnIO(source), pacecar::metrics::PawnIOStatus::Installed);
}

TEST(PawnIODetection, TextAndGuidanceAreProvided)
{
    EXPECT_NE(pacecar::metrics::PawnIOStatusText(pacecar::metrics::PawnIOStatus::Installed), nullptr);
    EXPECT_NE(pacecar::metrics::PawnIOStatusText(pacecar::metrics::PawnIOStatus::Absent), nullptr);
    EXPECT_FALSE(pacecar::metrics::PawnIOGuidance(pacecar::metrics::PawnIOStatus::Absent).empty());
    EXPECT_FALSE(pacecar::metrics::PawnIOGuidance(pacecar::metrics::PawnIOStatus::Installed).empty());
}