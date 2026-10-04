#pragma once

// Pure zoom model: the zoom factor's default (100%), supported range
// (25%..400%), step size, and the clamp applied on every change.
// Header-only with no Windows/D2D dependency so the model can be
// unit-tested headless; the renderer shares this one implementation.
namespace zoom {

constexpr float kMinZoom     = 0.25f;   // 25%
constexpr float kMaxZoom     = 4.0f;    // 400%
constexpr float kDefaultZoom = 1.0f;    // 100%

// The zoom guide asks the + and - buttons to move by ten percentage
// points per press, not by a multiplier. A multiplier looks even at
// 100% but is unpredictable elsewhere: x1.25 from 300% jumps 75
// points, and repeated presses never land on a round value. Ten
// points per press walks 100 -> 110 -> 120 and stays predictable at
// both ends of the range.
constexpr float kZoomStepPct = 10.0f;   // percentage points per press

// Clamp a requested factor into [kMinZoom, kMaxZoom].
inline float Clamp(float z) {
    if (z < kMinZoom) return kMinZoom;
    if (z > kMaxZoom) return kMaxZoom;
    return z;
}

// The current zoom as a whole percentage, which is what a percentage
// display has to show. Rounded rather than truncated so that 110% and
// 90% are reachable by stepping from 100%.
inline int Percent(float z) {
    const int p = static_cast<int>(Clamp(z) * 100.0f + 0.5f);
    if (p < static_cast<int>(kMinZoom * 100.0f)) {
        return static_cast<int>(kMinZoom * 100.0f);
    }
    if (p > static_cast<int>(kMaxZoom * 100.0f)) {
        return static_cast<int>(kMaxZoom * 100.0f);
    }
    return p;
}

// One press of + or -. Additive in percentage points, then clamped, so
// the last press at either limit lands exactly on that limit instead of
// stopping just short of it.
inline float Step(float z, bool increase) {
    const float delta = kZoomStepPct / 100.0f;
    const float next = increase ? (z + delta) : (z - delta);
    return Clamp(next);
}

// Zoom for a "fit width" request: scale so the given content width fills
// the available width. Clamped to the same range as every other entry
// point, so fit width cannot produce an out-of-range factor. A
// non-positive or non-finite width yields the default rather than a
// zero or negative scale.
inline float FitWidth(float availableWidthDip, float contentWidthDip) {
    if (!(contentWidthDip > 0.0f)) return kDefaultZoom;
    if (!(availableWidthDip > 0.0f)) return kDefaultZoom;
    const float factor = availableWidthDip / contentWidthDip;
    if (!(factor > 0.0f) || factor != factor) return kDefaultZoom;  // NaN
    return Clamp(factor);
}

}  // namespace zoom
