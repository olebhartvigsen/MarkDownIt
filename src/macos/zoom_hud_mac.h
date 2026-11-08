#import <Cocoa/Cocoa.h>

// The small overlay that says what the zoom level is while the reader
// changes it.
//
// It is a rounded pill of translucent black with the level in white on
// top, so the page under it shows through while the number stays legible
// in a light window and a dark one alike. It never takes a click or the
// keyboard, and it goes away by itself a moment after the last change.
@interface MdZoomHud : NSView

// Shows the level, fading in if the overlay was not already up, and
// restarts the wait that hides it again.
- (void)showPercent:(NSInteger)percent;

// Takes it down at once.
- (void)hideNow;

// Whether the overlay is on screen right now. The QA walk reads it, so
// the capture and the trace agree about what was shown.
- (BOOL)isShowing;

@end
