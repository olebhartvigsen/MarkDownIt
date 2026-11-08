#import <Cocoa/Cocoa.h>

// The small overlay that says what the zoom level is while the reader
// changes it.
//
// It is a floating label on the system's HUD material, blended with the
// window's own content so the page under it is what shows through. It
// never takes a click or the keyboard, and it goes away by itself a
// moment after the last change.
@interface MdZoomHud : NSVisualEffectView

// Shows the level, fading in if the overlay was not already up, and
// restarts the wait that hides it again.
- (void)showPercent:(NSInteger)percent;

// Takes it down at once.
- (void)hideNow;

// Whether the overlay is on screen right now. The QA walk reads it, so
// the capture and the trace agree about what was shown.
- (BOOL)isShowing;

@end
