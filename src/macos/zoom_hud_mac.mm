#import "zoom_hud_mac.h"

#include <cstdlib>

#include "crash_trace_mac.h"

namespace {

// A quick fade in, a short read, and a slower fade out: the overlay is
// there long enough to be read once, and gone before it is in the way.
const NSTimeInterval kFadeIn = 0.09;
const NSTimeInterval kHold = 0.85;
const NSTimeInterval kFadeOut = 0.25;

// The QA walk is the only place that wants the overlay's own comings
// and goings in the trace.
bool QaWalkRunning() {
    return std::getenv("MARKDOWNIT_QA_STATES") != nullptr;
}

}  // namespace

@implementation MdZoomHud {
    NSVisualEffectView* blur_;
    NSTextField* label_;
    NSTimer* hideTimer_;
    // Bumped on every show, so a fade that finishes late does not hide
    // an overlay a later zoom has just put back up.
    NSInteger generation_;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    [self setWantsLayer:YES];
    [[self layer] setCornerRadius:10.0];
    [[self layer] setMasksToBounds:YES];
    // A hairline of light along the edge, so a dark HUD also has a shape
    // on a dark page.
    [[self layer] setBorderWidth:0.8];
    [[self layer] setBorderColor:
        [[NSColor colorWithWhite:1.0 alpha:0.12] CGColor]];

    // Two layers: the system's HUD material, blended inside the window
    // and held back to just over two thirds, then the level itself on
    // top. The material alone is all but solid, which is no use for an
    // overlay the reader is meant to read the page through, and dimming
    // the whole view would take the digits with it.
    blur_ = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
    // A HUD is dark by definition, and it has to be: with the light
    // appearance the material went white over a white page.
    [blur_ setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameVibrantDark]];
    [blur_ setMaterial:NSVisualEffectMaterialHUDWindow];
    [blur_ setBlendingMode:NSVisualEffectBlendingModeWithinWindow];
    [blur_ setState:NSVisualEffectStateActive];
    [blur_ setAlphaValue:0.72];
    [blur_ setTranslatesAutoresizingMaskIntoConstraints:NO];
    [self addSubview:blur_];
    [NSLayoutConstraint activateConstraints:@[
        [[blur_ leadingAnchor] constraintEqualToAnchor:[self leadingAnchor]],
        [[blur_ trailingAnchor] constraintEqualToAnchor:[self trailingAnchor]],
        [[blur_ topAnchor] constraintEqualToAnchor:[self topAnchor]],
        [[blur_ bottomAnchor] constraintEqualToAnchor:[self bottomAnchor]],
    ]];

    label_ = [NSTextField labelWithString:@""];
    [label_ setFont:[NSFont monospacedDigitSystemFontOfSize:15.0
                                                    weight:NSFontWeightSemibold]];
    // Plain white, not the vibrant label colour: the digits sit above the
    // material, so they keep their strength while the pill behind them
    // stays see-through.
    [label_ setTextColor:[NSColor whiteColor]];
    [label_ setAlignment:NSTextAlignmentCenter];
    [label_ setTranslatesAutoresizingMaskIntoConstraints:NO];
    [self addSubview:label_];
    [NSLayoutConstraint activateConstraints:@[
        [[label_ leadingAnchor]
            constraintEqualToAnchor:[self leadingAnchor]
                           constant:16.0],
        [[label_ trailingAnchor]
            constraintEqualToAnchor:[self trailingAnchor]
                            constant:-16.0],
        [[label_ topAnchor]
            constraintEqualToAnchor:[self topAnchor]
                           constant:7.0],
        [[label_ bottomAnchor]
            constraintEqualToAnchor:[self bottomAnchor]
                            constant:-7.0],
        [[self widthAnchor] constraintGreaterThanOrEqualToConstant:68.0],
    ]];

    [self setAlphaValue:0.0];
    [self setHidden:YES];
    return self;
}

// A HUD is something the reader looks at, never something they click.
- (NSView*)hitTest:(NSPoint)point {
    (void)point;
    return nil;
}

- (BOOL)acceptsFirstResponder {
    return NO;
}

- (BOOL)isShowing {
    return ![self isHidden] && [self alphaValue] > 0.01;
}

- (void)showPercent:(NSInteger)percent {
    [label_ setStringValue:[NSString stringWithFormat:@"%ld%%", (long)percent]];
    [hideTimer_ invalidate];
    hideTimer_ = nil;
    ++generation_;

    const BOOL wasUp = [self isShowing];
    [self setHidden:NO];
    if (!wasUp) {
        [self setAlphaValue:0.0];
        [NSAnimationContext runAnimationGroup:^(NSAnimationContext* context) {
            [context setDuration:kFadeIn];
            [[self animator] setAlphaValue:1.0];
        } completionHandler:nil];
    }

    hideTimer_ = [NSTimer scheduledTimerWithTimeInterval:kHold
                                                  target:self
                                                selector:@selector(holdRanOut:)
                                                userInfo:nil
                                                 repeats:NO];
}

- (void)holdRanOut:(NSTimer*)timer {
    (void)timer;
    hideTimer_ = nil;
    const NSInteger shownFor = generation_;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext* context) {
        [context setDuration:kFadeOut];
        [[self animator] setAlphaValue:0.0];
    } completionHandler:^{
        if (shownFor != self->generation_) return;
        [self setHidden:YES];
        if (QaWalkRunning()) {
            mac::TraceMac("qa zoom hud: hid itself");
        }
    }];
}

- (void)hideNow {
    [hideTimer_ invalidate];
    hideTimer_ = nil;
    ++generation_;
    [self setAlphaValue:0.0];
    [self setHidden:YES];
}

@end
