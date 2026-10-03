#pragma once

// A bounded, per-format LRU cache for values keyed by (format id, string).
//
// The overlay caches `IDWriteTextLayout` objects so a layout is only rebuilt when its formatted
// string changes (design refs 04-ui-ux.md "Cache IDWriteTextLayout objects", 05-performance.md "no
// per-frame heap allocation"). This template holds the keying/eviction policy and is deliberately
// free of DirectWrite so it can be unit-tested headlessly and reused for other cached values.
//
// Hot path: `Get` for an existing (format, string) pair performs two allocation-free lookups (an
// `unordered_map` by format id and an `std::map` by string with a transparent comparator) and
// returns a reference. A *miss* allocates the string node and calls the factory; that only happens
// when the formatted text actually changes.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace pacecar::overlay
{
// Transparent comparator so `std::map<std::wstring, ...>::find` accepts a `std::wstring_view`
// without constructing a temporary `std::wstring` (no allocation).
struct WStringLess
{
    using is_transparent = void;

    [[nodiscard]] bool operator()(std::wstring_view a, std::wstring_view b) const noexcept
    {
        return a < b;
    }
};

template <class Value>
class TextLayoutCache
{
  public:
    explicit TextLayoutCache(std::size_t maxEntriesPerFormat = 48) noexcept
        : maxEntriesPerFormat_(maxEntriesPerFormat == 0 ? 1 : maxEntriesPerFormat)
    {
    }

    TextLayoutCache(const TextLayoutCache&) = delete;
    TextLayoutCache& operator=(const TextLayoutCache&) = delete;

    // Returns the cached value for (formatId, text), creating it with `create()` on a miss.
    template <class Factory>
    Value& Get(std::uint32_t formatId, std::wstring_view text, Factory&& create)
    {
        FormatCache& format = formats_[formatId];
        const auto found = format.entries.find(text);
        if (found != format.entries.end())
        {
            ++hits_;
            found->second.lastUse = ++useCounter_;
            return found->second.value;
        }

        ++misses_;
        if (format.entries.size() >= maxEntriesPerFormat_)
        {
            EvictOldest(format);
            ++evictions_;
        }
        const auto inserted =
            format.entries.try_emplace(std::wstring(text), Entry{create(), 0});
        inserted.first->second.lastUse = ++useCounter_;
        return inserted.first->second.value;
    }

    [[nodiscard]] bool Contains(std::uint32_t formatId, std::wstring_view text) const
    {
        const auto format = formats_.find(formatId);
        if (format == formats_.end())
        {
            return false;
        }
        return format->second.entries.find(text) != format->second.entries.end();
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        std::size_t total = 0;
        for (const auto& format : formats_)
        {
            total += format.second.entries.size();
        }
        return total;
    }

    [[nodiscard]] std::size_t FormatCount() const noexcept
    {
        return formats_.size();
    }

    [[nodiscard]] std::size_t MaxEntriesPerFormat() const noexcept
    {
        return maxEntriesPerFormat_;
    }

    void Clear() noexcept
    {
        formats_.clear();
        useCounter_ = 0;
        hits_ = 0;
        misses_ = 0;
        evictions_ = 0;
    }

    [[nodiscard]] std::uint64_t Hits() const noexcept
    {
        return hits_;
    }

    [[nodiscard]] std::uint64_t Misses() const noexcept
    {
        return misses_;
    }

    [[nodiscard]] std::uint64_t Evictions() const noexcept
    {
        return evictions_;
    }

  private:
    struct Entry
    {
        Value value{};
        std::uint64_t lastUse = 0;
    };

    struct FormatCache
    {
        std::map<std::wstring, Entry, WStringLess> entries;
    };

    static void EvictOldest(FormatCache& format)
    {
        auto victim = format.entries.begin();
        for (auto it = format.entries.begin(); it != format.entries.end(); ++it)
        {
            if (it->second.lastUse < victim->second.lastUse)
            {
                victim = it;
            }
        }
        if (victim != format.entries.end())
        {
            format.entries.erase(victim);
        }
    }

    std::unordered_map<std::uint32_t, FormatCache> formats_;
    std::size_t maxEntriesPerFormat_ = 1;
    std::uint64_t useCounter_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t evictions_ = 0;
};
} // namespace pacecar::overlay