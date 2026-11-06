#pragma once

// The NSView that draws the outline pane.
//
// The view is a thin shell around the OutlinePane: it draws through the
// RenderTarget seam and turns mouse events into a click on the pane.
// It is declared in its own header because it is Objective-C, and the
// rest of the app layer is plain C++.

#import <AppKit/AppKit.h>

namespace mac {

class OutlinePane;

}  // namespace mac

// The outline view of the window.
@interface MdOutlineView : NSView

// Creates a view that draws the given pane.
- (instancetype)initWithPane:(mac::OutlinePane*)pane;

// Scrolls a visible row into view, with a small margin, so the active
// heading and the keyboard selection stay readable (outline spec 6.1).
- (void)scrollRowIntoView:(int)row;

// Registers the tooltip rectangles for the visible rows, so a heading
// that shows truncated carries its whole text (outline spec 16).
- (void)refreshToolTips;

@end
