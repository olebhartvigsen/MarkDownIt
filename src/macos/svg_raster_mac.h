#ifndef MARKDOWNIT_SRC_MACOS_SVG_RASTER_MAC_H_
#define MARKDOWNIT_SRC_MACOS_SVG_RASTER_MAC_H_

// The SVG rasterizer on macOS: the shared reader in svg_mac.cpp, drawn
// through the RenderTarget seam. This is the platform's half of
// platform::SvgRaster; the shared document renderer never names the
// reader itself.

#include "svgscene.h"

namespace platform {

class MacSvgRaster final : public SvgRaster {
public:
    std::unique_ptr<SvgScene> NewScene() override;
};

}  // namespace platform

#endif  // MARKDOWNIT_SRC_MACOS_SVG_RASTER_MAC_H_
