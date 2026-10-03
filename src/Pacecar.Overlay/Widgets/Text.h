#pragma once

// The DirectWrite text pipeline (design refs 04-ui-ux.md, 05-performance.md).
//
// One `TextRenderer` owns the shared `IDWriteFactory`, a table of `IDWriteTextFormat` styles, and a
// bounded LRU cache of `IDWriteTextLayout` objects keyed by (format id, string). A layout is only
// rebuilt when the formatted string changes; drawing a cached layout allocates nothing. Numeric
// styles use a tabular/monospace family (Consolas) and request the OpenType `tnum` feature so digit
// widths are uniform and values do not jitter. Text is drawn with grayscale antialiasing because the
// surface is per-pixel alpha (ClearType is unpredictable over transparency; see design 03).

#include <cstdint>
#include <string_view>
#include <vector>

#include "WidgetCommon.h"
#include "pacecar/overlay/TextCache.h"
#include "pacecar/overlay/WidgetLayout.h"

namespace pacecar::overlay
{
enum class TextAlign
{
    Leading,
    Center,
    Trailing,
};

struct TextStyle
{
    const wchar_t* family = L"Segoe UI";
    float sizeDip = 11.0f;
    int weight = 400; // DWRITE_FONT_WEIGHT
    TextAlign align = TextAlign::Leading;
    // Request tabular figures where the font supports them (numeric readouts).
    bool tabular = false;
};

class TextRenderer
{
  public:
    TextRenderer() = default;
    ~TextRenderer() = default;

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // Creates the shared DirectWrite factory. Safe to call more than once.
    HRESULT Initialize();

    [[nodiscard]] bool Ready() const noexcept
    {
        return factory_ != nullptr;
    }

    // Registers a text format and returns its id. Styles are process-stable; register once at setup.
    std::uint32_t RegisterFormat(const TextStyle& style);

    // Draws `text` inside `rect` using the cached layout for `formatId`. `rect` is in DIPs and the
    // text is centered per the style alignment. `brush` is owned by the caller.
    void DrawText(ID2D1RenderTarget* target, std::uint32_t formatId, std::wstring_view text,
                  const RectF& rect, ID2D1Brush* brush);

    // Drops cached layouts (device loss / occlusion).
    void Trim();

    [[nodiscard]] std::size_t LayoutCount() const noexcept
    {
        return layouts_.Size();
    }

    [[nodiscard]] const TextLayoutCache<ComPtr<IDWriteTextLayout>>& Cache() const noexcept
    {
        return layouts_;
    }

  private:
    ComPtr<IDWriteTextLayout> CreateLayout(std::uint32_t formatId, std::wstring_view text);

    ComPtr<IDWriteFactory> factory_;
    std::vector<ComPtr<IDWriteTextFormat>> formats_;
    std::vector<TextStyle> styles_;
    TextLayoutCache<ComPtr<IDWriteTextLayout>> layouts_;
};
} // namespace pacecar::overlay