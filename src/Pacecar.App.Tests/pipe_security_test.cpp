#include <gtest/gtest.h>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include <sddl.h>

#include <cstdio>
#include <string>

#include "pacecar/app/AppIdentity.h"
#include "pacecar/metrics/PipeSecurity.h"

namespace
{
std::wstring UniquePipeName(const wchar_t* tag)
{
    wchar_t buffer[160] = {};
    swprintf_s(buffer, L"\\\\.\\pipe\\PacecarSecTest.%s.%lu", tag,
               static_cast<unsigned long>(GetCurrentProcessId()));
    return buffer;
}

class SecurityDescriptor
{
  public:
    explicit SecurityDescriptor(const std::wstring& sddl)
    {
        ok_ = ConvertStringSecurityDescriptorToSecurityDescriptorW(
                  sddl.c_str(), SDDL_REVISION_1, &descriptor_, nullptr) != FALSE;
    }

    ~SecurityDescriptor()
    {
        if (descriptor_ != nullptr)
        {
            LocalFree(descriptor_);
        }
    }

    SecurityDescriptor(const SecurityDescriptor&) = delete;
    SecurityDescriptor& operator=(const SecurityDescriptor&) = delete;

    [[nodiscard]] bool Ok() const noexcept
    {
        return ok_;
    }

    [[nodiscard]] SECURITY_ATTRIBUTES Attributes() const noexcept
    {
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = descriptor_;
        attributes.bInheritHandle = FALSE;
        return attributes;
    }

  private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    bool ok_ = false;
};

HANDLE CreateServerPipe(const std::wstring& name, SECURITY_ATTRIBUTES* sa)
{
    return CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096,
                            4096, 0, sa);
}
} // namespace

TEST(PipeSecurity, RejectsUnusableSid)
{
    EXPECT_TRUE(pacecar::metrics::BuildPipeSddl(L"").empty());
    EXPECT_TRUE(pacecar::metrics::BuildPipeSddl(L"not-a-sid").empty());
}

TEST(PipeSecurity, SddlGrantsUserAndDeniesNetworkAndAnonymous)
{
    const std::wstring sddl = pacecar::metrics::BuildPipeSddl(L"S-1-5-21-1-2-3-4");
    ASSERT_FALSE(sddl.empty());
    EXPECT_EQ(sddl.compare(0, 3, L"D:P"), 0);
    EXPECT_NE(sddl.find(L"S-1-5-21-1-2-3-4"), std::wstring::npos);
    EXPECT_NE(sddl.find(L";;AN)"), std::wstring::npos); // NT AUTHORITY\ANONYMOUS LOGON
    EXPECT_NE(sddl.find(L";;NU)"), std::wstring::npos); // NT AUTHORITY\NETWORK
    EXPECT_NE(sddl.find(L";;BA)"), std::wstring::npos); // BUILTIN\Administrators
}

TEST(PipeSecurity, ExplicitDaclAllowsTheIntendedUser)
{
    const std::wstring sid = pacecar::app::CurrentUserSid();
    ASSERT_FALSE(sid.empty()) << "could not determine the current user SID";

    const std::wstring sddl = pacecar::metrics::BuildPipeSddl(sid);
    ASSERT_FALSE(sddl.empty());
    SecurityDescriptor descriptor(sddl);
    ASSERT_TRUE(descriptor.Ok());

    SECURITY_ATTRIBUTES sa = descriptor.Attributes();
    const std::wstring name = UniquePipeName(L"allow");
    HANDLE server = CreateServerPipe(name, &sa);
    ASSERT_NE(server, INVALID_HANDLE_VALUE);

    HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    EXPECT_NE(client, INVALID_HANDLE_VALUE);
    if (client != INVALID_HANDLE_VALUE)
    {
        CloseHandle(client);
    }
    CloseHandle(server);
}

TEST(PipeSecurity, ExplicitDaclDeniesADifferentPrincipal)
{
    const std::wstring sid = pacecar::app::CurrentUserSid();
    if (sid == L"S-1-5-18")
    {
        GTEST_SKIP() << "running as LocalSystem; cannot test rejection of a non-matching principal";
    }

    // A DACL that grants only LocalSystem: the current (non-SYSTEM) principal must be refused.
    // This proves the pipe is governed by an explicit restrictive DACL, not a NULL/default one.
    SecurityDescriptor descriptor(L"D:P(A;;GA;;;SY)");
    ASSERT_TRUE(descriptor.Ok());
    SECURITY_ATTRIBUTES sa = descriptor.Attributes();
    const std::wstring name = UniquePipeName(L"deny");
    HANDLE server = CreateServerPipe(name, &sa);
    ASSERT_NE(server, INVALID_HANDLE_VALUE);

    HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    EXPECT_EQ(client, INVALID_HANDLE_VALUE);
    if (client == INVALID_HANDLE_VALUE)
    {
        EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_ACCESS_DENIED));
    }
    else
    {
        CloseHandle(client);
    }
    CloseHandle(server);
}