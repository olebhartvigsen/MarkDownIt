#pragma once

// Pure zoom model: the zoom factor's default (100%), supported range
// (25%..400%), step size, and the clamp applied on every change.
// Header-only with no Windows/D2D dependency so the model can be
// unit-tested headless; the renderer shares this one implementation.
namespace zoom {

constexpr float kMinZoom     = 0.25f;   // 25%
constexpr float kMaxZoom     = 4.0f;    // 400%
constexpr float kZoomStep    = 1.25f;   // factor per ZoomIn/ZoomOut
constexpr float kDefaultZoom = 1.0f;    // 100%

// Clamp a requested factor into [kMinZoom, kMaxZoom].
inline float Clamp(float z) {
    if (z < kMinZoom) return kMinZoom;
    if (z > kMaxZoom) return kMaxZoom;
    return z;
}

}  // namespace zoom
