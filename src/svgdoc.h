#pragma once

// Windows paint adapter for the authoritative mac::SvgDoc scene. Parsing,
// geometry, text placement and fitting all belong to that shared scene.

// Some callers already included Win32's DrawText macro. The shared seam
// must retain the same member name/ABI as its portable implementation.
#pragma push_macro("DrawText")
#undef DrawText
#include "macos/svg_mac.h"
#pragma pop_macro("DrawText")

#include <d2d1_3.h>
#include <dwrite.h>
#include <memory>
#include <string>

namespace svg {

class SvgDoc {
public:
    SvgDoc() = default;
    ~SvgDoc();

    // The vector-backed SVG cache transfers sole ownership of the scene.
    SvgDoc(SvgDoc&&) noexcept = default;
    SvgDoc& operator=(SvgDoc&&) noexcept = default;
    SvgDoc(const SvgDoc&) = delete;
    SvgDoc& operator=(const SvgDoc&) = delete;

    bool Load(ID2D1DeviceContext5* ctx, const std::string& xml);
    float Width() const { return scene_ ? scene_->Width() : 0.0f; }
    float Height() const { return scene_ ? scene_->Height() : 0.0f; }

    // A trace and injected metrics let CI exercise the real native adapter
    // against Mac scene commands without comparing system font metrics.
    void Draw(ID2D1DeviceContext5* ctx, IDWriteFactory* dw,
              float x, float y, float w, float h,
              mac::RenderTarget* trace = nullptr, mac::TextEngine* metrics = nullptr);
    void Release();

private:
    std::unique_ptr<mac::SvgDoc> scene_;
};

}  // namespace svg
