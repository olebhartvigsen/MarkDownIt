#pragma once

// The native toolbar at the top of the mac window.
//
// The Windows window runs a ribbon; the mac window follows the macOS
// Human Interface Guidelines instead: one standard toolbar in the
// title bar, system symbols without decoration, and no custom window
// chrome. The commands that are not toolbar material stay in the menu
// bar, where macOS expects them anyway, and the keyboard shortcuts
// live on the menu items. See references/macos-native-chrome.md for
// the decisions and the guidance they follow.
//
// The toolbar owns no state of its own. A click lands on the host (the
// window delegate) through the same selectors the menu items use, and
// the host sets the command states in its toolbarSync, so one copy of
// every command remains.

#import <AppKit/AppKit.h>

// Builds the window's toolbar. The host is the window delegate.
@interface MdToolbarController : NSObject <NSToolbarDelegate>
+ (instancetype)controllerWithHost:(id)host;

// The toolbar for the window. The controller stays its delegate, so
// the window keeps the controller alive.
- (NSToolbar*)toolbar;

// The identifiers the toolbar builds by default, in order. The chrome
// check reads them.
+ (NSArray<NSString*>*)defaultItemIdentifiers;
@end