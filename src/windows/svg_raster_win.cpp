// The Windows SVG rasterizer behind platform::SvgRaster.
//
// The scene is the Direct2D SVG document in src/svgdoc.cpp. It paints on
// its own device context, so it cannot ride the seam's offset wrapper:
// the shift the wrapper would have applied is added to the rectangle here,
// once, by walking the wrapper chain.

#include <windows.h>

#include <d2d1_3.h>
#include <dwrite.h>

// The platform headers put the DrawText macro in place; the seam keeps its
// own spelling, so it goes before any header that reads the seam.
#ifdef DrawText
#undef DrawText
#endif

#include "windows/svg_raster_win.h"

#include "platform/offset_target.h"
#include "svgdoc.h"

namespace platform {
namespace {

class WinSvgScene final : public SvgScene {
public:
    explicit WinSvgScene(WinSvgRaster* raster) : raster_(raster) {}

    bool Load(const std::string& xml) override {
        if (!raster_->context()) return false;
        return doc_.Load(raster_->context(), xml) && doc_.Width() > 0.0f &&
               doc_.Height() > 0.0f;
    }

    float Width() const override { return doc_.Width(); }
    float Height() const override { return doc_.Height(); }

    void Draw(RenderTarget& target, TextEngine& engine,
              const Rect& rect) override {
        (void)engine;
        // The rectangle arrives in content column coordinates, the space
        // the shared renderer paints in before its wrappers. The scene
        // draws on the concrete device, so the wrappers' shift has to be
        // read out and applied here.
        float dx = 0.0f;
        float dy = 0.0f;
        const RenderTarget* inner = &target;
        while (const auto* offset = dynamic_cast<const OffsetTarget*>(inner)) {
            dx += offset->dx();
            dy += offset->dy();
            inner = offset->inner();
        }
        doc_.Draw(raster_->context(), raster_->factory(), rect.x + dx,
                  rect.y + dy, rect.width, rect.height);
    }

private:
    WinSvgRaster* raster_;
    svg::SvgDoc doc_;
};

}  // namespace

std::unique_ptr<SvgScene> WinSvgRaster::NewScene() {
    return std::make_unique<WinSvgScene>(this);
}

}  // namespace platform
