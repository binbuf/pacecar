#include "pacecar/overlay/OverlayPlacement.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pacecar::overlay
{
namespace
{
int ClipToRange(int value, int low, int high) noexcept
{
    if (low > high)
    {
        return low;
    }
    return value < low ? low : (value > high ? high : value);
}
} // namespace

long long IntersectionArea(const IntRect& a, const IntRect& b) noexcept
{
    const int left = std::max(a.left, b.left);
    const int top = std::max(a.top, b.top);
    const int right = std::min(a.right, b.right);
    const int bottom = std::min(a.bottom, b.bottom);
    if (right <= left || bottom <= top)
    {
        return 0;
    }
    return static_cast<long long>(right - left) * static_cast<long long>(bottom - top);
}

bool IsSufficientlyVisible(const IntRect& rect, const MonitorWorkArea& monitor, int margin) noexcept
{
    if (rect.IsEmpty() || monitor.workArea.IsEmpty())
    {
        return false;
    }
    const IntRect& work = monitor.workArea;
    const int visibleWidth = std::min(rect.right, work.right) - std::max(rect.left, work.left);
    const int visibleHeight = std::min(rect.bottom, work.bottom) - std::max(rect.top, work.top);
    if (visibleWidth <= 0 || visibleHeight <= 0)
    {
        return false;
    }
    const int requiredWidth = std::min(margin > 0 ? margin : 1, rect.Width());
    const int requiredHeight = std::min(margin > 0 ? margin : 1, rect.Height());
    return visibleWidth >= requiredWidth && visibleHeight >= requiredHeight;
}

bool IsRectVisible(const IntRect& rect,
                   const std::vector<MonitorWorkArea>& monitors,
                   int margin) noexcept
{
    for (const MonitorWorkArea& monitor : monitors)
    {
        if (IsSufficientlyVisible(rect, monitor, margin))
        {
            return true;
        }
    }
    return false;
}

const MonitorWorkArea* FindBestMonitor(const IntRect& rect,
                                       const std::vector<MonitorWorkArea>& monitors) noexcept
{
    const MonitorWorkArea* best = nullptr;
    long long bestArea = 0;
    for (const MonitorWorkArea& monitor : monitors)
    {
        const long long area = IntersectionArea(rect, monitor.workArea);
        if (area > bestArea)
        {
            bestArea = area;
            best = &monitor;
        }
    }
    return best;
}

const MonitorWorkArea* PrimaryMonitor(const std::vector<MonitorWorkArea>& monitors) noexcept
{
    for (const MonitorWorkArea& monitor : monitors)
    {
        if (monitor.primary)
        {
            return &monitor;
        }
    }
    return monitors.empty() ? nullptr : &monitors.front();
}

const MonitorWorkArea* FindNearestMonitor(const IntRect& rect,
                                          const std::vector<MonitorWorkArea>& monitors) noexcept
{
    const MonitorWorkArea* nearest = nullptr;
    long long bestDistance = std::numeric_limits<long long>::max();
    const long long rectCenterX =
        static_cast<long long>(rect.left) + static_cast<long long>(rect.Width()) / 2;
    const long long rectCenterY =
        static_cast<long long>(rect.top) + static_cast<long long>(rect.Height()) / 2;
    for (const MonitorWorkArea& monitor : monitors)
    {
        const IntRect& work = monitor.workArea;
        const long long centerX =
            static_cast<long long>(work.left) + static_cast<long long>(work.Width()) / 2;
        const long long centerY =
            static_cast<long long>(work.top) + static_cast<long long>(work.Height()) / 2;
        const long long dx = centerX - rectCenterX;
        const long long dy = centerY - rectCenterY;
        const long long distance = dx * dx + dy * dy;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            nearest = &monitor;
        }
    }
    return nearest;
}

IntRect ClampToWorkArea(const IntRect& rect,
                        const std::vector<MonitorWorkArea>& monitors,
                        int margin) noexcept
{
    if (monitors.empty() || rect.IsEmpty())
    {
        return rect;
    }

    const MonitorWorkArea* best = FindBestMonitor(rect, monitors);
    if (best != nullptr && IsSufficientlyVisible(rect, *best, margin))
    {
        return rect;
    }

    const MonitorWorkArea* target = best;
    if (target == nullptr)
    {
        target = FindNearestMonitor(rect, monitors);
    }
    if (target == nullptr)
    {
        target = PrimaryMonitor(monitors);
    }
    if (target == nullptr || target->workArea.IsEmpty())
    {
        return rect;
    }

    const IntRect& work = target->workArea;
    const int width = std::min(rect.Width(), work.Width());
    const int height = std::min(rect.Height(), work.Height());
    const int x = ClipToRange(rect.left, work.left, work.right - width);
    const int y = ClipToRange(rect.top, work.top, work.bottom - height);
    return IntRect{x, y, x + width, y + height};
}

unsigned NormalizeDpi(unsigned dpi) noexcept
{
    if (dpi < 48 || dpi > 768)
    {
        return kBaseDpi;
    }
    return dpi;
}

float DpiScale(unsigned dpi) noexcept
{
    return static_cast<float>(NormalizeDpi(dpi)) / static_cast<float>(kBaseDpi);
}

int DipToPixels(float dip, unsigned dpi) noexcept
{
    const double scaled = static_cast<double>(dip) * static_cast<double>(NormalizeDpi(dpi)) /
                          static_cast<double>(kBaseDpi);
    return static_cast<int>(std::lround(scaled));
}

float PixelsToDip(int pixels, unsigned dpi) noexcept
{
    const double dip = static_cast<double>(pixels) * static_cast<double>(kBaseDpi) /
                       static_cast<double>(NormalizeDpi(dpi));
    return static_cast<float>(dip);
}

IntRect ScaleRectForDpi(const IntRect& rect, unsigned fromDpi, unsigned toDpi) noexcept
{
    const unsigned from = NormalizeDpi(fromDpi);
    const unsigned to = NormalizeDpi(toDpi);
    if (from == to)
    {
        return rect;
    }
    const double scale = static_cast<double>(to) / static_cast<double>(from);
    return IntRect{static_cast<int>(std::lround(rect.left * scale)),
                   static_cast<int>(std::lround(rect.top * scale)),
                   static_cast<int>(std::lround(rect.right * scale)),
                   static_cast<int>(std::lround(rect.bottom * scale))};
}
} // namespace pacecar::overlay