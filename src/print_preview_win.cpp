// The Windows print preview: the native window Ctrl+P opens, showing the
// pages File > Print will print before anything is sent to paper.
//
// The macOS print panel carries its own preview (AppKit), and the common
// PrintDlg on Windows carries none, so this window is the Windows half of
// that one panel: the same shared bands (src/layout/print_pages.h) at the
// default A4 page the QA walk reports, a Print button that opens the
// classic panel, and a Save as PDF button that writes the same bands
// through the shared pdfio writer (src/print_pdf.cpp), the answer to the
// mac panel's own Save as PDF.
//
// The window is chrome: the pages are drawn through the RenderTarget seam
// like every other surface, and only the frame, the buttons and the modal
// loop are Win32. The paper is the default A4 page; the print panel may
// offer a different one, and paper that changes the cut changes the pages,
// which references/printing.md records as the one honest difference between
// the preview and the sheet.
//
// Plan: .hermes/plans/2026-11-15_204703-printing.md

#include "print_preview_win.h"

#include <d2d1.h>
#include <d2d1helper.h>
#include <windowsx.h>

// renderer.h before any header that reads the seam, and never
// platform/seams.h by hand: windows.h leaves a DrawText macro behind, and
// renderer.h undefines it before the seam is read, so the render target's
// override matches the seam it overrides.
#include "renderer.h"
#include "layout/display_player.h"
#include "layout/print_pages.h"
#include "print_pdf.h"
#include "windows/render_target_win.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace print_preview {
namespace {

// One preview at a time, the rule the mac panel keeps by being a sheet on
// its window: a second Ctrl+P while the first is up does nothing.
bool g_open = false;

const wchar_t kFrameClass[] = L"MarkDownItPrintPreview";
const wchar_t kPagesClass[] = L"MarkDownItPrintPreviewPages";

const int kBarHeight = 46;
const int kPad = 12;
const int kButtonWidth = 118;
const int kButtonHeight = 30;
const int kButtonGap = 8;
const int kPageGap = 24;
const int kLabelWidth = 160;

const int kIdPrint = 1001;
const int kIdSavePdf = 1002;
const int kIdClose = 1003;
const UINT kSentinel = WM_APP + 1;

class PreviewWindow {
public:
    PreviewWindow(HWND owner, Renderer& renderer, IDWriteFactory* dwrite,
                  bool layoutReady, const Document& document,
                  const std::wstring& name)
        : owner_(owner), renderer_(renderer), dwrite_(dwrite),
          document_(document), name_(name) {
        const layout::PrintPage page;
        page_ = page;
        // The same guard the QA walk uses: the layout may not exist yet in
        // early walk states, and PrintLayout on an unbuilt engine is not a
        // call to make on hope.
        if (layoutReady && dwrite_ != nullptr) {
            laid_ = renderer_.PrintLayout(document_, page_.ContentWidth());
        }
        if (laid_) {
            bands_ = layout::PaginateDocument(*laid_, page_);
        }
    }

    // Creates the window and pumps messages until the reader closes it.
    Action Run() {
        if (!RegisterClasses()) return Action::None;

        RECT work = {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int width = std::max(520, static_cast<int>(
            (work.right - work.left) * 3 / 4));
        const int height = std::max(400, static_cast<int>(
            (work.bottom - work.top) * 3 / 4));
        const int x = work.left + ((work.right - work.left) - width) / 2;
        const int y = work.top + ((work.bottom - work.top) - height) / 3;

        std::wstring caption = L"Udskriftsvisning";
        if (!name_.empty()) caption += L" - " + name_;
        frame_ = CreateWindowExW(
            WS_EX_APPWINDOW, kFrameClass, caption.c_str(),
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
            x, y, width, height, owner_, nullptr, GetModuleHandleW(nullptr),
            this);
        if (!frame_) return Action::None;

        EnableWindow(owner_, FALSE);
        ShowWindow(frame_, SW_SHOW);
        UpdateWindow(frame_);
        SetForegroundWindow(frame_);
        SetFocus(pages_ != nullptr ? pages_ : frame_);

        MSG msg;
        for (;;) {
            const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
            if (got <= 0 || !IsWindow(frame_)) break;
            if (msg.message == WM_KEYDOWN && msg.hwnd != frame_ &&
                IsChild(frame_, msg.hwnd)) {
                if (msg.wParam == VK_ESCAPE) {
                    CloseWith(Action::None);
                    continue;
                }
                if (msg.wParam == VK_PRIOR || msg.wParam == VK_NEXT ||
                    msg.wParam == VK_UP || msg.wParam == VK_DOWN ||
                    msg.wParam == VK_HOME || msg.wParam == VK_END) {
                    msg.hwnd = pages_;
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (IsWindow(owner_)) {
            EnableWindow(owner_, TRUE);
            SetActiveWindow(owner_);
        }
        return action_;
    }

private:
    static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp,
                                      LPARAM lp) {
        PreviewWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<PreviewWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<PreviewWindow*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        return self->OnFrame(hwnd, msg, wp, lp);
    }

    static LRESULT CALLBACK PagesProc(HWND hwnd, UINT msg, WPARAM wp,
                                      LPARAM lp) {
        auto* self = reinterpret_cast<PreviewWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        return self->OnPages(hwnd, msg, wp, lp);
    }

    static bool RegisterClasses() {
        static bool done = false;
        if (done) return true;
        WNDCLASSEXW frame = {};
        frame.cbSize = sizeof(frame);
        frame.style = CS_HREDRAW | CS_VREDRAW;
        frame.lpfnWndProc = FrameProc;
        frame.hInstance = GetModuleHandleW(nullptr);
        frame.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        frame.hbrBackground = nullptr;
        frame.lpszClassName = kFrameClass;
        if (!RegisterClassExW(&frame)) return false;
        WNDCLASSEXW pages = {};
        pages.cbSize = sizeof(pages);
        pages.style = CS_HREDRAW | CS_VREDRAW;
        pages.lpfnWndProc = PagesProc;
        pages.hInstance = GetModuleHandleW(nullptr);
        pages.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        pages.hbrBackground = nullptr;
        pages.lpszClassName = kPagesClass;
        if (!RegisterClassExW(&pages)) return false;
        done = true;
        return true;
    }

    LRESULT OnFrame(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE: {
            HFONT font =
                static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HWND buttons[3] = {};
            buttons[0] = CreateWindowW(
                L"BUTTON", L"Udskriv...", WS_CHILD | WS_VISIBLE |
                BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPrint)),
                GetModuleHandleW(nullptr), nullptr);
            buttons[1] = CreateWindowW(
                L"BUTTON", L"Gem som PDF...", WS_CHILD | WS_VISIBLE |
                BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSavePdf)),
                GetModuleHandleW(nullptr), nullptr);
            buttons[2] = CreateWindowW(
                L"BUTTON", L"Luk", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdClose)),
                GetModuleHandleW(nullptr), nullptr);
            for (HWND b : buttons) {
                if (b) SendMessageW(b, WM_SETFONT,
                                    reinterpret_cast<WPARAM>(font),
                                    static_cast<LPARAM>(TRUE));
            }
            label_ = CreateWindowW(
                L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr),
                nullptr);
            if (label_) SendMessageW(label_, WM_SETFONT,
                                     reinterpret_cast<WPARAM>(font),
                                     static_cast<LPARAM>(TRUE));
            pages_ = CreateWindowW(
                kPagesClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr),
                nullptr);
            if (pages_ != nullptr) {
                SetWindowLongPtrW(pages_, GWLP_USERDATA,
                                  reinterpret_cast<LONG_PTR>(this));
            }
            EnableWindow(GetDlgItem(hwnd, kIdPrint),
                         laid_ != nullptr && !bands_.empty());
            EnableWindow(GetDlgItem(hwnd, kIdSavePdf),
                         laid_ != nullptr && !bands_.empty());
            UpdateLabel();
            return 0;
        }
        case WM_SIZE:
            LayoutChildren(hwnd, LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize.x = 460;
            info->ptMinTrackSize.y = 320;
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == kIdPrint) {
                CloseWith(Action::Print);
            } else if (id == kIdSavePdf) {
                CloseWith(Action::SavePdf);
            } else if (id == kIdClose) {
                CloseWith(Action::None);
            }
            return 0;
        }
        case WM_CLOSE:
            CloseWith(Action::None);
            return 0;
        case WM_DESTROY:
            ReleaseTarget();
            // Wake the pump: GetMessage would otherwise wait for a message
            // that never comes once the frame is gone.
            PostThreadMessageW(GetCurrentThreadId(), kSentinel, 0, 0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    LRESULT OnPages(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_ERASEBKGND:
            // The whole client is painted by Direct2D below.
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            PaintPages();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            ResizeTarget();
            UpdateScrollInfo();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_VSCROLL: {
            const int before = scroll_;
            switch (LOWORD(wp)) {
            case SB_LINEUP: scroll_ -= 30; break;
            case SB_LINEDOWN: scroll_ += 30; break;
            case SB_PAGEUP: scroll_ -= 90; break;
            case SB_PAGEDOWN: scroll_ += 90; break;
            case SB_TOP: scroll_ = 0; break;
            case SB_BOTTOM: scroll_ = MaxScroll(); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: {
                SCROLLINFO info = {};
                info.cbSize = sizeof(info);
                info.fMask = SIF_TRACKPOS;
                if (GetScrollInfo(hwnd, SB_VERT, &info)) {
                    scroll_ = static_cast<int>(info.nTrackPos);
                }
                break;
            }
            default: break;
            }
            ClampScroll();
            if (scroll_ != before) {
                UpdateScrollPos();
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateLabel();
            }
            return 0;
        }
        case WM_KEYDOWN: {
            // The modal pump sends page keys here from any child of the
            // frame, so the keyboard works whatever the focus sits on.
            const int before = scroll_;
            const float s = FitScale();
            const int page = static_cast<int>(page_.height * s) + kPageGap;
            switch (wp) {
            case VK_NEXT: scroll_ += page; break;
            case VK_PRIOR: scroll_ -= page; break;
            case VK_DOWN: scroll_ += 30; break;
            case VK_UP: scroll_ -= 30; break;
            case VK_HOME: scroll_ = 0; break;
            case VK_END: scroll_ = MaxScroll(); break;
            default: return 0;
            }
            ClampScroll();
            if (scroll_ != before) {
                UpdateScrollPos();
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateLabel();
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const int before = scroll_;
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            scroll_ -= delta * 2;  // two pixels per notch step, wheel notches
            ClampScroll();
            if (scroll_ != before) {
                UpdateScrollPos();
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateLabel();
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            // A click walks to that page: the sheet under the cursor becomes
            // the top of the view.
            const int y = GET_Y_LPARAM(lp);
            const float s = FitScale();
            const int step = static_cast<int>(
                page_.height * s) + kPageGap;
            if (step > 0) {
                scroll_ = ((y + scroll_) / step) * step;
                ClampScroll();
                UpdateScrollPos();
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateLabel();
            }
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    void CloseWith(Action action) {
        action_ = action;
        if (IsWindow(frame_)) DestroyWindow(frame_);
    }

    void LayoutChildren(HWND hwnd, int w, int h) {
        const int top = kBarHeight;
        if (h < top + 40) h = top + 40;
        const int y = (top - kButtonHeight) / 2;
        int x = kPad;
        MoveWindow(GetDlgItem(hwnd, kIdPrint), x, y, kButtonWidth,
                   kButtonHeight, TRUE);
        x += kButtonWidth + kButtonGap;
        MoveWindow(GetDlgItem(hwnd, kIdSavePdf), x, y, kButtonWidth,
                   kButtonHeight, TRUE);
        x += kButtonWidth + kButtonGap + 20;
        if (label_ != nullptr) {
            MoveWindow(label_, x, y + 6, kLabelWidth,
                       kButtonHeight - 12, TRUE);
        }
        // Close sits at the right end of the bar.
        MoveWindow(GetDlgItem(hwnd, kIdClose), w - kPad - kButtonWidth, y,
                   kButtonWidth, kButtonHeight, TRUE);
        if (pages_ != nullptr) {
            MoveWindow(pages_, 0, top, w, h - top, TRUE);
        }
    }

    // ---- Geometry -------------------------------------------------------

    float FitScale() const {
        RECT rc = {};
        if (pages_ != nullptr) GetClientRect(pages_, &rc);
        const int barWidth =
            GetSystemMetrics(SM_CXVSCROLL) + 2 * kPad + 2 * kPad;
        const int room = rc.right - barWidth;
        if (room <= 0 || !(page_.width > 0.0f)) return 0.5f;
        float s = static_cast<float>(room) / page_.width;
        if (s < 0.1f) s = 0.1f;
        if (s > 2.0f) s = 2.0f;
        return s;
    }

    int MaxScroll() const {
        RECT rc = {};
        if (pages_ != nullptr) GetClientRect(pages_, &rc);
        const float s = FitScale();
        const int strip = static_cast<int>(
            bands_.size() * (page_.height * s + kPageGap) + kPageGap);
        int max = strip - (rc.bottom - rc.top);
        if (max < 0) max = 0;
        return max;
    }

    void ClampScroll() {
        const int max = MaxScroll();
        if (scroll_ < 0) scroll_ = 0;
        if (scroll_ > max) scroll_ = max;
    }

    void UpdateScrollInfo() {
        ClampScroll();
        RECT rc = {};
        if (pages_ != nullptr) GetClientRect(pages_, &rc);
        const float s = FitScale();
        int strip = static_cast<int>(
            bands_.size() * (page_.height * s + kPageGap) + kPageGap);
        if (strip < 1) strip = 1;
        const int client = rc.bottom - rc.top;
        if (scroll_ > strip - client) scroll_ = strip - client;
        if (scroll_ < 0) scroll_ = 0;
        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_POS | SIF_PAGE;
        info.nMin = 0;
        info.nMax = strip - 1;
        info.nPage = static_cast<UINT>(client > 0 ? client : 1);
        info.nPos = scroll_;
        if (pages_ != nullptr) SetScrollInfo(pages_, SB_VERT, &info, TRUE);
    }

    void UpdateScrollPos() {
        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_POS;
        info.nPos = scroll_;
        if (pages_ != nullptr) SetScrollInfo(pages_, SB_VERT, &info, TRUE);
    }

    void UpdateLabel() {
        if (label_ == nullptr || bands_.empty()) return;
        const float s = FitScale();
        const int step = static_cast<int>(page_.height * s) + kPageGap;
        int page = 1;
        if (step > 0) {
            RECT rc = {};
            if (pages_ != nullptr) GetClientRect(pages_, &rc);
            page = (scroll_ + (rc.bottom - rc.top) / 2) / step + 1;
            if (page < 1) page = 1;
            if (page > static_cast<int>(bands_.size())) {
                page = static_cast<int>(bands_.size());
            }
        }
        wchar_t text[64];
        swprintf(text, 64, L"Side %d af %d", page,
                 static_cast<int>(bands_.size()));
        SetWindowTextW(label_, text);
    }

    // ---- Drawing --------------------------------------------------------

    void EnsureTarget() {
        if (rt_ != nullptr) return;
        RECT rc = {};
        if (pages_ != nullptr) GetClientRect(pages_, &rc);
        if (rc.right <= 0 || rc.bottom <= 0) return;
        ID2D1Factory* factory = nullptr;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     &factory)) ||
            factory == nullptr) {
            return;
        }
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_IGNORE));
        D2D1_HWND_RENDER_TARGET_PROPERTIES hw =
            D2D1::HwndRenderTargetProperties(
                pages_, D2D1::SizeU(rc.right, rc.bottom));
        // Software, like the printer path: the preview is a small window and
        // never worth a lost hardware context.
        ID2D1HwndRenderTarget* target = nullptr;
        if (FAILED(factory->CreateHwndRenderTarget(props, hw, &target))) {
            factory->Release();
            return;
        }
        factory->Release();
        rt_ = target;
        rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    }

    void ResizeTarget() {
        if (rt_ == nullptr) return;
        RECT rc = {};
        if (pages_ != nullptr) GetClientRect(pages_, &rc);
        if (rc.right <= 0 || rc.bottom <= 0) return;
        D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);
        if (FAILED(rt_->Resize(size))) ReleaseTarget();
    }

    void ReleaseTarget() {
        if (rt_ != nullptr) {
            rt_->Release();
            rt_ = nullptr;
        }
    }

    void PaintPages() {
        EnsureTarget();
        if (rt_ == nullptr || laid_ == nullptr) return;

        RECT rc = {};
        GetClientRect(pages_, &rc);
        const float viewH = static_cast<float>(rc.bottom);

        rt_->BeginDraw();
        rt_->SetTransform(D2D1::Matrix3x2F::Identity());
        rt_->Clear(D2D1::ColorF(0.94f, 0.94f, 0.94f, 1.0f));

        ID2D1SolidColorBrush* paper = nullptr;
        ID2D1SolidColorBrush* edge = nullptr;
        rt_->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White), &paper);
        rt_->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Gray), &edge);

        // A PDF reader does not lay a page background down, so the preview
        // shows what the reader will see: white pages, grey workspace around
        // them, hairline edges.
        for (size_t i = 0; i < bands_.size(); ++i) {
            const float s = FitScale();
            const float pageW = page_.width * s;
            const float pageH = page_.height * s;
            const float pageX =
                (static_cast<float>(rc.right) - pageW) * 0.5f;
            const float pageY =
                static_cast<float>(i) * (pageH + kPageGap) + kPageGap -
                static_cast<float>(scroll_);
            if (pageY > viewH || pageY + pageH + kPageGap < 0.0f) continue;

            if (paper != nullptr && edge != nullptr) {
                const D2D1_RECT_F rect = D2D1::RectF(
                    pageX, pageY, pageX + pageW, pageY + pageH);
                rt_->SetTransform(D2D1::Matrix3x2F::Identity());
                rt_->FillRectangle(rect, paper);
                rt_->DrawRectangle(rect, edge, 1.0f);
            }

            const layout::PagePlacement place =
                layout::PlacementForBand(page_, bands_[i]);
            // The seam is spoken in document units, one seam unit is 1/96
            // inch, and the scale turns that into this window's pixels. The
            // placement is the page's own rule, shared with the printer.
            const float tx = pageX / s + place.x;
            const float ty = pageY / s + place.y;
            rt_->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                              D2D1::Matrix3x2F::Translation(tx, ty));
            platform::D2DTarget seam(rt_, dwrite_);
            const display::DisplayList items =
                layout::PageItems(*laid_, bands_[i]);
            display::Play(items, &seam);
        }

        rt_->SetTransform(D2D1::Matrix3x2F::Identity());
        if (paper != nullptr) paper->Release();
        if (edge != nullptr) edge->Release();
        const HRESULT hr = rt_->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            ReleaseTarget();
            InvalidateRect(pages_, nullptr, FALSE);
        }
    }

    // ---- Members --------------------------------------------------------

    HWND owner_ = nullptr;
    HWND frame_ = nullptr;
    HWND pages_ = nullptr;
    HWND label_ = nullptr;
    Renderer& renderer_;
    IDWriteFactory* dwrite_ = nullptr;
    const Document& document_;
    std::wstring name_;
    layout::PrintPage page_;
    std::shared_ptr<const layout::DocumentLayout> laid_;
    std::vector<layout::PrintBand> bands_;
    ID2D1HwndRenderTarget* rt_ = nullptr;
    int scroll_ = 0;
    Action action_ = Action::None;
};

}  // namespace

Action ShowPreview(HWND owner, Renderer& renderer, IDWriteFactory* dwrite,
                   bool layoutReady, const Document& document,
                   const std::wstring& documentName) {
    if (owner == nullptr || dwrite == nullptr) return Action::None;
    if (g_open) return Action::None;
    g_open = true;
    Action result = Action::None;
    {
        PreviewWindow preview(owner, renderer, dwrite, layoutReady, document,
                              documentName);
        result = preview.Run();
    }
    g_open = false;
    return result;
}

}  // namespace print_preview
