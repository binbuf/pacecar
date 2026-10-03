#include "pacecar/app/TrayTooltip.h"

#include <cmath>
#include <vector>

#include "pacecar/util/Format.h"

namespace pacecar::app
{
namespace
{
void AppendPart(std::wstring& text, const std::wstring& part)
{
    if (part.empty())
    {
        return;
    }
    if (!text.empty())
    {
        text += L" | ";
    }
    text += part;
}
} // namespace

std::wstring FormatTrayTooltip(const pacecar::metrics::MetricsSnapshot& snapshot)
{
    std::vector<std::wstring> parts;

    if (snapshot.cpu.status.available)
    {
        parts.push_back(L"CPU " + pacecar::FormatPercent(snapshot.cpu.totalUtilizationPercent, 0));
    }
    if (snapshot.gpu.temperatureStatus.available && snapshot.gpu.temperatureC > 0.0)
    {
        parts.push_back(L"GPU " +
                        std::to_wstring(static_cast<int>(std::lround(snapshot.gpu.temperatureC))) +
                        L"C");
    }
    else if (snapshot.gpu.status.available)
    {
        parts.push_back(L"GPU " + pacecar::FormatPercent(snapshot.gpu.utilizationPercent, 0));
    }
    if (snapshot.memory.status.available)
    {
        parts.push_back(L"RAM " + pacecar::FormatPercent(snapshot.memory.usedPercent, 0));
    }

    std::wstring text;
    for (const std::wstring& part : parts)
    {
        AppendPart(text, part);
    }
    if (text.empty())
    {
        return L"Pacecar";
    }
    if (text.size() > kTrayTooltipCapacity)
    {
        text.resize(kTrayTooltipCapacity);
    }
    return text;
}
} // namespace pacecar::app