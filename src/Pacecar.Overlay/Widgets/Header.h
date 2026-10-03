#pragma once

// The header/status line (design ref 04-ui-ux.md): the app title on the left, a status line on the
// right ("Live", "Deep sensors off", provider unavailable), and a subtle separator.

#include "Text.h"
#include "WidgetStyles.h"

namespace pacecar::overlay
{
class Header
{
  public:
    void Draw(ID2D1RenderTarget* target, TextRenderer& text, const WidgetStyles& styles,
              const RectF& bounds, std::wstring_view title, std::wstring_view status,
              const ResolvedTheme& theme);

  private:
    CachedBrush titleBrush_;
    CachedBrush statusBrush_;
    CachedBrush separatorBrush_;
};
} // namespace pacecar::overlay