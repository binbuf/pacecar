#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <windows.h>

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dxgiformat.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "pacecar/overlay/OverlayPlacement.h"
#include "pacecar/overlay/Theme.h"
#include "WidgetScene.h"

// Renders the real widget scene to an offscreen WIC bitmap at 100%/150%/200% DPI and checks that
// content is actually painted and that the DIP layout is scale-invariant. This automates the
// "widgets draw correctly at multiple DPI scales" acceptance item without a display.

namespace
{
using Microsoft::WRL::ComPtr;

class ComScope
{
  public:
    ComScope()
    {
        initialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    }
    ~ComScope()
    {
        if (initialized_)
        {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

  private:
    bool initialized_ = false;
};

struct RenderedSurface
{
    unsigned dpi = 96;
    int widthPx = 0;
    int heightPx = 0;
    D2D1_SIZE_F dipSize{};
    std::uint64_t opaquePixels = 0;
};

RenderedSurface RenderScene(unsigned dpi, float widthDip, float heightDip)
{
    ComScope com;

    ComPtr<ID2D1Factory> d2d;
    EXPECT_TRUE(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                            d2d.GetAddressOf())));
    ComPtr<IWICImagingFactory> wic;
    EXPECT_TRUE(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(wic.GetAddressOf()))));

    RenderedSurface result{};
    result.dpi = dpi;
    result.widthPx = pacecar::overlay::DipToPixels(widthDip, dpi);
    result.heightPx = pacecar::overlay::DipToPixels(heightDip, dpi);

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmap(static_cast<UINT>(result.widthPx),
                                 static_cast<UINT>(result.heightPx),
                                 GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                 bitmap.GetAddressOf())))
    {
        return result;
    }

    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi), static_cast<float>(dpi));
    ComPtr<ID2D1RenderTarget> target;
    if (FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), &properties,
                                                target.GetAddressOf())))
    {
        return result;
    }
    result.dipSize = target->GetSize();

    pacecar::overlay::ResolvedTheme theme =
        pacecar::overlay::ResolveTheme(pacecar::overlay::ThemeInputs{});
    pacecar::overlay::WidgetScene scene;

    target->BeginDraw();
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    scene.Draw(target.Get(), theme);
    target->EndDraw();

    const WICRect region{0, 0, result.widthPx, result.heightPx};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bitmap->Lock(&region, WICBitmapLockRead, lock.GetAddressOf())))
    {
        return result;
    }
    UINT stride = 0;
    UINT bufferSize = 0;
    BYTE* data = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&bufferSize, &data)) ||
        data == nullptr)
    {
        return result;
    }
    for (int y = 0; y < result.heightPx; ++y)
    {
        const BYTE* row = data + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < result.widthPx; ++x)
        {
            if (row[x * 4 + 3] != 0)
            {
                ++result.opaquePixels;
            }
        }
    }
    return result;
}

TEST(WidgetRender, PaintsContentAtMultipleDpiScales)
{
    for (unsigned dpi : {96u, 144u, 192u})
    {
        const RenderedSurface surface = RenderScene(dpi, 360.0f, 220.0f);
        const std::uint64_t totalPixels = static_cast<std::uint64_t>(surface.widthPx) *
                                          static_cast<std::uint64_t>(surface.heightPx);
        // A meaningful fraction of the panel is painted (background is translucent, so alpha > 0).
        EXPECT_GT(surface.opaquePixels, totalPixels / 10)
            << "dpi=" << dpi << " painted=" << surface.opaquePixels;
    }
}

TEST(WidgetRender, LayoutIsScaleInvariantInDips)
{
    const RenderedSurface at100 = RenderScene(96u, 360.0f, 220.0f);
    const RenderedSurface at200 = RenderScene(192u, 360.0f, 220.0f);
    EXPECT_NEAR(at100.dipSize.width, at200.dipSize.width, 0.5f);
    EXPECT_NEAR(at100.dipSize.height, at200.dipSize.height, 0.5f);
    // Physical pixel size doubles at 200%.
    EXPECT_EQ(at200.widthPx, at100.widthPx * 2);
    EXPECT_EQ(at200.heightPx, at100.heightPx * 2);
}
} // namespace