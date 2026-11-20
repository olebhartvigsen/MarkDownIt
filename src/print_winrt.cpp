// The Windows print contract, the app's side of it. Plan:
// .hermes/plans/2026-11-19_233000-modern-windows-print-integration.md
//
// The layout of this file:
//
// - PrintSourceWin is the classic COM object the pipeline talks to. It
//   implements the three contract interfaces from print_contract_abi.h
//   with hand-rolled identity, the shape the working references use
//   (smourier/XpsPrintSamples, Chromium's metro print driver).
// - The window side (RegisterWindow, ShowPrintDialog, the task events)
//   speaks C++/WinRT: the headers come with the Windows SDK and link
//   against windowsapp.lib, an OS component, so the exe keeps its
//   zero-dependency shape.
// - The pages themselves never become this file's business: every build
//   request is messaged to the window (HandleWindowMessage answers on
//   the app's thread, inside its message loop), where the builder walks
//   the one print path shared with the classic panel. The source keeps
//   the finished page as read-only shared state and paints every medium
//   through layout::PaintBandPage, the same painter the DC driver uses.
//
// Threading, the two rules:
//
// - The contract's calls arrive on a pipeline thread. The source owns
//   its device chain and its copy of the page; nothing of the window is
//   touched from that thread except through the message send.
// - Paginate, MakePage and MakeDocument block their caller until the
//   window has answered, in SendMessageTimeout; a window that does not
//   pump gives up after the timeout and the dialog keeps its previous
//   pages instead of hanging the print.

// renderer.h before any header that reads the seam, and never
// platform/seams.h by hand: windows.h leaves a DrawText macro behind,
// and renderer.h undefines it before the seam is read, so the render
// target's override matches the seam it overrides.
#include "renderer.h"

#include "print_winrt.h"

#include "print_contract_abi.h"
#include "windows/render_target_win.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d3d11.h>
#include <documenttarget.h>
#include <dwrite.h>
#include <dxgi.h>
#include <printmanagerinterop.h>
#include <wincodec.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Printing.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace print_winrt {
namespace {

// The one build message, unique in the system so foreign windows cannot
// answer it: registered once, compared by value.
const UINT kBuildPageMessage = RegisterWindowMessageW(
    L"MarkDownItPrintPageBuild");

// The page the pipeline is asking about, from the options it handed us.
// A sheet in seam units, the imageable rect inside, and the printer's own
// resolution for the raster rules. False when the options cannot be read:
// the caller keeps the page it has. The sizes arrive in DIPs, and a DIP
// is a seam unit: 96 to the inch either way.
bool DescribeOptions(IInspectable* options, uint32_t jobPage,
                     float* sheetW, float* sheetH, float* imgX, float* imgY,
                     float* imgW, float* imgH, uint32_t* dpi) {
    if (options == nullptr) return false;
    try {
        winrt::com_ptr<::IInspectable> object;
        object.copy_from(options);
        const auto taskOptions =
            object.as<winrt::Windows::Graphics::Printing::PrintTaskOptions>();
        const auto desc = taskOptions.GetPageDescription(jobPage);
        *sheetW = desc.PageSize.Width;
        *sheetH = desc.PageSize.Height;
        *imgX = desc.ImageableRect.X;
        *imgY = desc.ImageableRect.Y;
        *imgW = desc.ImageableRect.Width;
        *imgH = desc.ImageableRect.Height;
        *dpi = desc.DpiX;
        return *sheetW > 0.0f && *sheetH > 0.0f;
    } catch (...) {
        return false;
    }
}

struct Snapshot {
    // Built by the window, read by the pipeline thread. Immutable while
    // shared: a new request replaces the pointer under the mutex.
    std::mutex mutex;
    layout::PrintPage page;
    std::shared_ptr<const layout::DocumentLayout> laid;
    std::vector<layout::PrintBand> bands;
};

// One build request, asked of the window from the pipeline thread. The
// window answers inside HandleWindowMessage; the send is the
// synchronization, so nothing further guards the fields between the two
// threads while the request is in flight.
struct BuildRequest {
    PageBuild pb;
    bool answered = false;
};

// ---------------------------------------------------------------------------
// The source: the object the pipeline interrogates.
// ---------------------------------------------------------------------------

class PrintSourceWin final
    : public print_contract::IPrintDocumentSource,
      public print_contract::IPrintDocumentPageSource,
      public print_contract::IPrintPreviewPageCollection,
      public IInspectable {
public:
    PrintSourceWin(HWND window, DialogInputs inputs)
        : window_(window), inputs_(std::move(inputs)) {}

    // IUnknown. One implementation overrides every inherited copy; the
    // compiler writes the adjustor thunks for each base subobject, the
    // shape of every multiply-inherited COM object.
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == IID_IAgileObject) {
            *out = static_cast<print_contract::IPrintDocumentSource*>(
                this);
        } else if (riid == __uuidof(IInspectable)) {
            *out = static_cast<IInspectable*>(this);
        } else if (riid ==
                   __uuidof(print_contract::IPrintDocumentSource)) {
            *out = static_cast<print_contract::IPrintDocumentSource*>(
                this);
        } else if (riid ==
                   __uuidof(print_contract::IPrintDocumentPageSource)) {
            *out =
                static_cast<print_contract::IPrintDocumentPageSource*>(
                    this);
        } else if (riid ==
                   __uuidof(print_contract::IPrintPreviewPageCollection)) {
            *out =
                static_cast<print_contract::IPrintPreviewPageCollection*>(
                    this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() {
        return ref_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    IFACEMETHODIMP_(ULONG) Release() {
        const ULONG left = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (left == 0) delete this;
        return left;
    }

    // IInspectable: the pipeline never asks beyond identity, and the
    // honest answers mirror the working reference (no class name, base
    // trust, an empty IID list).
    IFACEMETHODIMP GetIids(ULONG* count, IID** iids) {
        if (count == nullptr) return E_POINTER;
        *count = 0;
        if (iids != nullptr) *iids = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP GetRuntimeClassName(HSTRING*) { return E_NOTIMPL; }
    IFACEMETHODIMP GetTrustLevel(TrustLevel* level) {
        if (level == nullptr) return E_POINTER;
        *level = BaseTrust;
        return S_OK;
    }

    // The contract. The preview sink first, then the page feed and the
    // final job.
    IFACEMETHODIMP GetPreviewPageCollection(
        IPrintDocumentPackageTarget* packageTarget, void** collection) {
        if (collection == nullptr) return E_POINTER;
        *collection = nullptr;
        if (packageTarget == nullptr) return E_INVALIDARG;
        // The DXGI preview sink: the modern type first, the Win8.0
        // identity as the fallback, the pair Chromium keeps.
        HRESULT hr = packageTarget->GetPackageTarget(
            print_contract::kPrintPreviewDxgiPackageTargetType,
            __uuidof(print_contract::IPrintPreviewDxgiPackageTarget),
            reinterpret_cast<void**>(
                preview_.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            hr = packageTarget->GetPackageTarget(
                print_contract::kPrintPreviewDxgiPackageTargetTypeWin8,
                __uuidof(
                    print_contract::IPrintPreviewDxgiPackageTargetWin8),
                reinterpret_cast<void**>(
                    preview_.ReleaseAndGetAddressOf()));
        }
        if (FAILED(hr)) return hr;
        if (FAILED(EnsureDevices())) return E_FAIL;
        // The count is final or not yet known: the first Paginate, which
        // the dialog always sends before the first page, sets it.
        std::lock_guard<std::mutex> lock(snapshot_.mutex);
        if (!snapshot_.bands.empty() && preview_ != nullptr) {
            preview_->SetJobPageCount(print_contract::kFinalPageCount,
                                      static_cast<UINT>(
                                          snapshot_.bands.size()));
        }
        AddRef();
        *collection =
            static_cast<print_contract::IPrintPreviewPageCollection*>(
                this);
        return S_OK;
    }

    IFACEMETHODIMP MakeDocument(IInspectable* options,
                                IPrintDocumentPackageTarget* packageTarget);
    IFACEMETHODIMP Paginate(uint32_t jobPage, IInspectable* options);
    IFACEMETHODIMP MakePage(uint32_t desiredJobPage, float width,
                            float height);

private:
    // The device chain of the pipeline thread. The window keeps its
    // own; this one is created once per source and never shared.
    HRESULT EnsureDevices();

    // The preview surface, rebuilt when the requested size changes. The
    // dialog asks for the page box it wants to show, in DIPs, that is NOT
    // the paper's size; the surface is that box at the display's own dpi.
    HRESULT EnsurePreviewSurface(UINT width, UINT height, float dpi);

    // One page into the surface, then handed to the dialog. boxW/boxH
    // are the sizes MakePage asked for: the paper's bands are scaled to
    // fit them, the same fit the dialog itself would apply.
    HRESULT RenderPreview(uint32_t jobPage, float boxW, float boxH);

    // The final job: the pages as command lists through the print
    // control.
    HRESULT PrintPages(IPrintDocumentPackageTarget* packageTarget,
                       uint32_t rasterDpi);

    // Builds the page on the app thread; the window answers inside its
    // message loop or the send gives up. The snapshot adopts the answer.
    void RequestBuild(float sw, float sh, float ix, float iy, float iw,
                     float ih, uint32_t jobPage, uint32_t dpi);

    std::atomic<ULONG> ref_{1};
    HWND window_;
    DialogInputs inputs_;

    ComPtr<print_contract::IPrintPreviewDxgiPackageTarget> preview_;

    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dContext_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<ID3D11Texture2D> texture_;
    ComPtr<IDXGISurface> surface_;
    ComPtr<ID2D1Bitmap1> previewBitmap_;
    UINT surfaceWidth_ = 0;
    UINT surfaceHeight_ = 0;
    float surfaceDpi_ = 0.0f;

    Snapshot snapshot_;
    bool devicesFailed_ = false;
};

// ---------------------------------------------------------------------------
// The device chain, the preview pass, the final pass.
// ---------------------------------------------------------------------------

HRESULT PrintSourceWin::EnsureDevices() {
    if (d2dContext_ != nullptr) return S_OK;
    if (devicesFailed_) return E_FAIL;

    // BGRA support on the D3D device, which D2D asks for. Hardware
    // first with a WARP fallback behind it: the preview has no timing
    // constraints, and a machine that refuses hardware (RDP, locked
    // down sessions) still previews.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &level, 1,
        D3D11_SDK_VERSION, d3dDevice_.ReleaseAndGetAddressOf(), nullptr,
        nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, d3dDevice_.ReleaseAndGetAddressOf(), nullptr,
            nullptr);
    }
    if (FAILED(hr)) { devicesFailed_ = true; return hr; }

    hr = D2D1CreateFactory<ID2D1Factory1>(
        D2D1_FACTORY_TYPE_MULTI_THREADED,
        d2dFactory_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) { devicesFailed_ = true; return hr; }

    ComPtr<IDXGIDevice> dxgi;
    hr = d3dDevice_.As(&dxgi);
    if (FAILED(hr)) { devicesFailed_ = true; return hr; }
    hr = d2dFactory_->CreateDevice(dxgi.Get(),
                                    d2dDevice_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) { devicesFailed_ = true; return hr; }
    hr = d2dDevice_->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        d2dContext_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) { devicesFailed_ = true; return hr; }

    // The factory the seam target draws its text through, an isolated
    // one: this thread shares nothing with the window's own.
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(
            dwriteFactory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) { devicesFailed_ = true; return hr; }
    return S_OK;
}

HRESULT PrintSourceWin::EnsurePreviewSurface(UINT width, UINT height,
                                             float dpi) {
    if (previewBitmap_ != nullptr && surfaceWidth_ == width &&
        surfaceHeight_ == height && surfaceDpi_ == dpi) {
        return S_OK;
    }
    texture_.Reset();
    surface_.Reset();
    previewBitmap_.Reset();
    surfaceWidth_ = 0;
    surfaceHeight_ = 0;
    surfaceDpi_ = 0.0f;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    HRESULT hr = d3dDevice_->CreateTexture2D(
        &desc, nullptr, texture_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    hr = texture_.As(&surface_);
    if (FAILED(hr)) return hr;

    // The context carries the display's dpi, so a DIP in the paint below
    // is the same DIP the dialog's pane is showing.
    d2dContext_->SetDpi(dpi, dpi);
    D2D1_BITMAP_PROPERTIES1 props = {};
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = dpi;
    props.dpiY = dpi;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    hr = d2dContext_->CreateBitmapFromDxgiSurface(
        surface_.Get(), &props, previewBitmap_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;
    surfaceWidth_ = width;
    surfaceHeight_ = height;
    surfaceDpi_ = dpi;
    return S_OK;
}

HRESULT PrintSourceWin::RenderPreview(uint32_t jobPage, float boxW,
                                      float boxH) {
    // One page through the shared band painter onto the surface's
    // bitmap: the paper's bands, shrunk into the box the pane asked
    // for. The context transform is the only scale in the picture, the
    // mechanism the classic DC path uses for its printer resolution;
    // the display list never carries a transform of its own.
    std::shared_ptr<const layout::DocumentLayout> laid;
    layout::PrintPage page;
    std::vector<layout::PrintBand> bands;
    {
        std::lock_guard<std::mutex> lock(snapshot_.mutex);
        laid = snapshot_.laid;
        page = snapshot_.page;
        bands = snapshot_.bands;
    }
    if (!laid || jobPage > bands.size()) return E_FAIL;

    d2dContext_->SetTarget(previewBitmap_.Get());
    // Uniform shrink, the smaller of the two fits: a box that somehow
    // departs from the paper's shape letterboxes rather than stretches
    // the text. The dialog's own box keeps the paper's aspect, so the
    // two factors are the same in practice.
    const float kx = boxW / page.width;
    const float ky = boxH / page.height;
    const float k = kx < ky ? kx : ky;
    d2dContext_->SetTransform(
        D2D1::Matrix3x2F::Scale(k, k));
    d2dContext_->BeginDraw();
    d2dContext_->Clear(D2D1::ColorF(D2D1::ColorF::White));
    {
        platform::D2DTarget seam(d2dContext_.Get(),
                                 dwriteFactory_.Get());
        layout::PaintBandPage(seam, *laid, page, bands, jobPage - 1);
    }
    HRESULT hr = d2dContext_->EndDraw();
    d2dContext_->SetTarget(nullptr);
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    return hr;
}

void PrintSourceWin::RequestBuild(float sw, float sh, float ix, float iy,
                                  float iw, float ih, uint32_t jobPage,
                                  uint32_t dpi) {
    BuildRequest req;
    req.pb.sheetWidth = sw;
    req.pb.sheetHeight = sh;
    req.pb.imageableX = ix;
    req.pb.imageableY = iy;
    req.pb.imageableW = iw;
    req.pb.imageableH = ih;
    req.pb.jobPage = jobPage;
    req.pb.rasterDpi = dpi;
    DWORD_PTR sent = 0;
    SendMessageTimeoutW(window_, kBuildPageMessage, 0,
                        reinterpret_cast<LPARAM>(&req), SMTO_NORMAL, 15000,
                        &sent);
    if (!req.answered || !req.pb.ok || !req.pb.laid) return;
    std::lock_guard<std::mutex> lock(snapshot_.mutex);
    snapshot_.page = req.pb.page;
    snapshot_.laid = req.pb.laid;
    snapshot_.bands = req.pb.bands;
}

IFACEMETHODIMP PrintSourceWin::Paginate(uint32_t jobPage,
                                        IInspectable* options) {
    // The paper changed, or the window is asking ahead. Rebuild the
    // page on the app thread and publish the new count.
    float sw, sh, ix, iy, iw, ih;
    uint32_t dpi = 0;
    if (DescribeOptions(options, jobPage, &sw, &sh, &ix, &iy, &iw, &ih,
                        &dpi)) {
        RequestBuild(sw, sh, ix, iy, iw, ih, jobPage, dpi);
    } else {
        layout::PrintPage fresh;
        RequestBuild(fresh.width, fresh.height, fresh.margin, fresh.margin,
                     fresh.width - 2 * fresh.margin,
                     fresh.height - 2 * fresh.margin, jobPage, 0);
    }
    std::lock_guard<std::mutex> lock(snapshot_.mutex);
    const UINT pages = snapshot_.laid && !snapshot_.bands.empty()
                           ? static_cast<UINT>(snapshot_.bands.size())
                           : 0;
    if (preview_ != nullptr) {
        preview_->SetJobPageCount(print_contract::kFinalPageCount, pages);
        // The paper changed: what the pane holds is stale, and the count
        // above is the promise it refetches pages against.
        preview_->InvalidatePreview();
    }
    return S_OK;
}

IFACEMETHODIMP PrintSourceWin::MakePage(uint32_t desiredJobPage,
                                        float width, float height) {
    if (desiredJobPage ==
        print_contract::kJobPageApplicationDefined) {
        desiredJobPage = 1;
    }
    if (desiredJobPage == 0) return E_INVALIDARG;

    // A preview pass may begin before any Paginate arrived; give it the
    // default page rather than an empty pane.
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(snapshot_.mutex);
        have = snapshot_.laid &&
               desiredJobPage <= snapshot_.bands.size();
    }
    if (!have) {
        layout::PrintPage fresh;
        RequestBuild(fresh.width, fresh.height, fresh.margin, fresh.margin,
                     fresh.width - 2 * fresh.margin,
                     fresh.height - 2 * fresh.margin, desiredJobPage, 0);
        std::lock_guard<std::mutex> lock(snapshot_.mutex);
        have = snapshot_.laid &&
               desiredJobPage <= snapshot_.bands.size();
        if (!have) return E_FAIL;
    }

    if (FAILED(EnsureDevices())) return E_FAIL;

    // The dpi the pane shows at, the window's own display dpi; the value
    // DrawPage is told, and the scale of the surface's pixels.
    UINT dpi = GetDpiForWindow(window_);
    if (dpi == 0) {
        const HDC dc = GetDC(window_);
        dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
        if (dc) ReleaseDC(window_, dc);
    }
    if (dpi == 0) dpi = 96;
    const float fdpi = static_cast<float>(dpi);

    // The surface is the box MakePage asked for, at that dpi: the box is
    // the pane's page image, NOT the paper, so its pixels are box DIPs
    // times dpi over 96. The bands are drawn at the paper's own scale
    // and shrink into the box inside RenderPreview.
    const UINT w = static_cast<UINT>(
        std::ceil(std::max(1.0f, width) * fdpi / 96.0f));
    const UINT h = static_cast<UINT>(
        std::ceil(std::max(1.0f, height) * fdpi / 96.0f));
    if (FAILED(EnsurePreviewSurface(w, h, fdpi))) return E_FAIL;

    HRESULT hr = RenderPreview(desiredJobPage, width, height);
    if (FAILED(hr)) return hr;
    return preview_->DrawPage(desiredJobPage, surface_.Get(), fdpi,
                              fdpi);
}

IFACEMETHODIMP PrintSourceWin::MakeDocument(
    IInspectable* options, IPrintDocumentPackageTarget* packageTarget) {
    if (packageTarget == nullptr) return E_INVALIDARG;

    // The final page set follows the description the preview showed:
    // one build on the app thread, then the pages as command lists.
    float sw = 793.7f, sh = 1122.5f, ix = 0.0f, iy = 0.0f;
    float iw = 793.7f, ih = 1122.5f;
    uint32_t dpi = 0;
    if (!DescribeOptions(options, 1, &sw, &sh, &ix, &iy, &iw, &ih,
                         &dpi)) {
        layout::PrintPage fresh;
        sw = fresh.width;
        sh = fresh.height;
        iw = fresh.width - 2 * fresh.margin;
        ih = fresh.height - 2 * fresh.margin;
        ix = fresh.margin;
        iy = fresh.margin;
    }
    RequestBuild(sw, sh, ix, iy, iw, ih, 1, dpi);

    if (FAILED(EnsureDevices())) return E_FAIL;
    {
        std::lock_guard<std::mutex> lock(snapshot_.mutex);
        if (!snapshot_.laid || snapshot_.bands.empty()) {
            // An empty document is a closed, empty job: not an error
            // the user needs a message for.
            return S_OK;
        }
    }

    HRESULT hr = PrintPages(packageTarget, dpi);
    // A job the user dismissed while it was building is not an error:
    // the classic path lands here too, and it dies the same quiet way.
    if (hr == print_contract::kErrorPrintCancelled) return S_OK;
    return hr;
}

HRESULT PrintSourceWin::PrintPages(
    IPrintDocumentPackageTarget* packageTarget, uint32_t rasterDpi) {
    std::shared_ptr<const layout::DocumentLayout> laid;
    layout::PrintPage page;
    std::vector<layout::PrintBand> bands;
    {
        std::lock_guard<std::mutex> lock(snapshot_.mutex);
        laid = snapshot_.laid;
        page = snapshot_.page;
        bands = snapshot_.bands;
    }
    if (!laid || bands.empty()) return E_FAIL;

    // A preview pass may have left a dpi and a scale on the shared
    // context; the job's command lists are drawn at the paper's own
    // 96-based units and no transform.
    d2dContext_->SetDpi(96.0f, 96.0f);
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());

    // The pipeline rasterizes whatever it cannot pass through as
    // vectors at this resolution; the description carries the printer's
    // own, and a sane default stands in when it does not. The enum
    // members are set as their zero values (the default font subsetting,
    // the simple color space), spelled through the types: this SDK names
    // the members only through its helpers.
    D2D1_PRINT_CONTROL_PROPERTIES props = {};
    props.rasterDPI = rasterDpi > 0 ? static_cast<FLOAT>(rasterDpi)
                                    : 150.0f;
    props.fontSubset = static_cast<D2D1_PRINT_FONT_SUBSET_MODE>(0);
    props.colorSpace = static_cast<D2D1_COLOR_SPACE>(0);

    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory),
        reinterpret_cast<void**>(wic.ReleaseAndGetAddressOf()));
    if (hr == CO_E_NOTINITIALIZED) {
        // A pipeline thread that never initialized COM: the multithreaded
        // apartment is the contract's own model, and S_FALSE just says
        // somebody initialized it already.
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        hr = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IWICImagingFactory),
            reinterpret_cast<void**>(wic.ReleaseAndGetAddressOf()));
    }
    if (FAILED(hr)) return hr;

    ComPtr<ID2D1PrintControl> printControl;
    hr = d2dDevice_->CreatePrintControl(
        wic.Get(), packageTarget, &props,
        printControl.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return hr;

    // One band, one command list, one AddPage: the page the preview
    // showed is the page the printer draws.
    for (size_t i = 0; i < bands.size(); ++i) {
        ComPtr<ID2D1CommandList> commandList;
        hr = d2dContext_->CreateCommandList(
            commandList.ReleaseAndGetAddressOf());
        if (FAILED(hr)) break;
        d2dContext_->SetTarget(commandList.Get());
        d2dContext_->BeginDraw();
        d2dContext_->Clear(D2D1::ColorF(D2D1::ColorF::White));
        {
            platform::D2DTarget seam(d2dContext_.Get(),
                                     dwriteFactory_.Get());
            layout::PaintBandPage(seam, *laid, page, bands, i);
        }
        hr = d2dContext_->EndDraw();
        if (FAILED(hr)) break;
        hr = commandList->Close();
        if (FAILED(hr)) break;
        d2dContext_->SetTarget(nullptr);
        const D2D_SIZE_F size{page.width, page.height};
        hr = printControl->AddPage(commandList.Get(), size, nullptr);
        if (FAILED(hr)) break;
    }
    const HRESULT close = printControl->Close();
    if (FAILED(hr)) return hr;
    return close;
}

// ---------------------------------------------------------------------------
// The window side: registration, sessions, and the message hook.
// ---------------------------------------------------------------------------

struct Registration {
    HWND window = nullptr;
    ComPtr<IPrintManagerInterop> interop;
    winrt::Windows::Graphics::Printing::PrintManager manager{nullptr};
    winrt::event_token taskToken{};
    bool live = false;
};

struct Session {
    HWND window = nullptr;
    DialogInputs inputs;
    PrintSourceWin* source = nullptr;  // one reference, ours
    bool active = false;
};

std::mutex g_state;
Registration g_registration;
Session g_session;

}  // namespace

// ---------------------------------------------------------------------------
// The public API, see print_winrt.h.
// ---------------------------------------------------------------------------

bool ModernPrintEnabled() {
    // Unset, empty answers nothing; set to anything but "0" the
    // variable asks for the classic panel.
    wchar_t value[8] = {};
    const DWORD n = GetEnvironmentVariableW(
        L"MARKDOWNIT_NO_MODERN_PRINT", value, 8);
    return n == 0 || (value[0] == L'0' && value[1] == L'\0');
}

bool RegisterWindow(HWND window) {
    if (!ModernPrintEnabled() || window == nullptr) return false;
    try {
        // The activation below needs an apartment. Most app threads have
        // one by the time they get here, but this runs from WM_CREATE,
        // before anything else in the window touches COM, so the single
        // threaded apartment is asked for explicitly. S_FALSE just says
        // it was already there.
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        std::lock_guard<std::mutex> lock(g_state);
        if (g_registration.live) return g_registration.window == window;

        // The desktop entry into the WinRT print stack: the activation
        // factory as the interop interface, then the window's manager.
        auto interop = winrt::get_activation_factory<
            winrt::Windows::Graphics::Printing::PrintManager,
            IPrintManagerInterop>();
        ::IInspectable* raw = nullptr;
        const GUID want = __uuidof(::IInspectable);
        HRESULT hr = interop->GetForWindow(window, want,
                                           reinterpret_cast<void**>(&raw));
        if (FAILED(hr) || raw == nullptr) return false;
        winrt::com_ptr<::IInspectable> inspectable;
        inspectable.copy_from(raw);
        raw->Release();
        auto manager =
            inspectable
                .as<winrt::Windows::Graphics::Printing::PrintManager>();

        // The task event arrives on this window's own thread, the STA.
        // The handler reaches the session through the globals, no
        // captures: the delegate cannot be made to hold a reference.
        g_registration.taskToken = manager.PrintTaskRequested([](
            winrt::Windows::Graphics::Printing::PrintManager const&,
            winrt::Windows::Graphics::Printing::PrintTaskRequestedEventArgs
                const& args) {
            PrintSourceWin* source = nullptr;
            std::wstring title;
            {
                std::lock_guard<std::mutex> lock(g_state);
                if (!g_session.active || g_session.source == nullptr) {
                    // No print asked for: not ours, nothing to give.
                    return;
                }
                source = g_session.source;
                title = g_session.inputs.title;
            }
            if (title.empty()) title = L"MarkDownIt";
            PrintSourceWin* held = source;
            try {
                auto task = args.Request().CreatePrintTask(
                    title,
                    [held](
                        winrt::Windows::Graphics::Printing::
                            PrintTaskSourceRequestedArgs const&
                                requested) {
                        // The classic object wrapped in the projected
                        // interface, a reference for the task itself.
                        winrt::com_ptr<::IInspectable> object;
                        ::IInspectable* rawInsp = nullptr;
                        if (FAILED(held->QueryInterface(
                                __uuidof(::IInspectable),
                                reinterpret_cast<void**>(&rawInsp))) ||
                            rawInsp == nullptr) {
                            return;
                        }
                        object.copy_from(rawInsp);
                        rawInsp->Release();
                        requested.SetSource(
                            object.as<winrt::Windows::Graphics::Printing::
                                         IPrintDocumentSource>());
                    });
                task.Completed([](
                    winrt::Windows::Graphics::Printing::PrintTask const&,
                    winrt::Windows::Graphics::Printing::
                        PrintTaskCompletedEventArgs const& done) {
                    HWND window = nullptr;
                    bool failed = false;
                    {
                        std::lock_guard<std::mutex> lock(g_state);
                        if (g_session.active) {
                            window = g_session.window;
                            if (g_session.source != nullptr) {
                                g_session.source->Release();
                                g_session.source = nullptr;
                            }
                            g_session.active = false;
                            g_session.window = nullptr;
                            g_session.inputs = DialogInputs{};
                        }
                    }
                    failed = done.Completion() ==
                             winrt::Windows::Graphics::Printing::
                                 PrintTaskCompletion::Failed;
                    if (failed && window != nullptr) {
                        MessageBoxW(
                            window,
                            L"Udskriften kunne ikke gennemfoeres.",
                            L"MarkDownIt", MB_ICONERROR | MB_OK);
                    }
                    // The other ends are silence: cancelled, abandoned,
                    // submitted. A panel closed is nothing to report.
                });
            } catch (...) {
                // The task could not be made: the session waits for the
                // next attempt, and the user still has the classic path.
            }
        });

        g_registration.window = window;
        g_registration.interop = interop.get();
        g_registration.manager = manager;
        g_registration.live = true;
        return true;
    } catch (...) {
        try {
            std::lock_guard<std::mutex> lock(g_state);
            if (!g_registration.live &&
                g_registration.manager != nullptr) {
                // The subscription may have landed before the failure;
                // take it back so a later attempt starts clean.
                auto manager = g_registration.manager;
                manager.PrintTaskRequested(
                    g_registration.taskToken);
                g_registration = Registration{};
            }
        } catch (...) {
        }
        return false;
    }
}

bool ShowPrintDialog(HWND window, const DialogInputs& inputs) {
    if (window == nullptr || !inputs.build) return false;
    try {
        std::lock_guard<std::mutex> lock(g_state);
        if (!g_registration.live || g_registration.window != window) {
            return false;
        }
        if (g_session.active) {
            // One dialog at a time: the second ask falls back to the
            // classic panel instead of racing the first.
            return false;
        }
        PrintSourceWin* source = new PrintSourceWin(window, inputs);
        g_session.window = window;
        g_session.inputs = inputs;
        g_session.source = source;
        g_session.active = true;

        void* asyncOp = nullptr;
        const GUID opGuid = reinterpret_cast<const GUID&>(
            winrt::guid_of<
                winrt::Windows::Foundation::IAsyncOperation<bool>>());
        HRESULT hr = g_registration.interop->ShowPrintUIForWindowAsync(
            window, opGuid, &asyncOp);
        if (FAILED(hr)) {
            g_session.source->Release();
            g_session.source = nullptr;
            g_session.active = false;
            g_session.window = nullptr;
            g_session.inputs = DialogInputs{};
            return false;
        }
        if (asyncOp != nullptr) {
            static_cast<IUnknown*>(asyncOp)->Release();
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool HandleWindowMessage(HWND window, UINT message, WPARAM,
                         LPARAM lParam) {
    if (message != kBuildPageMessage) return false;
    BuildRequest* req = reinterpret_cast<BuildRequest*>(lParam);
    if (req == nullptr) return true;
    PageBuilder build;
    {
        std::lock_guard<std::mutex> lock(g_state);
        if (g_session.active && g_session.window == window) {
            build = g_session.inputs.build;
        }
    }
    if (build) {
        build(&req->pb);
        req->answered = true;
    }
    return true;
}

}  // namespace print_winrt
