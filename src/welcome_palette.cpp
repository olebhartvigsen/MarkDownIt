// The home screen's two palettes. See welcome_palette.h for why they are
// a record of their own.

#include "welcome_palette.h"

namespace platform {

// The colours of the screen, as a palette rather than constants.
//
// The screen is branded: a teal band over a cool page, and the light
// values below are the MarkDownIt site's own. A dark window gets the
// same brand at night, not an inversion: the band keeps its hue and
// deepens, the cards sit one step above the page, and the ink flips.
// Both sets are held to the contrast ratios in
// tests/palette_contrast_test.cpp, so "readable" is checked, not
// asserted.
WelcomePalette LightWelcomePalette() {
    WelcomePalette p;
    p.page = Color{0.957f, 0.973f, 0.969f, 1.000f};
    p.card = Color{1.000f, 1.000f, 1.000f, 1.000f};
    p.cardBorder = Color{0.886f, 0.910f, 0.910f, 1.000f};
    p.divider = Color{0.886f, 0.910f, 0.910f, 1.000f};
    p.shadow = Color{0.043f, 0.118f, 0.133f, 1.000f};
    p.bandTop = Color{0.043f, 0.118f, 0.133f, 1.000f};
    p.bandMid = Color{0.071f, 0.227f, 0.204f, 1.000f};
    p.bandBottom = Color{0.059f, 0.369f, 0.341f, 1.000f};
    p.bandText = Color{1.000f, 1.000f, 1.000f, 1.000f};
    p.bandMint = Color{0.851f, 0.949f, 0.933f, 1.000f};
    p.ghostInk = Color{1.000f, 1.000f, 1.000f, 0.42f};
    p.ghostFill = Color{1.000f, 1.000f, 1.000f, 0.06f};
    p.ghostHoverInk = Color{1.000f, 1.000f, 1.000f, 0.72f};
    p.ghostHoverFill = Color{1.000f, 1.000f, 1.000f, 0.14f};
    p.solid = Color{0.078f, 0.722f, 0.651f, 1.000f};
    p.solidHover = Color{0.051f, 0.580f, 0.533f, 1.000f};
    p.solidInk = Color{0.016f, 0.188f, 0.169f, 1.000f};
    p.title = Color{0.133f, 0.188f, 0.227f, 1.000f};
    p.preview = Color{0.357f, 0.420f, 0.478f, 1.000f};
    p.folder = Color{0.494f, 0.580f, 0.569f, 1.000f};
    p.accent = Color{0.059f, 0.463f, 0.431f, 1.000f};
    p.brightTeal = Color{0.078f, 0.722f, 0.651f, 1.000f};
    p.hint = Color{0.357f, 0.420f, 0.478f, 1.000f};
    p.markDisc = Color{1.0f, 1.0f, 1.0f};
    p.markInk = p.accent;
    return p;
}

WelcomePalette DarkWelcomePalette() {
    WelcomePalette p;
    // A mint-tinted near-black page, so the cards can still be a step
    // lighter without going grey.
    p.page = Color{0.039f, 0.055f, 0.051f, 1.000f};
    p.card = Color{0.090f, 0.118f, 0.114f, 1.000f};
    p.cardBorder = Color{0.165f, 0.204f, 0.200f, 1.000f};
    p.divider = Color{0.165f, 0.204f, 0.200f, 1.000f};
    p.shadow = Color{0.0f, 0.0f, 0.0f, 1.000f};
    // The band keeps the site's hue and deepens; the mint line and the
    // solid teal action stay recognisably the same brand, one step
    // brighter so they read against the darker ground.
    p.bandTop = Color{0.024f, 0.071f, 0.063f, 1.000f};
    p.bandMid = Color{0.043f, 0.176f, 0.157f, 1.000f};
    p.bandBottom = Color{0.043f, 0.290f, 0.259f, 1.000f};
    p.bandText = Color{0.918f, 0.961f, 0.949f, 1.000f};
    p.bandMint = Color{0.659f, 0.847f, 0.808f, 1.000f};
    p.ghostInk = Color{1.000f, 1.000f, 1.000f, 0.55f};
    p.ghostFill = Color{1.000f, 1.000f, 1.000f, 0.08f};
    p.ghostHoverInk = Color{1.000f, 1.000f, 1.000f, 0.85f};
    p.ghostHoverFill = Color{1.000f, 1.000f, 1.000f, 0.16f};
    p.solid = Color{0.078f, 0.722f, 0.651f, 1.000f};
    p.solidHover = Color{0.051f, 0.580f, 0.533f, 1.000f};
    p.solidInk = Color{0.016f, 0.188f, 0.169f, 1.000f};
    p.title = Color{0.902f, 0.929f, 0.922f, 1.000f};
    p.preview = Color{0.604f, 0.655f, 0.643f, 1.000f};
    p.folder = Color{0.494f, 0.561f, 0.545f, 1.000f};
    p.accent = Color{0.243f, 0.827f, 0.729f, 1.000f};
    p.brightTeal = p.accent;
    p.hint = Color{0.561f, 0.627f, 0.612f, 1.000f};
    p.markDisc = Color{0.918f, 0.961f, 0.949f, 1.000f};
    p.markInk = Color{0.043f, 0.290f, 0.259f, 1.000f};
    return p;
}

WelcomePalette WelcomePaletteFor(theme::Appearance appearance) {
    return theme::IsDark(appearance) ? DarkWelcomePalette()
                                     : LightWelcomePalette();
}

}  // namespace platform
