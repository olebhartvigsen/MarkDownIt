// Printing on macOS: one band per page, through Core Graphics.
// Plan: .hermes/plans/2026-11-15_204703-printing.md

#include "print_mac.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "cg_target_mac.h"
#include "crash_trace_mac.h"
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
               const layout::PagePlacement& place,
               const display::DisplayList& slice, CGFloat originY = 0.0) {
    const CGFloat pageW = page.width * kSeamToPoints;
    const CGFloat pageH = page.height * kSeamToPoints;
    CGContextSaveGState(cg);
    // The paper, edge to edge: printers do not all lay a white sheet down for
    // a page that draws nothing, and a dark window must not print dark. It is
    // laid where this page sits, because that is where AppKit clips it.
    CGContextClipToRect(cg, CGRectMake(0.0, originY, pageW, pageH));
    CGContextSetRGBFillColor(cg, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(cg, CGRectMake(0.0, originY, pageW, pageH));
    // The page's margin with the band's own offset taken back out, then seam
    // units: after this the display list's document coordinates land on this
    // page at 1/96 inch, which is what 100% means.
    CGContextTranslateCTM(cg, place.x * kSeamToPoints,
                          originY + place.y * kSeamToPoints);
    CGContextScaleCTM(cg, kSeamToPoints, kSeamToPoints);
    platform::CoreGraphicsTarget target(cg);
    display::Play(slice, &target);
    CGContextRestoreGState(cg);
}

// How much of a written page carries ink, counted on a thumbnail of it. A
// count, not a picture: it answers the only question worth asking about a
// page, which is whether anything landed on it.
int InkOnPage(CGPDFDocumentRef pdf, size_t pageNumber) {
    CGPDFPageRef page = CGPDFDocumentGetPage(pdf, pageNumber);
    if (!page) return -1;
    const size_t w = 160;
    const size_t h = 227;
    std::vector<unsigned char> pixels(w * h * 4, 255);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(pixels.data(), w, h, 8, w * 4,
                                             space,
                                             kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    if (!ctx) return -1;
    CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(ctx, CGRectMake(0.0, 0.0, (CGFloat)w, (CGFloat)h));
    const CGRect box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
    if (box.size.width > 0.0 && box.size.height > 0.0) {
        CGContextScaleCTM(ctx, (CGFloat)w / box.size.width,
                          (CGFloat)h / box.size.height);
        CGContextDrawPDFPage(ctx, page);
    }
    CGContextRelease(ctx);
    int inked = 0;
    for (size_t i = 0; i + 2 < pixels.size(); i += 4) {
        if (pixels[i] < 248 || pixels[i + 1] < 248 || pixels[i + 2] < 248) {
            ++inked;
        }
    }
    return inked;
}

}  // namespace

}  // namespace print_mac

// The view the print operation asks for its pages. It holds the layout of the
// document for the paper the panel chose, and draws the band of the page the
// operation asks for.
@interface MdPrintView : NSView {
    platform::MacRenderer* renderer_;
    const Document* document_;
    // The operation's print info, which the panel writes the chosen paper
    // into. A view has no printInfo of its own, and asking the operation is
    // only possible while it is the current one.
    NSPrintInfo* info_;
    layout::PrintPage page_;
    std::shared_ptr<const layout::DocumentLayout> laid_;
    std::vector<layout::PrintBand> bands_;
}

- (instancetype)initWithRenderer:(platform::MacRenderer*)renderer
                        document:(const Document*)document
                       printInfo:(NSPrintInfo*)info;
// Lays the document out for the imageable area of the sheet the panel chose.
- (void)prepareForPaper:(NSSize)paperPoints;

@end

@class MdPrintPanelDriver;

// The sheet's delegate is not retained by AppKit, and printing outlives the
// call that starts it, so the driver is held here until the panel has ended.
static MdPrintPanelDriver* g_panelDriver = nil;

// Where the operation reports that its sheet is done. The run does the whole
// job: the panel, the printer, a Save as PDF destination and a cancel are all
// AppKit's, so nothing here has to carry the reader's answers from one object
// to another.
@interface MdPrintPanelDriver : NSObject
- (void)printOperationDidRun:(NSPrintOperation*)operation
                     success:(BOOL)success
                 contextInfo:(void*)info;
@end

@implementation MdPrintPanelDriver

- (void)printOperationDidRun:(NSPrintOperation*)operation
                     success:(BOOL)success
                 contextInfo:(void*)info {
    (void)operation;
    (void)success;
    (void)info;
    // The anchor is the only thing holding this object, so the local strong
    // reference keeps it alive to the end of the method.
    __strong MdPrintPanelDriver* keepAlive = self;
    (void)keepAlive;
    g_panelDriver = nil;
}

@end

@implementation MdPrintView

- (instancetype)initWithRenderer:(platform::MacRenderer*)renderer
                        document:(const Document*)document
                       printInfo:(NSPrintInfo*)info {
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, 612.0, 792.0)];
    if (self) {
        renderer_ = renderer;
        document_ = document;
        info_ = info;
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
    NSRect imageable = [info_ imageablePageBounds];
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
    const display::DisplayList slice =
        layout::PageItems(*laid_, bands_[(size_t)index]);
    // What AppKit actually asks for, while the walk asks: the rect it clips,
    // the index that becomes of it, and the transform it set up. Guessing at
    // this is how a page ends up blank.
    if (const char* dbg = getenv("MARKDOWNIT_QA_TRACE_PRINT")) {
        if (dbg[0]) {
            const CGAffineTransform m = CGContextGetCTM(cg);
            platform::TraceMac(
                "qa print draw: dirty=%.1f,%.1f,%.1f,%.1f index=%d bands=%zu "
                "items=%zu ctm=%.3f,%.3f,%.3f,%.3f,%.1f,%.1f "
                "bounds=%.1f,%.1f",
                (double)NSMinX(dirtyRect), (double)NSMinY(dirtyRect),
                (double)NSWidth(dirtyRect), (double)NSHeight(dirtyRect),
                (int)index, bands_.size(), slice.items.size(), m.a, m.b, m.c,
                m.d, m.tx, m.ty, (double)NSWidth([self bounds]),
                (double)NSHeight([self bounds]));
        }
    }
    // The view's origin is the document's, so the page is drawn at its own
    // slot and the transform AppKit set up carries it onto the sheet.
    print_mac::PaintPage(cg, page_,
                         layout::PlacementForBand(page_, bands_[(size_t)index]),
                         slice, (CGFloat)index * pageH);
}

@end

namespace print_mac {

bool PrintDocument(NSWindow* window, platform::MacRenderer& renderer,
                   const Document& document, const std::string& title) {
    if (!window) return false;
    // One panel at a time: beginning a second sheet on a window that already
    // has one is an AppKit exception, and Command-P pressed twice is not a
    // reason to abort the app.
    if ([window attachedSheet] != nil || g_panelDriver != nil) return false;
    // A copy of the shared info, so the panel's own settings start from the
    // system defaults and the app never writes into them. The view reads the
    // sheet's paper from this same object.
    NSPrintInfo* info = [[NSPrintInfo sharedPrintInfo] copy];
    MdPrintView* view = [[MdPrintView alloc] initWithRenderer:&renderer
                                                     document:&document
                                                    printInfo:info];
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
    // The operation owns the panel: choosing a printer, choosing Save as PDF
    // and cancelling all land in AppKit's hands, and the pages go wherever the
    // reader pointed them.
    [operation setShowsPrintPanel:YES];
    [operation setShowsProgressPanel:YES];
    if (!title.empty()) {
        NSString* job = [NSString stringWithUTF8String:title.c_str()];
        if (job) [operation setJobTitle:job];
    }
    // The sheet does not own its delegate, and the run outlives this call, so
    // the driver is held here until the operation has reported back. Asked to
    // run with no delegate and no selector, AppKit put nothing on screen at
    // all: the reader pressed Print and no panel came up.
    MdPrintPanelDriver* driver = [[MdPrintPanelDriver alloc] init];
    g_panelDriver = driver;
    [operation
        runOperationModalForWindow:window
                          delegate:driver
                    didRunSelector:
                        @selector(printOperationDidRun:success:contextInfo:)
                       contextInfo:nullptr];
    return true;
}

bool PrintToFile(const std::string& path, platform::MacRenderer& renderer,
                 const Document& document, const std::string& title,
                 PrintFileReport* report) {
    if (report) *report = PrintFileReport();
    if (path.empty()) return false;
    NSString* file = [NSString stringWithUTF8String:path.c_str()];
    if (!file) return false;
    NSPrintInfo* info = [[NSPrintInfo sharedPrintInfo] copy];
    [info setLeftMargin:0.0];
    [info setRightMargin:0.0];
    [info setTopMargin:0.0];
    [info setBottomMargin:0.0];
    [info setHorizontallyCentered:NO];
    [info setVerticallyCentered:NO];
    NSMutableDictionary* settings = [info dictionary];
    [settings setObject:NSPrintSaveJob forKey:NSPrintJobDisposition];
    [settings setObject:[NSURL fileURLWithPath:file]
                 forKey:NSPrintJobSavingURL];
    MdPrintView* view = [[MdPrintView alloc] initWithRenderer:&renderer
                                                     document:&document
                                                    printInfo:info];
    NSPrintOperation* operation =
        [NSPrintOperation printOperationWithView:view printInfo:info];
    [operation setShowsPrintPanel:NO];
    [operation setShowsProgressPanel:NO];
    if (!title.empty()) {
        NSString* job = [NSString stringWithUTF8String:title.c_str()];
        if (job) [operation setJobTitle:job];
    }
    // This is the printer's own path: AppKit asks the view for its page range,
    // cuts each page and calls drawRect for it, so a page that comes out blank
    // here is blank on paper too.
    const BOOL ran = [operation runOperation];
    CFURLRef url = CFURLCreateWithFileSystemPath(
        nullptr, (__bridge CFStringRef)file, kCFURLPOSIXPathStyle, false);
    if (!url) return ran == YES;
    CGPDFDocumentRef pdf = CGPDFDocumentCreateWithURL(url);
    CFRelease(url);
    if (!pdf) return ran == YES;
    const int pages = (int)CGPDFDocumentGetNumberOfPages(pdf);
    if (report) {
        report->pages = pages;
        for (int i = 0; i < 3 && i < pages; ++i) {
            report->ink[i] = InkOnPage(pdf, (size_t)i + 1);
        }
    }
    CGPDFDocumentRelease(pdf);
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
        PaintPage(pdf, page, layout::PlacementForBand(page, bands[i]),
                  layout::PageItems(*laid, bands[i]));
        CGContextRestoreGState(pdf);
        CGPDFContextEndPage(pdf);
    }
    CGPDFContextClose(pdf);
    CGContextRelease(pdf);
    if (pageCount) *pageCount = (int)bands.size();
    return true;
}

}  // namespace print_mac
