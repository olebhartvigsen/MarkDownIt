#import <Cocoa/Cocoa.h>

// The small overlay that says what the zoom level is while the reader
// changes it.
//
// It is a rounded pill: the system's HUD material at reduced strength,
// so the page under it shows through blurred, with the level in plain
// white digits on top of that, crisp at any transparency. It never takes
// a click or the keyboard, and it goes away by itself a moment after the
// last change.
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
