#pragma once

// The theme's platform spellings, behind portable helpers.
//
// theme.h keeps the palette and the font names in the platform's own
// type: a D2D1_COLOR_F and a wide string on Windows, a plain four float
// struct and a UTF-8 string everywhere else. That is what lets the
// Windows build hand a token straight to Direct2D and DirectWrite. Code
// that is shared by both ports, like the document renderer, needs the
// value and not the spelling, so the conversions live here, with no
// platform type and no platform header:
//
//   - a colour as the 0xRRGGBB integer the SVG rewrite and the diagram
//     changes speak,
//   - a font family name as UTF-8, which is what the text seam's
//     FontSpec carries.
//
// theme.h aliases its old names onto these (Rgb24 stays a wrapper), so
// no mac source had to change when the helpers moved.

#include <cstdint>
#include <string>

#include "textenc.h"

namespace theme {

// A colour as 0xRRGGBB. Each channel is clamped to 0..1 and rounded to
// the nearest of 255 steps, the mapping the SVG transform and the
// diagram changes have always used.
inline uint32_t Rgb24Of(float r, float g, float b) {
    const auto channel = [](float v) {
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        return static_cast<uint32_t>(v * 255.0f + 0.5f);
    };
    return (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

// A font family name as UTF-8, from the UTF-8 form.
inline std::string FamilyUtf8(const std::string& name) { return name; }

// A font family name as UTF-8, from a UTF-8 literal or pointer.
inline std::string FamilyUtf8(const char* name) {
    return name ? std::string(name) : std::string();
}

// A font family name as UTF-8, from the wide form the Windows theme
// keeps. The conversion is the shared one, so a 32-bit wchar_t and a
// 16-bit one with a surrogate pair give the same bytes.
inline std::string FamilyUtf8(const wchar_t* name) {
    return textenc::WideToUtf8(std::wstring(name ? name : L""));
}

}  // namespace theme
