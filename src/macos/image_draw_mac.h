#pragma once

// Drawing the images with Core Graphics.
//
// The image cache hands the renderer an opaque handle. The content
// view turns that handle into a Core Graphics image, so the renderer
// never has to know what is behind it.

#include <CoreGraphics/CoreGraphics.h>

#include "platform/seams.h"

namespace mac {

// Draws the image into the rectangle.
//
// The image keeps its aspect ratio and is centred in the rectangle, so
// an image that does not fit the box is scaled down instead of being
// stretched.
void DrawImageIntoContext(CGContextRef context, BitmapHandle image,
                          const Rect& rect);

// Draws a disclosure chevron with the system's own symbol, in the
// asked colour, into the rectangle. Returns false when the symbol is
// not available, so the caller can draw its own mark instead.
bool DrawChevronIntoContext(CGContextRef context, const Rect& rect,
                            bool expanded, const Color& color);

}  // namespace mac
