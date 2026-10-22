#import "table_picker_mac.h"

#include "tablepicker_model.h"

// The grid geometry, in points. AppKit scales points to the backing store
// itself, so the grid keeps its size on a Retina display without any of the
// manual DPI arithmetic the Windows popup does. The values match the
// Windows constants (16, 6 and 22) so the two pickers look alike.
static const CGFloat kCellSize = 16.0;
static const CGFloat kMargin = 6.0;
static const CGFloat kLabelHeight = 22.0;
// Distance kept from the screen edges when the picker has to move.
static const CGFloat kEdgeInset = 4.0;

@interface MdTablePickerWindow : NSPanel
@end

@implementation MdTablePickerWindow
// A borderless panel is not key by default. The picker needs the keyboard,
// so it says yes and takes focus while it is up.
- (BOOL)canBecomeKeyWindow {
    return YES;
}
@end

static MdTablePickerWindow* gPickerWindow = nil;

@interface MdTablePickerView : NSView
@property(nonatomic, copy) void (^onCommit)(int cols, int rows);
@property(nonatomic, assign) NSWindow* previousKeyWindow;
@property(nonatomic, assign) BOOL closing;
@end

@implementation MdTablePickerView {
    TablePickerModel model_;
    NSTrackingArea* tracking_;
}

- (id)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        model_.Set(3, 3);
        tracking_ = nil;
    }
    return self;
}

- (BOOL)isFlipped {
    // Draw from the top down: the first row of cells sits at the top, the
    // way the Windows popup draws it. Without this the grid would grow
    // upward and the hover row would be upside down.
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (tracking_) [self removeTrackingArea:tracking_];
    tracking_ = [[NSTrackingArea alloc]
        initWithRect:[self bounds]
             options:(NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                      NSTrackingActiveAlways | NSTrackingInVisibleRect)
               owner:self
            userInfo:nil];
    [self addTrackingArea:tracking_];
}

- (NSRect)cellRectForColumn:(int)column rowFromTop:(int)row {
    // Flipped view: y grows downward from the top edge.
    const CGFloat x = kMargin + column * kCellSize;
    const CGFloat y = kMargin + row * kCellSize;
    return NSMakeRect(x, y, kCellSize, kCellSize);
}

- (int)columnAtPoint:(NSPoint)point {
    const CGFloat x = point.x - kMargin;
    if (x < 0) return -1;
    const int column = static_cast<int>(x / kCellSize);
    return column >= TablePickerModel::kMaxCols ? -1 : column;
}

- (int)rowAtPoint:(NSPoint)point {
    const CGFloat y = point.y - kMargin;
    if (y < 0) return -1;
    const int row = static_cast<int>(y / kCellSize);
    return row >= TablePickerModel::kMaxRows ? -1 : row;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[NSColor controlBackgroundColor] setFill];
    NSRectFill([self bounds]);

    for (int row = 0; row < TablePickerModel::kMaxRows; ++row) {
        for (int column = 0; column < TablePickerModel::kMaxCols; ++column) {
            const NSRect cell = NSInsetRect(
                [self cellRectForColumn:column rowFromTop:row], 1.0, 1.0);
            const BOOL chosen = column < model_.cols && row < model_.rows;
            NSBezierPath* path =
                [NSBezierPath bezierPathWithRoundedRect:cell
                                                xRadius:2.0
                                                yRadius:2.0];
            if (chosen) {
                [[NSColor controlAccentColor] setFill];
                [path fill];
            } else {
                [[NSColor separatorColor] setStroke];
                [path setLineWidth:1.0];
                [path stroke];
            }
        }
    }

    // The label sits under the grid and counts the header row, the way Word
    // writes it: three data rows read "4 x 3 Table".
    NSString* label = [NSString stringWithUTF8String:model_.Label().c_str()];
    NSDictionary* attributes = @{
        NSForegroundColorAttributeName : [NSColor labelColor],
        NSFontAttributeName : [NSFont systemFontOfSize:11.0],
    };
    const NSSize size = [label sizeWithAttributes:attributes];
    const NSRect band = NSMakeRect(kMargin, [self bounds].size.height -
                                                kLabelHeight - kMargin + 2.0,
                                   [self bounds].size.width - 2 * kMargin,
                                   kLabelHeight);
    const NSPoint at = NSMakePoint(
        band.origin.x + (band.size.width - size.width) / 2.0,
        band.origin.y + (band.size.height - size.height) / 2.0);
    [label drawAtPoint:at withAttributes:attributes];
}

- (void)setSizeFromPoint:(NSPoint)point {
    const int column = [self columnAtPoint:point];
    const int row = [self rowAtPoint:point];
    if (column < 0 || row < 0) return;
    const int beforeCols = model_.cols;
    const int beforeRows = model_.rows;
    model_.Set(column + 1, row + 1);
    if (model_.cols != beforeCols || model_.rows != beforeRows) {
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseMoved:(NSEvent*)event {
    [self setSizeFromPoint:[self convertPoint:[event locationInWindow]
                                      fromView:nil]];
}

- (void)mouseDragged:(NSEvent*)event {
    [self mouseMoved:event];
}

- (void)mouseDown:(NSEvent*)event {
    [self setSizeFromPoint:[self convertPoint:[event locationInWindow]
                                      fromView:nil]];
    [self commit];
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    // Leaving the popup cancels, the way the Windows grid closes when the
    // pointer leaves it. The click that opened the picker lands before the
    // pointer is inside, so this does not fight the first hover.
    [self cancel];
}

- (void)keyDown:(NSEvent*)event {
    switch ([event keyCode]) {
    case 123:  // left
        model_.Move(-1, 0);
        [self setNeedsDisplay:YES];
        return;
    case 124:  // right
        model_.Move(1, 0);
        [self setNeedsDisplay:YES];
        return;
    case 125:  // down
        model_.Move(0, 1);
        [self setNeedsDisplay:YES];
        return;
    case 126:  // up
        model_.Move(0, -1);
        [self setNeedsDisplay:YES];
        return;
    case 36:   // Return
    case 76:   // keypad Enter
        [self commit];
        return;
    case 53:   // Escape
        [self cancel];
        return;
    default:
        break;
    }
    [super keyDown:event];
}

- (void)commit {
    if (self.closing) return;
    const int cols = model_.cols;
    const int rows = model_.rows;
    void (^done)(int, int) = self.onCommit;
    [self teardown];
    if (done) done(cols, rows);
}

- (void)cancel {
    if (self.closing) return;
    [self teardown];
}

- (void)teardown {
    self.closing = YES;
    [[self window] orderOut:nil];
    if (gPickerWindow) {
        [gPickerWindow orderOut:nil];
        gPickerWindow = nil;
    }
    // Give the keyboard back to the window the user came from. Without
    // this the document keeps no key window after the picker closes.
    if (self.previousKeyWindow) {
        [self.previousKeyWindow makeKeyWindow];
    }
}

@end

@implementation MdTablePicker

+ (void)dismiss {
    if (gPickerWindow) {
        [gPickerWindow orderOut:nil];
        gPickerWindow = nil;
    }
}

+ (void)presentBelowAnchor:(NSPoint)anchor
                  onCommit:(void (^)(int cols, int rows))commit {
    [MdTablePicker dismiss];

    const CGFloat width = TablePickerModel::kMaxCols * kCellSize + 2 * kMargin;
    const CGFloat height =
        TablePickerModel::kMaxRows * kCellSize + kLabelHeight + 2 * kMargin;

    // The anchor is the top-left corner the picker hangs from. AppKit
    // counts y upward, so the window's origin sits one height below it.
    CGFloat x = anchor.x;
    CGFloat y = anchor.y - height;

    // Keep the whole picker inside the screen's work area. The Windows
    // popup does the same with the monitor work area, and the guideline
    // asks for it explicitly.
    NSScreen* screen = [NSScreen mainScreen];
    for (NSScreen* candidate in [NSScreen screens]) {
        if (NSPointInRect(anchor, [candidate frame])) {
            screen = candidate;
            break;
        }
    }
    const NSRect work = [screen visibleFrame];
    if (x + width > NSMaxX(work) - kEdgeInset) {
        x = NSMaxX(work) - kEdgeInset - width;
    }
    if (x < NSMinX(work) + kEdgeInset) x = NSMinX(work) + kEdgeInset;
    if (y < NSMinY(work) + kEdgeInset) y = NSMinY(work) + kEdgeInset;
    if (y + height > NSMaxY(work) - kEdgeInset) {
        y = NSMaxY(work) - kEdgeInset - height;
    }

    MdTablePickerWindow* window = [[MdTablePickerWindow alloc]
        initWithContentRect:NSMakeRect(x, y, width, height)
                  styleMask:NSWindowStyleMaskBorderless
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [window setLevel:NSPopUpMenuWindowLevel];
    [window setHasShadow:YES];
    [window setOpaque:YES];
    [window setBackgroundColor:[NSColor controlBackgroundColor]];

    MdTablePickerView* grid = [[MdTablePickerView alloc]
        initWithFrame:NSMakeRect(0, 0, width, height)];
    grid.onCommit = commit;
    grid.previousKeyWindow = [NSApp keyWindow];
    [window setContentView:grid];
    [window makeFirstResponder:grid];

    gPickerWindow = window;
    [window makeKeyAndOrderFront:nil];
}

@end
