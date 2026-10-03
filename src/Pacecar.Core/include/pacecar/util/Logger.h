#pragma once

// Small structured logger. It is inert until diagnostics are enabled, so shipping builds can call
// Logger freely without emitting output. Identical messages (same level + text) are suppressed
// within a rate-limit window to avoid flooding the sink during sampling.
//
// This is deliberately not a general logging framework: no third-party dependency, a single
// process-wide instance, an optional file sink, and a testable sink callback.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string_view>

namespace pacecar
{
enum class LogLevel
{
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Off,
};

[[nodiscard]] const wchar_t* LogLevelName(LogLevel level) noexcept;

class Logger
{
  public:
    using Sink = std::function<void(LogLevel, std::wstring_view)>;
    using Clock = std::chrono::steady_clock;

    static Logger& Instance() noexcept;

    void SetDiagnosticsEnabled(bool enabled) noexcept;
    [[nodiscard]] bool DiagnosticsEnabled() const noexcept;

    void SetMinimumLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel MinimumLevel() const noexcept;

    // Replaces the sink. An empty sink (the default) writes via OutputDebugStringW while
    // diagnostics are enabled. Pass nullptr to clear.
    void SetSink(Sink sink) noexcept;

    // Opens an append-mode UTF-8-ish file sink at path. Returns false if the file cannot be opened.
    [[nodiscard]] bool SetFileSink(const std::wstring_view path) noexcept;
    void CloseFileSink() noexcept;

    // Rate limit: at most `maxPerWindow` emissions of an identical message within `window`.
    void SetRateLimit(std::chrono::milliseconds window, std::size_t maxPerWindow = 1) noexcept;

    // Emits the message and returns true when it was written; returns false when diagnostics are
    // off, the level is below the minimum, or the message was rate limited.
    bool Log(LogLevel level, std::wstring_view message);

    // Clears rate-limit state (used by tests and on reconfiguration).
    void ResetRateLimit() noexcept;

    void Flush() noexcept;

  private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static constexpr std::size_t kMaxTrackedMessages = 64;

    struct RateSlot
    {
        std::uint64_t hash = 0;
        LogLevel level = LogLevel::Trace;
        Clock::time_point last{};
        std::size_t emitted = 0;
        std::size_t suppressed = 0;
        bool used = false;
    };

    void Emit(LogLevel level, std::wstring_view message, std::size_t suppressed);
    [[nodiscard]] RateSlot* FindOrCreateSlot(std::uint64_t hash, LogLevel level) noexcept;

    bool diagnosticsEnabled_ = false;
    LogLevel minimumLevel_ = LogLevel::Info;
    Sink sink_{};
    std::FILE* file_ = nullptr;
    std::chrono::milliseconds rateWindow_{1000};
    std::size_t rateMaxPerWindow_ = 1;
    RateSlot slots_[kMaxTrackedMessages]{};
    mutable std::mutex mutex_{};
};

// Convenience wrappers. No-ops unless diagnostics are enabled.
void LogTrace(std::wstring_view message);
void LogDebug(std::wstring_view message);
void LogInfo(std::wstring_view message);
void LogWarn(std::wstring_view message);
void LogError(std::wstring_view message);
} // namespace pacecar