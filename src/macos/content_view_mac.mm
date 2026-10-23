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
#include "cg_target_mac.h"
#include "text_engine_mac.h"
#include "image_draw_mac.h"

namespace mac {

namespace {

// The default width of the window's content area.
constexpr float kDefaultWidth = 900.0f;
constexpr float kDefaultHeight = 700.0f;

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

    // The renderer lays the document out at the full view width: it
    // takes the theme padding off, caps the reading width and centers
    // the column, the same arithmetic the Windows renderer uses.
    documentHeight_ = renderer_.Measure(app_->GetDocument(), width_);
    contentWidth_ = renderer_.contentWidth();
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

void ContentView::ScrollToOffset(uint32_t offset) {
    float y = 0.0f;
    if (!renderer_.YForOffset(offset, y)) return;

    // The place is put a third of the way down the view, so the reader
    // sees the text around it.
    const float target = y - height_ * 0.35f;
    SetScrollOffset(target < 0.0f ? 0.0f : target);
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
    // A positive delta asks for the part of the document above the
    // current place, so the offset goes down instead of up.
    const float offset = content_->scrollOffset() - static_cast<float>(delta);
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
