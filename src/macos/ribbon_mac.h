#pragma once

// The command strip at the top of the mac window.
//
// The Windows build runs a ribbon: a Quick Access Toolbar, an
// application menu, and two tabs of groups, Home and Edit. The mac
// build had only the menu bar, so its window looked bare next to the
// Windows one. The strip brings the same commands, in the same tabs
// and groups, to the top of the window. The layout mirrors
// src/ribbon.xml, and tools/check_mac_ribbon.py fails the build when
// the two drift apart.
//
// The menu bar stays. On macOS the system expects the commands there
// too, and the keyboard shortcuts live on the menu items.
//
// The strip owns no state of its own. A click lands on the host (the
// window delegate) through the same selectors the menu items use, and
// the host answers the two questions the strip has: whether a command
// is on, and whether it is available right now. That keeps one copy of
// every command.

#import <AppKit/AppKit.h>

// The window delegate, which carries the commands out the same way it
// does for the menu bar.
@protocol MdRibbonHost <NSObject>
// Whether a toggle command shows as pressed. The command ids are the
// ones the Windows ribbon markup uses ("cmdBold", "cmdWrap", ...).
- (BOOL)ribbonCommandOn:(NSString*)commandId;
// Whether a command is available right now. The table commands are
// only available with the caret in a table, for example.
- (BOOL)ribbonCommandAvailable:(NSString*)commandId;
@end

@interface MdRibbonView : NSView
// The height the strip wants. The window constrains it to this.
+ (CGFloat)ribbonHeight;
// Builds the strip. The host is the target of every command.
+ (MdRibbonView*)ribbonWithHost:(id<MdRibbonHost>)host;
// Re-reads the command states from the host.
- (void)refresh;
@end
