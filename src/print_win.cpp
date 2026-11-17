// Printing on Windows: the panel, the printer DC, and one band per page.
// Plan: .hermes/plans/2026-11-15_204703-printing.md

#include "print_win.h"

#include <commdlg.h>
#include <d2d1.h>
#include <d2d1helper.h>

#include <algorithm>
#include <memory>
#include <vector>

// renderer.h before any header that reads the seam, and never
// platform/seams.h by hand: windows.h leaves a DrawText macro behind, and
// renderer.h undefines it before the seam is read, so the render target's
// override matches the seam it overrides.
#include "renderer.h"
#include "layout/display_player.h"
#include "layout/print_pages.h"
#include "windows/render_target_win.h"

namespace print_win {
namespace {

// Seam units per device pixel at the printer's own resolution.
float SeamFromPixels(float pixels, int dpi) {
    if (dpi <= 0) return pixels;
    return pixels * 96.0f / static_cast<float>(dpi);
}

// The printable area in seam units, with the margin the page keeps. The
// unprintable edge of the sheet stays outside the layout, so no glyph is laid
// where the printer cannot reach it, and the margin shrinks on a small sheet
// instead of eating the whole page.
layout::PrintPage PageFromDC(HDC dc) {
    const int dpiX = GetDeviceCaps(dc, LOGPIXELSX);
    const int dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    layout::PrintPage page;
    page.width = SeamFromPixels(static_cast<float>(GetDeviceCaps(dc, HORZRES)), dpiX);
    page.height = SeamFromPixels(static_cast<float>(GetDeviceCaps(dc, VERTRES)), dpiY);
    const float smallest = (std::min)(page.width, page.height);
    page.margin = (std::min)(75.6f, (std::max)(0.0f, smallest * 0.15f));
    return page;
}

// One document through one printer DC. The page model, the cut and the slice
// are the shared ones; this function owns the job, the pages and the target.
bool PrintToDC(HDC dc, Renderer& renderer, const Document& document,
               const std::wstring& title) {
    const layout::PrintPage page = PageFromDC(dc);
    if (!(page.ContentWidth() > 0.0f) || !(page.ContentHeight() > 0.0f)) {
        return false;
    }

    std::shared_ptr<const layout::DocumentLayout> laid =
        renderer.PrintLayout(document, page.ContentWidth());
    if (!laid) return false;
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(*laid, page);
    if (bands.empty()) return false;

    // The paper's own resolution, so a seam unit lands on paper at 1/96 inch
    // without a scale in the drawing code (the target's dpi does the work).
    const int dpiX = GetDeviceCaps(dc, LOGPIXELSX);
    const int dpiY = GetDeviceCaps(dc, LOGPIXELSY);

    ID2D1Factory* factory = nullptr;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory)) ||
        !factory) {
        return false;
    }

    RECT area = {0, 0, GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES)};
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_IGNORE),
        static_cast<float>(dpiX), static_cast<float>(dpiY),
        D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    ID2D1DCRenderTarget* target = nullptr;
    const HRESULT created = factory->CreateDCRenderTarget(&props, &target);
    if (FAILED(created) || !target) {
        factory->Release();
        return false;
    }
    const HRESULT bound = target->BindDC(dc, &area);
    if (FAILED(bound)) {
        target->Release();
        factory->Release();
        return false;
    }
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Grayscale, never ClearType: subpixel text relies on the display's own
    // stripes and prints as coloured fringes on paper.
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    DOCINFOW info = {};
    info.cbSize = sizeof(info);
    const std::wstring jobName = title.empty() ? std::wstring(L"MarkDownIt") : title;
    info.lpszDocName = jobName.c_str();

    bool ok = StartDocW(dc, &info) > 0;
    for (size_t i = 0; ok && i < bands.size(); ++i) {
        if (StartPage(dc) <= 0) {
            ok = false;
            break;
        }
        target->BeginDraw();
        // The margin is the page's own; the target's dpi turns the rest into
        // paper. Clear is in device space, so it fills the sheet with white
        // whatever the transform says.
        target->SetTransform(
            D2D1::Matrix3x2F::Translation(page.margin, page.margin));
        target->Clear(D2D1::ColorF(D2D1::ColorF::White));
        platform::D2DTarget seam(target);
        display::Play(layout::SliceForBand(laid->list, bands[i]), &seam);
        if (FAILED(target->EndDraw())) {
            ok = false;
        }
        if (EndPage(dc) <= 0) ok = false;
    }
    if (ok && EndDoc(dc) <= 0) ok = false;
    if (!ok) AbortDoc(dc);

    target->Release();
    factory->Release();
    return ok;
}

}  // namespace

bool PrintDocument(HWND owner, Renderer& renderer, const Document& document,
                   const std::wstring& title) {
    PRINTDLGEXW panel = {};
    panel.lStructSize = sizeof(panel);
    panel.hwndOwner = owner;
    panel.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS |
                  PD_USEDEVMODECOPIESANDCOLLATE;
    panel.nCopies = 1;
    panel.nStartPage = START_PAGE_GENERAL;
    panel.dwResultAction = 0;

    const HRESULT shown = PrintDlgExW(&panel);
    bool printed = false;
    if (SUCCEEDED(shown) && panel.dwResultAction == PD_RESULT_PRINT &&
        panel.hDC) {
        printed = PrintToDC(panel.hDC, renderer, document, title);
        // The panel handed the DC over; the print job has ended above.
        DeleteDC(panel.hDC);
    }
    if (panel.hDevMode) GlobalFree(panel.hDevMode);
    if (panel.hDevNames) GlobalFree(panel.hDevNames);
    return printed;
}

}  // namespace print_win
