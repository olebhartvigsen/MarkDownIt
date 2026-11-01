// The mac palette: the theme's colour slots, filled from AppKit's
// semantic colours instead of fixed numbers.
//
// The visual style guide asks for the system's own colours, so the
// document follows Light Mode and Dark Mode and the user's contrast
// settings the way the rest of the system does. The slots keep their
// theme names, so the renderer works from the same struct it always
// did. The mermaid diagram colours stay mermaid's own default theme on
// purpose: the diagram's identity is mermaid's, not the app's, the way
// a rendered diagram keeps its colours on the web.

#import <AppKit/AppKit.h>

#include "system_theme_mac.h"

namespace mac {
namespace {

// One semantic colour as the palette's four floats, resolved through
// sRGB so the numbers mean what the renderer expects.
ThemeColor FromColor(NSColor* color) {
    NSColor* rgb = [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (!rgb) rgb = color;
    CGFloat r = 0.0, g = 0.0, b = 0.0, a = 1.0;
    @try {
        [rgb getRed:&r green:&g blue:&b alpha:&a];
    } @catch (NSException* e) {
        r = 0.0;
        g = 0.0;
        b = 0.0;
        a = 1.0;
    }
    ThemeColor c;
    c.r = static_cast<float>(r);
    c.g = static_cast<float>(g);
    c.b = static_cast<float>(b);
    c.a = static_cast<float>(a);
    return c;
}

// The same colour at a weaker alpha: the subtle fills (a table row, a
// highlight) want a wash of a semantic colour, not a solid one.
ThemeColor Faded(NSColor* color, CGFloat alpha) {
    return FromColor([color colorWithAlphaComponent:alpha]);
}

}  // namespace

Palette SystemPalette() {
    Palette p = BasePalette();
    p.textPrimary = FromColor([NSColor labelColor]);
    p.textMuted = FromColor([NSColor secondaryLabelColor]);
    p.heading = FromColor([NSColor labelColor]);
    p.link = FromColor([NSColor linkColor]);
    p.codeBg = FromColor([NSColor windowBackgroundColor]);
    p.codeBorder = FromColor([NSColor separatorColor]);
    // p.mermaidNodeFill / mermaidNodeBorder / mermaidText stay the base
    // values: the diagrams keep mermaid's default theme.
    p.inlineCodeBg = FromColor([NSColor quaternaryLabelColor]);
    p.quoteBar = FromColor([NSColor separatorColor]);
    p.quoteText = FromColor([NSColor secondaryLabelColor]);
    p.rule = FromColor([NSColor separatorColor]);
    p.tableBorder = FromColor([NSColor separatorColor]);
    p.tableHeaderBg = FromColor([NSColor quaternaryLabelColor]);
    p.tableRowAlt = Faded([NSColor quaternaryLabelColor], 0.55);
    p.selectionBg = FromColor([NSColor selectedContentBackgroundColor]);
    p.selectionText = FromColor([NSColor selectedTextColor]);
    p.findMatchBg = Faded([NSColor systemYellowColor], 0.30);
    p.findCurrentMatchBg = Faded([NSColor systemYellowColor], 0.62);
    p.markerBg = Faded([NSColor systemYellowColor], 0.45);
    p.markerLine = Faded([NSColor systemOrangeColor], 0.85);
    return p;
}

// The chrome palette for the current appearance: the shared fixed
// values in Light Mode, the warm-dark counterparts in Dark Mode.
ChromePalette ChromePaletteForAppearance(NSAppearance* appearance) {
    NSAppearanceName best = [(appearance ? appearance : NSApp.effectiveAppearance)
        bestMatchFromAppearancesWithNames:@[
            NSAppearanceNameAqua, NSAppearanceNameDarkAqua
        ]];
    if ([best isEqualToString:NSAppearanceNameDarkAqua]) {
        return DarkChromePalette();
    }
    return BaseChromePalette();
}

ChromePalette SystemChromePalette() {
    return ChromePaletteForAppearance(nil);
}

}  // namespace mac
