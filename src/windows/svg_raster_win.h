#ifndef MARKDOWNIT_SRC_WINDOWS_SVG_RASTER_WIN_H_
#define MARKDOWNIT_SRC_WINDOWS_SVG_RASTER_WIN_H_

// The Windows SVG rasterizer behind platform::SvgRaster: the Direct2D SVG
// scene in src/svgdoc.cpp, which is what the Windows painter has always
// drawn a picture with. This is the platform's half of the seam; the
// shared document renderer never names the scene itself.

#include <memory>

// windows.h turns DrawText into DrawTextW through a macro, which rips the
// seam's virtual method out of the class. The seam is included with the
// macro gone, the way render_target_win.h does it.
#ifdef DrawText
#undef DrawText
#endif
#include "svgscene.h"

struct ID2D1DeviceContext5;
struct IDWriteFactory;

namespace platform {

class WinSvgRaster final : public SvgRaster {
public:
    // The device the scenes are read and drawn on. A Direct2D scene
    // belongs to the device that made it, so the shell sets this before
    // the frame that draws the picture.
    void SetDevice(ID2D1DeviceContext5* context, IDWriteFactory* factory) {
        context_ = context;
        factory_ = factory;
    }

    ID2D1DeviceContext5* context() const { return context_; }
    IDWriteFactory* factory() const { return factory_; }

    std::unique_ptr<SvgScene> NewScene() override;

private:
    ID2D1DeviceContext5* context_ = nullptr;
    IDWriteFactory* factory_ = nullptr;
};

}  // namespace platform

#endif  // MARKDOWNIT_SRC_WINDOWS_SVG_RASTER_WIN_H_
