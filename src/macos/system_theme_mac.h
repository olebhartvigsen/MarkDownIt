#pragma once

#include "theme.h"

namespace mac {

// The document's colours: the theme's colour slots, resolved from the
// platform's own semantic colours so the values follow Light Mode and
// Dark Mode and the accessibility settings instead of being fixed
// numbers. Resolve it while the wanted appearance is the current
// drawing appearance.
Palette SystemPalette();

}  // namespace mac
