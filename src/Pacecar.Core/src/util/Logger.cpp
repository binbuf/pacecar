#include "pacecar/util/Logger.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cwchar>
#include <string>
#include <utility>

namespace pacecar
{
namespace
{
std::uint64_t HashMessage(std::wstring_view message) noexcept
{
    // FNV-1a over the UTF-16 code units; only used to bucket rate-limit state.
    std::uint64_t hash = 1469598103934665603ull;
    for (const wchar_t ch : message)
    {
        hash ^= static_cast<std::uint16_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::wstring BuildLine(LogLevel level, std::wstring_view message,
                       std::size_t suppressed) noexcept
{
    std::wstring line;
    line.reserve(message.size() + 32);
    line.push_back(L'[');
    line.append(LogLevelName(level));
    line.append(L"] ");
    line.append(message);
    if (suppressed > 0)
    {
        wchar_t suffix[48]{};
        _snwprintf_s(suffix, _countof(suffix), _TRUNCATE, L" (suppressed %zu)", suppressed);
        line.append(suffix);
    }
    return line;
}
} // namespace

const wchar_t* LogLevelName(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
        return L"TRACE";
    case LogLevel::Debug:
        return L"DEBUG";
    case LogLevel::Info:
        return L"INFO";
    case LogLevel::Warn:
        return L"WARN";
    case LogLevel::Error:
        return L"ERROR";
    case LogLevel::Off:
    default:
        return L"OFF";
    }
}

Logger& Logger::Instance() noexcept
{
    static Logger instance;
    return instance;
}

Logger::~Logger()
{
    CloseFileSink();
}

void Logger::SetDiagnosticsEnabled(bool enabled) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    diagnosticsEnabled_ = enabled;
}

bool Logger::DiagnosticsEnabled() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return diagnosticsEnabled_;
}

void Logger::SetMinimumLevel(LogLevel level) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    minimumLevel_ = level;
}

LogLevel Logger::MinimumLevel() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return minimumLevel_;
}

void Logger::SetSink(Sink sink) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = std::move(sink);
}

bool Logger::SetFileSink(std::wstring_view path) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr)
    {
        std::fclose(file_);
        file_ = nullptr;
    }
    const std::wstring widePath(path);
    if (_wfopen_s(&file_, widePath.c_str(), L"a, ccs=UTF-8") != 0)
    {
        file_ = nullptr;
        return false;
    }
    return true;
}

void Logger::CloseFileSink() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr)
    {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void Logger::SetRateLimit(std::chrono::milliseconds window, std::size_t maxPerWindow) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    rateWindow_ = window;
    rateMaxPerWindow_ = maxPerWindow == 0 ? 1 : maxPerWindow;
}

void Logger::ResetRateLimit() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (RateSlot& slot : slots_)
    {
        slot = RateSlot{};
    }
}

void Logger::Flush() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr)
    {
        std::fflush(file_);
    }
}

Logger::RateSlot* Logger::FindOrCreateSlot(std::uint64_t hash, LogLevel level) noexcept
{
    RateSlot* oldest = &slots_[0];
    for (RateSlot& slot : slots_)
    {
        if (slot.used && slot.hash == hash && slot.level == level)
        {
            return &slot;
        }
        if (!slot.used)
        {
            return &slot;
        }
        if (slot.last < oldest->last)
        {
            oldest = &slot;
        }
    }
    // All slots in use: evict the least recently seen.
    *oldest = RateSlot{};
    return oldest;
}

bool Logger::Log(LogLevel level, std::wstring_view message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!diagnosticsEnabled_ || level < minimumLevel_ || level == LogLevel::Off)
    {
        return false;
    }

    const auto now = Clock::now();
    const std::uint64_t hash = HashMessage(message);
    RateSlot* slot = FindOrCreateSlot(hash, level);

    if (slot->used && (now - slot->last) < rateWindow_)
    {
        if (slot->emitted < rateMaxPerWindow_)
        {
            ++slot->emitted;
            Emit(level, message, 0);
            return true;
        }
        ++slot->suppressed;
        return false;
    }

    // A new message or an expired window: report how many duplicates the last window dropped.
    const std::size_t suppressed = slot->used ? slot->suppressed : 0;
    slot->used = true;
    slot->hash = hash;
    slot->level = level;
    slot->last = now;
    slot->emitted = 1;
    slot->suppressed = 0;

    Emit(level, message, suppressed);
    return true;
}

void Logger::Emit(LogLevel level, std::wstring_view message, std::size_t suppressed)
{
    const std::wstring line = BuildLine(level, message, suppressed);
    if (sink_)
    {
        sink_(level, line);
        return;
    }
    if (file_ != nullptr)
    {
        std::fputws(line.c_str(), file_);
        std::fputws(L"\n", file_);
        return;
    }
    OutputDebugStringW(line.c_str());
    OutputDebugStringW(L"\n");
}

void LogTrace(std::wstring_view message)
{
    Logger::Instance().Log(LogLevel::Trace, message);
}
void LogDebug(std::wstring_view message)
{
    Logger::Instance().Log(LogLevel::Debug, message);
}
void LogInfo(std::wstring_view message)
{
    Logger::Instance().Log(LogLevel::Info, message);
}
void LogWarn(std::wstring_view message)
{
    Logger::Instance().Log(LogLevel::Warn, message);
}
void LogError(std::wstring_view message)
{
    Logger::Instance().Log(LogLevel::Error, message);
}
} // namespace pacecar