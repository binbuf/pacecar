#include "pacecar/metrics/DeviceSelection.h"

#include <cstring>

namespace pacecar::metrics
{
namespace
{
[[nodiscard]] char ToLowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool IsSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (ToLowerAscii(a[i]) != ToLowerAscii(b[i]))
        {
            return false;
        }
    }
    return true;
}

void CopyLower(char* destination, std::size_t capacity, std::string_view text) noexcept
{
    std::size_t length = 0;
    for (const char c : text)
    {
        if (length + 1 >= capacity)
        {
            break;
        }
        destination[length++] = ToLowerAscii(c);
    }
    destination[length] = '\0';
}
} // namespace

DeviceSelection ParseDeviceSelection(std::string_view text) noexcept
{
    while (!text.empty() && IsSpace(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsSpace(text.back()))
    {
        text.remove_suffix(1);
    }

    if (text.empty() || EqualsIgnoreCase(text, "auto") || EqualsIgnoreCase(text, "all"))
    {
        return DeviceSelection{};
    }

    bool allDigits = true;
    std::uint64_t value = 0;
    bool overflow = false;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
        {
            allDigits = false;
            break;
        }
        value = value * 10u + static_cast<std::uint64_t>(c - '0');
        if (value > 0xFFFFFFFFull)
        {
            overflow = true;
        }
    }
    if (allDigits)
    {
        if (overflow)
        {
            return DeviceSelection{}; // out of range -> All
        }
        DeviceSelection selection{};
        selection.kind = DeviceSelectionKind::Index;
        selection.index = static_cast<std::uint32_t>(value);
        return selection;
    }

    DeviceSelection selection{};
    selection.kind = DeviceSelectionKind::Name;
    CopyLower(selection.name, kNameCapacity, text);
    return selection;
}

bool NameContainsInsensitive(std::string_view haystack, std::string_view needle) noexcept
{
    if (needle.empty())
    {
        return true;
    }
    if (needle.size() > haystack.size())
    {
        return false;
    }
    const std::size_t last = haystack.size() - needle.size();
    for (std::size_t start = 0; start <= last; ++start)
    {
        bool match = true;
        for (std::size_t i = 0; i < needle.size(); ++i)
        {
            if (ToLowerAscii(haystack[start + i]) != ToLowerAscii(needle[i]))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
}
} // namespace pacecar::metrics