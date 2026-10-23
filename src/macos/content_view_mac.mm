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
    // The renderer reads the source when it normalizes a caret past a
    // marker, the same pointer the Windows cache holds.
    if (app_) renderer_.SetSourceText(&app_->sourceText());
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

    // The caret goes on top of the text, but not over a selection:
    // the selection is the marker of where the next edit lands.
    if (caretVisible_ && selection.Empty()) {
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
    uint32_t offset = 0;
    if (!renderer_.PointToOffset(x, y + scrollOffset_, &offset)) {
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
    if (renderer_.PointToOffset(x, y + scrollOffset_, &offset)) {
        app_->SetCaret(offset);
        return;
    }

    // Below the last of the text the caret goes to the end of the
    // document.
    if (y + scrollOffset_ >= renderer_.TextBottom() &&
        app_->buffer().Length() > 0) {
        app_->SetCaret(app_->buffer().Length());
        return;
    }

    // A click that misses the text entirely, a pointer in the margin,
    // selects the line it is level with, the way Word does it.
    uint32_t lineStart = 0;
    uint32_t lineEnd = 0;
    if (renderer_.LineRangeAtPoint(x, y + scrollOffset_, &lineStart,
                                   &lineEnd)) {
        app_->SetSelection(lineStart, lineEnd);
        return;
    }

    app_->SetCaret(0);
}

void ContentView::SelectWordOrParagraphAt(float x, float y, bool paragraph) {
    if (!app_) return;

    uint32_t offset = 0;
    if (!renderer_.PointToOffset(x, y + scrollOffset_, &offset)) {
        // A click in the margin has no word or paragraph either, so it
        // keeps the Word-like line selection.
        uint32_t lineStart = 0;
        uint32_t lineEnd = 0;
        if (renderer_.LineRangeAtPoint(x, y + scrollOffset_, &lineStart,
                                       &lineEnd)) {
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
    if (!renderer_.PointToOffset(x, y + scrollOffset_, &offset)) {
        // A pointer below the document extends to its end; a pointer
        // that left the text keeps the selection's edge.
        if (y + scrollOffset_ >= renderer_.TextBottom()) {
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
}

- (instancetype)initWithContent:(mac::ContentView*)content app:(mac::App*)app {
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 700)];
    if (self) {
        content_ = content;
        app_ = app;
        desiredX_ = -1.0f;
        dragMode_ = 0;
        lastDragPoint_ = NSMakePoint(0.0, 0.0);
    }
    return self;
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

    // The click takes the keyboard, so typing goes to the document
    // without a detour through the menu bar.
    [self.window makeFirstResponder:self];
    desiredX_ = -1.0f;
    [self caretMoved];

    const NSInteger clicks = [event clickCount];
    const NSEventModifierFlags flags =
        [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;

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

- (void)blink:(NSTimer*)timer {
    (void)timer;
    if (!content_) return;
    content_->SetCaretVisible(!content_->caretVisible());
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

// The caret moved or the document changed: the blink restarts and the
// view redraws.
- (void)caretMoved {
    [self resetCaretBlink];
    [self setNeedsDisplay:YES];
}

- (BOOL)becomeFirstResponder {
    const BOOL result = [super becomeFirstResponder];
    if (content_) content_->SetCaretVisible(true);
    [self startBlinking];
    return result;
}

- (BOOL)resignFirstResponder {
    [self stopBlinking];
    // The caret is hidden while the view is not focused, the way the
    // Windows window hides it.
    if (content_) content_->SetCaretVisible(false);
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
    content_->ExtendSelectionTo(static_cast<float>(point.x),
                                 static_cast<float>(point.y), dragMode_);
    [self updateAutoscroll];
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event {
    (void)event;
    [self stopAutoscroll];
    [self setNeedsDisplay:YES];
}

// The autoscroll: while a drag pulls past the top or the bottom of the
// view, a timer keeps scrolling and the selection keeps following the
// pointer, so a long drag can reach past the window.
- (void)updateAutoscroll {
    const BOOL outside = lastDragPoint_.y < 0.0 ||
                         lastDragPoint_.y > self.bounds.size.height;
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
    // Eight points a tick, the step the Windows window uses.
    const float step = lastDragPoint_.y < 0.0 ? -8.0f : 8.0f;
    content_->SetScrollOffset(content_->scrollOffset() + step);
    content_->ExtendSelectionTo(static_cast<float>(lastDragPoint_.x),
                                 static_cast<float>(lastDragPoint_.y),
                                 dragMode_);
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
    [self caretMoved];
}

- (void)doCommandBySelector:(SEL)selector {
    if (!app_) {
        [super doCommandBySelector:selector];
        return;
    }

    if (selector == @selector(insertNewline:)) {
        app_->InsertParagraphBreak();
        [self caretMoved];
        return;
    }

    if (selector == @selector(insertTab:)) {
        // Tab is structural inside a table, and the column the vertical
        // moves aim for starts fresh after the caret changed.
        desiredX_ = -1.0f;
        app_->HandleTab(false);
        [self caretMoved];
        return;
    }

    if (selector == @selector(insertBacktab:)) {
        desiredX_ = -1.0f;
        app_->HandleTab(true);
        [self caretMoved];
        return;
    }

    if (selector == @selector(deleteBackward:)) {
        app_->DeleteBackward();
        [self caretMoved];
        return;
    }

    if (selector == @selector(deleteForward:)) {
        app_->DeleteForward();
        [self caretMoved];
        return;
    }

    // The word deletions use the shared word boundaries, the same ones
    // the double click and the word moves use. Command-backspace is the
    // line variant of it.
    if (selector == @selector(deleteWordBackward:) ||
        selector == @selector(deleteToBeginningOfLine:)) {
        app_->DeleteWordBackward();
        [self caretMoved];
        return;
    }

    if (selector == @selector(deleteWordForward:)) {
        app_->DeleteWordForward();
        [self caretMoved];
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
    float viewY = y - content_->scrollOffset();
    viewY += up ? -step : step;

    const uint32_t target = content_->HitTestPoint(desiredX_, viewY);
    if (target == UINT32_MAX) return;

    [self setCaret:target extend:extend];
    [self caretMoved];
}

// Home and End: the edges of the visual line.
- (void)moveToLineEdge:(bool)start extend:(bool)extend {
    if (!app_ || !content_) return;
    desiredX_ = -1.0f;

    const uint32_t target =
        content_->LineBoundary(app_->selection().active.offset, !start);
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
