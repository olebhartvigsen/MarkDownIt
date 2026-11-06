// macOS implementation of the platform-neutral text layout seam.
//
// The shared layout cache asks a layout a handful of questions; this file
// answers them with Core Text. The Windows counterpart is
// src/textlayout_dwrite.cpp.
//
// Core Text lays text out one line at a time: a CTTypesetter is asked
// where each line breaks, and every break becomes a CTLine. The
// DirectWrite layout on Windows keeps a whole block in one object, so
// the Windows side has to reconstruct a line's text range by hit testing
// the line's edges. Core Text knows the ranges directly, because the
// typesetter produced them.
//
// Positions are reported as UTF-16 code-unit indices, which is what
// Core Text uses natively and what the cache's u16ToSrc mapping is keyed
// on.

#include "textlayout.h"

#if !defined(_WIN32)

#include <CoreText/CoreText.h>
#include <CoreFoundation/CoreFoundation.h>

#include <vector>

// A laid-out block of text: the string plus one CTLine per visual line,
// and the metrics the layout cache asks for.
//
// This lives at global scope because textlayout.h forward-declares it
// there as the macOS TextLayoutHandle.
struct MdCoreTextLayout {
    CFStringRef text = nullptr;       // owned
    std::vector<CTLineRef> lines;      // owned
    std::vector<uint32_t> lineStarts;  // UTF-16 offset of each line start
    std::vector<uint32_t> lineEnds;    // UTF-16 offset just past each line
    std::vector<float> lineHeights;    // height of each line, in points
    std::vector<float> lineWidths;     // width of each line, in points
    float maxWidth = 0.0f;
};

namespace {

// Height of one line, including its leading.
float MeasureLineHeight(CTLineRef line) {
    CGFloat ascent = 0.0f;
    CGFloat descent = 0.0f;
    CGFloat leading = 0.0f;
    CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    return static_cast<float>(ascent + descent + leading);
}

}  // namespace

namespace textlayout {

TextLayoutHandle Create(const std::string& utf8, float maxWidth) {
    if (maxWidth <= 0.0f) return nullptr;

    CFStringRef cfText = CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(utf8.data()),
        static_cast<CFIndex>(utf8.size()), kCFStringEncodingUTF8, false);
    if (!cfText) return nullptr;

    MdCoreTextLayout* layout = new (std::nothrow) MdCoreTextLayout();
    if (!layout) {
        CFRelease(cfText);
        return nullptr;
    }
    layout->text = cfText;
    layout->maxWidth = maxWidth;

    CFAttributedStringRef attributed =
        CFAttributedStringCreate(kCFAllocatorDefault, cfText, nullptr);
    if (!attributed) {
        CFRelease(cfText);
        delete layout;
        return nullptr;
    }
    CTTypesetterRef typesetter = CTTypesetterCreateWithAttributedString(attributed);
    CFRelease(attributed);
    if (!typesetter) {
        CFRelease(cfText);
        delete layout;
        return nullptr;
    }

    const CFIndex total = CFStringGetLength(cfText);
    CFIndex start = 0;
    while (start < total) {
        CFIndex count = CTTypesetterSuggestLineBreak(typesetter, start, maxWidth);
        if (count <= 0) {
            // No progress is possible: treat the rest as one line so the
            // caller still gets a usable layout.
            count = total - start;
            if (count <= 0) break;
        }
        CTLineRef line = CTTypesetterCreateLine(typesetter, CFRangeMake(start, count));
        if (!line) break;
        layout->lines.push_back(line);
        layout->lineStarts.push_back(static_cast<uint32_t>(start));
        layout->lineEnds.push_back(static_cast<uint32_t>(start + count));
        layout->lineHeights.push_back(MeasureLineHeight(line));
        layout->lineWidths.push_back(static_cast<float>(
            CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr)));
        start += count;
    }

    if (layout->lines.empty()) {
        // An empty block still gets one (zero width) line, so hit testing
        // on an empty paragraph lands at offset 0 instead of failing.
        CTLineRef line = CTTypesetterCreateLine(typesetter, CFRangeMake(0, 0));
        if (line) {
            layout->lines.push_back(line);
            layout->lineStarts.push_back(0);
            layout->lineEnds.push_back(0);
            layout->lineHeights.push_back(MeasureLineHeight(line));
            layout->lineWidths.push_back(0.0f);
        }
    }

    CFRelease(typesetter);
    return layout;
}

void Release(TextLayoutHandle handle) {
    MdCoreTextLayout* layout = static_cast<MdCoreTextLayout*>(handle);
    if (!layout) return;
    for (CTLineRef line : layout->lines) {
        if (line) CFRelease(line);
    }
    if (layout->text) CFRelease(layout->text);
    delete layout;
}

uint32_t LineCount(TextLayoutHandle handle) {
    MdCoreTextLayout* layout = static_cast<MdCoreTextLayout*>(handle);
    if (!layout) return 0;
    return static_cast<uint32_t>(layout->lines.size());
}

float LineHeight(TextLayoutHandle handle, uint32_t lineIndex) {
    MdCoreTextLayout* layout = static_cast<MdCoreTextLayout*>(handle);
    if (!layout || lineIndex >= layout->lineHeights.size()) return 0.0f;
    return layout->lineHeights[lineIndex];
}

bool LineTextRange(TextLayoutHandle handle, uint32_t lineIndex,
                   float layoutWidth, uint32_t* outStart, uint32_t* outEnd) {
    (void)layoutWidth;
    MdCoreTextLayout* layout = static_cast<MdCoreTextLayout*>(handle);
    if (!layout || lineIndex >= layout->lineStarts.size()) return false;
    if (outStart) *outStart = layout->lineStarts[lineIndex];
    if (outEnd) *outEnd = layout->lineEnds[lineIndex];
    return true;
}

Hit HitTestPoint(TextLayoutHandle handle, float x, float y, bool* outTrailing) {
    Hit hit;
    MdCoreTextLayout* layout = static_cast<MdCoreTextLayout*>(handle);
    if (!layout || layout->lines.empty()) return hit;

    // Find the line whose vertical band contains y. The lines are stacked
    // in order with no extra leading between them, so accumulate heights.
    uint32_t lineIndex = 0;
    float lineTop = 0.0f;
    for (uint32_t i = 0; i < layout->lines.size(); ++i) {
        const float h = layout->lineHeights[i];
        if (y < lineTop + h || i + 1 == layout->lines.size()) {
            lineIndex = i;
            break;
        }
        lineTop += h;
    }

    CTLineRef line = layout->lines[lineIndex];
    const CGPoint point = CGPointMake(x, y - lineTop);
    const CFIndex index = CTLineGetStringIndexForPosition(line, point);
    hit.textPosition = static_cast<uint32_t>(index < 0 ? 0 : index);

    // Core Text reports the nearest glyph; whether the click landed on
    // the leading or the trailing half decides the caret side.
    const double lineWidth = layout->lineWidths[lineIndex];
    const CGFloat runWidth = CTLineGetOffsetForStringIndex(line, index, nullptr);
    hit.trailing = (static_cast<double>(x) - runWidth) > (lineWidth - runWidth) * 0.5;
    hit.height = layout->lineHeights[lineIndex];
    hit.inside = (y >= lineTop) && (y < lineTop + layout->lineHeights[lineIndex]) &&
                 (x >= 0.0f) && (x <= layout->maxWidth);
    if (outTrailing) *outTrailing = hit.trailing;
    return hit;
}

Hit HitTestTextPosition(TextLayoutHandle handle, uint32_t textPosition,
                        float* outX, float* outY) {
    Hit hit;
    MdCoreTextLayout* layout = static_cast<MdCoreTextLayout*>(handle);
    if (!layout || layout->lines.empty()) return hit;

    // Find the line that contains this UTF-16 position.
    uint32_t lineIndex = 0;
    for (uint32_t i = 0; i < layout->lines.size(); ++i) {
        if (textPosition < layout->lineEnds[i] || i + 1 == layout->lines.size()) {
            lineIndex = i;
            break;
        }
    }

    CTLineRef line = layout->lines[lineIndex];
    const CFIndex index = static_cast<CFIndex>(textPosition);
    const CGFloat offset = CTLineGetOffsetForStringIndex(line, index, nullptr);
    if (outX) *outX = static_cast<float>(offset);

    float lineTop = 0.0f;
    for (uint32_t i = 0; i < lineIndex; ++i) lineTop += layout->lineHeights[i];
    if (outY) *outY = lineTop;

    hit.textPosition = textPosition;
    hit.height = layout->lineHeights[lineIndex];
    hit.inside = true;
    return hit;
}

}  // namespace textlayout

#endif  // !_WIN32
