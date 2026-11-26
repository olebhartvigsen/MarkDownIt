#include "layout/range_highlights.h"

namespace layout {
namespace {

// First index with value >= target. Exact for a non-decreasing vector, and
// the same answer the old linear scan gave; callers only use it after
// checking the mapping really is non-decreasing.
uint32_t LowerBoundMapping(const std::vector<uint32_t>& mapping,
                           uint32_t target) {
    size_t lo = 0;
    size_t hi = mapping.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (mapping[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    return static_cast<uint32_t>(lo);
}

bool IsNonDecreasingMapping(const std::vector<uint32_t>& mapping) {
    for (size_t i = 1; i < mapping.size(); ++i) {
        if (mapping[i] < mapping[i - 1]) return false;
    }
    return true;
}

// Map a document byte offset to a UTF-16 index inside one block, using a
// linear scan. These block mappings are not guaranteed to be sorted: a table
// cell can carry gaps where md4c split the text at a mark or an HTML tag,
// which is why the selection code scans too. A binary search over an unsorted
// map silently highlights the wrong characters.
uint32_t MapSrcToU16Linear(const std::vector<uint32_t>& u16ToSrc,
                           uint32_t u16Len, uint32_t srcOff) {
    for (size_t i = 0; i < u16ToSrc.size(); ++i) {
        if (u16ToSrc[i] >= srcOff) return static_cast<uint32_t>(i);
    }
    return u16Len;
}

// The UTF-16 range one document range covers inside the block. Returns
// end <= start when the block does not cover the range at all, which the
// caller skips.
struct U16Range {
    uint32_t start = 0;
    uint32_t end = 0;
};

U16Range ToU16Range(const TextMatch& m, const BlockTextMap& map,
                    bool monotone) {
    const uint32_t mStart = m.start;
    const uint32_t mEnd = m.start + m.length;
    U16Range out;
    // Matches are document ranges. Skip anything this block does not cover
    // rather than clipping, so a match is never drawn twice by two adjacent
    // blocks.
    if (mEnd <= map.blockStart || mStart >= map.blockEnd) return out;
    out.start = monotone
        ? LowerBoundMapping(*map.u16ToSrc, mStart)
        : MapSrcToU16Linear(*map.u16ToSrc, map.u16Len, mStart);
    out.end = (mEnd >= map.blockEnd)
        ? map.u16Len
        : (monotone ? LowerBoundMapping(*map.u16ToSrc, mEnd)
                    : MapSrcToU16Linear(*map.u16ToSrc, map.u16Len, mEnd));
    if (out.start > map.u16Len) out.start = map.u16Len;
    if (out.end > map.u16Len) out.end = map.u16Len;
    if (out.end <= out.start) return out;  // nothing left to fill
    return out;
}

bool Usable(const BlockTextMap& map) {
    return map.u16ToSrc && !map.u16ToSrc->empty() && map.u16Len != 0 &&
           map.blockEnd > map.blockStart;
}

}  // namespace

bool BlockIntersectsAny(const std::vector<TextMatch>& ranges,
                        const BlockTextMap& map) {
    for (size_t i = 0; i < ranges.size(); ++i) {
        const TextMatch& m = ranges[i];
        if (m.length == 0) continue;
        if (m.start + m.length <= map.blockStart) continue;
        if (m.start >= map.blockEnd) continue;
        return true;
    }
    return false;
}

void PaintMatchHighlights(platform::RenderTarget& target,
                          platform::TextRangeSource& source,
                          const BlockTextMap& map, const PaintBand& band,
                          const std::vector<TextMatch>& matches, int current,
                          const platform::Color& match,
                          const platform::Color& currentMatch) {
    if (!Usable(map)) return;
    if (matches.empty()) return;
    if (!BlockIntersectsAny(matches, map)) return;

    const bool monotone = IsNonDecreasingMapping(*map.u16ToSrc);
    std::vector<platform::Rect> fills;
    std::vector<platform::Rect> currentFills;
    for (size_t i = 0; i < matches.size(); ++i) {
        const TextMatch& m = matches[i];
        if (m.length == 0) continue;
        const U16Range u16 = ToU16Range(m, map, monotone);
        if (u16.end <= u16.start) continue;
        source.AppendRangeRects(
            u16.start, u16.end, band.top, band.bottom,
            static_cast<int>(i) == current ? &currentFills : &fills);
    }
    if (!fills.empty()) target.FillRects(fills, match);
    // The current match on top, the way the interleaved loop drew it last
    // whenever it overlapped a weaker fill.
    if (!currentFills.empty()) target.FillRects(currentFills, currentMatch);
}

void PaintMarkerHighlights(platform::RenderTarget& target,
                           platform::TextRangeSource& source,
                           const BlockTextMap& map, const PaintBand& band,
                           const std::vector<TextMatch>& markers,
                           const platform::Color& fill,
                           const platform::Color& line) {
    if (!Usable(map)) return;
    if (markers.empty()) return;
    if (!BlockIntersectsAny(markers, map)) return;

    const bool monotone = IsNonDecreasingMapping(*map.u16ToSrc);
    std::vector<platform::Rect> fills;
    std::vector<platform::Rect> lines;
    for (size_t i = 0; i < markers.size(); ++i) {
        const TextMatch& m = markers[i];
        if (m.length == 0) continue;
        const U16Range u16 = ToU16Range(m, map, monotone);
        if (u16.end <= u16.start) continue;
        const size_t before = fills.size();
        source.AppendRangeRects(u16.start, u16.end, band.top, band.bottom,
                                &fills);
        for (size_t r = before; r < fills.size(); ++r) {
            // A thin line along the bottom keeps a mark from relying on
            // colour alone.
            const platform::Rect& rect = fills[r];
            platform::Rect under;
            under.x = rect.x;
            under.width = rect.width;
            under.y = rect.y + rect.height - 2.0f;
            if (under.y < rect.y) under.y = rect.y;
            // The line never runs past the rectangle it marks, which for a
            // band under two points tall is the whole band.
            under.height = rect.y + rect.height - under.y;
            lines.push_back(under);
        }
    }
    if (!fills.empty()) target.FillRects(fills, fill);
    if (!lines.empty()) target.FillRects(lines, line);
}

}  // namespace layout