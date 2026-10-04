#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "targetver.h"

#include "HelperLauncher.h"

#include <cstdio>
#include <utility>

#include <windows.h>
#include <shellapi.h>

namespace pacecar::overlay
{
namespace
{
constexpr wchar_t kHelperExecutableName[] = L"Pacecar.Sensors.exe";

std::wstring DirectoryOfCurrentExecutable()
{
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return {};
    }
    std::wstring path(modulePath, length);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
    {
        return {};
    }
    return path.substr(0, slash + 1);
}

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

HelperLaunchOutcome LaunchViaRunAs(const std::wstring& executable)
{
    HelperLaunchOutcome outcome;
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.nShow = SW_HIDE;

    if (ShellExecuteExW(&info) == FALSE)
    {
        const DWORD error = GetLastError();
        outcome.error = error;
        outcome.declined = error == ERROR_CANCELLED;
        return outcome;
    }

    outcome.started = true;
    outcome.process = info.hProcess;
    return outcome;
}
} // namespace

HelperLauncher::HelperLauncher() : launch_(&LaunchViaRunAs)
{
}

HelperLauncher::HelperLauncher(HelperLaunchFunction launch)
    : launch_(launch ? std::move(launch) : HelperLaunchFunction(&LaunchViaRunAs))
{
}

HelperLauncher::~HelperLauncher()
{
    Stop();
}

std::wstring HelperLauncher::ExecutablePath()
{
    const std::wstring directory = DirectoryOfCurrentExecutable();
    if (directory.empty())
    {
        return kHelperExecutableName;
    }
    return directory + kHelperExecutableName;
}

HelperLaunchResult HelperLauncher::EnsureElevated()
{
    return EnsureElevated(ExecutablePath());
}

HelperLaunchResult HelperLauncher::EnsureElevated(const std::wstring& executable)
{
    HelperLaunchResult result;
    if (Running())
    {
        result.launched = true;
        result.alreadyRunning = true;
        result.message = L"helper already running";
        return result;
    }

    if (!FileExists(executable))
    {
        result.executableFound = false;
        result.message = L"helper executable not found: " + executable;
        return result;
    }

    const HelperLaunchOutcome outcome = launch_(executable);
    result = ClassifyOutcome(outcome);
    if (outcome.started)
    {
        process_ = outcome.process;
    }
    return result;
}

HelperLaunchResult HelperLauncher::ClassifyOutcome(const HelperLaunchOutcome& outcome)
{
    HelperLaunchResult result;
    if (outcome.started)
    {
        result.launched = true;
        result.message = L"launched elevated helper";
    }
    else if (outcome.declined)
    {
        result.elevationDeclined = true;
        result.message = L"elevation was declined by the user";
    }
    else
    {
        wchar_t buffer[128] = {};
        swprintf_s(buffer, L"failed to launch helper (error %lu)", outcome.error);
        result.message = buffer;
    }
    return result;
}

bool HelperLauncher::Running() const noexcept
{
    if (process_ == nullptr)
    {
        return false;
    }
    return WaitForSingleObject(reinterpret_cast<HANDLE>(process_), 0) == WAIT_TIMEOUT;
}

void HelperLauncher::Stop()
{
    if (process_ == nullptr)
    {
        return;
    }
    if (WaitForSingleObject(reinterpret_cast<HANDLE>(process_), 0) == WAIT_TIMEOUT)
    {
        static_cast<void>(TerminateProcess(reinterpret_cast<HANDLE>(process_), 0));
    }
    CloseProcessHandle();
}

void HelperLauncher::CloseProcessHandle() noexcept
{
    if (process_ != nullptr)
    {
        CloseHandle(reinterpret_cast<HANDLE>(process_));
        process_ = nullptr;
    }
}
} // namespace pacecar::overlay