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
#include "theme.h"

namespace platform {
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

}  // namespace

Palette SystemPalette() {
    // The document palette is assembled in shared code, for both ports,
    // from the appearance the process is drawing in. This function used
    // to build an article palette out of AppKit colours, which meant a
    // document palette existed on one platform only and the two ports
    // rendered different bytes for the same style. What stays native is
    // the one thing the system owns: the accent-coloured selection.
    Palette p = PaletteFor(ActiveMarkdownStyle(), AppearanceFor(nil));
    p.selectionBg = FromColor([NSColor selectedContentBackgroundColor]);
    p.selectionText = FromColor([NSColor selectedTextColor]);
    return p;
}

theme::Appearance AppearanceFor(NSAppearance* appearance) {
    // Nil means "the appearance this code is drawing in", which is the
    // current drawing appearance, not the application's. A window can
    // carry an appearance of its own (the QA walk's dark state does), and
    // reading NSApp.effectiveAppearance ignored it, so the frame went
    // dark while the page kept the light palette.
    NSAppearance* wanted = appearance ? appearance : nil;
    if (!wanted) {
        wanted = [NSAppearance currentDrawingAppearance];
    }
    if (!wanted) wanted = NSApp.effectiveAppearance;
    NSAppearanceName best = [wanted
        bestMatchFromAppearancesWithNames:@[
            NSAppearanceNameAqua, NSAppearanceNameDarkAqua
        ]];
    return [best isEqualToString:NSAppearanceNameDarkAqua]
               ? theme::Appearance::Dark
               : theme::Appearance::Light;
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

// The colours of the sidebar, resolved from the system's own
// semantic colours under the asked appearance, so the pane follows
// Light Mode, Dark Mode, the user's accent colour and the contrast
// settings the way the rest of the system does.
ChromePalette SidebarPaletteForAppearance(NSAppearance* appearance) {
    ChromePalette c = {};
    __block ThemeColor text = {};
    __block ThemeColor muted = {};
    __block ThemeColor activeBg = {};
    __block ThemeColor activeText = {};
    void (^resolve)(void) = ^{
        text = FromColor([NSColor labelColor]);
        muted = FromColor([NSColor secondaryLabelColor]);
        activeBg = FromColor([NSColor selectedContentBackgroundColor]);
        activeText = FromColor([NSColor alternateSelectedControlTextColor]);
    };
    if (appearance) {
        [appearance performAsCurrentDrawingAppearance:resolve];
    } else {
        resolve();
    }
    // The material below the pane is the backdrop; the pane itself
    // stays clear. That is right in light, where the sidebar material is
    // the familiar grey, and wrong in dark, where it sits beside a black
    // page as a light grey slab. In dark the pane takes the page colour,
    // so the outline and the document read as one surface.
    c.paneBg = ThemeColor{0.0f, 0.0f, 0.0f, 0.0f};
    const bool dark =
        appearance != nil &&
        [[appearance name] isEqualToString:NSAppearanceNameDarkAqua];
    if (dark) {
        c.paneBg = DarkPalette().pageBg;
    }
    c.paneText = text;
    c.paneTextMuted = muted;
    c.paneActiveBg = activeBg;
    c.paneActiveText = activeText;
    c.paneArrow = muted;
    return c;
}

ChromePalette SystemChromePalette() {
    return ChromePaletteForAppearance(nil);
}

}  // namespace platform
