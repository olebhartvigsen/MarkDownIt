#pragma once

// The macOS menu bar.
//
// The Windows build has a ribbon with tabs, groups and commands. macOS
// has a menu bar, so the same commands live in the menu the way a Mac
// user expects: File, Edit, View, Format and Table.
//
// The items carry no target of their own. AppKit walks the responder
// chain, which ends at the app delegate, and calls the command there. A
// hand made target that does not implement the selectors is what left
// every item grey.

#include <string>
#include <vector>

@class NSMenu;

namespace mac {

// Builds the main menu bar and returns it.
//
// The returned menu is owned by the caller, which hands it to NSApp.
NSMenu* BuildMainMenu();

}  // namespace mac
