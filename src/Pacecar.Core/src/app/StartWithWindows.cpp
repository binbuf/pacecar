#include "pacecar/app/StartWithWindows.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

namespace pacecar::app
{
namespace
{
std::wstring FormatWin32Error(LSTATUS status)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, static_cast<DWORD>(status),
                                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message;
    if (length != 0 && buffer != nullptr)
    {
        message.assign(buffer, length);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' ||
                                    message.back() == L' '))
        {
            message.pop_back();
        }
    }
    else
    {
        message = L"error " + std::to_wstring(status);
    }
    if (buffer != nullptr)
    {
        LocalFree(buffer);
    }
    return message;
}

void SetError(std::wstring* error, const wchar_t* context, LSTATUS status)
{
    if (error != nullptr)
    {
        *error = std::wstring(context) + L": " + FormatWin32Error(status);
    }
}
} // namespace

std::wstring BuildStartupCommand(std::wstring_view executablePath, bool startHidden)
{
    std::wstring command = L"\"";
    command.append(executablePath);
    command.push_back(L'"');
    if (startHidden)
    {
        command += L" --start-hidden";
    }
    return command;
}

bool ReadStartupValue(std::wstring_view subKey, std::wstring_view valueName, std::wstring& command)
{
    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(HKEY_CURRENT_USER, std::wstring(subKey).c_str(), 0,
                                             KEY_QUERY_VALUE, &key);
    if (openStatus != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    LSTATUS status = RegQueryValueExW(key, std::wstring(valueName).c_str(), nullptr, &type, nullptr,
                                      &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0)
    {
        RegCloseKey(key);
        return false;
    }

    std::wstring value;
    value.resize(size / sizeof(wchar_t));
    status = RegQueryValueExW(key, std::wstring(valueName).c_str(), nullptr, &type,
                              reinterpret_cast<LPBYTE>(value.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
    {
        return false;
    }
    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }
    command = std::move(value);
    return true;
}

bool WriteStartupValue(std::wstring_view subKey, std::wstring_view valueName,
                       std::wstring_view command, std::wstring* error)
{
    HKEY key = nullptr;
    const LSTATUS status =
        RegCreateKeyExW(HKEY_CURRENT_USER, std::wstring(subKey).c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS)
    {
        SetError(error, L"open Run key", status);
        return false;
    }
    const std::wstring name(valueName);
    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LSTATUS setStatus = RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                                             reinterpret_cast<const BYTE*>(command.data()), bytes);
    RegCloseKey(key);
    if (setStatus != ERROR_SUCCESS)
    {
        SetError(error, L"write Run value", setStatus);
        return false;
    }
    return true;
}

bool DeleteStartupValue(std::wstring_view subKey, std::wstring_view valueName, std::wstring* error)
{
    HKEY key = nullptr;
    const std::wstring keyPath(subKey);
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_SET_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND)
    {
        return true; // Nothing to remove.
    }
    if (status != ERROR_SUCCESS)
    {
        SetError(error, L"open Run key", status);
        return false;
    }
    const std::wstring name(valueName);
    status = RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
    {
        SetError(error, L"delete Run value", status);
        return false;
    }
    return true;
}

bool SetStartWithWindows(std::wstring_view executablePath, bool enabled, bool startHidden,
                         std::wstring* error)
{
    if (!enabled)
    {
        return DeleteStartupValue(kStartupRunKey, kStartupValueName, error);
    }
    return WriteStartupValue(kStartupRunKey, kStartupValueName,
                             BuildStartupCommand(executablePath, startHidden), error);
}

bool IsStartWithWindowsEnabled(std::wstring* commandOut, std::wstring* error)
{
    std::wstring command;
    if (!ReadStartupValue(kStartupRunKey, kStartupValueName, command))
    {
        if (error != nullptr)
        {
            *error = L"Run value is not set";
        }
        return false;
    }
    if (commandOut != nullptr)
    {
        *commandOut = std::move(command);
    }
    return true;
}
} // namespace pacecar::app