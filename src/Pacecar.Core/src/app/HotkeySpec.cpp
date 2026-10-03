#include "pacecar/app/HotkeySpec.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <vector>

namespace pacecar::app
{
namespace
{
std::wstring_view Trim(std::wstring_view text) noexcept
{
    while (!text.empty() && std::iswspace(text.front()) != 0)
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::iswspace(text.back()) != 0)
    {
        text.remove_suffix(1);
    }
    return text;
}

std::wstring Lower(std::wstring_view text)
{
    std::wstring lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return lowered;
}

struct NamedKey
{
    const wchar_t* name;
    std::uint32_t virtualKey;
};

constexpr std::array<NamedKey, 15> kNamedKeys{{
    {L"backspace", kHotkeyVkBackspace}, {L"tab", kHotkeyVkTab},
    {L"enter", kHotkeyVkEnter},         {L"return", kHotkeyVkEnter},
    {L"escape", kHotkeyVkEscape},       {L"esc", kHotkeyVkEscape},
    {L"space", kHotkeyVkSpace},         {L"pageup", kHotkeyVkPageUp},
    {L"pgup", kHotkeyVkPageUp},         {L"pagedown", kHotkeyVkPageDown},
    {L"pgdn", kHotkeyVkPageDown},       {L"end", kHotkeyVkEnd},
    {L"home", kHotkeyVkHome},           {L"insert", kHotkeyVkInsert},
    {L"delete", kHotkeyVkDelete},
}};

// Parses a key token. Returns 0 (invalid) when the token is not a recognized key. `error` is set
// only when the token looks like a key but uses an unsupported form (e.g. an out-of-range F-key).
std::uint32_t ParseKeyToken(std::wstring_view token, std::wstring& error)
{
    const std::wstring lower = Lower(token);
    if (lower.empty())
    {
        error = L"missing key";
        return 0;
    }
    if (lower.size() == 1)
    {
        const wchar_t c = lower.front();
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9'))
        {
            return static_cast<std::uint32_t>(std::towupper(c));
        }
        error = L"unsupported key '" + std::wstring(token) + L"'";
        return 0;
    }
    if (lower.front() == L'f' && lower.size() <= 3)
    {
        bool digits = true;
        int value = 0;
        for (std::size_t i = 1; i < lower.size(); ++i)
        {
            const wchar_t c = lower[i];
            if (c < L'0' || c > L'9')
            {
                digits = false;
                break;
            }
            value = value * 10 + (c - L'0');
        }
        if (digits && value >= 1 && value <= 24)
        {
            return kHotkeyVkF1 + static_cast<std::uint32_t>(value - 1);
        }
        error = L"unsupported function key '" + std::wstring(token) + L"'";
        return 0;
    }
    for (const NamedKey& named : kNamedKeys)
    {
        if (lower == named.name)
        {
            return named.virtualKey;
        }
    }
    // Arrow keys use a compact set of spellings.
    if (lower == L"left" || lower == L"leftarrow")
    {
        return kHotkeyVkLeft;
    }
    if (lower == L"right" || lower == L"rightarrow")
    {
        return kHotkeyVkRight;
    }
    if (lower == L"up" || lower == L"uparrow")
    {
        return kHotkeyVkUp;
    }
    if (lower == L"down" || lower == L"downarrow")
    {
        return kHotkeyVkDown;
    }
    error = L"unknown key '" + std::wstring(token) + L"'";
    return 0;
}

bool ParseModifier(std::wstring_view token, std::uint32_t& modifiers)
{
    const std::wstring lower = Lower(token);
    if (lower == L"ctrl" || lower == L"control")
    {
        modifiers |= kHotkeyControl;
        return true;
    }
    if (lower == L"shift")
    {
        modifiers |= kHotkeyShift;
        return true;
    }
    if (lower == L"alt")
    {
        modifiers |= kHotkeyAlt;
        return true;
    }
    if (lower == L"win" || lower == L"super" || lower == L"meta")
    {
        modifiers |= kHotkeyWin;
        return true;
    }
    return false;
}
} // namespace

HotkeyParseResult ParseHotkey(std::wstring_view text)
{
    HotkeyParseResult result;
    const std::wstring_view trimmed = Trim(text);
    if (trimmed.empty())
    {
        result.error = L"empty hotkey";
        return result;
    }

    std::vector<std::wstring_view> tokens;
    std::size_t start = 0;
    while (start <= trimmed.size())
    {
        const std::size_t plus = trimmed.find(L'+', start);
        const std::size_t end = plus == std::wstring_view::npos ? trimmed.size() : plus;
        tokens.push_back(Trim(trimmed.substr(start, end - start)));
        if (plus == std::wstring_view::npos)
        {
            break;
        }
        start = plus + 1;
    }

    for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
    {
        if (tokens[i].empty() || !ParseModifier(tokens[i], result.binding.modifiers))
        {
            result.error = tokens[i].empty() ? L"empty modifier"
                                             : L"unknown modifier '" + std::wstring(tokens[i]) + L"'";
            return result;
        }
    }

    const std::wstring_view keyToken = tokens.back();
    // A trailing "+" or a modifier in the key slot is a missing key.
    if (keyToken.empty() || ParseModifier(keyToken, result.binding.modifiers))
    {
        result.error = L"missing key";
        return result;
    }
    result.binding.virtualKey = ParseKeyToken(keyToken, result.error);
    return result;
}

std::wstring FormatHotkey(const HotkeyBinding& binding)
{
    if (!binding.valid())
    {
        return {};
    }
    std::wstring text;
    const auto append = [&text](const wchar_t* part)
    {
        if (!text.empty())
        {
            text.push_back(L'+');
        }
        text += part;
    };
    if ((binding.modifiers & kHotkeyControl) != 0)
    {
        append(L"Ctrl");
    }
    if ((binding.modifiers & kHotkeyShift) != 0)
    {
        append(L"Shift");
    }
    if ((binding.modifiers & kHotkeyAlt) != 0)
    {
        append(L"Alt");
    }
    if ((binding.modifiers & kHotkeyWin) != 0)
    {
        append(L"Win");
    }

    if (!text.empty())
    {
        text.push_back(L'+');
    }
    const std::uint32_t vk = binding.virtualKey;
    if (vk >= static_cast<std::uint32_t>(L'A') && vk <= static_cast<std::uint32_t>(L'Z'))
    {
        text.push_back(static_cast<wchar_t>(vk));
        return text;
    }
    if (vk >= static_cast<std::uint32_t>(L'0') && vk <= static_cast<std::uint32_t>(L'9'))
    {
        text.push_back(static_cast<wchar_t>(vk));
        return text;
    }
    if (vk >= kHotkeyVkF1 && vk < kHotkeyVkF1 + 24)
    {
        text += L"F";
        text += std::to_wstring(vk - kHotkeyVkF1 + 1);
        return text;
    }
    switch (vk)
    {
    case kHotkeyVkBackspace:
        text += L"Backspace";
        break;
    case kHotkeyVkTab:
        text += L"Tab";
        break;
    case kHotkeyVkEnter:
        text += L"Enter";
        break;
    case kHotkeyVkEscape:
        text += L"Escape";
        break;
    case kHotkeyVkSpace:
        text += L"Space";
        break;
    case kHotkeyVkPageUp:
        text += L"PageUp";
        break;
    case kHotkeyVkPageDown:
        text += L"PageDown";
        break;
    case kHotkeyVkEnd:
        text += L"End";
        break;
    case kHotkeyVkHome:
        text += L"Home";
        break;
    case kHotkeyVkLeft:
        text += L"Left";
        break;
    case kHotkeyVkUp:
        text += L"Up";
        break;
    case kHotkeyVkRight:
        text += L"Right";
        break;
    case kHotkeyVkDown:
        text += L"Down";
        break;
    case kHotkeyVkInsert:
        text += L"Insert";
        break;
    case kHotkeyVkDelete:
        text += L"Delete";
        break;
    default:
        return {};
    }
    return text;
}
} // namespace pacecar::app