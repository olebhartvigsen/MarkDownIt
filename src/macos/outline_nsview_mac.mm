// The NSView that draws the outline pane.
//
// The view is a thin shell around the OutlinePane: it draws through the
// RenderTarget seam and turns mouse events into a click on the pane.

#import "outline_nsview_mac.h"

#import "outline_pane_mac.h"
#import "cg_target_mac.h"

@implementation MdOutlineView {
    mac::OutlinePane* _pane;
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

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!_pane) return;

    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    if (!context) return;

    // The view is flipped, so the origin is at the top left and the y
    // axis points down. A flip of -1 on the y axis makes Core Graphics
    // draw that way.
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, 0.0, NSMaxY(self.bounds));
    CGContextScaleCTM(context, 1.0, -1.0);

    mac::CoreGraphicsTarget target(context);
    _pane->Draw(&target, static_cast<float>(self.bounds.size.width),
                static_cast<float>(self.bounds.size.height));

    CGContextRestoreGState(context);
}

- (void)mouseDown:(NSEvent*)event {
    if (!_pane) return;

    const NSPoint point = [self convertPoint:[event locationInWindow]
                                    fromView:nil];
    _pane->ClickAt(static_cast<float>(point.x), static_cast<float>(point.y));
    [self setNeedsDisplay:YES];
}

@end
