#include "pacecar/util/Format.h"

#include <windows.h>

#include <cstdio>
#include <cwchar>

namespace pacecar
{
namespace
{
// Appends into a caller-owned buffer, guaranteeing a trailing null when there is room. Truncation
// is silent and never writes past the span.
class Builder
{
  public:
    explicit Builder(std::span<wchar_t> out) noexcept : out_(out)
    {
        if (!out_.empty())
        {
            out_[0] = L'\0';
        }
    }

    void Append(std::wstring_view text) noexcept
    {
        for (const wchar_t ch : text)
        {
            if (len_ + 1 >= out_.size())
            {
                break;
            }
            out_[len_++] = ch;
        }
        Terminate();
    }

    void AppendDouble(double value, int decimals, wchar_t separator) noexcept
    {
        if (decimals < 0)
        {
            decimals = 0;
        }
        else if (decimals > 9)
        {
            decimals = 9;
        }

        wchar_t number[64]{};
        const int written =
            _snwprintf_s(number, _countof(number), _TRUNCATE, L"%.*f", decimals, value);
        if (written < 0)
        {
            return;
        }

        for (int i = 0; i < written; ++i)
        {
            if (number[i] == L'.')
            {
                number[i] = separator;
            }
        }
        Append(std::wstring_view(number, static_cast<std::size_t>(written)));
    }

    [[nodiscard]] std::size_t Length() const noexcept
    {
        return len_;
    }

    [[nodiscard]] std::wstring_view View() const noexcept
    {
        return std::wstring_view(out_.data(), len_);
    }

  private:
    void Terminate() noexcept
    {
        if (!out_.empty())
        {
            out_[len_] = L'\0';
        }
    }

    std::span<wchar_t> out_;
    std::size_t len_ = 0;
};

void AppendByteUnits(Builder& builder, double value, int decimals, wchar_t separator) noexcept
{
    constexpr const wchar_t* kUnits[] = {L"B", L"KiB", L"MiB", L"GiB", L"TiB", L"PiB"};
    constexpr int kLastUnit = static_cast<int>(_countof(kUnits)) - 1;

    int unit = 0;
    while (value >= 1024.0 && unit < kLastUnit)
    {
        value /= 1024.0;
        ++unit;
    }

    builder.AppendDouble(value, unit == 0 ? 0 : decimals, separator);
    builder.Append(L" ");
    builder.Append(kUnits[unit]);
}

std::wstring ToWString(std::wstring_view view)
{
    return std::wstring(view);
}
} // namespace

wchar_t DecimalSeparator(DecimalStyle style) noexcept
{
    if (style == DecimalStyle::Invariant)
    {
        return L'.';
    }

    wchar_t buffer[8]{};
    const int count = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, buffer,
                                      static_cast<int>(_countof(buffer)));
    if (count > 1 && buffer[0] != L'\0')
    {
        return buffer[0];
    }
    return L'.';
}

std::wstring_view FormatPercent(std::span<wchar_t> buffer, double percent, int decimals,
                                DecimalStyle style) noexcept
{
    Builder builder(buffer);
    if (buffer.empty())
    {
        return {};
    }
    builder.AppendDouble(percent, decimals, DecimalSeparator(style));
    builder.Append(L"%");
    return builder.View();
}

std::wstring FormatPercent(double percent, int decimals, DecimalStyle style)
{
    wchar_t buffer[64];
    return ToWString(FormatPercent(buffer, percent, decimals, style));
}

std::wstring_view FormatBytes(std::span<wchar_t> buffer, std::uint64_t bytes, int decimals,
                              DecimalStyle style) noexcept
{
    Builder builder(buffer);
    if (buffer.empty())
    {
        return {};
    }
    AppendByteUnits(builder, static_cast<double>(bytes), decimals, DecimalSeparator(style));
    return builder.View();
}

std::wstring FormatBytes(std::uint64_t bytes, int decimals, DecimalStyle style)
{
    wchar_t buffer[64];
    return ToWString(FormatBytes(buffer, bytes, decimals, style));
}

std::wstring_view FormatRate(std::span<wchar_t> buffer, double bytesPerSecond, int decimals,
                             DecimalStyle style) noexcept
{
    Builder builder(buffer);
    if (buffer.empty())
    {
        return {};
    }
    AppendByteUnits(builder, bytesPerSecond, decimals, DecimalSeparator(style));
    builder.Append(L"/s");
    return builder.View();
}

std::wstring FormatRate(double bytesPerSecond, int decimals, DecimalStyle style)
{
    wchar_t buffer[64];
    return ToWString(FormatRate(buffer, bytesPerSecond, decimals, style));
}

std::wstring_view FormatFrequency(std::span<wchar_t> buffer, double hertz, int decimals,
                                  DecimalStyle style) noexcept
{
    Builder builder(buffer);
    if (buffer.empty())
    {
        return {};
    }
    builder.AppendDouble(hertz / 1.0e9, decimals, DecimalSeparator(style));
    builder.Append(L" GHz");
    return builder.View();
}

std::wstring FormatFrequency(double hertz, int decimals, DecimalStyle style)
{
    wchar_t buffer[64];
    return ToWString(FormatFrequency(buffer, hertz, decimals, style));
}

std::wstring_view FormatTemperature(std::span<wchar_t> buffer, double celsius, int decimals,
                                    DecimalStyle style) noexcept
{
    Builder builder(buffer);
    if (buffer.empty())
    {
        return {};
    }
    builder.AppendDouble(celsius, decimals, DecimalSeparator(style));
    builder.Append(L"\u00B0C");
    return builder.View();
}

std::wstring FormatTemperature(double celsius, int decimals, DecimalStyle style)
{
    wchar_t buffer[64];
    return ToWString(FormatTemperature(buffer, celsius, decimals, style));
}

std::wstring_view FormatDuration(std::span<wchar_t> buffer, double seconds, int decimals,
                                 DecimalStyle style) noexcept
{
    Builder builder(buffer);
    if (buffer.empty())
    {
        return {};
    }

    const wchar_t separator = DecimalSeparator(style);
    if (seconds < 0.0 || seconds < 60.0)
    {
        builder.AppendDouble(seconds, decimals, separator);
        builder.Append(L"s");
        return builder.View();
    }

    if (seconds < 3600.0)
    {
        const int total = static_cast<int>(seconds);
        const int minutes = total / 60;
        const int remainder = total % 60;
        wchar_t text[32]{};
        _snwprintf_s(text, _countof(text), _TRUNCATE, L"%dm %02ds", minutes, remainder);
        builder.Append(text);
        return builder.View();
    }

    const int total = static_cast<int>(seconds);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    wchar_t text[32]{};
    _snwprintf_s(text, _countof(text), _TRUNCATE, L"%dh %02dm", hours, minutes);
    builder.Append(text);
    return builder.View();
}

std::wstring FormatDuration(double seconds, int decimals, DecimalStyle style)
{
    wchar_t buffer[64];
    return ToWString(FormatDuration(buffer, seconds, decimals, style));
}
} // namespace pacecar