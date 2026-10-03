#pragma once

// Tabular-friendly value formatting for the overlay.
//
// Every formatter has two forms:
//   * a buffer form that writes into a caller-owned std::span<wchar_t> and allocates nothing, for
//     the render hot path; and
//   * a std::wstring conveniences form for tests and one-off use.
// The buffer form always null-terminates when the span is non-empty and truncates rather than
// overruns. The decimal separator defaults to the invariant '.' and can be overridden with the
// system locale separator when explicitly requested.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace pacecar
{
enum class DecimalStyle
{
    Invariant,
    System,
};

// Invariant always returns L'.'. System queries LOCALE_NAME_USER_DEFAULT, falling back to L'.'.
[[nodiscard]] wchar_t DecimalSeparator(DecimalStyle style) noexcept;

// percent is a 0..100 value; renders e.g. "12.3%".
[[nodiscard]] std::wstring_view FormatPercent(std::span<wchar_t> buffer, double percent,
                                              int decimals = 1,
                                              DecimalStyle style = DecimalStyle::Invariant) noexcept;
[[nodiscard]] std::wstring FormatPercent(double percent, int decimals = 1,
                                         DecimalStyle style = DecimalStyle::Invariant);

// IEC units, e.g. "0 B", "1023 B", "1.0 KiB", "3.5 GiB". Byte-valued output has no decimals.
[[nodiscard]] std::wstring_view FormatBytes(std::span<wchar_t> buffer, std::uint64_t bytes,
                                            int decimals = 1,
                                            DecimalStyle style = DecimalStyle::Invariant) noexcept;
[[nodiscard]] std::wstring FormatBytes(std::uint64_t bytes, int decimals = 1,
                                       DecimalStyle style = DecimalStyle::Invariant);

// Bytes per second, e.g. "1.0 MiB/s".
[[nodiscard]] std::wstring_view FormatRate(std::span<wchar_t> buffer, double bytesPerSecond,
                                           int decimals = 1,
                                           DecimalStyle style = DecimalStyle::Invariant) noexcept;
[[nodiscard]] std::wstring FormatRate(double bytesPerSecond, int decimals = 1,
                                      DecimalStyle style = DecimalStyle::Invariant);

// hertz is in Hz; renders GHz, e.g. "3.60 GHz".
[[nodiscard]] std::wstring_view FormatFrequency(std::span<wchar_t> buffer, double hertz,
                                                int decimals = 2,
                                                DecimalStyle style = DecimalStyle::Invariant) noexcept;
[[nodiscard]] std::wstring FormatFrequency(double hertz, int decimals = 2,
                                           DecimalStyle style = DecimalStyle::Invariant);

// Celsius, e.g. "55.0°C" (the degree symbol is U+00B0).
[[nodiscard]] std::wstring_view FormatTemperature(std::span<wchar_t> buffer, double celsius,
                                                  int decimals = 1,
                                                  DecimalStyle style = DecimalStyle::Invariant) noexcept;
[[nodiscard]] std::wstring FormatTemperature(double celsius, int decimals = 1,
                                             DecimalStyle style = DecimalStyle::Invariant);

// Human-readable seconds: "12.3s", "1m 30s", or "1h 01m".
[[nodiscard]] std::wstring_view FormatDuration(std::span<wchar_t> buffer, double seconds,
                                               int decimals = 1,
                                               DecimalStyle style = DecimalStyle::Invariant) noexcept;
[[nodiscard]] std::wstring FormatDuration(double seconds, int decimals = 1,
                                          DecimalStyle style = DecimalStyle::Invariant);
} // namespace pacecar