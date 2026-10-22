// The macOS content view.
//
// Two things live here: the ContentView, which holds the layout and the
// hit testing, and MdContentView, the NSView subclass that draws it and
// turns the events into edits.
//
// The drawing goes through the RenderTarget seam, so this file is the
// only place in the mac layer that talks to Core Graphics directly.

#include "content_view_mac.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include <algorithm>

#include "content_nsview_mac.h"
#include "text_engine_mac.h"
#include "image_draw_mac.h"

namespace mac {

namespace {

// The default width of the window's content area.
constexpr float kDefaultWidth = 900.0f;
constexpr float kDefaultHeight = 700.0f;

// The gap between the edge of the view and the text column.
constexpr float kGutter = 24.0f;

// The circle constant, for the pie slices.
constexpr double kPi = 3.14159265358979323846;

// The offset of the character before the given offset.
uint32_t PreviousCharacter(const std::string& text, uint32_t offset) {
    if (offset == 0 || text.empty()) return 0;

    uint32_t start = offset - 1;
    // Walk back over the continuation bytes of a multi byte character.
    while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
        --start;
    }
    return start;
}

// The offset of the character after the given offset.
uint32_t NextCharacter(const std::string& text, uint32_t offset) {
    if (offset >= text.size()) return static_cast<uint32_t>(text.size());

    uint32_t end = offset + 1;
    while (end < text.size() &&
           (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        ++end;
    }
    return end;
}

}  // namespace

// ---------------------------------------------------------------------------
// The Core Graphics render target
// ---------------------------------------------------------------------------

namespace {

// A render target that draws into a CGContext.
//
// The context is flipped so that y grows downwards, which is the way
// the renderer lays the document out. Every method works in points,
// because the caller scales the context once.
class CoreGraphicsTarget : public RenderTarget {
public:
    explicit CoreGraphicsTarget(CGContextRef context) : context_(context) {}

    void FillRect(const Rect& rect, const Color& color) override {
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextFillRect(context_, CGRectMake(rect.x, rect.y, rect.width,
                                               rect.height));
        CGContextRestoreGState(context_);
    }

    void FillRoundedRect(const Rect& rect, float radius,
                         const Color& color) override {
        if (radius <= 0.0f) {
            FillRect(rect, color);
            return;
        }
        const CGRect bounds = CGRectMake(rect.x, rect.y, rect.width, rect.height);
        const CGRect box = CGRectInset(bounds, radius, radius);

        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextBeginPath(context_);
        CGContextAddRect(context_, box);
        CGContextClip(context_);
        CGContextFillEllipseInRect(context_,
                                   CGRectMake(box.origin.x, box.origin.y,
                                              box.size.width, box.size.height));
        CGContextRestoreGState(context_);

        // The four corners.
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextFillEllipseInRect(
            context_, CGRectMake(bounds.origin.x, bounds.origin.y, radius * 2.0f,
                                 radius * 2.0f));
        CGContextFillEllipseInRect(
            context_,
            CGRectMake(bounds.origin.x + bounds.size.width - radius * 2.0f,
                       bounds.origin.y, radius * 2.0f, radius * 2.0f));
        CGContextFillEllipseInRect(
            context_,
            CGRectMake(bounds.origin.x,
                       bounds.origin.y + bounds.size.height - radius * 2.0f,
                       radius * 2.0f, radius * 2.0f));
        CGContextFillEllipseInRect(
            context_,
            CGRectMake(bounds.origin.x + bounds.size.width - radius * 2.0f,
                       bounds.origin.y + bounds.size.height - radius * 2.0f,
                       radius * 2.0f, radius * 2.0f));
        CGContextRestoreGState(context_);
    }

    void StrokeRect(const Rect& rect, const Color& color,
                    float lineWidth) override {
        CGContextSaveGState(context_);
        CGContextSetRGBStrokeColor(context_, color.r, color.g, color.b, color.a);
        CGContextSetLineWidth(context_, lineWidth);
        CGContextStrokeRect(context_, CGRectMake(rect.x, rect.y, rect.width,
                                                 rect.height));
        CGContextRestoreGState(context_);
    }

    void StrokeLine(float x0, float y0, float x1, float y1, const Color& color,
                    float lineWidth) override {
        CGContextSaveGState(context_);
        CGContextSetRGBStrokeColor(context_, color.r, color.g, color.b, color.a);
        CGContextSetLineWidth(context_, lineWidth);
        CGContextMoveToPoint(context_, x0, y0);
        CGContextAddLineToPoint(context_, x1, y1);
        CGContextStrokePath(context_);
        CGContextRestoreGState(context_);
    }

    void DrawText(const std::string& utf8, const FontSpec& font, float x,
                  float baselineY, const Color& color) override {
        NSString* text = [[NSString alloc] initWithBytes:utf8.data()
                                                   length:utf8.size()
                                                 encoding:NSUTF8StringEncoding];
        if (!text) return;

        NSFont* nsFont = BuildFont(font);
        if (!nsFont) return;

        NSDictionary* attributes = @{
            NSFontAttributeName : nsFont,
            NSForegroundColorAttributeName :
                [NSColor colorWithSRGBRed:color.r green:color.g blue:color.b
                                     alpha:color.a]
        };

        [text drawAtPoint:NSMakePoint(x, baselineY) withAttributes:attributes];
    }

    void DrawBitmap(BitmapHandle image, const Rect& rect) override {
        mac::DrawImageIntoContext(context_, image, rect);
    }

    void FillEllipse(const Rect& rect, const Color& color) override {
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextFillEllipseInRect(context_,
                                   CGRectMake(rect.x, rect.y, rect.width,
                                              rect.height));
        CGContextRestoreGState(context_);
    }

    void StrokeEllipse(const Rect& rect, const Color& color,
                       float lineWidth) override {
        CGContextSaveGState(context_);
        CGContextSetRGBStrokeColor(context_, color.r, color.g, color.b, color.a);
        CGContextSetLineWidth(context_, lineWidth);
        CGContextStrokeEllipseInRect(context_,
                                     CGRectMake(rect.x, rect.y, rect.width,
                                                rect.height));
        CGContextRestoreGState(context_);
    }

    void FillPolygon(const std::vector<Point>& points,
                     const Color& color) override {
        if (points.size() < 3) return;
        // Core Graphics takes the points as one array.
        std::vector<CGPoint> coordinates;
        coordinates.reserve(points.size());
        for (const Point& point : points) {
            coordinates.push_back(CGPointMake(point.x, point.y));
        }
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextBeginPath(context_);
        CGContextMoveToPoint(context_, coordinates[0].x, coordinates[0].y);
        CGContextAddLines(context_, coordinates.data(), coordinates.size());
        CGContextClosePath(context_);
        CGContextFillPath(context_);
        CGContextRestoreGState(context_);
    }

    void StrokePolygon(const std::vector<Point>& points,
                       const Color& color, float lineWidth,
                       bool closed) override {
        if (points.size() < 2) return;
        std::vector<CGPoint> coordinates;
        coordinates.reserve(points.size());
        for (const Point& point : points) {
            coordinates.push_back(CGPointMake(point.x, point.y));
        }
        CGContextSaveGState(context_);
        CGContextSetRGBStrokeColor(context_, color.r, color.g, color.b,
                                   color.a);
        CGContextSetLineWidth(context_, lineWidth > 0.0f ? lineWidth : 1.0f);
        CGContextBeginPath(context_);
        CGContextMoveToPoint(context_, coordinates[0].x, coordinates[0].y);
        CGContextAddLines(context_, coordinates.data(), coordinates.size());
        if (closed) CGContextClosePath(context_);
        CGContextStrokePath(context_);
        CGContextRestoreGState(context_);
    }

    void FillPieSlice(const Rect& rect, float startAngle, float endAngle,
                      const Color& color) override {
        // The seam counts the angles clockwise from three o'clock, and
        // Core Graphics counts them counter-clockwise, so the angles are
        // negated. The context is flipped, with the y axis pointing
        // downwards, so the negated angle sweeps clockwise on screen.
        const CGFloat centreX = rect.x + rect.width * 0.5;
        const CGFloat centreY = rect.y + rect.height * 0.5;
        const CGFloat radius = std::min(rect.width, rect.height) * 0.5;
        const CGFloat start = -startAngle * kPi / 180.0;
        const CGFloat end = -endAngle * kPi / 180.0;
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, color.r, color.g, color.b, color.a);
        CGContextBeginPath(context_);
        CGContextMoveToPoint(context_, centreX, centreY);
        CGContextAddArc(context_, centreX, centreY, radius, start, end, true);
        CGContextClosePath(context_);
        CGContextFillPath(context_);
        CGContextRestoreGState(context_);
    }

    void PushClip(const Rect& rect) override {
        CGContextSaveGState(context_);
        CGContextClipToRect(context_, CGRectMake(rect.x, rect.y, rect.width,
                                                 rect.height));
    }

    void PopClip() override { CGContextRestoreGState(context_); }

private:
    // Builds an NSFont from a font request.
    static NSFont* BuildFont(const FontSpec& spec) {
        NSFontWeight weight = spec.bold ? NSFontWeightBold : NSFontWeightRegular;
        NSFont* font = nil;

        if (spec.monospace) {
            const char* families[] = {"Menlo", "SF Mono", "Monaco",
                                      "Courier New"};
            for (const char* family : families) {
                font = [NSFont fontWithName:[NSString stringWithUTF8String:family]
                                      size:spec.size];
                if (font) break;
            }
        } else {
            const char* families[] = {"Helvetica Neue", "Helvetica", "SF Pro Text",
                                      "Lucida Grande"};
            for (const char* family : families) {
                font = [NSFont fontWithName:[NSString stringWithUTF8String:family]
                                      size:spec.size];
                if (font) break;
            }
        }

        if (!font) {
            font = [NSFont systemFontOfSize:spec.size weight:weight];
        }

        if (spec.italic) {
            // There is no class method that takes an italic flag, so the
            // italic is asked for through a font descriptor.
            NSFontDescriptor* descriptor =
                [font.fontDescriptor fontDescriptorWithSymbolicTraits:
                                       NSFontDescriptorTraitItalic];
            NSFont* italic = [NSFont fontWithDescriptor:descriptor size:spec.size];
            if (italic) font = italic;
        }
        return font;
    }

    CGContextRef context_;
};

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
    renderer_.SetTheme(BaseMetrics(), BasePalette());
    renderer_.SetStandaloneSvg(app_ && app_->IsStandaloneSvg());
    renderer_.SetStandaloneSvg(app_ && app_->IsStandaloneSvg());
    width_ = kDefaultWidth;
    height_ = kDefaultHeight;
    Relayout();
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

    // The text column is the view width, less the gutters, capped at the
    // width the theme asks for.
    const LayoutMetrics& metrics = BaseMetrics();
    const float available = width_ - kGutter * 2.0f;
    float column = available;
    if (metrics.maxContentWidth > 0.0f && column > metrics.maxContentWidth) {
        column = metrics.maxContentWidth;
    }
    if (column < 120.0f) column = 120.0f;

    contentWidth_ = column;
    documentHeight_ = renderer_.Measure(app_->GetDocument(), column);
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

    // The selection goes under the text, so it is drawn first.
    const Selection& selection = app_->selection();
    if (!selection.Empty()) {
        const uint32_t from = selection.Start();
        const uint32_t to = selection.End();
        for (const Rect& rect : renderer_.SelectionRects(from, to)) {
            Rect drawn = rect;
            drawn.y -= scrollOffset_;
            target->FillRect(drawn, SelectionColor());
        }
    }

    renderer_.Render(app_->GetDocument(), scrollOffset_);

    // The caret goes on top of the text.
    if (caretVisible_) {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        const uint32_t caret = selection.active.offset;
        if (renderer_.OffsetToRect(caret, &x, &y, &width, &height)) {
            Rect drawn;
            drawn.x = x;
            drawn.y = y - scrollOffset_;
            drawn.width = 1.0f;
            drawn.height = height > 0.0f ? height : CaretHeight();
            target->FillRect(drawn, CaretColor());
        }
    }

    target_ = nullptr;
}

uint32_t ContentView::HitTestPoint(float x, float y) const {
    return renderer_.HitTestPoint(x, y + scrollOffset_);
}

bool ContentView::OffsetToRect(uint32_t offset, float* outX, float* outY,
                               float* outWidth, float* outHeight) const {
    return renderer_.OffsetToRect(offset, outX, outY, outWidth, outHeight);
}

void ContentView::PlaceCaretAt(float x, float y) {
    if (!app_) return;

    // A click in the left margin misses the text. It selects the line
    // the click is level with, the way Word does it.
    uint32_t lineStart = 0;
    uint32_t lineEnd = 0;
    if (renderer_.LineRangeAtPoint(x, y + scrollOffset_, &lineStart,
                                   &lineEnd)) {
        app_->SetSelection(lineStart, lineEnd);
        return;
    }

    app_->SetCaret(HitTestPoint(x, y));
}

void ContentView::SelectWordOrParagraphAt(float x, float y, bool paragraph) {
    if (!app_) return;

    // A click in the left margin misses the text, and there is no word
    // or paragraph there either.
    uint32_t lineStart = 0;
    uint32_t lineEnd = 0;
    if (renderer_.LineRangeAtPoint(x, y + scrollOffset_, &lineStart,
                                   &lineEnd)) {
        return;
    }

    const uint32_t offset = HitTestPoint(x, y);
    if (paragraph) {
        app_->SelectParagraphAt(offset);
    } else {
        app_->SelectWordAt(offset);
    }
}

void ContentView::ExtendSelectionTo(float x, float y) {
    if (!app_) return;
    const uint32_t offset = HitTestPoint(x, y);
    app_->SetSelection(app_->selection().anchor.offset, offset);
}

Color ContentView::SelectionColor() const {
    // The selection is a soft blue, the same one the Windows app uses.
    Color color;
    color.r = 0.27f;
    color.g = 0.52f;
    color.b = 0.89f;
    color.a = 0.35f;
    return color;
}

Color ContentView::CaretColor() const {
    Color color;
    color.r = 0.10f;
    color.g = 0.10f;
    color.b = 0.12f;
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

void ContentView::SetContentWidthMode(int mode) {
    renderer_.SetContentWidthMode(mode);
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
}

- (instancetype)initWithContent:(mac::ContentView*)content app:(mac::App*)app {
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 700)];
    if (self) {
        content_ = content;
        app_ = app;
    }
    return self;
}

- (BOOL)isFlipped {
    // The renderer lays the document out with y growing downwards, so
    // the view is flipped. The whole drawing code then works in the
    // same direction as the rest of the app.
    return YES;
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    if (content_) {
        content_->SetSize(static_cast<float>(size.width),
                          static_cast<float>(size.height));
    }
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!content_) return;

    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    if (!context) return;

    // The view is flipped, so the origin is at the top left and the y
    // axis points down. A flip of -1 on the y axis makes Core Graphics
    // draw that way.
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, 0.0, NSMaxY(self.bounds));
    CGContextScaleCTM(context, 1.0, -1.0);

    mac::CoreGraphicsTarget target(context);
    content_->Draw(&target);

    CGContextRestoreGState(context);
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

    // A double click takes the word and a triple click takes the
    // paragraph, the way the Windows app does it.
    const NSInteger clicks = [event clickCount];
    if (clicks >= 2) {
        content_->SelectWordOrParagraphAt(static_cast<float>(point.x),
                                           static_cast<float>(point.y),
                                           clicks >= 3);
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
    if (content_) content_->SetCaretVisible(true);
}

- (void)blink:(NSTimer*)timer {
    (void)timer;
    if (!content_) return;
    content_->SetCaretVisible(!content_->caretVisible());
}

- (BOOL)becomeFirstResponder {
    const BOOL result = [super becomeFirstResponder];
    [self startBlinking];
    return result;
}

- (BOOL)resignFirstResponder {
    [self stopBlinking];
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
    content_->ExtendSelectionTo(static_cast<float>(point.x),
                                 static_cast<float>(point.y));
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event {
    (void)event;
    [self setNeedsDisplay:YES];
}

- (void)scrollWheel:(NSEvent*)event {
    if (!content_) return;
    CGFloat delta = [event scrollingDeltaY];
    if (delta == 0.0) {
        delta = [event scrollingDeltaX];
    }
    const float offset = content_->scrollOffset() + static_cast<float>(delta);
    content_->SetScrollOffset(offset);
    [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent*)event {
    if (!app_) {
        [super keyDown:event];
        return;
    }

    // AppKit translates the key event into the right command selector,
    // which doCommandBySelector: below handles. The return key needs
    // the shift variant, which AppKit does not distinguish on its own.
    if ([event keyCode] == 36) {
        const NSEventModifierFlags flags =
            [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
        if ((flags & NSEventModifierFlagShift) != 0) {
            app_->InsertText("\n");
            [self setNeedsDisplay:YES];
            return;
        }
    }

    [super keyDown:event];
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
    [self setNeedsDisplay:YES];
}

- (void)doCommandBySelector:(SEL)selector {
    if (!app_) {
        [super doCommandBySelector:selector];
        return;
    }

    if (selector == @selector(insertNewline:)) {
        app_->InsertParagraphBreak();
        [self setNeedsDisplay:YES];
        return;
    }

    if (selector == @selector(insertTab:) ||
        selector == @selector(insertBacktab:)) {
        app_->InsertText("\t");
        [self setNeedsDisplay:YES];
        return;
    }

    if (selector == @selector(deleteBackward:) ||
        selector == @selector(deleteWordBackward:)) {
        app_->DeleteBackward();
        [self setNeedsDisplay:YES];
        return;
    }

    if (selector == @selector(deleteForward:) ||
        selector == @selector(deleteWordForward:)) {
        app_->DeleteForward();
        [self setNeedsDisplay:YES];
        return;
    }

    if (selector == @selector(moveLeft:) || selector == @selector(moveRight:)) {
        [self moveCaretHorizontally:selector == @selector(moveLeft:)];
        [self setNeedsDisplay:YES];
        return;
    }

    if (selector == @selector(moveUp:) || selector == @selector(moveDown:)) {
        [self moveCaretVertically:selector == @selector(moveUp:)];
        [self setNeedsDisplay:YES];
        return;
    }

    [super doCommandBySelector:selector];
}

// Moves the caret one character left or right.
- (void)moveCaretHorizontally:(bool)left {
    if (!app_ || !content_) return;

    const Selection& selection = app_->selection();
    const std::string& text = app_->sourceText();

    uint32_t target = selection.active.offset;
    if (selection.Empty()) {
        target = left ? mac::PreviousCharacter(text, target)
                      : mac::NextCharacter(text, target);
    } else {
        // A selection collapses to its edge.
        target = left ? selection.Start() : selection.End();
    }

    [self setCaret:target extend:NO];
}

// Moves the caret one line up or down, keeping the horizontal position.
- (void)moveCaretVertically:(bool)up {
    if (!app_ || !content_) return;

    const Selection& selection = app_->selection();
    const uint32_t caret = selection.active.offset;

    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (!content_->OffsetToRect(caret, &x, &y, &width, &height)) {
        return;
    }

    // The document y becomes a view y, and the line height is the
    // distance the caret moves.
    const float lineHeight = height > 0.0f ? height : 16.0f;
    float viewY = y - content_->scrollOffset();
    viewY += up ? -lineHeight : lineHeight;

    const uint32_t target = content_->HitTestPoint(x, viewY);
    if (target == caret) return;

    [self setCaret:target extend:NO];
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
