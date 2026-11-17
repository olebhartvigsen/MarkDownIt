// Printing on macOS: one band per page, through Core Graphics.
// Plan: .hermes/plans/2026-11-15_204703-printing.md

#include "print_mac.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "cg_target_mac.h"
#include "layout/display_player.h"
#include "renderer_mac.h"

namespace print_mac {
namespace {

// One seam unit (1/96 inch) is 0.75 of a point (1/72 inch). The layout, the
// theme and the Windows driver all speak seam units; only paper speaks
// points, so this is the one conversion.
constexpr CGFloat kSeamToPoints = 72.0 / 96.0;

// Paints one page's slice. The context is top-down (the origin is the page's
// top left corner and y grows downward) and clipped to the page, which is the
// space the layout and the display list are already in.
void PaintPage(CGContextRef cg, const layout::PrintPage& page,
               const display::DisplayList& slice) {
    const CGFloat pageW = page.width * kSeamToPoints;
    const CGFloat pageH = page.height * kSeamToPoints;
    CGContextSaveGState(cg);
    // The paper, edge to edge: printers do not all lay a white sheet down for
    // a page that draws nothing, and a dark window must not print dark.
    CGContextClipToRect(cg, CGRectMake(0.0, 0.0, pageW, pageH));
    CGContextSetRGBFillColor(cg, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(cg, CGRectMake(0.0, 0.0, pageW, pageH));
    // The page's margin, then seam units: after this the display list's own
    // coordinates land on paper at 1/96 inch, which is what 100% means.
    CGContextTranslateCTM(cg, page.margin * kSeamToPoints,
                          page.margin * kSeamToPoints);
    CGContextScaleCTM(cg, kSeamToPoints, kSeamToPoints);
    platform::CoreGraphicsTarget target(cg);
    display::Play(slice, &target);
    CGContextRestoreGState(cg);
}

}  // namespace

}  // namespace print_mac

// The view the print operation asks for its pages. It holds the layout of the
// document for the paper the panel chose, and draws the band of the page the
// operation asks for.
@interface MdPrintView : NSView {
    platform::MacRenderer* renderer_;
    const Document* document_;
    layout::PrintPage page_;
    std::shared_ptr<const layout::DocumentLayout> laid_;
    std::vector<layout::PrintBand> bands_;
}

- (instancetype)initWithRenderer:(platform::MacRenderer*)renderer
                        document:(const Document*)document;
// Lays the document out for the imageable area of the sheet the panel chose.
- (void)prepareForPaper:(NSSize)paperPoints;

@end

@implementation MdPrintView

- (instancetype)initWithRenderer:(platform::MacRenderer*)renderer
                        document:(const Document*)document {
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, 612.0, 792.0)];
    if (self) {
        renderer_ = renderer;
        document_ = document;
        // A4 until the panel says otherwise, the same default page the
        // Windows driver builds from the printer's own paper.
        page_ = layout::PrintPage();
    }
    return self;
}

// A top-down view: the layout's y already runs downward, and so does the
// display list, so no coordinate is ever mirrored twice.
- (BOOL)isFlipped {
    return YES;
}

- (void)prepareForPaper:(NSSize)paperPoints {
    if (!renderer_ || !document_) return;
    // Points to seam units, once.
    page_.width = (float)(paperPoints.width / print_mac::kSeamToPoints);
    page_.height = (float)(paperPoints.height / print_mac::kSeamToPoints);
    const float smallest = std::min(page_.width, page_.height);
    page_.margin = std::min(75.6f, std::max(0.0f, smallest * 0.15f));
    if (!(page_.ContentWidth() > 0.0f) || !(page_.ContentHeight() > 0.0f)) {
        bands_.clear();
        return;
    }
    laid_ = renderer_->PrintLayout(*document_, page_.ContentWidth());
    bands_ = laid_ ? layout::PaginateDocument(*laid_, page_)
                   : std::vector<layout::PrintBand>();
    if (bands_.empty()) return;
    // Every page gets its own slot in the view, so AppKit can ask for one
    // page at a time and only that band is drawn.
    [self setFrameSize:NSMakeSize(page_.width * print_mac::kSeamToPoints,
                                  page_.height * print_mac::kSeamToPoints *
                                      (CGFloat)bands_.size())];
}

- (BOOL)knowsPageRange:(NSRangePointer)range {
    // The panel has been dismissed by the time this is asked, so the paper
    // size is the one the user picked.
    NSRect imageable = [[self printInfo] imageablePageBounds];
    [self prepareForPaper:imageable.size];
    range->location = 1;
    range->length = bands_.empty() ? 1 : bands_.size();
    return YES;
}

- (NSRect)rectForPage:(NSInteger)page {
    const CGFloat pageW = page_.width * print_mac::kSeamToPoints;
    const CGFloat pageH = page_.height * print_mac::kSeamToPoints;
    return NSMakeRect(0.0, (CGFloat)(page - 1) * pageH, pageW, pageH);
}

- (void)drawRect:(NSRect)dirtyRect {
    CGContextRef cg = [[NSGraphicsContext currentContext] CGContext];
    if (!cg || bands_.empty()) return;
    const CGFloat pageH = page_.height * print_mac::kSeamToPoints;
    NSInteger index = pageH > 0.0 ? (NSInteger)floor(NSMinY(dirtyRect) / pageH) : 0;
    if (index < 0) index = 0;
    if (index >= (NSInteger)bands_.size()) {
        index = (NSInteger)bands_.size() - 1;
    }
    CGContextSaveGState(cg);
    // The view's origin is the document's, so the page's slot is moved to the
    // origin and the slice is drawn in its own page coordinates.
    CGContextTranslateCTM(cg, 0.0, -(CGFloat)index * pageH);
    print_mac::PaintPage(cg, page_,
                         layout::SliceForBand(laid_->list, bands_[(size_t)index]));
    CGContextRestoreGState(cg);
}

@end

namespace print_mac {

bool PrintDocument(NSWindow* window, platform::MacRenderer& renderer,
                   const Document& document, const std::string& title) {
    if (!window) return false;
    MdPrintView* view = [[MdPrintView alloc] initWithRenderer:&renderer
                                                     document:&document];
    // A copy of the shared info, so the panel's own settings start from the
    // system defaults and the app never writes into them.
    NSPrintInfo* info = [[NSPrintInfo sharedPrintInfo] copy];
    // The page model owns the margin on paper, so the view fills the sheet's
    // imageable area and AppKit adds nothing of its own.
    [info setLeftMargin:0.0];
    [info setRightMargin:0.0];
    [info setTopMargin:0.0];
    [info setBottomMargin:0.0];
    [info setHorizontallyCentered:NO];
    [info setVerticallyCentered:NO];
    NSPrintOperation* operation =
        [NSPrintOperation printOperationWithView:view printInfo:info];
    [operation setShowsPrintPanel:YES];
    [operation setShowsProgressPanel:YES];
    if (!title.empty()) {
        NSString* job = [NSString stringWithUTF8String:title.c_str()];
        if (job) [operation setJobTitle:job];
    }
    const BOOL ran = [operation runOperationModalForWindow:window
                                                  delegate:nil
                                            didRunSelector:nullptr
                                               contextInfo:nullptr];
    return ran == YES;
}

bool WritePdf(const std::string& path, platform::MacRenderer& renderer,
              const Document& document, const layout::PrintPage& page,
              int* pageCount) {
    if (pageCount) *pageCount = 0;
    if (path.empty()) return false;
    std::shared_ptr<const layout::DocumentLayout> laid =
        renderer.PrintLayout(document, page.ContentWidth());
    if (!laid) return false;
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(*laid, page);
    if (bands.empty()) return false;

    CGRect media = CGRectMake(0.0, 0.0, page.width * kSeamToPoints,
                              page.height * kSeamToPoints);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, (const UInt8*)path.c_str(), (CFIndex)path.size(), false);
    if (!url) return false;
    CGContextRef pdf = CGPDFContextCreateWithURL(url, &media, nullptr);
    CFRelease(url);
    if (!pdf) return false;

    for (size_t i = 0; i < bands.size(); ++i) {
        CGPDFContextBeginPage(pdf, nullptr);
        // A PDF context is bottom-up; the page painter expects top-down, so
        // the flip happens here and nowhere else.
        CGContextSaveGState(pdf);
        CGContextTranslateCTM(pdf, 0.0, media.size.height);
        CGContextScaleCTM(pdf, 1.0, -1.0);
        PaintPage(pdf, page, layout::SliceForBand(laid->list, bands[i]));
        CGContextRestoreGState(pdf);
        CGPDFContextEndPage(pdf);
    }
    CGPDFContextClose(pdf);
    CGContextRelease(pdf);
    if (pageCount) *pageCount = (int)bands.size();
    return true;
}

}  // namespace print_mac
