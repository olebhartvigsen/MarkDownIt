// The Settings window, built from the standard controls: a titled
// window with a small form of rows, the way the system's own settings
// windows are laid out. Every row goes through the same commands the
// menu items take.

#import "settings_window_mac.h"

#include "strings_mac.h"

@implementation MacSettingsWindow {
    id<MacSettingsHost> host_;
    NSButton* outlineCheckbox_;
}

- (instancetype)initWithHost:(id<MacSettingsHost>)host {
    // A settings window does not resize, so the style mask carries
    // only the title and the close button.
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 380, 130)
                  styleMask:(NSWindowStyleMaskTitled |
                             NSWindowStyleMaskClosable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self = [super initWithWindow:window];
    if (!self) {
        return nil;
    }
    host_ = host;
    [window setTitle:macstrings::kSettingsWindowTitle];
    [window setReleasedWhenClosed:NO];

    // The rows: a section header and the outline switch. The
    // document width row is gone on purpose: the pane always fits
    // the window with the margin the padding leaves, so there is
    // nothing to set.
    NSTextField* section =
        [NSTextField labelWithString:macstrings::kSettingsViewSection];
    [section setFont:[NSFont systemFontOfSize:0
                                       weight:NSFontWeightSemibold]];

    outlineCheckbox_ = [NSButton checkboxWithTitle:macstrings::kSettingsOutlineLabel
                                            target:self
                                            action:@selector(outlineChanged:)];

    NSStackView* stack =
        [NSStackView stackViewWithViews:@[ section, outlineCheckbox_ ]];
    [stack setOrientation:NSUserInterfaceLayoutOrientationVertical];
    [stack setAlignment:NSLayoutAttributeLeading];
    [stack setSpacing:12.0];
    [stack setEdgeInsets:NSEdgeInsetsMake(20.0, 20.0, 20.0, 20.0)];
    [window setContentView:stack];

    // The window takes the size the rows ask for, with the system's
    // spacing.
    NSSize fit = [stack fittingSize];
    if (fit.width < 240.0 || fit.height < 100.0) {
        fit = NSMakeSize(380.0, 130.0);
    }
    [window setContentSize:fit];
    return self;
}

- (void)showSettings {
    // Read the app's state every time the window comes up, so another
    // part of the app (the View menu, the ribbon) can never leave the
    // controls stale.
    [outlineCheckbox_
        setState:[host_ settingsOutlineVisible] ? NSControlStateValueOn
                                                : NSControlStateValueOff];
    if (![[self window] isVisible]) {
        [[self window] center];
    }
    [self showWindow:nil];
    [[self window] makeKeyAndOrderFront:nil];
}

// The outline row: the checkbox state is the pane's visibility.
- (void)outlineChanged:(id)sender {
    (void)sender;
    [host_ applyOutlineVisible:[outlineCheckbox_ state] ==
                               NSControlStateValueOn];
}

@end
