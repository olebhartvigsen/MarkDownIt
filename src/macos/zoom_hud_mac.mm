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
    NSTextField* label_;
    NSTimer* hideTimer_;
    // Bumped on every show, so a fade that finishes late does not hide
    // an overlay a later zoom has just put back up.
    NSInteger generation_;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    // The system's HUD material, blended inside the window: the page
    // behind the overlay is what shows through it, not the desktop.
    [self setMaterial:NSVisualEffectMaterialHUDWindow];
    [self setBlendingMode:NSVisualEffectBlendingModeWithinWindow];
    [self setState:NSVisualEffectStateActive];
    [self setWantsLayer:YES];
    [[self layer] setCornerRadius:10.0];
    [[self layer] setMasksToBounds:YES];

    label_ = [NSTextField labelWithString:@""];
    [label_ setFont:[NSFont monospacedDigitSystemFontOfSize:15.0
                                                    weight:NSFontWeightSemibold]];
    [label_ setTextColor:[NSColor labelColor]];
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
