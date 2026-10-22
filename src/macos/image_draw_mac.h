#pragma once

// Drawing the images with Core Graphics.
//
// The image cache hands the renderer an opaque handle. The content
// view turns that handle into a Core Graphics image, so the renderer
// never has to know what is behind it.

#include <CoreGraphics/CoreGraphics.h>

#include "seams.h"

namespace mac {

// Draws the image into the rectangle.
//
// The image keeps its aspect ratio and is centred in the rectangle, so
// an image that does not fit the box is scaled down instead of being
// stretched.
void DrawImageIntoContext(CGContextRef context, BitmapHandle image,
                          const Rect& rect);

}  // namespace mac
