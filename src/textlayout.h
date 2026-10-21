#pragma once
#include <cstdint>
#include <string>

// Platform-neutral text layout seam.
//
// The shared layout cache stores one laid-out block per rendered element
// and asks the layout a handful of questions: how many visual lines it
// has, how tall each line is, which text a line covers, where a point
// lands, and where a text position sits. DirectWrite answers them on
// Windows; Core Text answers them on macOS.
//
// Positions are reported as UTF-16 code-unit indices into the block's
// text. That is what DirectWrite uses natively, and it is what the
// cache's u16ToSrc mapping is keyed on, so the cache converts the
// positions to UTF-8 source offsets itself. Nothing above this seam
// needs to know which text engine produced the layout.

#if defined(_WIN32)

struct IDWriteTextLayout;
using TextLayoutHandle = IDWriteTextLayout*;

#else

struct MdCoreTextLayout;
using TextLayoutHandle = MdCoreTextLayout*;

#endif

namespace textlayout {

// The result of hit testing a point against a layout.
struct Hit {
    // UTF-16 code-unit index of the hit position.
    uint32_t textPosition = 0;
    // Height of the line the hit landed on, in points.
    float height = 0.0f;
    // True when the point landed on the trailing half of a glyph.
    bool trailing = false;
    // True when the point is inside the layout's bounds.
    bool inside = false;
};

// Lay out `utf8` into lines no wider than `maxWidth` points.
// Returns nullptr when the text cannot be laid out.
TextLayoutHandle Create(const std::string& utf8, float maxWidth);

// Release a layout handle. Safe on nullptr.
void Release(TextLayoutHandle handle);

// Number of visual lines in the layout.
uint32_t LineCount(TextLayoutHandle handle);

// Height of one visual line, in points. Returns 0 for an invalid index.
float LineHeight(TextLayoutHandle handle, uint32_t lineIndex);

// The UTF-16 text range a visual line covers.
//
// `layoutWidth` is the width of the whole layout, in points. The Windows
// implementation needs it because DirectWrite has no direct query for a
// line's text range and finds one by hit testing the left and right
// edges of the line; the right edge is probed at the layout width so the
// hit lands on the line's last insertion position.
bool LineTextRange(TextLayoutHandle handle, uint32_t lineIndex,
                   float layoutWidth, uint32_t* outStart, uint32_t* outEnd);

// Hit test a point in layout-local coordinates. `outTrailing` is optional.
Hit HitTestPoint(TextLayoutHandle handle, float x, float y,
                 bool* outTrailing);

// Map a UTF-16 position to a point in layout-local coordinates.
Hit HitTestTextPosition(TextLayoutHandle handle, uint32_t textPosition,
                        float* outX, float* outY);

}  // namespace textlayout
