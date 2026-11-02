// The macOS content view.
//
// Two things live here: the ContentView, which holds the layout and the
// hit testing, and MdContentView, the NSView subclass that draws it and
// turns the events into edits.
//
// The drawing goes through the RenderTarget seam, so this file is the
// only place in the mac layer that talks to Core Graphics directly.

#include "content_view_mac.h"
#include "system_theme_mac.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>

#include "content_nsview_mac.h"
#include "navigation.h"
#include "cg_target_mac.h"
#include "text_engine_mac.h"
#include "image_draw_mac.h"
#include "zoommodel.h"

namespace mac {

namespace {

// The default width of the window's content area.
constexpr float kDefaultWidth = 900.0f;
constexpr float kDefaultHeight = 700.0f;

}  // namespace


// ---------------------------------------------------------------------------
// ContentView
// ---------------------------------------------------------------------------

ContentView::ContentView(App* app)
    : app_(app), engine_(nullptr), target_(nullptr) {
    engine_ = new CoreTextEngine();
    // The diagrams are measured with an engine of their own, so the
    // measurement of the document text stays where the hit tester
    // expects it.
    renderer_.SetDiagramTextEngine(new CoreTextEngine());
    renderer_.SetTextEngine(engine_);
    renderer_.SetTheme(BaseMetrics(), mac::SystemPalette());
    renderer_.SetStandaloneSvg(app_ && app_->IsStandaloneSvg());
    // The renderer reads the source when it normalizes a caret past a
    // marker, the same pointer the Windows cache holds.
    if (app_) renderer_.SetSourceText(&app_->sourceText());
    width_ = kDefaultWidth;
    height_ = kDefaultHeight;
    Relayout();
}

// The model keeps table cell text as UTF-32; an accessibility label
// wants UTF-8.
static std::string AxCellTextUtf8(const std::u32string& text) {
    std::string out;
    for (char32_t cp : text) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::vector<ContentView::AxTable> ContentView::AccessibilityTables() const {
    std::vector<AxTable> tables;
    const Document& doc = app_->GetDocument();
    if (doc.nodes.empty()) return tables;
    const std::vector<MacRenderer::TextRegion> regions =
        renderer_.TextRegions();
    const Selection& selection = app_->selection();
    const uint32_t caret =
        selection.Empty() ? selection.active.offset : selection.Start();

    for (const Node& node : doc.nodes) {
        if (node.block != BlockKind::Table) continue;
        const uint32_t tableStart = node.contentOffset;
        const uint32_t tableEnd = node.contentOffset + node.contentLength;
        // The cell regions of this table, in document order, which is
        // the order of the model's rows and cells.
        std::vector<const MacRenderer::TextRegion*> cellRegions;
        for (const MacRenderer::TextRegion& region : regions) {
            if (region.cell && region.srcStart >= tableStart &&
                region.srcEnd <= tableEnd) {
                cellRegions.push_back(&region);
            }
        }
        if (cellRegions.empty()) continue;

        AxTable table;
        size_t index = 0;
        int rowIndex = 0;
        for (const TableRow& row : node.rows) {
            int columnIndex = 0;
            for (const TableCell& cell : row.cells) {
                if (index >= cellRegions.size()) break;
                const MacRenderer::TextRegion& region =
                    *cellRegions[index++];
                AxCell ax;
                const std::vector<Rect> rects =
                    renderer_.RectsForRange(region.srcStart, region.srcEnd);
                if (rects.empty()) {
                    // An empty cell keeps its box addressable.
                    ax.x = region.trackX;
                    ax.y = region.top - scrollOffset_;
                    ax.width = (region.x + region.width) - region.trackX;
                    ax.height = region.emptyBandHeight;
                } else {
                    float minX = rects[0].x, minY = rects[0].y;
                    float maxX = rects[0].x + rects[0].width;
                    float maxY = rects[0].y + rects[0].height;
                    for (const Rect& rect : rects) {
                        if (rect.x < minX) minX = rect.x;
                        if (rect.y < minY) minY = rect.y;
                        if (rect.x + rect.width > maxX) maxX = rect.x + rect.width;
                        if (rect.y + rect.height > maxY) maxY = rect.y + rect.height;
                    }
                    ax.x = minX;
                    ax.y = minY - scrollOffset_;
                    ax.width = maxX - minX;
                    ax.height = maxY - minY;
                }
                ax.text = AxCellTextUtf8(cell.text);
                ax.row = rowIndex;
                ax.column = columnIndex;
                ax.header = rowIndex == 0;
                ax.caretCell = caret >= region.srcStart && caret <= region.srcEnd;
                table.cells.push_back(ax);
                ++columnIndex;
            }
            ++rowIndex;
        }
        if (table.cells.empty()) continue;
        float minX = table.cells[0].x, minY = table.cells[0].y;
        float maxX = table.cells[0].x + table.cells[0].width;
        float maxY = table.cells[0].y + table.cells[0].height;
        for (const AxCell& cell : table.cells) {
            if (cell.x < minX) minX = cell.x;
            if (cell.y < minY) minY = cell.y;
            if (cell.x + cell.width > maxX) maxX = cell.x + cell.width;
            if (cell.y + cell.height > maxY) maxY = cell.y + cell.height;
        }
        table.x = minX;
        table.y = minY;
        table.width = maxX - minX;
        table.height = maxY - minY;
        tables.push_back(table);
    }
    return tables;
}

ContentView::~ContentView() {
    delete engine_;
    engine_ = nullptr;
}

void ContentView::OnDocumentChanged() {
    // The document may have changed from a markdown file to a
    // standalone SVG file, or the other way round.
    renderer_.SetStandaloneSvg(app_ && app_->IsStandaloneSvg());

    Relayout();
    Invalidate();
}

void ContentView::Invalidate() {
    // The window asks the view to redraw. The view itself does not know
    // about the window, so the platform layer polls this through the
    // AppShell seam. On the mac side the NSView invalidates itself.
}

void ContentView::SetEditing(bool on) {
    // The layout follows the mode: reading draws the blank line next
    // to a list as a thin strip, editing keeps its full line, so the
    // document is laid out again.
    renderer_.SetEditing(on);
    Relayout();
    Invalidate();
}

void ContentView::RefreshTheme() {
    renderer_.SetTheme(BaseMetrics(), mac::SystemPalette());
    // The laid-out runs carry the palette's colours inside them, so the
    // document is laid out again. Without that a palette switch kept
    // the previous appearance baked into the text: dark mode drew the
    // light appearance's dark ink on the dark page, which the launch
    // smoke photographed.
    Relayout();
    Invalidate();
}

void ContentView::SetSize(float width, float height) {
    if (width_ == width && height_ == height) return;
    width_ = width;
    height_ = height;
    Relayout();
}

void ContentView::SetScrollOffset(float offset) {
    if (offset < 0.0f) offset = 0.0f;
    scrollOffset_ = offset;
}

void ContentView::Relayout() {
    if (!app_) return;

    // The renderer lays the document out at the full view width: it
    // takes the theme padding off, caps the reading width and centers
    // the column, the same arithmetic the Windows renderer uses.
    documentHeight_ = renderer_.Measure(app_->GetDocument(), width_);
    contentWidth_ = renderer_.contentWidth();

    // The platform layer follows: its document view takes the
    // content's size, so the scrollers span the whole document.
    if (layoutChanged_) layoutChanged_();
}

void ContentView::SetWelcomeFiles(const std::vector<RecentFile>& files) {
    welcome_.SetRecentFiles(files);
}

int ContentView::WelcomeCardAt(float x, float y) const {
    return welcome_.HitTest(x, y);
}

std::string ContentView::WelcomePathForCard(int index) const {
    return welcome_.GetPath(index);
}

void ContentView::Draw(RenderTarget* target) {
    // Without a document the welcome screen takes its place.
    if (app_ && app_->WelcomeMode()) {
        welcome_.Layout(width_, height_);
        welcome_.Render(target, width_, height_, hoverCard_);
        target_ = nullptr;
        return;
    }
    if (!target || !app_) return;

    target_ = target;
    renderer_.SetRenderTarget(target);

    // The selection goes under the text, so it is drawn first. The
    // rectangles are in document coordinates, the space this view
    // draws in now that the scroll view scrolls it.
    const Selection& selection = app_->selection();
    if (!selection.Empty()) {
        const uint32_t from = selection.Start();
        const uint32_t to = selection.End();
        for (const Rect& rect : renderer_.SelectionRects(from, to)) {
            target->FillRect(rect, SelectionColor());
        }
    }

    renderer_.Render(app_->GetDocument(), 0.0f);

    // The caret goes on top of the text, but not over a selection:
    // the selection is the marker of where the next edit lands, and
    // read mode shows no caret at all.
    if (caretVisible_ && app_->Editing() && selection.Empty()) {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        const uint32_t caret = selection.active.offset;
        if (renderer_.OffsetToRect(caret, &x, &y, &width, &height)) {
            Rect drawn;
            drawn.x = x;
            drawn.y = y;
            drawn.width = 1.0f;
            drawn.height = height > 0.0f ? height : CaretHeight();
            target->FillRect(drawn, CaretColor());
        }
    }

    // A text drag paints its drop caret on top, so the user sees
    // where the moved text lands (drag and drop 3).
    if (textDragActive_ && textDragTarget_ != UINT32_MAX) {
        float dropX = 0.0f;
        float dropY = 0.0f;
        float dropW = 0.0f;
        float dropH = 0.0f;
        const uint32_t dropOffset =
            renderer_.NormalizeToRenderedCaret(textDragTarget_);
        if (dropOffset != UINT32_MAX &&
            renderer_.OffsetToRect(dropOffset, &dropX, &dropY,
                                   &dropW, &dropH)) {
            Rect drawn;
            drawn.x = dropX;
            drawn.y = dropY;
            drawn.width = 1.0f;
            drawn.height = dropH > 0.0f ? dropH : CaretHeight();
            target->FillRect(drawn, CaretColor());
        }
    }

    target_ = nullptr;
}

void ContentView::ScrollToOffset(uint32_t offset) {
    // A third of the way down the view, so the reader sees the text
    // around it.
    ScrollToOffsetPlaced(offset, height_ * 0.35f);
}

void ContentView::ScrollToOffsetPlaced(uint32_t offset, float drop) {
    float y = 0.0f;
    if (!renderer_.YForOffset(offset, y)) return;

    // The scrolling itself belongs to the platform layer, so the
    // scrollers follow along.
    const float target = y - drop;
    if (scrollTo_) scrollTo_(target < 0.0f ? 0.0f : target);
}

float ContentView::zoom() const {
    return renderer_.zoom();
}

void ContentView::NotifyScrollChanged() {
    if (scrollChanged_) scrollChanged_();
}

bool ContentView::CaretIsOnScreen() const {
    float y = 0.0f;
    if (!renderer_.YForOffset(app_->selection().Start(), y)) return false;
    return y >= scrollOffset_ - 1.0f &&
           y <= scrollOffset_ + viewHeight() + 1.0f;
}

uint32_t ContentView::ViewportProbeOffset() const {
    // The reading line, a quarter down the viewport, at the middle
    // of the text column (outline spec 19 and 21).
    return HitTestPoint(width_ * 0.5f, scrollOffset_ + viewHeight() * 0.25f);
}

uint32_t ContentView::HitTestPoint(float x, float y) const {
    // The point arrives in document coordinates, because the scroll
    // view scrolls the whole view.
    uint32_t offset = 0;
    if (!renderer_.PointToOffset(x, y, &offset)) {
        return UINT32_MAX;
    }
    return offset;
}

uint32_t ContentView::NormalizeToRenderedCaret(uint32_t offset) const {
    return renderer_.NormalizeToRenderedCaret(offset);
}

bool ContentView::OffsetToRect(uint32_t offset, float* outX, float* outY,
                               float* outWidth, float* outHeight) const {
    return renderer_.OffsetToRect(offset, outX, outY, outWidth, outHeight);
}

void ContentView::PlaceCaretAt(float x, float y) {
    if (!app_) return;

    // A point on drawn text is a caret: the renderer decides the
    // character, and the caret never lands inside a marker.
    uint32_t offset = 0;
    if (renderer_.PointToOffset(x, y, &offset)) {
        app_->SetCaret(offset);
        return;
    }

    // Below the last of the text the caret goes to the end of the
    // document.
    if (y >= renderer_.TextBottom() && app_->buffer().Length() > 0) {
        app_->SetCaret(app_->buffer().Length());
        return;
    }

    // A click that misses the text entirely, a pointer in the margin,
    // selects the line it is level with, the way Word does it.
    uint32_t lineStart = 0;
    uint32_t lineEnd = 0;
    if (renderer_.LineRangeAtPoint(x, y, &lineStart, &lineEnd)) {
        app_->SetSelection(lineStart, lineEnd);
        return;
    }

    app_->SetCaret(0);
}

void ContentView::SelectWordOrParagraphAt(float x, float y, bool paragraph) {
    if (!app_) return;

    uint32_t offset = 0;
    if (!renderer_.PointToOffset(x, y, &offset)) {
        // A click in the margin has no word or paragraph either, so it
        // keeps the Word-like line selection.
        uint32_t lineStart = 0;
        uint32_t lineEnd = 0;
        if (renderer_.LineRangeAtPoint(x, y, &lineStart, &lineEnd)) {
            app_->SetSelection(lineStart, lineEnd);
        }
        return;
    }

    if (paragraph) {
        app_->SelectParagraphAt(offset);
    } else {
        app_->SelectWordAt(offset);
    }
}

void ContentView::ExtendSelectionTo(float x, float y, int mode) {
    if (!app_) return;

    uint32_t offset = 0;
    if (!renderer_.PointToOffset(x, y, &offset)) {
        // A pointer below the document extends to its end; a pointer
        // that left the text keeps the selection's edge.
        if (y >= renderer_.TextBottom()) {
            offset = app_->buffer().Length();
        } else {
            return;
        }
    }

    const uint32_t anchor = app_->selection().anchor.offset;
    if (mode == 1) {
        // A drag that started with a double click moves the edge by
        // whole words.
        uint32_t spanStart = 0;
        uint32_t spanEnd = 0;
        if (app_->WordSpanForOffset(offset, &spanStart, &spanEnd)) {
            offset = offset <= anchor ? spanStart : spanEnd;
        }
    } else if (mode == 2) {
        // A drag that started with a triple click moves the edge by
        // whole paragraphs.
        uint32_t paraStart = 0;
        uint32_t paraEnd = 0;
        if (app_->ParagraphRangeForOffset(offset, &paraStart, &paraEnd)) {
            offset = offset <= anchor ? paraStart : paraEnd;
        }
    }

    // A drag that began inside a table cell never leaves that cell:
    // the fixed end decides and the dragged end is clamped to the
    // cell's walls, so a text selection cannot escape the way a click
    // cannot (table spec: mouse drag over cells).
    TableCellRef dragCell;
    if (TableCellAtOffset(app_->GetDocument(), app_->buffer().Text(),
                          anchor, &dragCell) && !dragCell.separatorRow) {
        const uint32_t textLen =
            static_cast<uint32_t>(app_->buffer().Text().size());
        if (offset < dragCell.srcOffset) {
            offset = std::min(dragCell.srcOffset, textLen);
        } else if (offset > dragCell.srcEnd) {
            offset = std::min(dragCell.srcEnd, textLen);
        }
    }

    app_->SetSelection(anchor, offset);
}

uint32_t ContentView::StepGrapheme(uint32_t offset, bool left) const {
    return renderer_.StepGrapheme(offset, left);
}

uint32_t ContentView::StepWord(uint32_t offset, bool left) const {
    return renderer_.StepWord(offset, left);
}

uint32_t ContentView::LineBoundary(uint32_t offset, bool end) const {
    return renderer_.LineBoundary(offset, end);
}

Color ContentView::SelectionColor() const {
    // The selection wash follows the user's accent colour and the
    // appearance: the system's selected-content colour at the weight
    // the fixed blue carried, its alpha light so the text under it
    // stays readable.
    const ThemeColor c = mac::SystemPalette().selectionBg;
    Color color;
    color.r = c.r;
    color.g = c.g;
    color.b = c.b;
    color.a = 0.35f;
    return color;
}

Color ContentView::CaretColor() const {
    // The caret follows the label colour, so it stays visible in
    // both appearances: a fixed near-black bar disappeared over the
    // dark paper.
    const ThemeColor c = mac::SystemPalette().textPrimary;
    Color color;
    color.r = c.r;
    color.g = c.g;
    color.b = c.b;
    color.a = 1.0f;
    return color;
}

void ContentView::SetMarkers(const std::vector<TextMatch>* markers) {
    renderer_.SetMarkers(markers);
    Invalidate();
}

void ContentView::SetSearchMatches(const std::vector<TextMatch>* matches,
                                   int currentIndex) {
    renderer_.SetSearchMatches(matches, currentIndex);
    Invalidate();
}

void ContentView::SetCaretVisible(bool visible) {
    if (caretVisible_ == visible) {
        return;
    }
    caretVisible_ = visible;
    Invalidate();
}

void ContentView::SetWrap(bool wrap) {
    renderer_.SetWrap(wrap);
    Relayout();
}

void ContentView::SetSourceMode(bool on) {
    renderer_.SetSourceMode(on);
    Relayout();
}

bool ContentView::ArmTextDrag(float x, float y) {
    if (!app_ || app_->selection().Empty()) return false;
    const uint32_t offset = HitTestPoint(x, y);
    if (offset == UINT32_MAX) return false;
    const uint32_t start = app_->selection().Start();
    const uint32_t end = app_->selection().End();
    if (offset < start || offset >= end) return false;
    textDragArmed_ = true;
    textDragActive_ = false;
    textDragDownX_ = x;
    textDragDownY_ = y;
    textDragTarget_ = UINT32_MAX;
    return true;
}

void ContentView::DragTextTo(float x, float y) {
    if (!textDragArmed_) return;
    if (!textDragActive_) {
        // The system drag threshold, the same four points the
        // Windows build reads from SM_CXDRAG: below it the press
        // is still a plain click (drag and drop 1).
        if (std::abs(x - textDragDownX_) < 4.0f &&
            std::abs(y - textDragDownY_) < 4.0f) {
            return;
        }
        textDragActive_ = true;
    }
    const uint32_t offset = HitTestPoint(x, y);
    if (offset != UINT32_MAX) textDragTarget_ = offset;
}

void ContentView::CancelTextDrag() {
    textDragArmed_ = false;
    textDragActive_ = false;
    textDragTarget_ = UINT32_MAX;
}

void ContentView::SetContentWidthMode(int mode) {
    renderer_.SetContentWidthMode(mode);
    Relayout();
}

void ContentView::SetZoom(float zoom) {
    renderer_.SetZoom(zoom);
    Relayout();
}

float ContentView::CaretHeight() const {
    if (!app_) return 16.0f;
    const uint32_t offset = app_->selection().active.offset;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (OffsetToRect(offset, &x, &y, &width, &height) && height > 0.0f) {
        return height;
    }
    return engine_->LineHeight(FontSpec{}) * 1.2f;
}

}  // namespace mac

// ---------------------------------------------------------------------------
// The NSView
// ---------------------------------------------------------------------------

// One wheel step for the command zoom: a mouse notch counts for this
// much, and a trackpad's finer deltas add up to it.
static const CGFloat kZoomWheelUnitsPerStep = 120.0;

// How much a pinch has to add up to before the zoom moves a step.
static const CGFloat kPinchStepMagnification = 0.1;

// The content view of the window.
//
// The view draws the document, shows the caret and the selection, and
// turns the mouse and the keyboard into edits through the shared edit
// controller.
@implementation MdContentView {
    mac::ContentView* content_;
    mac::App* app_;
    // The timer that blinks the caret.
    NSTimer* blinkTimer_;
    // The area that tells the view the mouse moved over it.
    NSTrackingArea* trackingArea_;
    // The x the vertical moves aim for, in document coordinates. A
    // fresh click or a horizontal move drops it, the way the Windows
    // window keeps its own desired x.
    float desiredX_;
    // The unit a drag moves by: 0 is a character, 1 a word, 2 a
    // paragraph. The second and third click of a multiple click set it.
    int dragMode_;
    // The timer that keeps scrolling while a drag pulls past an edge.
    NSTimer* autoscrollTimer_;
    // The last point a drag reached, which the autoscroll extends to.
    NSPoint lastDragPoint_;
    // The wheel deltas the command zoom has not stepped on yet.
    CGFloat wheelZoomAcc_;
    // The pinch magnification the zoom has not stepped on yet.
    CGFloat pinchAcc_;
    // Told every time the zoom changes, so the window can save it.
    void (^zoomChangedHandler_)(float);
    // Told when the palette was resolved again, so the window can
    // refresh the colours of the views beside the document.
    void (^themeChangedHandler_)(void);
}

- (instancetype)initWithContent:(mac::ContentView*)content app:(mac::App*)app {
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 700)];
    if (self) {
        content_ = content;
        app_ = app;
        desiredX_ = -1.0f;
        dragMode_ = 0;
        lastDragPoint_ = NSMakePoint(0.0, 0.0);

        // The view asks the platform layer to scroll and to follow the
        // content size, because the scroll view owns the place and its
        // scrollers.
        __weak MdContentView* weakSelf = self;
        content_->SetScrollToHandler([weakSelf](float y) {
            [weakSelf scrollToDocumentY:y];
        });
        content_->SetLayoutChangedHandler([weakSelf] {
            [weakSelf syncFrameToContent];
        });
    }
    return self;
}

// The view joined the scroll view: the clip view reports every scroll
// and every size change from here on.
- (void)viewDidMoveToSuperview {
    [super viewDidMoveToSuperview];
    NSClipView* clip = [self enclosingScrollView].contentView;
    if (!clip) return;

    [clip setPostsFrameChangedNotifications:YES];
    [clip setPostsBoundsChangedNotifications:YES];
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center addObserver:self
               selector:@selector(clipChanged:)
                   name:NSViewFrameDidChangeNotification
                 object:clip];
    [center addObserver:self
               selector:@selector(clipChanged:)
                   name:NSViewBoundsDidChangeNotification
                 object:clip];
    [self clipChanged:nil];

    // The startup palette resolution lands under the view's own
    // appearance: the first one ran without it.
    [self resolvePalette];
}

// The palette belongs to the appearance: resolve it again whenever the
// system or the display changes it, and at startup. The window around
// the document is told, so it can refresh its own colours.
- (void)resolvePalette {
    [self.effectiveAppearance performAsCurrentDrawingAppearance:^{
        content_->RefreshTheme();
    }];
    if (themeChangedHandler_) {
        themeChangedHandler_();
    }
    [self setNeedsDisplay:YES];
}

- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    [self resolvePalette];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

// The clip view scrolled or changed size. The content lays itself out
// for the viewport, and the document view takes the content's size, so
// the scrollers describe the whole document at the current zoom
// (zoom guideline 4).
- (void)clipChanged:(NSNotification*)notification {
    (void)notification;
    NSClipView* clip = [self enclosingScrollView].contentView;
    if (!clip || !content_) return;

    const NSRect clipBounds = [clip bounds];
    content_->SetSize(static_cast<float>(clipBounds.size.width),
                      static_cast<float>(clipBounds.size.height));
    content_->SetScrollOffset(static_cast<float>(clipBounds.origin.y));
    content_->NotifyScrollChanged();
    [self setNeedsDisplay:YES];
}

// The document view takes the content's size. A smaller frame can
// leave the place past the new edge (a resize or a zoom out), so the
// place is clamped and the position survives (zoom guideline 6.15).
- (void)syncFrameToContent {
    if (!content_) return;
    NSClipView* clip = [self enclosingScrollView].contentView;
    if (!clip) return;

    const NSSize clipSize = [clip bounds].size;
    NSSize size = clipSize;
    if (app_ && !app_->WelcomeMode()) {
        const float width = content_->ContentWidth();
        const float height = content_->DocumentHeight();
        if (width > size.width) size.width = width;
        if (height > size.height) size.height = height;
    }
    if (!NSEqualSizes(size, [self frame].size)) {
        [self setFrameSize:size];
    }

    [clip scrollToPoint:[clip constrainBoundsRect:[clip bounds]].origin];
}

// Scrolls the clip view to the given place, clamped to the content.
- (void)scrollToDocumentPoint:(NSPoint)point {
    NSClipView* clip = [self enclosingScrollView].contentView;
    if (!clip) return;

    NSRect bounds = [clip bounds];
    bounds.origin = point;
    [clip scrollToPoint:[clip constrainBoundsRect:bounds].origin];
}

- (void)scrollToDocumentY:(float)y {
    NSClipView* clip = [self enclosingScrollView].contentView;
    if (!clip) return;

    NSRect bounds = [clip bounds];
    bounds.origin.y = y;
    [clip scrollToPoint:[clip constrainBoundsRect:bounds].origin];
}

// The zoom the document is at.
- (float)zoomFactor {
    return content_ ? content_->zoom() : 1.0f;
}

- (void)setThemeChangedHandler:(void (^)(void))handler {
    themeChangedHandler_ = handler;
}

- (void)setZoomChangedHandler:(void (^)(float))handler {
    zoomChangedHandler_ = handler;
}

// One press of + or -, anchored where the gesture happened.
- (void)zoomStepAt:(NSPoint)anchorDoc increase:(BOOL)increase {
    const float next = zoom::Step([self zoomFactor], increase ? true : false);
    if (next == [self zoomFactor]) return;
    [self applyZoom:next anchorDoc:anchorDoc];
}

// The menu and the buttons: the middle of the viewport is the anchor.
- (void)zoomTo:(float)factor {
    if (!content_) return;
    factor = zoom::Clamp(factor);
    if (factor == [self zoomFactor]) return;

    const NSRect visible = [self visibleRect];
    [self applyZoom:factor
          anchorDoc:NSMakePoint(NSMidX(visible), NSMidY(visible))];
}

// Applies the zoom and puts the anchor point back where it was, the
// arithmetic the Windows window uses: the content point under the
// anchor stays under it (zoom guideline 1.3).
- (void)applyZoom:(float)factor anchorDoc:(NSPoint)anchor {
    NSClipView* clip = [self enclosingScrollView].contentView;
    if (!clip) return;

    const NSPoint origin = [clip bounds].origin;
    const float oldZoom = [self zoomFactor];
    const float scale = oldZoom > 0.0f ? factor / oldZoom : 1.0f;
    // Where the anchor sat inside the viewport, in points.
    const CGFloat insetX = anchor.x - origin.x;
    const CGFloat insetY = anchor.y - origin.y;

    content_->SetZoom(factor);

    // The frame follows the new content size through the layout hook,
    // so the scroll below lands on settled geometry.
    NSRect bounds = [clip bounds];
    bounds.origin = NSMakePoint(anchor.x * scale - insetX,
                                anchor.y * scale - insetY);
    [clip scrollToPoint:[clip constrainBoundsRect:bounds].origin];
    [self setNeedsDisplay:YES];

    if (zoomChangedHandler_) {
        zoomChangedHandler_(factor);
    }
}

- (BOOL)acceptsFirstResponder {
    // The document view takes the keyboard: typing and the key
    // commands arrive as the text input messages below.
    return YES;
}

- (BOOL)canBecomeKeyView {
    return YES;
}

- (BOOL)isFlipped {
    // The renderer lays the document out with y growing downwards, so
    // the view is flipped. The whole drawing code then works in the
    // same direction as the rest of the app.
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!content_) return;

    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    if (!context) return;

    // The view is flipped, so the origin is already at the top left and
    // the y axis already points down, which is the space the renderer
    // lays the document out in. Flipping the context again here turned
    // the whole document over.
    mac::CoreGraphicsTarget target(context);
    content_->Draw(&target);
}

- (void)mouseDown:(NSEvent*)event {
    if (!content_) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
                                    fromView:nil];

    // A click on a card of the welcome screen opens that file.
    if (app_ && app_->WelcomeMode()) {
        const int card = content_->WelcomeCardAt(static_cast<float>(point.x),
                                                 static_cast<float>(point.y));
        if (card >= 0) {
            app_->RequestOpenFile(content_->WelcomePathForCard(card));
            return;
        }
    }

    // The click takes the keyboard, so typing goes to the document
    // without a detour through the menu bar.
    [self.window makeFirstResponder:self];
    desiredX_ = -1.0f;
    [self caretMoved];

    const NSInteger clicks = [event clickCount];
    const NSEventModifierFlags flags =
        [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;

    // A modified click on a link activates it (link activation
    // 1-6): an internal anchor moves the viewport, an allowlisted
    // external URL opens in the browser, and anything else is
    // consumed without an external action. The plain click keeps
    // placing the caret, so a link never blocks editing.
    if (clicks == 1 &&
        (flags & (NSEventModifierFlagCommand |
                  NSEventModifierFlagControl)) != 0 &&
        (flags & NSEventModifierFlagShift) == 0) {
        const uint32_t offset = content_->HitTestPoint(
            static_cast<float>(point.x), static_cast<float>(point.y));
        if (offset != UINT32_MAX) {
            const std::string url = app_->LinkAtOffset(offset);
            if (!url.empty()) {
                uint32_t heading = 0;
                if (app_->HeadingOffsetForAnchor(url, &heading)) {
                    content_->ScrollToOffset(heading);
                } else {
                    app_->OpenExternalUrl(url);
                }
                return;
            }
        }
    }

    // A shift click extends the selection from the anchor instead of
    // placing a caret, the way the Windows window works.
    if (clicks == 1 && (flags & NSEventModifierFlagShift) != 0) {
        dragMode_ = 0;
        content_->ExtendSelectionTo(static_cast<float>(point.x),
                                     static_cast<float>(point.y), dragMode_);
        [self setNeedsDisplay:YES];
        return;
    }

    // A double click takes the word and a triple click takes the
    // paragraph, the way the Windows app does it. A drag that follows
    // keeps moving the edge by the same unit.
    if (clicks >= 2) {
        dragMode_ = clicks >= 3 ? 2 : 1;
        content_->SelectWordOrParagraphAt(static_cast<float>(point.x),
                                           static_cast<float>(point.y),
                                           clicks >= 3);
        [self setNeedsDisplay:YES];
        return;
    }

    dragMode_ = 0;

    // A press inside the current selection arms a text drag
    // instead of placing a new selection (drag and drop 1); the
    // release without a drag collapses the selection then.
    if (content_->ArmTextDrag(static_cast<float>(point.x),
                              static_cast<float>(point.y))) {
        [self setNeedsDisplay:YES];
        return;
    }

    content_->PlaceCaretAt(static_cast<float>(point.x),
                           static_cast<float>(point.y));
    [self setNeedsDisplay:YES];
}

// The caret blinks while the document has the focus. The timer flips
// the caret, and the view redraws itself.
- (void)startBlinking {
    if (blinkTimer_) return;
    blinkTimer_ = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                  target:self
                                                selector:@selector(blink:)
                                                userInfo:nil
                                                 repeats:YES];
}

- (void)stopBlinking {
    [blinkTimer_ invalidate];
    blinkTimer_ = nil;
}

// The QA walk parks the caret off and stops its blink: a still
// capture must not depend on the blink phase, and the two platforms'
// pictures are compared.
- (void)qaParkCaret {
    [self stopBlinking];
    if (content_) content_->SetCaretVisible(false);
    [self setNeedsDisplay:YES];
}

- (void)blink:(NSTimer*)timer {
    (void)timer;
    if (!content_) return;
    content_->SetCaretVisible(!content_->caretVisible());
    // The flip only lands on screen through a redraw: the model's
    // Invalidate is a no-op on this platform, because the view owns
    // the drawing.
    [self setNeedsDisplay:YES];
}

// A fresh edit or a move shows the caret again and restarts the blink,
// so the caret never stays hidden right after typing.
- (void)resetCaretBlink {
    if (content_) content_->SetCaretVisible(true);
    if (blinkTimer_) {
        [blinkTimer_ invalidate];
        blinkTimer_ = nil;
    }
    if ([self.window firstResponder] == self) {
        [self startBlinking];
    }
}

// The caret moved or the document changed: the blink restarts, the
// caret is brought back into view and the view redraws.
- (void)caretMoved {
    [self resetCaretBlink];
    [self scrollCaretIntoView];
    [self setNeedsDisplay:YES];
}

// An edit replaced the context the remembered column stood for, so
// the vertical moves start over from the new caret (navigation rule
// 8), and the view double-blinks like after a move.
- (void)documentEdited {
    desiredX_ = -1.0f;
    [self resetCaretBlink];
    [self scrollCaretIntoView];
    [self setNeedsDisplay:YES];
}

// Keeps the caret inside the viewport, with a small margin, the way
// the Windows window scrolls it into view after every edit.
- (void)scrollCaretIntoView {
    if (!content_ || !app_) return;

    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (!content_->OffsetToRect(app_->selection().active.offset, &x, &y,
                                &width, &height)) {
        return;
    }
    if (height <= 0.0f) height = 16.0f;

    const NSRect visible = [self visibleRect];
    const CGFloat margin = 8.0;
    NSPoint target = visible.origin;

    if (y - margin < NSMinY(visible)) {
        target.y = y - margin;
    } else if (y + height + margin > NSMaxY(visible)) {
        target.y = y + height + margin - visible.size.height;
    }

    if (x - margin < NSMinX(visible)) {
        target.x = x - margin;
    } else if (x + width + margin > NSMaxX(visible)) {
        target.x = x + width + margin - visible.size.width;
    }

    if (!NSEqualPoints(target, visible.origin)) {
        [self scrollToDocumentPoint:target];
    }
}

- (BOOL)becomeFirstResponder {
    const BOOL result = [super becomeFirstResponder];
    if (content_) content_->SetCaretVisible(true);
    [self startBlinking];
    [self setNeedsDisplay:YES];
    return result;
}

- (BOOL)resignFirstResponder {
    [self stopBlinking];
    // The caret is hidden while the view is not focused, the way the
    // Windows window hides it.
    if (content_) content_->SetCaretVisible(false);
    [self setNeedsDisplay:YES];
    return [super resignFirstResponder];
}

- (void)rightMouseDown:(NSEvent*)event {
    if (!content_) return;

    // The right click places the caret first, so the menu works on the
    // text under the pointer.
    const NSPoint point = [self convertPoint:[event locationInWindow]
                                    fromView:nil];
    content_->PlaceCaretAt(static_cast<float>(point.x),
                           static_cast<float>(point.y));
    [self setNeedsDisplay:YES];

    mac::App* app = content_->app();
    id target = [NSApp delegate];
    if (!app || !target) return;

    NSMenu* menu = [[NSMenu alloc] init];
    const bool hasSelection = app->HasSelection();

    NSMenuItem* undo = [menu addItemWithTitle:@"Undo"
                                        action:@selector(undoEdit:)
                                 keyEquivalent:@""];
    undo.target = target;
    undo.enabled = app->undo().CanUndo();

    NSMenuItem* redo = [menu addItemWithTitle:@"Redo"
                                        action:@selector(redoEdit:)
                                 keyEquivalent:@""];
    redo.target = target;
    redo.enabled = app->undo().CanRedo();

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* cut = [menu addItemWithTitle:@"Cut"
                                       action:@selector(cutSelection:)
                                keyEquivalent:@""];
    cut.target = target;
    cut.enabled = hasSelection;

    NSMenuItem* copy = [menu addItemWithTitle:@"Copy"
                                        action:@selector(copySelection:)
                                 keyEquivalent:@""];
    copy.target = target;
    copy.enabled = hasSelection;

    NSMenuItem* paste = [menu addItemWithTitle:@"Paste"
                                         action:@selector(pasteSelection:)
                                  keyEquivalent:@""];
    paste.target = target;

    NSMenuItem* selectAll = [menu addItemWithTitle:@"Select All"
                                             action:@selector(selectAll:)
                                      keyEquivalent:@""];
    selectAll.target = target;

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* bold = [menu addItemWithTitle:@"Bold"
                                        action:@selector(toggleBold:)
                                 keyEquivalent:@""];
    bold.target = target;

    NSMenuItem* italic = [menu addItemWithTitle:@"Italic"
                                          action:@selector(toggleItalic:)
                                   keyEquivalent:@""];
    italic.target = target;

    NSMenuItem* code = [menu addItemWithTitle:@"Code"
                                        action:@selector(toggleCode:)
                                 keyEquivalent:@""];
    code.target = target;

    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
}

// The mouse moves are only sent when the view asks for them.
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (trackingArea_) {
        [self removeTrackingArea:trackingArea_];
        trackingArea_ = nil;
    }
    trackingArea_ = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow
               owner:self
            userInfo:nil];
    [self addTrackingArea:trackingArea_];
}

- (void)mouseMoved:(NSEvent*)event {
    if (!content_ || !app_ || !app_->WelcomeMode()) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
                                    fromView:nil];
    const int card = content_->WelcomeCardAt(static_cast<float>(point.x),
                                             static_cast<float>(point.y));
    if (card == content_->HoveredWelcomeCard()) return;
    content_->SetHoveredWelcomeCard(card);
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event {
    if (!content_) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
                                    fromView:nil];
    lastDragPoint_ = point;

    // An armed text drag moves the drop target instead of the
    // selection (drag and drop 1-3).
    if (content_->textDragArmed() || content_->textDragActive()) {
        content_->DragTextTo(static_cast<float>(point.x),
                             static_cast<float>(point.y));
        [self updateAutoscroll];
        [self setNeedsDisplay:YES];
        return;
    }

    content_->ExtendSelectionTo(static_cast<float>(point.x),
                                 static_cast<float>(point.y), dragMode_);
    [self updateAutoscroll];
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event {
    [self stopAutoscroll];
    if (content_->textDragArmed() || content_->textDragActive()) {
        const bool dragged = content_->textDragActive();
        const uint32_t drop = content_->TextDragDropOffset();
        content_->CancelTextDrag();
        if (dragged) {
            // The drop is one undo entry, and the moved text
            // stays selected (drag and drop 3-4).
            if (drop != UINT32_MAX) app_->MoveSelectedText(drop);
            [self caretMoved];
        } else {
            // No drag: the release collapses the selection at
            // the normalized click position (drag and drop 1).
            const NSPoint point = [self convertPoint:[event locationInWindow]
                                            fromView:nil];
            content_->PlaceCaretAt(static_cast<float>(point.x),
                                   static_cast<float>(point.y));
        }
        [self setNeedsDisplay:YES];
        return;
    }
    (void)event;
    [self setNeedsDisplay:YES];
}

// The autoscroll: while a drag pulls past the top or the bottom of the
// view, a timer keeps scrolling and the selection keeps following the
// pointer, so a long drag can reach past the window.
- (void)updateAutoscroll {
    const NSRect visible = [self visibleRect];
    const BOOL outside = lastDragPoint_.y < NSMinY(visible) ||
                         lastDragPoint_.y > NSMaxY(visible);
    if (outside && !autoscrollTimer_) {
        autoscrollTimer_ =
            [NSTimer scheduledTimerWithTimeInterval:0.05
                                             target:self
                                           selector:@selector(autoscrollStep:)
                                           userInfo:nil
                                            repeats:YES];
    } else if (!outside && autoscrollTimer_) {
        [self stopAutoscroll];
    }
}

- (void)stopAutoscroll {
    [autoscrollTimer_ invalidate];
    autoscrollTimer_ = nil;
}

- (void)autoscrollStep:(NSTimer*)timer {
    (void)timer;
    if (!content_) return;
    // The step grows with how far the pointer is held past the edge,
    // with the Windows floor and ceiling: a drag a little outside
    // scrolls slowly, a drag far outside runs at the ceiling (spec:
    // drag selection with autoscroll).
    const NSRect visible = [self visibleRect];
    const bool above = lastDragPoint_.y < NSMinY(visible);
    const float outsideBy = above
        ? static_cast<float>(NSMinY(visible) - lastDragPoint_.y)
        : static_cast<float>(lastDragPoint_.y - NSMaxY(visible));
    const float step = std::min(96.0f, std::max(24.0f, outsideBy * 0.5f));
    const float direction = above ? -1.0f : 1.0f;
    NSClipView* clip = [self enclosingScrollView].contentView;
    if (clip) {
        NSRect bounds = [clip bounds];
        bounds.origin.y += direction * step;
        [clip scrollToPoint:[clip constrainBoundsRect:bounds].origin];
    }
    content_->ExtendSelectionTo(static_cast<float>(lastDragPoint_.x),
                                 static_cast<float>(lastDragPoint_.y),
                                 dragMode_);
    [self setNeedsDisplay:YES];
}

// The wheel with the command key zooms, with deltas that accumulate so
// a notched mouse and a smooth trackpad step alike (zoom guideline
// 1.6). Everything else is the scroll view's: the plain wheel scrolls,
// shift and the wheel scroll sideways, and the scrollers follow.
- (void)scrollWheel:(NSEvent*)event {
    if (!content_) {
        [super scrollWheel:event];
        return;
    }

    const NSEventModifierFlags flags =
        [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
    if ((flags & NSEventModifierFlagCommand) != 0) {
        const CGFloat raw = [event scrollingDeltaY];
        wheelZoomAcc_ += [event hasPreciseScrollingDeltas] ? raw
                                                           : raw * 120.0;
        const NSPoint anchor = [self convertPoint:[event locationInWindow]
                                         fromView:nil];
        while (wheelZoomAcc_ >= kZoomWheelUnitsPerStep) {
            wheelZoomAcc_ -= kZoomWheelUnitsPerStep;
            [self zoomStepAt:anchor increase:YES];
        }
        while (wheelZoomAcc_ <= -kZoomWheelUnitsPerStep) {
            wheelZoomAcc_ += kZoomWheelUnitsPerStep;
            [self zoomStepAt:anchor increase:NO];
        }
        return;
    }

    [super scrollWheel:event];
}

// A trackpad pinch zooms around the gesture, the way a magnifying
// gesture is expected to (zoom guideline 1.6).
- (void)magnifyWithEvent:(NSEvent*)event {
    if (!content_) return;

    pinchAcc_ += [event magnification];
    const NSPoint anchor = [self convertPoint:[event locationInWindow]
                                     fromView:nil];
    while (pinchAcc_ >= kPinchStepMagnification) {
        pinchAcc_ -= kPinchStepMagnification;
        [self zoomStepAt:anchor increase:YES];
    }
    while (pinchAcc_ <= -kPinchStepMagnification) {
        pinchAcc_ += kPinchStepMagnification;
        [self zoomStepAt:anchor increase:NO];
    }
}

- (void)keyDown:(NSEvent*)event {
    if (!app_) {
        [super keyDown:event];
        return;
    }

    // AppKit turns the key event into insertText: or a command
    // selector, which doCommandBySelector: handles below. That is what
    // makes typing, Return, Tab, the arrows and the deletions work.
    // The shift-return soft break is the one key AppKit does not
    // distinguish on its own.
    if ([event keyCode] == 36) {
        const NSEventModifierFlags flags =
            [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
        if ((flags & NSEventModifierFlagShift) != 0) {
            app_->InsertSoftBreak();
            [self caretMoved];
            return;
        }
    }

    // Esc ends a text-drag attempt without changing anything
    // (drag and drop 7): the text, the selection and the history
    // stay as they were.
    if ([event keyCode] == 53 &&
        (content_->textDragArmed() || content_->textDragActive())) {
        content_->CancelTextDrag();
        [self setNeedsDisplay:YES];
        return;
    }

    // The Windows clipboard alias keys, for keyboards that carry them:
    // Ctrl+Insert copies, Shift+Insert pastes, Shift+Delete cuts and
    // Ctrl+Y redoes. Each reaches the same command its Cmd shortcut
    // reaches (spec: the required standard shortcuts).
    const NSEventModifierFlags aliasFlags =
        [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
    const bool aliasCtrl = (aliasFlags & NSEventModifierFlagControl) != 0;
    const bool aliasCmd = (aliasFlags & NSEventModifierFlagCommand) != 0;
    const bool aliasShift = (aliasFlags & NSEventModifierFlagShift) != 0;
    if (!aliasCmd) {
        const unsigned short key = [event keyCode];
        if (aliasCtrl && !aliasShift && key == 114) {  // Ctrl+Insert
            app_->CopyToClipboard();
            return;
        }
        if (!aliasCtrl && aliasShift && key == 114) {  // Shift+Insert
            app_->PasteFromClipboard();
            [self documentEdited];
            return;
        }
        if (!aliasCtrl && aliasShift && key == 117) {  // Shift+Delete
            app_->CutToClipboard();
            [self setNeedsDisplay:YES];
            return;
        }
        if (aliasCtrl && !aliasShift && key == 16) {   // Ctrl+Y
            app_->Redo();
            [self documentEdited];
            return;
        }
    }

    [self interpretKeyEvents:@[event]];
}

- (void)insertText:(NSString*)text {
    if (!app_) {
        [super insertText:text];
        return;
    }
    const char* utf8 = [text UTF8String];
    if (utf8) {
        app_->InsertText(std::string(utf8));
    }
    [self documentEdited];
}

// The input method's commit, the one that carries a replacement range.
// The committed string replaces the marked text in the buffer; the
// shared controller turns that into exactly one undo entry.
- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    (void)replacementRange;
    NSString* plain = [string isKindOfClass:[NSAttributedString class]]
        ? [string string]
        : string;
    if (!app_ || plain == nil) return;
    const char* utf8 = [plain UTF8String];
    if (utf8) {
        app_->InsertText(std::string(utf8));
        [self documentEdited];
    }
}

// The input method asks whether a composition is being written. The
// marked text lives in the buffer, so the painter draws it like any
// other text and the history stays untouched until the confirmation
// (text editing spec, "Almindeligt tekstinput").
- (BOOL)hasMarkedText {
    return app_ && app_->HasMarkedText();
}

- (NSRange)markedRange {
    if (!app_ || !app_->HasMarkedText()) {
        return NSMakeRange(NSNotFound, 0);
    }
    uint32_t location = 0;
    uint32_t length = 0;
    app_->MarkedRangeUtf16(&location, &length);
    return NSMakeRange(location, length);
}

- (NSRange)selectedRange {
    if (!app_) return NSMakeRange(NSNotFound, 0);
    uint32_t location = 0;
    uint32_t length = 0;
    app_->SelectionRangeUtf16(&location, &length);
    return NSMakeRange(location, length);
}

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange {
    (void)selectedRange;
    (void)replacementRange;
    NSString* plain = [string isKindOfClass:[NSAttributedString class]]
        ? [string string]
        : string;
    if (!app_ || plain == nil) return;
    const char* utf8 = [plain UTF8String];
    if (utf8) {
        app_->SetMarkedText(std::string(utf8));
        [self documentEdited];
    }
}

- (void)unmarkText {
    if (!app_) return;
    app_->UnmarkText();
    [self documentEdited];
}

- (NSArray*)validAttributesForMarkedText {
    return @[];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
    actualRange:(NSRangePointer)actualRange {
    (void)range;
    if (actualRange) *actualRange = NSMakeRange(NSNotFound, 0);
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    (void)point;
    return NSNotFound;
}

- (NSRect)firstRectForCharacterRange:(NSRange)range
    actualRange:(NSRangePointer)actualRange {
    (void)range;
    if (actualRange) *actualRange = NSMakeRange(NSNotFound, 0);
    // No rectangle: AppKit places the candidate window itself.
    return NSZeroRect;
}

// The accessibility layer (table spec 47). The document tells a screen
// reader where its tables, their rows, headers and cells sit, from the
// same geometry the painter and the hit test use.
- (BOOL)isAccessibilityElement {
    return YES;
}

- (NSString*)accessibilityRole {
    return NSAccessibilityTextAreaRole;
}

- (NSString*)accessibilityLabel {
    return @"Document";
}

- (NSRect)axFrameForRect:(const mac::Rect*)rect {
    NSRect inView = NSMakeRect(rect->x, rect->y, rect->width, rect->height);
    NSRect inWindow = [self convertRect:inView toView:nil];
    return [self.window convertRectToScreen:inWindow];
}

- (NSArray*)accessibilityChildren {
    NSMutableArray* children = [NSMutableArray array];
    if (!content_) return children;
    const std::vector<mac::ContentView::AxTable> tables =
        content_->AccessibilityTables();
    for (const mac::ContentView::AxTable& table : tables) {
        const mac::Rect tableRect{table.x, table.y, table.width, table.height};
        NSAccessibilityElement* tableElement =
            [NSAccessibilityElement accessibilityElementWithRole:
                                        NSAccessibilityTableRole
                                                       frame:[self axFrameForRect:&tableRect]
                                                       label:@"Table"
                                                      parent:self];
        NSMutableArray* rows = [NSMutableArray array];
        size_t i = 0;
        while (i < table.cells.size()) {
            const int rowIndex = table.cells[i].row;
            NSMutableArray* rowCells = [NSMutableArray array];
            const mac::ContentView::AxCell& first = table.cells[i];
            float minX = first.x, minY = first.y;
            float maxX = first.x + first.width;
            float maxY = first.y + first.height;
            while (i < table.cells.size() && table.cells[i].row == rowIndex) {
                const mac::ContentView::AxCell& cell = table.cells[i];
                if (cell.x < minX) minX = cell.x;
                if (cell.y < minY) minY = cell.y;
                if (cell.x + cell.width > maxX) maxX = cell.x + cell.width;
                if (cell.y + cell.height > maxY) maxY = cell.y + cell.height;
                const mac::Rect cellRect{cell.x, cell.y, cell.width, cell.height};
                NSAccessibilityElement* cellElement =
                    [NSAccessibilityElement accessibilityElementWithRole:
                                                NSAccessibilityStaticTextRole
                                                               frame:[self axFrameForRect:&cellRect]
                                                               label:[NSString
                                                                   stringWithUTF8String:
                                                                       cell.text.c_str()]
                                                              parent:self];
                [cellElement setAccessibilityRowIndexRange:
                    NSMakeRange(static_cast<NSUInteger>(cell.row), 1)];
                [cellElement setAccessibilityColumnIndexRange:
                    NSMakeRange(static_cast<NSUInteger>(cell.column), 1)];
                [cellElement setAccessibilitySelected:cell.caretCell];
                [rowCells addObject:cellElement];
                ++i;
            }
            const mac::Rect rowRect{minX, minY, maxX - minX, maxY - minY};
            NSAccessibilityElement* rowElement =
                [NSAccessibilityElement accessibilityElementWithRole:
                                            NSAccessibilityRowRole
                                                           frame:[self axFrameForRect:&rowRect]
                                                           label:(rowIndex == 0
                                                               ? @"Header row"
                                                               : @"Row")
                                                          parent:self];
            [rowElement setAccessibilityRowIndexRange:
                NSMakeRange(static_cast<NSUInteger>(rowIndex), 1)];
            [rowElement setAccessibilityChildren:rowCells];
            [rows addObject:rowElement];
        }
        [tableElement setAccessibilityChildren:rows];
        [children addObject:tableElement];
    }
    return children;
}

- (void)doCommandBySelector:(SEL)selector {
    if (!app_) {
        [super doCommandBySelector:selector];
        return;
    }

    if (selector == @selector(insertNewline:)) {
        app_->InsertParagraphBreak();
        [self documentEdited];
        return;
    }

    if (selector == @selector(insertTab:)) {
        // Tab is structural inside a table, and the column the vertical
        // moves aim for starts fresh after the caret changed.
        app_->HandleTab(false);
        [self documentEdited];
        return;
    }

    if (selector == @selector(insertBacktab:)) {
        app_->HandleTab(true);
        [self documentEdited];
        return;
    }

    if (selector == @selector(deleteBackward:)) {
        app_->DeleteBackward();
        [self documentEdited];
        return;
    }

    if (selector == @selector(deleteForward:)) {
        app_->DeleteForward();
        [self documentEdited];
        return;
    }

    // The word deletions use the shared word boundaries, the same ones
    // the double click and the word moves use. Command-backspace is the
    // line variant of it.
    if (selector == @selector(deleteWordBackward:) ||
        selector == @selector(deleteToBeginningOfLine:)) {
        app_->DeleteWordBackward();
        [self documentEdited];
        return;
    }

    if (selector == @selector(deleteWordForward:)) {
        app_->DeleteWordForward();
        [self documentEdited];
        return;
    }

    // The arrows: one grapheme per press, one word with the option key.
    if (selector == @selector(moveLeft:)) {
        [self moveHorizontal:YES word:NO extend:NO];
        return;
    }
    if (selector == @selector(moveRight:)) {
        [self moveHorizontal:NO word:NO extend:NO];
        return;
    }
    if (selector == @selector(moveLeftAndModifySelection:)) {
        [self moveHorizontal:YES word:NO extend:YES];
        return;
    }
    if (selector == @selector(moveRightAndModifySelection:)) {
        [self moveHorizontal:NO word:NO extend:YES];
        return;
    }
    if (selector == @selector(moveWordLeft:)) {
        [self moveHorizontal:YES word:YES extend:NO];
        return;
    }
    if (selector == @selector(moveWordRight:)) {
        [self moveHorizontal:NO word:YES extend:NO];
        return;
    }
    if (selector == @selector(moveWordLeftAndModifySelection:)) {
        [self moveHorizontal:YES word:YES extend:YES];
        return;
    }
    if (selector == @selector(moveWordRightAndModifySelection:)) {
        [self moveHorizontal:NO word:YES extend:YES];
        return;
    }

    // Up and down keep the column, and the page keys move by most of a
    // windowful.
    if (selector == @selector(moveUp:)) {
        [self moveVertical:YES extend:NO];
        return;
    }
    if (selector == @selector(moveDown:)) {
        [self moveVertical:NO extend:NO];
        return;
    }
    if (selector == @selector(moveUpAndModifySelection:)) {
        [self moveVertical:YES extend:YES];
        return;
    }
    if (selector == @selector(moveDownAndModifySelection:)) {
        [self moveVertical:NO extend:YES];
        return;
    }
    if (selector == @selector(scrollPageUp:)) {
        [self moveVerticalPage:YES extend:NO];
        return;
    }
    if (selector == @selector(scrollPageDown:)) {
        [self moveVerticalPage:NO extend:NO];
        return;
    }
    // Shift+Page selects the page it moves by (spec AC-02).
    if (selector == @selector(pageUpAndModifySelection:)) {
        [self moveVerticalPage:YES extend:YES];
        return;
    }
    if (selector == @selector(pageDownAndModifySelection:)) {
        [self moveVerticalPage:NO extend:YES];
        return;
    }

    // Home and End move inside the visual line; the command variants
    // reach the edges of the document.
    if (selector == @selector(moveToBeginningOfLine:)) {
        [self moveToLineEdge:YES extend:NO];
        return;
    }
    if (selector == @selector(moveToEndOfLine:)) {
        [self moveToLineEdge:NO extend:NO];
        return;
    }
    if (selector == @selector(moveToBeginningOfLineAndModifySelection:)) {
        [self moveToLineEdge:YES extend:YES];
        return;
    }
    if (selector == @selector(moveToEndOfLineAndModifySelection:)) {
        [self moveToLineEdge:NO extend:YES];
        return;
    }
    // The line-edge selectors carry Command+Left/Right and the Home and
    // End keys; the modify variants select (spec S7).
    if (selector == @selector(moveToLeftEndOfLine:)) {
        [self moveToLineEdge:YES extend:NO];
        return;
    }
    if (selector == @selector(moveToRightEndOfLine:)) {
        [self moveToLineEdge:NO extend:NO];
        return;
    }
    if (selector == @selector(moveToLeftEndOfLineAndModifySelection:)) {
        [self moveToLineEdge:YES extend:YES];
        return;
    }
    if (selector == @selector(moveToRightEndOfLineAndModifySelection:)) {
        [self moveToLineEdge:NO extend:YES];
        return;
    }
    if (selector == @selector(moveToBeginningOfDocument:) ||
        selector == @selector(scrollToBeginningOfDocument:)) {
        [self moveToDocumentEdge:YES extend:NO];
        return;
    }
    if (selector == @selector(moveToEndOfDocument:) ||
        selector == @selector(scrollToEndOfDocument:)) {
        [self moveToDocumentEdge:NO extend:NO];
        return;
    }
    if (selector == @selector(moveToBeginningOfDocumentAndModifySelection:)) {
        [self moveToDocumentEdge:YES extend:YES];
        return;
    }
    if (selector == @selector(moveToEndOfDocumentAndModifySelection:)) {
        [self moveToDocumentEdge:NO extend:YES];
        return;
    }

    [super doCommandBySelector:selector];
}

// Moves the caret one grapheme or one word to the left or right.
- (void)moveHorizontal:(bool)left word:(bool)word extend:(bool)extend {
    if (!app_ || !content_) return;
    desiredX_ = -1.0f;

    const Selection& selection = app_->selection();
    uint32_t target = selection.active.offset;
    if (selection.Empty()) {
        target = word ? (left ? content_->StepWord(target, true)
                              : content_->StepWord(target, false))
                      : (left ? content_->StepGrapheme(target, true)
                              : content_->StepGrapheme(target, false));
    } else if (!extend) {
        // A move without the shift key collapses the selection to the
        // edge it moves from.
        target = left ? selection.Start() : selection.End();
    }
    [self setCaret:target extend:extend];
    [self caretMoved];
}

// Moves the caret to the line above or below.
- (void)moveVertical:(bool)up extend:(bool)extend {
    // A vertical move without the shift key collapses a selection to
    // the edge it moves from: Up to the start, Down to the end, without
    // stepping to another line first, the Windows rule. The remembered
    // column is kept, exactly as the Windows build keeps it.
    if (app_ && !extend && !app_->selection().Empty()) {
        const Selection& selection = app_->selection();
        [self setCaret:(up ? selection.Start() : selection.End()) extend:NO];
        [self caretMoved];
        return;
    }
    // Inside a table cell Up and Down walk rows and keep the
    // column (spec AC-10): the logical move comes first and the
    // visual line move is the fallback, the order the Windows
    // build uses.
    if (app_ && content_ && app_->selection().Empty()) {
        uint32_t target = 0;
        if (app_->MoveTableRow(up, &target)) {
            desiredX_ = -1.0f;
            [self setCaret:target extend:extend];
            [self caretMoved];
            return;
        }
    }
    [self moveVerticallyBy:0.0f up:up extend:extend];
}

// Moves the caret by most of a windowful, which the page keys do.
- (void)moveVerticalPage:(bool)up extend:(bool)extend {
    if (!content_) return;
    [self moveVerticallyBy:content_->viewHeight() * 0.9f
                        up:up
                    extend:extend];
}

- (void)moveVerticallyBy:(float)distance up:(bool)up extend:(bool)extend {
    if (!app_ || !content_) return;

    const Selection& selection = app_->selection();
    const uint32_t caret = selection.active.offset;

    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (!content_->OffsetToRect(caret, &x, &y, &width, &height)) return;

    // The column the moves aim for, kept across the run until a fresh
    // click or a horizontal move drops it, so a short line does not
    // pull the caret left on the way down.
    if (desiredX_ < 0.0f) desiredX_ = x;

    const float step = distance > 0.0f
        ? distance
        : (height > 0.0f ? height : 16.0f);
    // The point comes back in document coordinates, the space the hit
    // tester works in.
    float docY = y;
    docY += up ? -step : step;

    const uint32_t target = content_->HitTestPoint(desiredX_, docY);
    if (target == UINT32_MAX) return;

    [self setCaret:target extend:extend];
    [self caretMoved];
}

// Home and End: the edges of the visual line. A collapsed move with
// a selection goes to the first (Home) or the last (End) line of the
// selection, not to the active edge, the way the Windows build does
// it.
- (void)moveToLineEdge:(bool)start extend:(bool)extend {
    if (!app_ || !content_) return;
    desiredX_ = -1.0f;

    const Selection& selection = app_->selection();
    uint32_t from = selection.active.offset;
    if (!extend && !selection.Empty()) {
        from = start ? selection.Start() : selection.End();
    }

    const uint32_t target = content_->LineBoundary(from, !start);
    [self setCaret:target extend:extend];
    [self caretMoved];
}

// Command-up and command-down: the edges of the document.
- (void)moveToDocumentEdge:(bool)start extend:(bool)extend {
    if (!app_ || !content_) return;
    desiredX_ = -1.0f;

    const uint32_t target = start
        ? 0
        : static_cast<uint32_t>(app_->sourceText().size());
    [self setCaret:target extend:extend];
    content_->ScrollToOffset(target);
    [self caretMoved];
}

// Sets the caret, extending the selection when the shift key is down.
- (void)setCaret:(uint32_t)offset extend:(bool)extend {
    if (!app_) return;

    if (extend) {
        app_->SetSelection(app_->selection().anchor.offset, offset);
    } else {
        app_->SetCaret(offset);
    }
}

@end
