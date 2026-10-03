#include "HelperClient.h"

namespace pacecar::overlay
{
bool HelperClient::TryConnect()
{
    // T16 owns the real named-pipe connection. Absence is the expected MVP state.
    connected_ = false;
    return connected_;
}

void HelperClient::Disconnect()
{
    connected_ = false;
}

std::wstring HelperClient::Status() const
{
    return connected_ ? L"connected" : L"unavailable (helper stub; T16)";
}
} // namespace pacecar::overlay