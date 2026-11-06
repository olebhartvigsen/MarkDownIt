#pragma once

#include "theme.h"

#ifdef __OBJC__
// Forward, so the header works before Cocoa is imported; the Objective-C
// declaration itself has to live at global scope.
@class NSAppearance;
#endif

namespace mac {

// The document's colours: the theme's colour slots, resolved from the
// platform's own semantic colours so the values follow Light Mode and
// Dark Mode and the accessibility settings instead of being fixed
// numbers. Resolve it while the wanted appearance is the current
// drawing appearance.
Palette SystemPalette();

// The chrome colours: the shared chrome palette the Windows build
// paints its pane and strip from, with a warm-dark counterpart for
// Dark Mode. The light values are fixed, so the two platforms match
// pixel for pixel there.
ChromePalette SystemChromePalette();
#ifdef __OBJC__
// The same palette, resolved against one appearance. The chrome of a
// window follows that window's appearance, which can differ from the
// app's.
ChromePalette ChromePaletteForAppearance(NSAppearance* appearance);

// The outline pane as a sidebar: the window material below the pane
// supplies the backdrop, so the pane paints nothing of its own; the
// words and the marks take the system's label colours, and the active
// row wears the system's selection colour with its matching text
// colour, the way a sidebar marks where you are.
ChromePalette SidebarPaletteForAppearance(NSAppearance* appearance);
#endif

}  // namespace mac
