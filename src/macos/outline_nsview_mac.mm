// The NSView that draws the outline pane.
//
// The view is a thin shell around the OutlinePane: it draws through the
// RenderTarget seam, turns mouse events into clicks on the pane, and
// carries the keyboard navigation, the hover, the tooltip and the
// accessibility the outline spec asks for.

#import "outline_nsview_mac.h"

#import "outline_pane_mac.h"
#import "cg_target_mac.h"

@implementation MdOutlineView {
    mac::OutlinePane* _pane;
    NSTrackingArea* _tracking;
}

- (instancetype)initWithPane:(mac::OutlinePane*)pane {
    self = [super initWithFrame:NSMakeRect(0, 0, 200, 400)];
    if (self) {
        _pane = pane;
    }
    return self;
}

- (BOOL)isFlipped {
    // The pane lays its rows out with the origin at the top left and
    // the y axis pointing down.
    return YES;
}

// The outline takes the keyboard, so the arrows, Home, End and Enter
// work on it (outline spec 14).
- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    if (_pane) _pane->SetFocused(true);
    [self setNeedsDisplay:YES];
    return YES;
}

- (BOOL)resignFirstResponder {
    if (_pane) _pane->SetFocused(false);
    [self setNeedsDisplay:YES];
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!_pane) return;

    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    if (!context) return;

    // The view is flipped, so the origin is already at the top left and
    // the y axis already points down, which is the space the pane lays
    // its rows out in. Flipping the context again here turned the rows
    // over.
    mac::CoreGraphicsTarget target(context);
    _pane->Draw(&target, static_cast<float>(self.bounds.size.width),
                static_cast<float>(self.bounds.size.height));
}

// The hover keeps a line under the row it is on, one of the three
// states of the outline (outline spec 13).
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                     NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
               owner:self
            userInfo:NULL];
    [self addTrackingArea:_tracking];
}

- (void)mouseMoved:(NSEvent*)event {
    if (!_pane) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
                                    fromView:nil];
    _pane->SetHoveredVisibleRow(
        _pane->VisibleRowAt(static_cast<float>(point.y)));
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    if (!_pane) return;
    _pane->SetHoveredVisibleRow(-1);
    [self setNeedsDisplay:YES];
}

// A click selects the row, takes the keyboard, and then does what the
// pane says: the disclosure triangle opens and closes the subtree, and
// any other spot asks the owner to jump (outline spec 15).
- (void)mouseDown:(NSEvent*)event {
    if (!_pane) return;

    [self.window makeFirstResponder:self];
    const NSPoint point = [self convertPoint:[event locationInWindow]
                                    fromView:nil];
    _pane->SetSelectedVisibleRow(
        _pane->VisibleRowAt(static_cast<float>(point.y)));
    _pane->ClickAt(static_cast<float>(point.x), static_cast<float>(point.y));
    [self setNeedsDisplay:YES];
}

// Scrolls a visible row into view with half a row of margin above and
// below, so the row never sits on the pane's edge (outline spec 6.1).
- (void)scrollRowIntoView:(int)row {
    if (!_pane || row < 0) return;
    NSRect rect = NSMakeRect(0,
                             row * mac::OutlinePane::kRowHeightDip,
                             self.bounds.size.width,
                             mac::OutlinePane::kRowHeightDip);
    rect.origin.y -= mac::OutlinePane::kRowHeightDip * 0.5f;
    rect.size.height += mac::OutlinePane::kRowHeightDip;
    [self scrollRectToVisible:rect];
}

- (void)keyDown:(NSEvent*)event {
    if (!_pane || _pane->Visible().empty()) {
        [super keyDown:event];
        return;
    }

    switch (event.keyCode) {
        case 126:  // Up
            _pane->MoveSelection(-1);
            break;
        case 125:  // Down
            _pane->MoveSelection(1);
            break;
        case 123:  // Left: close the subtree of the selection
            _pane->SetSelectionCollapsed(true);
            break;
        case 124:  // Right: open it
            _pane->SetSelectionCollapsed(false);
            break;
        case 36:   // Return
        case 76:   // Enter
            _pane->ActivateSelection();
            break;
        case 115:  // Home
            _pane->SetSelectedVisibleRow(0);
            break;
        case 119:  // End
            _pane->SetSelectedVisibleRow(
                static_cast<int>(_pane->Visible().size()) - 1);
            break;
        default:
            [super keyDown:event];
            return;
    }
    [self scrollRowIntoView:_pane->SelectedVisibleRow()];
    [self setNeedsDisplay:YES];
}

// The tooltip carries the whole heading when the row shows less
// (outline spec 16).
- (NSString*)view:(NSView*)view
    stringForToolTip:(NSToolTipTag)tag
               point:(NSPoint)point
            userData:(void*)data {
    (void)view;
    (void)tag;
    (void)data;
    if (!_pane) return nil;
    const int row = _pane->VisibleRowAt(static_cast<float>(point.y));
    if (row < 0 || !_pane->IsTruncated(row)) return nil;
    const std::string text = _pane->FullTextAt(row);
    if (text.empty()) return nil;
    return [NSString stringWithUTF8String:text.c_str()];
}

- (void)refreshToolTips {
    if (!_pane) return;
    [self removeAllToolTips];
    for (size_t row = 0; row < _pane->Visible().size(); row++) {
        const mac::Rect bounds =
            _pane->RowBounds(static_cast<int>(row), self.bounds.size.width);
        [self addToolTipRect:NSMakeRect(bounds.x, bounds.y, bounds.width,
                                        bounds.height)
                       owner:self
                    userData:NULL];
    }
}

// The outline is a list of headings to assistive technology: each
// visible heading carries its text, its level, and whether it is the
// heading the caret is in (outline spec 22).
- (BOOL)isAccessibilityElement {
    return YES;
}

- (NSString*)accessibilityRole {
    return NSAccessibilityOutlineRole;
}

- (NSString*)accessibilityLabel {
    return @"Outline";
}

- (NSArray*)accessibilityChildren {
    NSMutableArray* children = [NSMutableArray array];
    if (!_pane || _pane->Visible().empty()) return children;

    const int activeRow = _pane->ActiveVisibleRow();
    for (size_t row = 0; row < _pane->Visible().size(); row++) {
        const OutlineItem& item = _pane->Items()[_pane->Visible()[row]];
        const std::string text =
            _pane->FullTextAt(static_cast<int>(row));
        if (text.empty()) continue;

        const mac::Rect bounds = _pane->RowBounds(
            static_cast<int>(row), self.bounds.size.width);
        NSRect rect = [self convertRect:NSMakeRect(bounds.x, bounds.y,
                                                   bounds.width,
                                                   bounds.height)
                                 toView:nil];
        rect = [self.window convertRectToScreen:rect];

        NSAccessibilityElement* element =
            [NSAccessibilityElement accessibilityElementWithRole:
                                        NSAccessibilityStaticTextRole
                                                       frame:rect
                                                       label:[NSString
                                                           stringWithUTF8String:
                                                               text.c_str()]
                                                      parent:self];
        [element setAccessibilityDisclosureLevel:item.level];
        [element setAccessibilitySelected:
            (static_cast<int>(row) == activeRow)];
        [children addObject:element];
    }
    return children;
}

@end
