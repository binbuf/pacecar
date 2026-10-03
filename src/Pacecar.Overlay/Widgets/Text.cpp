#include "Text.h"

#include <algorithm>
#include <limits>

namespace pacecar::overlay
{
namespace
{
// A layout is built with a generous fixed box and positioned by metrics at draw time, so one cached
// layout can be reused regardless of the tile's width. Word wrapping is disabled on the format.
constexpr float kLayoutBox = 4096.0f;

// OpenType feature tag 'tnum' (tabular figures), little-endian.
constexpr DWRITE_FONT_FEATURE_TAG kTabularFigures = static_cast<DWRITE_FONT_FEATURE_TAG>(
    static_cast<std::uint32_t>('t') | (static_cast<std::uint32_t>('n') << 8) |
    (static_cast<std::uint32_t>('u') << 16) | (static_cast<std::uint32_t>('m') << 24));
} // namespace

HRESULT TextRenderer::Initialize()
{
    if (factory_ != nullptr)
    {
        return S_OK;
    }
    return DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                               reinterpret_cast<IUnknown**>(factory_.GetAddressOf()));
}

std::uint32_t TextRenderer::RegisterFormat(const TextStyle& style)
{
    if (factory_ == nullptr)
    {
        return 0;
    }
    ComPtr<IDWriteTextFormat> format;
    const HRESULT created = factory_->CreateTextFormat(
        style.family, nullptr, static_cast<DWRITE_FONT_WEIGHT>(style.weight),
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style.sizeDip, L"",
        format.GetAddressOf());
    if (FAILED(created))
    {
        formats_.push_back(nullptr);
        styles_.push_back(style);
        return static_cast<std::uint32_t>(formats_.size() - 1);
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    formats_.push_back(std::move(format));
    styles_.push_back(style);
    return static_cast<std::uint32_t>(formats_.size() - 1);
}

ComPtr<IDWriteTextLayout> TextRenderer::CreateLayout(std::uint32_t formatId, std::wstring_view text)
{
    ComPtr<IDWriteTextLayout> layout;
    if (factory_ == nullptr || formatId >= formats_.size() || formats_[formatId] == nullptr)
    {
        return layout;
    }

    const std::wstring_view safeText = text.empty() ? std::wstring_view(L" ") : text;
    if (FAILED(factory_->CreateTextLayout(safeText.data(), static_cast<UINT32>(safeText.size()),
                                          formats_[formatId].Get(), kLayoutBox, kLayoutBox,
                                          layout.GetAddressOf())))
    {
        layout.Reset();
        return layout;
    }

    if (styles_[formatId].tabular)
    {
        ComPtr<IDWriteTypography> typography;
        if (SUCCEEDED(factory_->CreateTypography(typography.GetAddressOf())))
        {
            const DWRITE_FONT_FEATURE feature{kTabularFigures, 1};
            if (SUCCEEDED(typography->AddFontFeature(feature)))
            {
                const DWRITE_TEXT_RANGE range{0, static_cast<UINT32>(safeText.size())};
                layout->SetTypography(typography.Get(), range);
            }
        }
    }
    return layout;
}

void TextRenderer::DrawText(ID2D1RenderTarget* target, std::uint32_t formatId,
                            std::wstring_view text, const RectF& rect, ID2D1Brush* brush)
{
    if (target == nullptr || brush == nullptr || rect.IsEmpty())
    {
        return;
    }
    if (formats_.empty() || formatId >= formats_.size())
    {
        return;
    }

    // Explicit grayscale AA: the layered surface has an alpha channel, where ClearType is
    // unpredictable (design 03 "Transparency and text").
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    IDWriteTextLayout* layout = layouts_.Get(formatId, text, [this, formatId, text]()
                                             { return CreateLayout(formatId, text); })
                                    .Get();
    if (layout == nullptr)
    {
        return;
    }

    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics)))
    {
        return;
    }

    float x = rect.left;
    float y = rect.top;
    switch (styles_[formatId].align)
    {
    case TextAlign::Center:
        x = rect.left + (rect.Width() - metrics.width) * 0.5f;
        break;
    case TextAlign::Trailing:
        x = rect.right - metrics.width;
        break;
    case TextAlign::Leading:
    default:
        break;
    }
    y = rect.top + (rect.Height() - metrics.height) * 0.5f;

    target->DrawTextLayout(D2D1::Point2F(x, y), layout, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void TextRenderer::Trim()
{
    layouts_.Clear();
}
} // namespace pacecar::overlay