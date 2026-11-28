#ifndef MARKDOWNIT_SRC_SVGSCENE_H_
#define MARKDOWNIT_SRC_SVGSCENE_H_

// The SVG rasterizer a document renderer draws a fence picture with.
//
// The shared renderer owns the crop, the placement, the appearance remap
// of the markup and the link boxes; the reader and the drawing calls are
// the platform's. macOS answers with its own parser and the RenderTarget
// seam (src/macos/svg_raster_mac.cpp); the Windows shell keeps Direct2D's
// SVG scene for its own painter until it adopts this class. A renderer
// with no rasterizer draws the fence as the code specimen it is.

#include <memory>
#include <string>

#include "platform/seams.h"

namespace platform {

// One parsed picture. Width and Height are its natural size in points,
// which the layout charges.
class SvgScene {
public:
    virtual ~SvgScene() = default;

    // Reads `xml`, whose light-dark() and dark-page remap the caller has
    // already resolved. False when nothing drawable is in it.
    virtual bool Load(const std::string& xml) = 0;

    virtual float Width() const = 0;
    virtual float Height() const = 0;

    // Draws the picture into `rect`, scaled to it.
    virtual void Draw(RenderTarget& target, TextEngine& engine,
                      const Rect& rect) = 0;
};

// The factory behind the seam. One per platform.
class SvgRaster {
public:
    virtual ~SvgRaster() = default;

    // A scene with nothing loaded in it yet.
    virtual std::unique_ptr<SvgScene> NewScene() = 0;
};

}  // namespace platform

#endif  // MARKDOWNIT_SRC_SVGSCENE_H_
