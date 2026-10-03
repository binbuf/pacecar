#include "Hotkey.h"

#include "pacecar/util/Logger.h"

namespace pacecar::overlay
{
UINT HotkeyToWin32Modifiers(std::uint32_t modifiers) noexcept
{
    UINT result = MOD_NOREPEAT;
    if ((modifiers & pacecar::app::kHotkeyControl) != 0)
    {
        result |= MOD_CONTROL;
    }
    if ((modifiers & pacecar::app::kHotkeyShift) != 0)
    {
        result |= MOD_SHIFT;
    }
    if ((modifiers & pacecar::app::kHotkeyAlt) != 0)
    {
        result |= MOD_ALT;
    }
    if ((modifiers & pacecar::app::kHotkeyWin) != 0)
    {
        result |= MOD_WIN;
    }
    return result;
}

HotkeyManager::~HotkeyManager()
{
    UnregisterAll();
}

void HotkeyManager::Attach(HWND hwnd)
{
    UnregisterAll();
    hwnd_ = hwnd;
    lastError_.clear();
}

bool HotkeyManager::Register(int id, const pacecar::app::HotkeyBinding& binding)
{
    lastError_.clear();
    Unregister(id);
    if (hwnd_ == nullptr)
    {
        lastError_ = L"no window for hotkey registration";
        return false;
    }
    if (!binding.valid())
    {
        lastError_ = L"invalid hotkey binding";
        return false;
    }

    const UINT modifiers = HotkeyToWin32Modifiers(binding.modifiers);
    if (RegisterHotKey(hwnd_, id, modifiers, binding.virtualKey) == FALSE)
    {
        const DWORD code = GetLastError();
        lastError_ = L"hotkey '" + pacecar::app::FormatHotkey(binding) + L"' is already in use";
        LogWarn(std::wstring(L"hotkey: registration failed (") + std::to_wstring(code) + L")");
        return false;
    }
    entries_[id] = Entry{binding, pacecar::app::FormatHotkey(binding)};
    return true;
}

void HotkeyManager::Unregister(int id)
{
    const auto it = entries_.find(id);
    if (it == entries_.end())
    {
        return;
    }
    if (hwnd_ != nullptr)
    {
        UnregisterHotKey(hwnd_, id);
    }
    entries_.erase(it);
}

void HotkeyManager::UnregisterAll()
{
    for (const auto& [id, entry] : entries_)
    {
        static_cast<void>(entry);
        if (hwnd_ != nullptr)
        {
            UnregisterHotKey(hwnd_, id);
        }
    }
    entries_.clear();
}

bool HotkeyManager::IsRegistered(int id) const noexcept
{
    return entries_.find(id) != entries_.end();
}

bool HotkeyManager::HandleHotkey(int id) const
{
    if (entries_.find(id) == entries_.end())
    {
        return false;
    }
    if (callback_)
    {
        callback_(id);
    }
    return true;
}
} // namespace pacecar::overlay