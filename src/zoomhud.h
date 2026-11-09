#pragma once

#include <cmath>
#include <cstdint>
#include "zoommodel.h"

// View-only state, independent of the document, device and message loop.
namespace zoomhud {

constexpr std::uint64_t kFadeInMs = 90;
// Like the Mac HUD, the hold deadline is measured from the zoom event,
// not from the end of the fade in.
constexpr std::uint64_t kHoldMs = 850;
constexpr std::uint64_t kFadeOutMs = 250;
constexpr float kFontSizeDip = 15.0f;
constexpr float kTopDip = 18.0f;
constexpr float kMinWidthDip = 68.0f;
constexpr float kPaddingXDip = 16.0f;
constexpr float kPaddingYDip = 7.0f;
constexpr float kRadiusDip = 10.0f;

// Leave room for the pill's stroke. Suppress it rather than clip the
// digits when a resized content pane cannot fit the fixed-size overlay.
inline bool FitsViewport(float viewportWidth, float viewportHeight,
                         float pillWidth, float pillHeight) {
    return viewportWidth >= pillWidth + 2.0f &&
           viewportHeight >= kTopDip + pillHeight + 1.0f;
}

class State {
public:
    // Both factors may be raw requests: clamp before deciding whether the
    // operation changed anything. No-op zooms must not restart the timer.
    bool ShowZoomChange(float before, float after, std::uint64_t now) {
        if (!std::isfinite(before) || !std::isfinite(after)) return false;
        if (zoom::Clamp(before) == zoom::Clamp(after)) return false;
        const bool wasVisible = Opacity(now) > 0.01f;
        percent_ = zoom::Percent(after);
        started_ = now;
        fadeIn_ = !wasVisible;
        active_ = true;
        return true;
    }

    float Opacity(std::uint64_t now) const {
        if (!active_) return 0.0f;
        const auto elapsed = now >= started_ ? now - started_ : 0;
        if (elapsed >= kHoldMs + kFadeOutMs) return 0.0f;
        if (elapsed >= kHoldMs)
            return 1.0f - static_cast<float>(elapsed - kHoldMs) /
                static_cast<float>(kFadeOutMs);
        if (fadeIn_ && elapsed < kFadeInMs)
            return static_cast<float>(elapsed) / static_cast<float>(kFadeInMs);
        return 1.0f;
    }

    // Retire expired state even when a delayed/coalesced timer skips frames.
    bool Tick(std::uint64_t now) {
        if (active_ && now >= started_ &&
            now - started_ >= kHoldMs + kFadeOutMs) Hide();
        return active_;
    }

    void Hide() { active_ = false; }
    bool Active() const { return active_; }
    int Percent() const { return percent_; }

private:
    std::uint64_t started_ = 0;
    int percent_ = 100;
    bool active_ = false;
    bool fadeIn_ = true;
};

}  // namespace zoomhud
