#pragma once

// PawnIO presence detection (design refs 06-security-distribution.md, 02-metrics.md).
//
// Deep sensors depend on the user installing the signed PawnIO driver; Pacecar never bundles a
// kernel driver and never ships or uses WinRing0 (blocklisted). The overlay only needs to *detect*
// PawnIO so it can explain deep-sensor availability; the elevated helper performs the actual reads.
//
// Detection checks the device interface (`\\.\GLOBALROOT\Device\PawnIO`) and the uninstall registry
// key. The OS calls sit behind `IPawnIOSystemSource` so the decision logic is exercised with fakes
// and never depends on the machine running the tests.

#include <memory>
#include <string>

namespace pacecar::metrics
{
enum class PawnIOStatus
{
    Absent,
    Installed,
};

class IPawnIOSystemSource
{
  public:
    virtual ~IPawnIOSystemSource() = default;

    // True when the PawnIO device object exists (open succeeded or was refused as access-denied).
    [[nodiscard]] virtual bool DevicePresent() = 0;

    // True when the PawnIO uninstall entry exists (64-bit or WOW6432Node view).
    [[nodiscard]] virtual bool UninstallKeyPresent() = 0;
};

// PawnIO counts as installed when either probe reports it.
[[nodiscard]] PawnIOStatus DetectPawnIO(IPawnIOSystemSource& source) noexcept;

// Real Win32 probes.
[[nodiscard]] std::unique_ptr<IPawnIOSystemSource> MakeWin32PawnIOSource();

[[nodiscard]] const wchar_t* PawnIOStatusText(PawnIOStatus status) noexcept;

// User-facing guidance for the About/diagnostics text.
[[nodiscard]] std::wstring PawnIOGuidance(PawnIOStatus status);
} // namespace pacecar::metrics