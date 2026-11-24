#pragma once

// The home screen's colours, on their own.
//
// The welcome screen is branded rather than token-based: a teal band over
// a cool page. It therefore carries its own palette record, with a light
// set (the MarkDownIt site's own) and a dark set (the same brand at
// night). The record lives in a translation unit of its own so the
// contrast suite can hold both sets to the same ratios without linking
// the screen, the SVG reader or a platform header.
//
// A shell asks WelcomePaletteFor(appearance) and hands the result to
// WelcomeScreen::SetPalette. The screen never asks the OS anything.

#include "platform/seams.h"
#include "thememode.h"

namespace platform {

struct WelcomePalette {
    Color page;
    Color card;
    Color cardBorder;
    Color divider;
    Color shadow;
    Color bandTop;
    Color bandMid;
    Color bandBottom;
    Color bandText;
    Color bandMint;
    Color ghostInk;
    Color ghostFill;
    Color ghostHoverInk;
    Color ghostHoverFill;
    Color solid;
    Color solidHover;
    Color solidInk;
    Color title;
    Color preview;
    Color folder;
    Color accent;
    Color brightTeal;
    Color hint;
    Color markDisc;
    Color markInk;
};

WelcomePalette LightWelcomePalette();
WelcomePalette DarkWelcomePalette();
WelcomePalette WelcomePaletteFor(theme::Appearance appearance);

}  // namespace platform
