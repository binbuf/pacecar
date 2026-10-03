#include "pacecar/app/AppIdentity.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include <sddl.h>

namespace pacecar::app
{
namespace
{
constexpr wchar_t kSingletonPrefix[] = L"Local\\Pacecar.Singleton.";
constexpr wchar_t kActivationMessagePrefix[] = L"Pacecar.Activate.";
constexpr wchar_t kActivationWindowPrefix[] = L"Pacecar.SingletonWindow.";
} // namespace

std::wstring MakeSingletonName(std::wstring_view suffix)
{
    std::wstring name = kSingletonPrefix;
    name.append(suffix);
    return name;
}

std::wstring MakeActivationMessageName(std::wstring_view suffix)
{
    std::wstring name = kActivationMessagePrefix;
    name.append(suffix);
    return name;
}

std::wstring MakeActivationWindowClass(std::wstring_view suffix)
{
    std::wstring name = kActivationWindowPrefix;
    name.append(suffix);
    return name;
}

std::wstring CurrentUserSid()
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE)
    {
        return {};
    }

    DWORD size = 0;
    static_cast<void>(GetTokenInformation(token, TokenUser, nullptr, 0, &size));
    if (size == 0)
    {
        CloseHandle(token);
        return {};
    }

    std::wstring sid;
    auto* buffer = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, size));
    if (buffer != nullptr)
    {
        if (GetTokenInformation(token, TokenUser, buffer, size, &size) != FALSE)
        {
            const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer);
            LPWSTR stringSid = nullptr;
            if (ConvertSidToStringSidW(user->User.Sid, &stringSid) != FALSE)
            {
                sid.assign(stringSid);
                LocalFree(stringSid);
            }
        }
        HeapFree(GetProcessHeap(), 0, buffer);
    }

    CloseHandle(token);
    return sid;
}
} // namespace pacecar::app