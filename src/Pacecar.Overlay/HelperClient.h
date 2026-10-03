#pragma once

// No-op stub for the optional elevated sensor helper (design ref 01-architecture.md "Process
// model"). Task T16 replaces this with the real named-pipe client.
//
// The lifecycle calls `TryConnect()` once, non-blocking. The stub always reports failure, so deep
// sensors are marked unavailable and the app stays fully functional, which is the required behavior
// when the helper is absent.

#include <string>

namespace pacecar::overlay
{
class HelperClient
{
  public:
    // Attempts a non-blocking connection. Returns true only when a helper is connected (never, in
    // the MVP stub).
    bool TryConnect();

    void Disconnect();

    [[nodiscard]] bool Connected() const noexcept
    {
        return connected_;
    }

    // One-line status for diagnostics ("unavailable (helper stub; T16)" or "connected").
    [[nodiscard]] std::wstring Status() const;

  private:
    bool connected_ = false;
};
} // namespace pacecar::overlay