#include "Renderer.h"

#include "Widgets/WidgetScene.h"
#include "pacecar/overlay/OverlayPlacement.h"

#include <cstring>
#include <cstdio>
#include <string>

#include <d2d1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <dxgiformat.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace pacecar::overlay
{
namespace
{
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

#define PC_RETURN_IF_FAILED(expression)     \
    do                                      \
    {                                       \
        const HRESULT pcHr_ = (expression); \
        if (FAILED(pcHr_))                  \
        {                                   \
            return pcHr_;                   \
        }                                   \
    } while (false)

const D2D1_COLOR_F kTransparent = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f);

std::wstring FormatHresult(HRESULT hr)
{
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"0x%08lX", static_cast<unsigned long>(hr));
    return buffer;
}

// --- Recipe A: layered window + UpdateLayeredWindow -------------------------------------------

class LayeredRenderer final : public IRenderer
{
  public:
    LayeredRenderer() = default;
    ~LayeredRenderer() override
    {
        ReleaseSurface();
    }

    [[nodiscard]] OverlayRecipe Recipe() const noexcept override
    {
        return OverlayRecipe::Layered;
    }

    [[nodiscard]] std::wstring_view Name() const noexcept override
    {
        return L"Layered/UpdateLayeredWindow (Recipe A)";
    }

    [[nodiscard]] std::wstring Describe() const override
    {
        return L"Recipe A: WS_EX_LAYERED + UpdateLayeredWindow(ULW_ALPHA), premultiplied WIC bitmap";
    }

    HRESULT Initialize(HWND hwnd, int widthPx, int heightPx, unsigned dpi) override
    {
        hwnd_ = hwnd;
        if (d2dFactory_ == nullptr)
        {
            D2D1_FACTORY_OPTIONS options{};
            PC_RETURN_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                                  __uuidof(ID2D1Factory), &options,
                                                  reinterpret_cast<void**>(
                                                      d2dFactory_.GetAddressOf())));
        }
        if (wicFactory_ == nullptr)
        {
            PC_RETURN_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                                                 CLSCTX_INPROC_SERVER, IID_PPV_ARGS(
                                                                             wicFactory_
                                                                                 .GetAddressOf())));
        }
        return CreateSurface(widthPx, heightPx, dpi);
    }

    HRESULT Resize(int widthPx, int heightPx, unsigned dpi) override
    {
        return CreateSurface(widthPx, heightPx, dpi);
    }

    void SetTheme(const ResolvedTheme& theme) override
    {
        theme_ = theme;
        dirty_ = true;
    }

    void Invalidate() noexcept override
    {
        dirty_ = true;
    }

    PresentResult Present() override
    {
        if (!dirty_)
        {
            return PresentResult{S_OK, false};
        }
        if (renderTarget_ == nullptr && width_ > 0 && height_ > 0)
        {
            const HRESULT recreated = CreateSurface(width_, height_, dpi_);
            if (FAILED(recreated))
            {
                return PresentResult{recreated, false};
            }
        }
        if (renderTarget_ == nullptr)
        {
            return PresentResult{S_OK, false};
        }

        renderTarget_->BeginDraw();
        renderTarget_->Clear(kTransparent);
        scene_.Draw(renderTarget_.Get(), theme_);
        const HRESULT drawResult = renderTarget_->EndDraw();
        if (FAILED(drawResult))
        {
            return PresentResult{drawResult, false};
        }

        const HRESULT copyResult = CopyToDib();
        if (FAILED(copyResult))
        {
            return PresentResult{copyResult, false};
        }

        HDC screenDc = GetDC(nullptr);
        if (screenDc == nullptr)
        {
            return PresentResult{HRESULT_FROM_WIN32(GetLastError()), false};
        }
        POINT source{0, 0};
        SIZE size{width_, height_};
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        const BOOL updated = UpdateLayeredWindow(hwnd_, screenDc, nullptr, &size, memoryDc_, &source,
                                                 0, &blend, ULW_ALPHA);
        const DWORD lastError = updated ? ERROR_SUCCESS : GetLastError();
        ReleaseDC(nullptr, screenDc);
        if (!updated)
        {
            return PresentResult{HRESULT_FROM_WIN32(lastError), false};
        }
        dirty_ = false;
        return PresentResult{S_OK, true};
    }

    void Trim() noexcept override
    {
        scene_.Trim();
        renderTarget_.Reset();
        dirty_ = true;
    }

  private:
    HRESULT CreateSurface(int widthPx, int heightPx, unsigned dpi)
    {
        ReleaseSurface();
        if (widthPx <= 0 || heightPx <= 0)
        {
            return E_INVALIDARG;
        }
        if (d2dFactory_ == nullptr || wicFactory_ == nullptr)
        {
            return E_UNEXPECTED;
        }

        width_ = widthPx;
        height_ = heightPx;
        dpi_ = NormalizeDpi(dpi);

        PC_RETURN_IF_FAILED(wicFactory_->CreateBitmap(
            static_cast<UINT>(widthPx), static_cast<UINT>(heightPx), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapCacheOnLoad, wicBitmap_.GetAddressOf()));

        const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi_), static_cast<float>(dpi_));
        PC_RETURN_IF_FAILED(d2dFactory_->CreateWicBitmapRenderTarget(
            wicBitmap_.Get(), &properties, renderTarget_.GetAddressOf()));

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = widthPx;
        bitmapInfo.bmiHeader.biHeight = -heightPx; // top-down
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        HDC screenDc = GetDC(nullptr);
        if (screenDc == nullptr)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        dib_ = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
        memoryDc_ = CreateCompatibleDC(screenDc);
        ReleaseDC(nullptr, screenDc);
        if (dib_ == nullptr || memoryDc_ == nullptr || dibBits_ == nullptr)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        previousDib_ = SelectObject(memoryDc_, dib_);
        dirty_ = true;
        return S_OK;
    }

    HRESULT CopyToDib()
    {
        if (wicBitmap_ == nullptr || dibBits_ == nullptr)
        {
            return E_UNEXPECTED;
        }
        const WICRect region{0, 0, width_, height_};
        ComPtr<IWICBitmapLock> lock;
        PC_RETURN_IF_FAILED(wicBitmap_->Lock(&region, WICBitmapLockRead, lock.GetAddressOf()));
        UINT stride = 0;
        UINT bufferSize = 0;
        BYTE* source = nullptr;
        PC_RETURN_IF_FAILED(lock->GetStride(&stride));
        PC_RETURN_IF_FAILED(lock->GetDataPointer(&bufferSize, &source));
        const std::size_t rowBytes = static_cast<std::size_t>(width_) * 4u;
        if (source == nullptr || stride < rowBytes || bufferSize < stride * static_cast<UINT>(height_))
        {
            return E_UNEXPECTED;
        }
        for (int y = 0; y < height_; ++y)
        {
            std::memcpy(static_cast<BYTE*>(dibBits_) + static_cast<std::size_t>(y) * rowBytes,
                        source + static_cast<std::size_t>(y) * stride, rowBytes);
        }
        return S_OK;
    }

    void ReleaseSurface() noexcept
    {
        renderTarget_.Reset();
        wicBitmap_.Reset();
        if (memoryDc_ != nullptr)
        {
            if (previousDib_ != nullptr)
            {
                SelectObject(memoryDc_, previousDib_);
                previousDib_ = nullptr;
            }
            DeleteDC(memoryDc_);
            memoryDc_ = nullptr;
        }
        if (dib_ != nullptr)
        {
            DeleteObject(dib_);
            dib_ = nullptr;
        }
        dibBits_ = nullptr;
    }

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    unsigned dpi_ = kBaseDpi;
    bool dirty_ = true;
    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;
    ComPtr<IWICBitmap> wicBitmap_;
    ComPtr<ID2D1RenderTarget> renderTarget_;
    WidgetScene scene_;
    ResolvedTheme theme_{};
    HBITMAP dib_ = nullptr;
    HDC memoryDc_ = nullptr;
    HGDIOBJ previousDib_ = nullptr;
    void* dibBits_ = nullptr;
};

// --- Recipe B: DirectComposition + composition swapchain --------------------------------------

class CompositionRenderer final : public IRenderer
{
  public:
    CompositionRenderer() = default;
    ~CompositionRenderer() override = default;

    [[nodiscard]] OverlayRecipe Recipe() const noexcept override
    {
        return OverlayRecipe::Composition;
    }

    [[nodiscard]] std::wstring_view Name() const noexcept override
    {
        return L"DirectComposition swapchain (Recipe B)";
    }

    [[nodiscard]] std::wstring Describe() const override
    {
        std::wstring text = L"Recipe B: D3D11 ";
        text += hardware_ ? L"hardware" : L"unknown";
        text += L", FLIP_SEQUENTIAL, FLIP_DISCARD=";
        text += FormatHresult(flipDiscard_);
        text += colorSpaceSet_ ? L", colorSpace=SDR sRGB (G22_P709)" : L", colorSpace=default";
        return text;
    }

    HRESULT Initialize(HWND hwnd, int widthPx, int heightPx, unsigned dpi) override
    {
        hwnd_ = hwnd;
        if (widthPx <= 0 || heightPx <= 0)
        {
            return E_INVALIDARG;
        }
        width_ = widthPx;
        height_ = heightPx;
        dpi_ = NormalizeDpi(dpi);

        if (d3dDevice_ == nullptr)
        {
            UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
            const HRESULT created = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                d3dDevice_.GetAddressOf(), &level, d3dContext_.GetAddressOf());
            if (FAILED(created))
            {
                return created;
            }
            hardware_ = true;
        }

        if (swapChain_ == nullptr)
        {
            const HRESULT swapped = CreateCompositionSwapChain();
            if (FAILED(swapped))
            {
                return swapped;
            }
        }
        if (dcompDevice_ == nullptr)
        {
            const HRESULT targeted = CreateCompositionTarget();
            if (FAILED(targeted))
            {
                return targeted;
            }
        }
        if (d2dDevice_ == nullptr)
        {
            const HRESULT d2dCreated = CreateDirect2DDevice();
            if (FAILED(d2dCreated))
            {
                return d2dCreated;
            }
        }
        return CreateTargetBitmap();
    }

    HRESULT Resize(int widthPx, int heightPx, unsigned dpi) override
    {
        if (swapChain_ == nullptr)
        {
            return E_UNEXPECTED;
        }
        width_ = widthPx;
        height_ = heightPx;
        dpi_ = NormalizeDpi(dpi);
        if (d2dContext_ != nullptr)
        {
            d2dContext_->SetTarget(nullptr);
        }
        targetBitmap_.Reset();
        PC_RETURN_IF_FAILED(swapChain_->ResizeBuffers(2, static_cast<UINT>(widthPx),
                                                      static_cast<UINT>(heightPx),
                                                      DXGI_FORMAT_B8G8R8A8_UNORM, 0));
        dirty_ = true;
        return CreateTargetBitmap();
    }

    void SetTheme(const ResolvedTheme& theme) override
    {
        theme_ = theme;
        dirty_ = true;
    }

    void Invalidate() noexcept override
    {
        dirty_ = true;
    }

    PresentResult Present() override
    {
        if (!dirty_)
        {
            return PresentResult{S_OK, false};
        }
        if (d2dContext_ == nullptr || swapChain_ == nullptr)
        {
            return PresentResult{S_OK, false};
        }
        if (targetBitmap_ == nullptr)
        {
            const HRESULT created = CreateTargetBitmap();
            if (FAILED(created))
            {
                return PresentResult{created, false};
            }
        }

        d2dContext_->BeginDraw();
        d2dContext_->Clear(kTransparent);
        scene_.Draw(d2dContext_.Get(), theme_);
        const HRESULT drawResult = d2dContext_->EndDraw();
        if (FAILED(drawResult))
        {
            return PresentResult{drawResult, false};
        }
// Sync interval 0: DWM owns composition timing for a composition swapchain.
        const HRESULT presentResult = swapChain_->Present(0, 0);
        if (FAILED(presentResult))
        {
            return PresentResult{presentResult, false};
        }
        if (dcompDevice_ != nullptr)
        {
            dcompDevice_->Commit();
        }
        dirty_ = false;
        return PresentResult{S_OK, true};
    }

    void Trim() noexcept override
    {
        scene_.Trim();
        if (d2dContext_ != nullptr)
        {
            d2dContext_->SetTarget(nullptr);
        }
        targetBitmap_.Reset();
        dirty_ = true;
    }

  private:
    HRESULT CreateCompositionSwapChain()
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        PC_RETURN_IF_FAILED(d3dDevice_.As(&dxgiDevice));
        ComPtr<IDXGIAdapter> adapter;
        PC_RETURN_IF_FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()));
        ComPtr<IDXGIFactory2> factory;
        PC_RETURN_IF_FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())));

        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = static_cast<UINT>(width_);
        description.Height = static_cast<UINT>(height_);
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.Stereo = FALSE;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        description.Flags = 0;
        PC_RETURN_IF_FAILED(factory->CreateSwapChainForComposition(
            d3dDevice_.Get(), &description, nullptr, swapChain_.GetAddressOf()));

        // Empirically settle design open decision #6: FLIP_DISCARD is undocumented for composition
        // swapchains and is expected to fail with E_INVALIDARG. Keep FLIP_SEQUENTIAL as the real
        // chain regardless of the outcome.
        DXGI_SWAP_CHAIN_DESC1 probe = description;
        probe.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> probeChain;
        flipDiscard_ = factory->CreateSwapChainForComposition(d3dDevice_.Get(), &probe, nullptr,
                                                               probeChain.GetAddressOf());

        ComPtr<IDXGISwapChain3> swapChain3;
        if (SUCCEEDED(swapChain_.As(&swapChain3)))
        {
            colorSpaceSet_ = SUCCEEDED(
                swapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709));
        }
        return S_OK;
    }

    HRESULT CreateCompositionTarget()
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        PC_RETURN_IF_FAILED(d3dDevice_.As(&dxgiDevice));
        PC_RETURN_IF_FAILED(DCompositionCreateDevice(
            dxgiDevice.Get(), IID_PPV_ARGS(dcompDevice_.GetAddressOf())));
        PC_RETURN_IF_FAILED(
            dcompDevice_->CreateTargetForHwnd(hwnd_, FALSE, dcompTarget_.GetAddressOf()));
        PC_RETURN_IF_FAILED(dcompDevice_->CreateVisual(dcompVisual_.GetAddressOf()));
        PC_RETURN_IF_FAILED(dcompVisual_->SetContent(swapChain_.Get()));
        PC_RETURN_IF_FAILED(dcompTarget_->SetRoot(dcompVisual_.Get()));
        return dcompDevice_->Commit();
    }

    HRESULT CreateDirect2DDevice()
    {
        D2D1_FACTORY_OPTIONS options{};
        PC_RETURN_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                              __uuidof(ID2D1Factory1), &options,
                                              reinterpret_cast<void**>(d2dFactory1_.GetAddressOf())));
        ComPtr<IDXGIDevice> dxgiDevice;
        PC_RETURN_IF_FAILED(d3dDevice_.As(&dxgiDevice));
        PC_RETURN_IF_FAILED(d2dFactory1_->CreateDevice(dxgiDevice.Get(), d2dDevice_.GetAddressOf()));
        PC_RETURN_IF_FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                            d2dContext_.GetAddressOf()));
        return S_OK;
    }

    HRESULT CreateTargetBitmap()
    {
        if (d2dContext_ == nullptr)
        {
            return E_UNEXPECTED;
        }
        ComPtr<IDXGISurface> backBuffer;
        PC_RETURN_IF_FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())));
        const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<float>(dpi_), static_cast<float>(dpi_));
        PC_RETURN_IF_FAILED(d2dContext_->CreateBitmapFromDxgiSurface(
            backBuffer.Get(), &properties, targetBitmap_.GetAddressOf()));
        d2dContext_->SetTarget(targetBitmap_.Get());
        return S_OK;
    }

    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    unsigned dpi_ = kBaseDpi;
    bool dirty_ = true;
    bool hardware_ = false;
    bool colorSpaceSet_ = false;
    HRESULT flipDiscard_ = E_NOTIMPL;
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;
    ComPtr<ID2D1Factory1> d2dFactory1_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dContext_;
    ComPtr<ID2D1Bitmap1> targetBitmap_;
    WidgetScene scene_;
    ResolvedTheme theme_{};
};
} // namespace

std::unique_ptr<IRenderer> CreateLayeredRenderer() noexcept
{
    return std::make_unique<LayeredRenderer>();
}

std::unique_ptr<IRenderer> CreateCompositionRenderer() noexcept
{
    return std::make_unique<CompositionRenderer>();
}

std::unique_ptr<IRenderer> CreateRenderer(OverlayRecipe recipe) noexcept
{
    if (recipe == OverlayRecipe::Composition)
    {
        return CreateCompositionRenderer();
    }
    return CreateLayeredRenderer();
}
} // namespace pacecar::overlay