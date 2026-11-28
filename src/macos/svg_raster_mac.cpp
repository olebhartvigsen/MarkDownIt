// The macOS SVG rasterizer behind platform::SvgRaster.

#include "macos/svg_raster_mac.h"

#include "macos/svg_mac.h"

namespace platform {
namespace {

class MacSvgScene final : public SvgScene {
public:
    bool Load(const std::string& xml) override { return doc_.Load(xml); }
    float Width() const override { return doc_.Width(); }
    float Height() const override { return doc_.Height(); }
    void Draw(RenderTarget& target, TextEngine& engine,
              const Rect& rect) override {
        doc_.Draw(&target, &engine, rect.x, rect.y, rect.width, rect.height);
    }

private:
    SvgDoc doc_;
};

}  // namespace

std::unique_ptr<SvgScene> MacSvgRaster::NewScene() {
    return std::make_unique<MacSvgScene>();
}

}  // namespace platform
