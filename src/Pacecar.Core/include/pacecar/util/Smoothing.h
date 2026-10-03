#pragma once

// Exponential moving average for stabilizing percentage/utilization readouts. Raw values belong in
// RingBuffer history; Ema is only the display-smoothed value.

namespace pacecar
{
class Ema
{
  public:
    Ema() noexcept = default;

    // alpha is the weight of the newest sample in [0, 1]; values outside are clamped.
    explicit Ema(double alpha) noexcept
    {
        SetAlpha(alpha);
    }

    // Derives alpha from a window of N samples as 2 / (N + 1); N is treated as at least 1.
    [[nodiscard]] static Ema FromWindow(int window) noexcept
    {
        if (window < 1)
        {
            window = 1;
        }
        return Ema(2.0 / (static_cast<double>(window) + 1.0));
    }

    void SetAlpha(double alpha) noexcept
    {
        if (alpha < 0.0)
        {
            alpha = 0.0;
        }
        else if (alpha > 1.0)
        {
            alpha = 1.0;
        }
        alpha_ = alpha;
    }

    [[nodiscard]] double Alpha() const noexcept
    {
        return alpha_;
    }

    // The first sample is passed through unchanged (no previous value to blend with).
    double Push(double sample) noexcept
    {
        if (!hasValue_)
        {
            value_ = sample;
            hasValue_ = true;
        }
        else
        {
            value_ += alpha_ * (sample - value_);
        }
        return value_;
    }

    [[nodiscard]] double Value() const noexcept
    {
        return value_;
    }
    [[nodiscard]] bool HasValue() const noexcept
    {
        return hasValue_;
    }

    void Reset() noexcept
    {
        value_ = 0.0;
        hasValue_ = false;
    }

  private:
    double alpha_ = 0.5;
    double value_ = 0.0;
    bool hasValue_ = false;
};
} // namespace pacecar