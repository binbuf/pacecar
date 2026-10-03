#pragma once

// Fixed-capacity, preallocated ring buffer for per-metric sample history.
//
// The storage is sized exactly once in the constructor and never resized: Push, iteration, and
// downsampling do not allocate. This lets the sampler and render paths hold spans over the same
// backing store without a lock on the hot path.

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace pacecar
{
template <typename T> class RingBuffer
{
  public:
    explicit RingBuffer(std::size_t capacity) : data_(capacity) {}

    // Appends a sample, overwriting the oldest one once the buffer is full.
    void Push(const T& value) noexcept
    {
        if (data_.empty())
        {
            return;
        }
        data_[head_] = value;
        head_ = (head_ + 1) % data_.size();
        if (size_ < data_.size())
        {
            ++size_;
        }
    }

    void Push(T&& value) noexcept
    {
        if (data_.empty())
        {
            return;
        }
        data_[head_] = std::move(value);
        head_ = (head_ + 1) % data_.size();
        if (size_ < data_.size())
        {
            ++size_;
        }
    }

    [[nodiscard]] std::size_t Capacity() const noexcept
    {
        return data_.size();
    }
    [[nodiscard]] std::size_t Size() const noexcept
    {
        return size_;
    }
    [[nodiscard]] bool Empty() const noexcept
    {
        return size_ == 0;
    }
    [[nodiscard]] bool Full() const noexcept
    {
        return size_ == data_.size();
    }

    // Stable pointer to the backing store; unchanged for the buffer's lifetime.
    [[nodiscard]] const T* Data() const noexcept
    {
        return data_.data();
    }

    // Indexed from the oldest retained sample (At(0)) to the newest (At(Size() - 1)).
    [[nodiscard]] const T& At(std::size_t index) const noexcept
    {
        return data_[Physical(index)];
    }

    [[nodiscard]] const T& Oldest(std::size_t offset = 0) const noexcept
    {
        return At(offset);
    }

    // offset 0 is the newest sample; larger offsets walk backwards in time.
    [[nodiscard]] const T& Newest(std::size_t offset = 0) const noexcept
    {
        return At(size_ - 1 - offset);
    }

    [[nodiscard]] const T& Latest() const noexcept
    {
        return Newest(0);
    }

    // Visits samples from oldest to newest. No copies, no allocation.
    template <typename F> void ForEachOldestFirst(F&& fn) const
    {
        for (std::size_t i = 0; i < size_; ++i)
        {
            fn(At(i));
        }
    }

    // Visits samples from newest to oldest. No copies, no allocation.
    template <typename F> void ForEachNewestFirst(F&& fn) const
    {
        for (std::size_t i = 0; i < size_; ++i)
        {
            fn(Newest(i));
        }
    }

    // Writes up to out.size() samples spanning oldest..newest into the caller-provided buffer and
    // returns how many were written. With count == 1 the newest sample is used.
    std::size_t Downsample(std::span<T> out) const noexcept
    {
        if (out.empty() || size_ == 0)
        {
            return 0;
        }
        if (out.size() >= size_)
        {
            for (std::size_t i = 0; i < size_; ++i)
            {
                out[i] = At(i);
            }
            return size_;
        }
        const std::size_t count = out.size();
        if (count == 1)
        {
            out[0] = Latest();
            return 1;
        }
        const std::size_t span = size_ - 1;
        const std::size_t steps = count - 1;
        for (std::size_t k = 0; k < count; ++k)
        {
            out[k] = At((k * span) / steps);
        }
        return count;
    }

  private:
    // Maps a chronological index to a physical slot. When the buffer has wrapped, `head_` is the
    // oldest slot; before it wraps, samples live at the front of the vector.
    [[nodiscard]] std::size_t Physical(std::size_t index) const noexcept
    {
        if (size_ < data_.size())
        {
            return index;
        }
        return (head_ + index) % data_.size();
    }

    std::vector<T> data_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};
} // namespace pacecar