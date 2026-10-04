#pragma once

// On-demand launch of the optional elevated `Pacecar.Sensors` helper (design decision 0021,
// open decision #4). The UI stays `asInvoker`; only the helper elevates, producing a single UAC
// prompt when the user enables deep sensors. The launcher finds the helper next to the UI
// executable and starts it with the `runas` verb, keeping the process handle so the helper can be
// terminated when deep sensors are turned off or the UI exits.
//
// The launch call is injectable so the "executable missing", "UAC declined", and "started" branches
// can be exercised without starting a real elevated process. A Windows service (LocalSystem) can
// replace this later without any protocol change (see ADR 0021).

#include <functional>
#include <string>

namespace pacecar::overlay
{
struct HelperLaunchOutcome
{
    bool started = false;
    bool declined = false;
    void* process = nullptr;
    unsigned long error = 0;
};

using HelperLaunchFunction = std::function<HelperLaunchOutcome(const std::wstring& executable)>;

struct HelperLaunchResult
{
    bool launched = false;          // A new elevated helper process was started this call.
    bool alreadyRunning = false;    // A previously launched helper is still alive.
    bool executableFound = true;    // `Pacecar.Sensors.exe` was found next to the UI.
    bool elevationDeclined = false; // The user declined the UAC prompt (ERROR_CANCELLED).
    std::wstring message;           // Human-readable outcome for diagnostics/logging.
};

class HelperLauncher
{
  public:
    HelperLauncher();
    explicit HelperLauncher(HelperLaunchFunction launch);
    ~HelperLauncher();

    HelperLauncher(const HelperLauncher&) = delete;
    HelperLauncher& operator=(const HelperLauncher&) = delete;

    // Where the helper is expected: next to the running UI executable.
    [[nodiscard]] static std::wstring ExecutablePath();

    // Starts the helper elevated if it is not already running. Never throws; failures are reported
    // in the result so the caller can surface "install/launch the helper" guidance.
    HelperLaunchResult EnsureElevated();

    // Same as above but for an explicit helper executable path (used by tests and by a future
    // packaging layout where the helper lives elsewhere).
    HelperLaunchResult EnsureElevated(const std::wstring& executable);

    // Maps a raw launch attempt to the caller-facing result. Exposed so the started/declined/failed
    // branches are unit-testable without creating a process.
    [[nodiscard]] static HelperLaunchResult ClassifyOutcome(const HelperLaunchOutcome& outcome);

    [[nodiscard]] bool Running() const noexcept;

    // Terminates a helper this launcher started (no-op if it exited already).
    void Stop();

  private:
    void CloseProcessHandle() noexcept;

    HelperLaunchFunction launch_;
    void* process_ = nullptr;
};
} // namespace pacecar::overlay