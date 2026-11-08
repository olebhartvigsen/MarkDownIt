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

    // A plain translucent fill, not the system HUD material. The
    // material's own tint follows the window while the label follows the
    // pill, and over a white page the two came out as white digits on a
    // light pill: invisible, in three different arrangements, and only
    // in a light window. A fill of our own is the same in every
    // appearance: the page shows through at a little over a quarter,
    // which is enough to read the level by and little enough to stay out
    // of the way.
    [self setWantsLayer:YES];
    [[self layer] setCornerRadius:10.0];
    [[self layer] setMasksToBounds:YES];
    [[self layer] setBackgroundColor:
        [[NSColor colorWithWhite:0.0 alpha:0.72] CGColor]];
    // A hairline of light along the edge, so the pill also has a shape on
    // a dark page.
    [[self layer] setBorderWidth:0.8];
    [[self layer] setBorderColor:
        [[NSColor colorWithWhite:1.0 alpha:0.12] CGColor]];

    label_ = [NSTextField labelWithString:@""];
    [label_ setFont:[NSFont monospacedDigitSystemFontOfSize:15.0
                                                    weight:NSFontWeightSemibold]];
    // White on the fixed fill, whatever the window is wearing.
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
