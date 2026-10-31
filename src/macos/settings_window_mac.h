// The Settings window.
//
// A macOS app keeps its preferences in a window of its own, opened
// from the app menu with Command comma. The settings themselves stay
// in the same JSON file as before; this window is the standard way to
// reach them.
#pragma once

#import <AppKit/AppKit.h>

// What the window asks the app about, and what it tells it to change.
// The app delegate conforms; the window never knows the delegate type.
@protocol MacSettingsHost <NSObject>

// The document width mode as it is now (0..3, the modes of
// AppSettings::contentWidthMode).
- (NSInteger)settingsContentWidthMode;

// Applies a width mode, through the same path the View menu command
// takes.
- (void)applyContentWidthMode:(NSInteger)mode;

// Whether the outline pane is shown now.
- (BOOL)settingsOutlineVisible;

// Shows or hides the outline pane, through the same path the View
// menu command takes.
- (void)applyOutlineVisible:(BOOL)visible;

@end

// The settings window controller. One instance, kept by the delegate.
@interface MacSettingsWindow : NSWindowController

- (instancetype)initWithHost:(id<MacSettingsHost>)host;

// Shows the window, centered the first time. The controls read the
// app's state every time, so they can never show something stale.
- (void)showSettings;

@end
