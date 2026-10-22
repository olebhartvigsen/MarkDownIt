// A platform-neutral fallback for the text layout seam.
//
// The seam has two real implementations: DirectWrite on Windows and Core
// Text on macOS. This file is the third, for every other platform. It
// exists so the shared code, and the test suite that runs against it, can
// be built and run on a machine that has neither text engine.
//
// The layout it produces is an approximation: a fixed advance per
// character, and a line height of one and a half the font size. It is NOT
// pixel accurate, so anything that depends on exact glyph positions
// (the renderer, the caret geometry) does not use it. What it does give
// is a working line breaking, so the parser, the editor and the document
// model can be exercised.
//
// Positions are UTF-16 code units, the same as the other two
// implementations.

#include "textlayout.h"

#if !defined(_WIN32) && !defined(__APPLE__)

#include <cmath>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace {

// The advance per character, in points. The body font is 15 points, so
// this is roughly the width of a digit in a proportional face.
const float kAdvance = 7.5f;
// The line height, as a multiple of the font size.
const float kLineHeightFactor = 1.5f;
const float kFontSize = 15.0f;

// The line height of a layout.
const float kLineHeight = kFontSize * kLineHeightFactor;

}  // namespace

// The fallback layout. The Windows implementation uses the DirectWrite
// type and the macOS one uses the Core Text type, so this needs a type
// of its own.
struct MdFallbackLayout {
    // The text, in UTF-16 code units. Each unit is one code unit of the
    // original UTF-8 text, so the indices line up with the source.
    std::vector<uint16_t> units;
    // The UTF-16 index where each visual line starts and ends.
    std::vector<uint32_t> lineStart;
    std::vector<uint32_t> lineEnd;
    // The width of the layout, in points.
    float width = 0.0f;
};

namespace textlayout {

TextLayoutHandle Create(const std::string& utf8, float maxWidth) {
    // The text, as UTF-16 code units. A UTF-8 character outside the
    // basic plane becomes a surrogate pair, so it counts as two units,
    // which is what the cache expects.
    MdFallbackLayout* layout = new (std::nothrow) MdFallbackLayout();
    if (!layout) return nullptr;

    size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        uint32_t code = c;
        size_t bytes = 1;
        if (c >= 0xF0) {
            code = c & 0x07u;
            bytes = 4;
        } else if (c >= 0xE0) {
            code = c & 0x0Fu;
            bytes = 3;
        } else if (c >= 0xC0) {
            code = c & 0x1Fu;
            bytes = 2;
        }
        for (size_t k = 1; k < bytes && i + k < utf8.size(); ++k) {
            code = (code << 6) |
                   (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
        }
        i += bytes;

        if (code > 0xFFFF) {
            // A surrogate pair.
            code -= 0x10000;
            const uint16_t high =
                static_cast<uint16_t>(0xD800 + (code >> 10));
            const uint16_t low =
                static_cast<uint16_t>(0xDC00 + (code & 0x3FF));
            layout->units.push_back(high);
            layout->units.push_back(low);
        } else {
            layout->units.push_back(static_cast<uint16_t>(code));
        }
    }

    // The line breaking. A line holds as many characters as fit in the
    // width, and a break happens at the last space before that, so words
    // stay together. A text with no spaces breaks at the exact width.
    const float width = maxWidth > 0.0f ? maxWidth : kAdvance * 60.0f;
    const uint32_t maxChars =
        static_cast<uint32_t>(std::floor(width / kAdvance));
    uint32_t lineStart = 0;
    uint32_t lastSpace = 0;
    bool sawSpace = false;
    for (uint32_t index = 0; index < layout->units.size(); ++index) {
        const bool isSpace = layout->units[index] == ' ';
        if (isSpace) {
            lastSpace = index;
            sawSpace = true;
        }
        const uint32_t charsInLine = index - lineStart + 1;
        if (charsInLine < maxChars) continue;

        // The line is full. It ends at the last space, if there was one
        // inside the line, and at the exact width otherwise.
        uint32_t lineEnd;
        if (sawSpace && lastSpace > lineStart) {
            lineEnd = lastSpace;
        } else {
            lineEnd = index + 1;
        }
        layout->lineStart.push_back(lineStart);
        layout->lineEnd.push_back(lineEnd);
        lineStart = lineEnd;
        sawSpace = false;
    }
    if (lineStart < layout->units.size() || layout->lineStart.empty()) {
        layout->lineStart.push_back(lineStart);
        layout->lineEnd.push_back(
            static_cast<uint32_t>(layout->units.size()));
    }

    // The width of the widest line.
    float widest = 0.0f;
    for (size_t i = 0; i < layout->lineStart.size(); ++i) {
        const float lineWidth =
            static_cast<float>(layout->lineEnd[i] - layout->lineStart[i]) *
            kAdvance;
        if (lineWidth > widest) widest = lineWidth;
    }
    layout->width = widest;
    return layout;
}

void Release(TextLayoutHandle handle) {
    delete static_cast<MdFallbackLayout*>(handle);
}

uint32_t LineCount(TextLayoutHandle handle) {
    MdFallbackLayout* layout = static_cast<MdFallbackLayout*>(handle);
    if (!layout) return 0;
    return static_cast<uint32_t>(layout->lineStart.size());
}

float LineHeight(TextLayoutHandle handle, uint32_t lineIndex) {
    MdFallbackLayout* layout = static_cast<MdFallbackLayout*>(handle);
    if (!layout || lineIndex >= layout->lineStart.size()) return 0.0f;
    return kLineHeight;
}

bool LineTextRange(TextLayoutHandle handle, uint32_t lineIndex,
                   float layoutWidth, uint32_t* outStart, uint32_t* outEnd) {
    (void)layoutWidth;
    MdFallbackLayout* layout = static_cast<MdFallbackLayout*>(handle);
    if (!layout || lineIndex >= layout->lineStart.size()) return false;
    if (outStart) *outStart = layout->lineStart[lineIndex];
    if (outEnd) *outEnd = layout->lineEnd[lineIndex];
    return true;
}

Hit HitTestPoint(TextLayoutHandle handle, float x, float y,
                 bool* outTrailing) {
    Hit hit;
    MdFallbackLayout* layout = static_cast<MdFallbackLayout*>(handle);
    if (!layout) return hit;

    // The line the point landed on.
    const uint32_t line =
        static_cast<uint32_t>(std::floor(y / kLineHeight));
    if (line >= layout->lineStart.size()) {
        // Below the last line. The hit lands at the end of the text.
        hit.textPosition = static_cast<uint32_t>(layout->units.size());
        hit.height = kLineHeight;
        hit.inside = true;
        if (outTrailing) *outTrailing = false;
        return hit;
    }

    // The character within the line.
    const uint32_t lineStart = layout->lineStart[line];
    const uint32_t lineEnd = layout->lineEnd[line];
    const float intoLine = x;
    uint32_t offset = lineStart;
    if (intoLine <= 0.0f) {
        offset = lineStart;
    } else {
        const float charWidth = kAdvance;
        const uint32_t chars =
            static_cast<uint32_t>(std::floor(intoLine / charWidth));
        offset = lineStart + chars;
        if (offset > lineEnd) offset = lineEnd;
    }

    // Whether the point landed on the trailing half of the character.
    const float charLeft =
        static_cast<float>(offset - lineStart) * kAdvance;
    const bool trailing = (intoLine - charLeft) > (kAdvance * 0.5f);

    hit.textPosition = offset;
    hit.height = kLineHeight;
    hit.trailing = trailing;
    hit.inside = true;
    if (outTrailing) *outTrailing = trailing;
    return hit;
}

Hit HitTestTextPosition(TextLayoutHandle handle, uint32_t textPosition,
                        float* outX, float* outY) {
    Hit hit;
    MdFallbackLayout* layout = static_cast<MdFallbackLayout*>(handle);
    if (!layout) return hit;

    // The line the position is on.
    uint32_t line = 0;
    for (size_t i = 0; i < layout->lineStart.size(); ++i) {
        if (textPosition >= layout->lineStart[i]) line = static_cast<uint32_t>(i);
    }

    const uint32_t lineStart = layout->lineStart[line];
    const float x = static_cast<float>(textPosition - lineStart) * kAdvance;
    const float y = static_cast<float>(line) * kLineHeight;

    hit.textPosition = textPosition;
    hit.height = kLineHeight;
    hit.inside = true;
    if (outX) *outX = x;
    if (outY) *outY = y;
    return hit;
}

}  // namespace textlayout

#endif  // !_WIN32 && !__APPLE__
