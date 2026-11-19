// Printing on Windows: the panel, the printer DC, and one band per page.
// Plan: .hermes/plans/2026-11-15_204703-printing.md
//
// The print dialog a PDF needs is not this one: the panel lists the
// installed printers, and Microsoft Print to PDF is one of them, but that
// route depends on a GDI driver actually drawing the pages. The reliable
// PDF on Windows is the preview window's Save as PDF, which writes the
// same bands through the shared pdfio writer (src/print_pdf.cpp). This
// job stays the general printer path.
//
// Three hard rules here:
// - SOFTWARE rendering, never DEFAULT: a printer DC has no hardware path,
//   and the plan's risk 2 names a driver that refuses the other kind.
// - BindDC per page, after StartPage: a driver may reset its DC between
//   pages, and each page binds the same full page rectangle.
// - The target is 96 dpi and the printer's own resolution is a scale in
//   the transform: a DC target's DPI property makes some drivers size the
//   bitmap by it and hand back pages a multiple too large, fonts first.
// - Every failure is spoken: a job that dies quietly looks exactly like
//   "cannot print", to the reader and to us. The caller shows the message.

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

std::string HresultText(const char* what, HRESULT code) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s (0x%08lX)", what,
             static_cast<unsigned long>(code));
    return std::string(buf);
}

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

// One document through one printer DC. The page model, the cut and the
// slice are the shared ones; this function owns the job, the pages and the
// target, and it says why it failed instead of returning quietly.
bool PrintToDC(HDC dc, Renderer& renderer, IDWriteFactory* dwrite,
               const Document& document, const std::wstring& title,
               std::string* message) {
    auto fail = [message](std::string text) {
        if (message != nullptr) *message = std::move(text);
        return false;
    };

    const layout::PrintPage page = PageFromDC(dc);
    if (!(page.ContentWidth() > 0.0f) || !(page.ContentHeight() > 0.0f)) {
        return fail("the printer reported a paper size with no printable area");
    }

    std::shared_ptr<const layout::DocumentLayout> laid =
        renderer.PrintLayout(document, page.ContentWidth());
    if (!laid) {
        return fail("the document could not be laid out for the printer's paper");
    }
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(*laid, page);
    if (bands.empty()) {
        return fail("the document produced no pages for this paper");
    }

    // The paper's own resolution, as the scale the target draws through.
    // The target itself stays at 96, the documented default where one DIP is
    // one device pixel: a DC target's own DPI property fights the BindDC
    // rectangle on some drivers and hands back pages a multiple too large
    // (the "fonts are far too big" print), so the printer's dpi goes into
    // the drawing, never into the target.
    const int dpiX = GetDeviceCaps(dc, LOGPIXELSX);
    const int dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    const float perSeamX = dpiX > 0 ? dpiX / 96.0f : 1.0f;
    const float perSeamY = dpiY > 0 ? dpiY / 96.0f : 1.0f;
    RECT area = {0, 0, GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES)};

    ID2D1Factory* factory = nullptr;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
    if (FAILED(hr) || !factory) {
        return fail(HresultText("Direct2D could not start", hr));
    }

    // Software on purpose: a printer DC has no hardware path, and DEFAULT
    // picks one on machines where it costs a driver round trip that a spool
    // can refuse. The plan's risk 2 lives here. The DPI stays 96 on both
    // axes; see the comment above for why the printer's own resolution is
    // a transform, not a property.
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_IGNORE),
        96.0f, 96.0f,
        D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    ID2D1DCRenderTarget* target = nullptr;
    hr = factory->CreateDCRenderTarget(&props, &target);
    if (FAILED(hr) || !target) {
        factory->Release();
        return fail(HresultText("the printer would not take a drawing surface",
                                 hr));
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
    if (!ok) {
        target->Release();
        factory->Release();
        return fail("the printer refused the job (the queue may be paused, "
                    "or the driver rejected the document)");
    }

    for (size_t i = 0; ok && i < bands.size(); ++i) {
        if (StartPage(dc) <= 0) {
            ok = false;
            fail("the printer stopped accepting pages");
            break;
        }
        // Bind inside the page: a driver is free to reset its DC between
        // pages, and every page binds the same whole sheet rectangle.
        hr = target->BindDC(dc, &area);
        if (FAILED(hr)) {
            ok = false;
            fail(HresultText("the printer's page could not be bound", hr));
            break;
        }
        target->BeginDraw();
        // The margin is the page's own; the printer's resolution is the
        // scale, so a seam unit is perSeam device pixels, which is 1/96 inch
        // on paper. Clear is in device space, so it fills the sheet with
        // white whatever the transform says.
        const layout::PagePlacement place =
            layout::PlacementForBand(page, bands[i]);
        target->SetTransform(
            D2D1::Matrix3x2F::Scale(perSeamX, perSeamY) *
            D2D1::Matrix3x2F::Translation(place.x, place.y));
        target->Clear(D2D1::ColorF(D2D1::ColorF::White));
        // The factory with it, or the text formats have no faces to draw.
        platform::D2DTarget seam(target, dwrite);
        display::Play(layout::PageItems(*laid, bands[i]), &seam);
        hr = target->EndDraw();
        if (FAILED(hr)) {
            ok = false;
            fail(HresultText("a page could not be drawn on this printer", hr));
            break;
        }
        if (EndPage(dc) <= 0) {
            ok = false;
            fail("the printer rejected a finished page");
            break;
        }
    }
    if (ok && EndDoc(dc) <= 0) {
        ok = false;
        fail("the printer ended the job with an error");
    }
    if (!ok) AbortDoc(dc);

    target->Release();
    factory->Release();
    return ok;
}

}  // namespace

bool PrintDocument(HWND owner, Renderer& renderer, IDWriteFactory* dwrite,
                   const Document& document, const std::wstring& title,
                   std::string* message) {
    // The classic panel: every SDK declares it, and the extended one's page
    // range is not read anyway, because printing is a whole document
    // operation here (references/printing.md).
    PRINTDLGW panel = {};
    panel.lStructSize = sizeof(panel);
    panel.hwndOwner = owner;
    panel.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS |
                  PD_USEDEVMODECOPIESANDCOLLATE;
    panel.nCopies = 1;
    panel.hDevMode = nullptr;
    panel.hDevNames = nullptr;

    bool printed = false;
    if (PrintDlgW(&panel) && panel.hDC) {
        printed = PrintToDC(panel.hDC, renderer, dwrite, document, title,
                            message);
        // The panel handed the DC over; the print job has ended above.
        DeleteDC(panel.hDC);
    }
    // A cancelled panel leaves these null and sets no extended error, which
    // is why the return value alone decides.
    if (panel.hDevMode) GlobalFree(panel.hDevMode);
    if (panel.hDevNames) GlobalFree(panel.hDevNames);
    return printed;
}

}  // namespace print_win
