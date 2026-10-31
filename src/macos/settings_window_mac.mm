// The Settings window, built from the standard controls: a titled
// window with a small form of rows, the way the system's own settings
// windows are laid out. Every row goes through the same commands the
// menu items take.

#import "settings_window_mac.h"

#include "strings_mac.h"

@implementation MacSettingsWindow {
    id<MacSettingsHost> host_;
    NSPopUpButton* widthPopup_;
    NSButton* outlineCheckbox_;
}

- (instancetype)initWithHost:(id<MacSettingsHost>)host {
    // A settings window does not resize, so the style mask carries
    // only the title and the close button.
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 420, 170)
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

    // The rows: a section header, the document width, and the
    // outline switch.
    NSTextField* section =
        [NSTextField labelWithString:macstrings::kSettingsViewSection];
    [section setFont:[NSFont systemFontOfSize:0
                                       weight:NSFontWeightSemibold]];

    NSTextField* widthLabel =
        [NSTextField labelWithString:macstrings::kSettingsWidthLabel];
    widthPopup_ = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(0, 0, 180, 0)
           pullsDown:NO];
    [widthPopup_ addItemsWithTitles:@[
        macstrings::kWidthStandard, macstrings::kWidthWide,
        macstrings::kWidthExtraWide, macstrings::kWidthFull
    ]];
    [widthPopup_ setTarget:self];
    [widthPopup_ setAction:@selector(widthChanged:)];
    NSStackView* widthRow =
        [NSStackView stackViewWithViews:@[ widthLabel, widthPopup_ ]];
    [widthRow setOrientation:NSUserInterfaceLayoutOrientationHorizontal];
    [widthRow setSpacing:8.0];

    outlineCheckbox_ = [NSButton checkboxWithTitle:macstrings::kSettingsOutlineLabel
                                            target:self
                                            action:@selector(outlineChanged:)];

    NSStackView* stack =
        [NSStackView stackViewWithViews:@[ section, widthRow, outlineCheckbox_ ]];
    [stack setOrientation:NSUserInterfaceLayoutOrientationVertical];
    [stack setAlignment:NSLayoutAttributeLeading];
    [stack setSpacing:12.0];
    [stack setEdgeInsets:NSEdgeInsetsMake(20.0, 20.0, 20.0, 20.0)];
    [window setContentView:stack];

    // The window takes the size the rows ask for, with the system's
    // spacing.
    NSSize fit = [stack fittingSize];
    if (fit.width < 240.0 || fit.height < 100.0) {
        fit = NSMakeSize(420.0, 170.0);
    }
    [window setContentSize:fit];
    return self;
}

- (void)showSettings {
    // Read the app's state every time the window comes up, so another
    // part of the app (the View menu, the ribbon) can never leave the
    // controls stale.
    [widthPopup_ selectItemAtIndex:[host_ settingsContentWidthMode]];
    [outlineCheckbox_
        setState:[host_ settingsOutlineVisible] ? NSControlStateValueOn
                                                : NSControlStateValueOff];
    if (![[self window] isVisible]) {
        [[self window] center];
    }
    [self showWindow:nil];
    [[self window] makeKeyAndOrderFront:nil];
}

// The document width row: the index is the width mode the settings
// keep.
- (void)widthChanged:(id)sender {
    (void)sender;
    [host_ applyContentWidthMode:[widthPopup_ indexOfSelectedItem]];
}

// The outline row: the checkbox state is the pane's visibility.
- (void)outlineChanged:(id)sender {
    (void)sender;
    [host_ applyOutlineVisible:[outlineCheckbox_ state] ==
                               NSControlStateValueOn];
}

@end
