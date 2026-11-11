#include "renderer.h"
#include "blockspacing.h"
#include "hash.h"

#include <cstring>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <cmath>
#include "colortext.h"
#include "crash_trace.h"
#include "imagehelper.h"
#include "svgtext.h"
#include "layout/text_runs.h"
#include "layout/selection_rects.h"
#include "mermaid/model.h"
#include "mermaid/layout_internal.h"
#include "mermaid/layout.h"
#include "mermaid/layout_cache.h"
#include "mermaid/measure_dwrite.h"
#include <cmath>

// crash_trace.h above brings windows.h, which redefines DrawText
// after renderer.h's removal; the player and the seam target the
// shared layout hands back must keep the seam's spelling.
#ifdef DrawText
#undef DrawText
#endif
#include "layout/display_player.h"
#include "windows/render_target_win.h"

#include <utility>

static const float kPtToDip = 96.0f / 72.0f;

// The shared walk answers a textless heading with one phantom line,
// the caret line the spec wants, so both passes charge and paint the
// record as the walk hands it back. The phantom keeps Measure and
// Render on the same advance, and it matches the mac walk the shared
// layout is ported from.

// The engine the shared walk measures through, Batch 3. The renderer
// wraps its DWriteEngine in this adapter, the same role the mac
// renderer's ZoomEngine plays, plus one Windows specific duty the mac
// engine does not need:
//
// 1. Zoom: the shared modules speak point sized runs into the seam
//    and bake the zoom into neither the font sizes nor the wrap
//    width. The raw DWriteEngine measures exactly what it is given,
//    so the zoom goes on here, on the way in.
//
// 2. Words: the modules hand each span as ONE run, and the seam's
//    wrap rule is "a run never breaks inside itself" (the rule the
//    golden engine spells, and the DWrite twin follows; the CoreText
//    twin breaks inside runs, which is the mac engine's own affair).
//    A plain paragraph is a single unbroken run of a whole sentence,
//    so against the raw engine it would be one long line that runs
//    past the column. The adapter splits each run at its newline
//    boundaries (the hard break a code card carries inside itself)
//    and at its spaces, so every break the old DirectWrite layout
//    ever took at a word edge is taken here too. A piece with no
//    space and no break in it (a URL, a run of ideographs) stays one
//    unbreakable piece, the documented consensus of the seam.
class WalkTextEngine final : public platform::TextEngine {
public:
    WalkTextEngine(platform::TextEngine* inner, float zoom)
        : inner_(inner), zoom_(zoom) {}

    void BeginLayout() override { inner_->BeginLayout(); }

    std::vector<platform::TextLine> MeasureWrapped(
        const std::vector<platform::GlyphRun>& runs,
        float maxWidth, float lineSpacing) override {
        std::vector<platform::TextLine> lines;
        std::vector<platform::GlyphRun> words;
        auto flush = [&]() {
            std::vector<platform::GlyphRun> scaled;
            scaled.reserve(words.size());
            for (const platform::GlyphRun& word : words) {
                platform::GlyphRun piece = word;
                if (zoom_ != 1.0f) piece.font.size *= zoom_;
                scaled.push_back(std::move(piece));
            }
            std::vector<platform::TextLine> part =
                inner_->MeasureWrapped(scaled, maxWidth, lineSpacing);
            lines.insert(lines.end(), part.begin(), part.end());
            words.clear();
        };
        for (const platform::GlyphRun& run : runs) {
            size_t at = 0;
            while (at < run.text.size()) {
                size_t end = at;
                while (end < run.text.size() &&
                       run.text[end] != '\n' && run.text[end] != ' ') {
                    ++end;
                }
                platform::GlyphRun piece = run;
                if (end >= run.text.size()) {
                    piece.text = run.text.substr(at, end - at);
                    at = end;
                } else if (run.text[end] == ' ') {
                    // The space closes its word: a trailing space rides
                    // the line as an invisible overhang, never as the
                    // first thing on the next one.
                    piece.text = run.text.substr(at, end - at + 1);
                    at = end + 1;
                } else {
                    piece.text = run.text.substr(at, end - at);
                    if (!piece.text.empty() && piece.text.back() == '\r') {
                        piece.text.pop_back();
                    }
                    at = end + 1;
                }
                if (!piece.text.empty()) words.push_back(std::move(piece));
                if (end < run.text.size() && run.text[end] == '\n') {
                    // A newline ends its line: an empty source line
                    // flushes an empty word set and the engine answers
                    // the one empty line a code row needs.
                    flush();
                }
            }
        }
        flush();
        if (lines.empty()) {
            platform::TextLine line;
            line.height = 12.0f;
            lines.push_back(line);
        }
        return lines;
    }

    uint32_t HitTestOffset(const platform::TextLine& line, float x,
                           bool* outTrailing) const override {
        return inner_->HitTestOffset(line, x, outTrailing);
    }

    platform::Rect OffsetToRect(const platform::TextLine& line,
                                uint32_t offset) const override {
        return inner_->OffsetToRect(line, offset);
    }

    // The spacing question arrives about a font of a measured line, so
    // its size already carries the zoom.
    float LineHeight(const platform::FontSpec& font) const override {
        return inner_->LineHeight(font);
    }

private:
    platform::TextEngine* inner_;
    float zoom_;
};

// The item spans of the shared records, merged the way the mac
// renderer merges its blocks and items: blocks and items both arrive
// in document order, so one forward cursor names the range of items
// whose reference y falls inside a block's band. A block with no
// items keeps an empty range. Measure reads the ranges for the scroll
// width of the pictures, the overlay passes of Render for an image
// item's own rect.
static void MergeNodeItemSpans(
    const std::vector<layout::LaidBlock>& blocks,
    const display::DisplayList& list, size_t nodeCount,
    std::vector<std::pair<uint32_t, uint32_t>>* byNode) {
    const uint32_t none = static_cast<uint32_t>(list.items.size());
    byNode->assign(nodeCount, {none, none});
    std::vector<std::pair<uint32_t, uint32_t>> byBlock(
        blocks.size(), {none, none});
    size_t block = 0;
    for (uint32_t i = 0; i < list.items.size(); ++i) {
        const display::Item& di = list.items[i];
        float refY = 0.0f;
        bool hasY = false;
        if (const auto* fill = std::get_if<display::FillRect>(&di)) {
            refY = fill->rect.y; hasY = true;
        } else if (const auto* fill =
                       std::get_if<display::FillRoundedRect>(&di)) {
            refY = fill->rect.y; hasY = true;
        } else if (const auto* fill =
                       std::get_if<display::FillRoundedRectSides>(&di)) {
            refY = fill->rect.y; hasY = true;
        } else if (const auto* s = std::get_if<display::StrokeRect>(&di)) {
            refY = s->rect.y; hasY = true;
        } else if (const auto* s =
                       std::get_if<display::StrokeRoundedRect>(&di)) {
            refY = s->rect.y; hasY = true;
        } else if (const auto* s = std::get_if<display::StrokeLine>(&di)) {
            refY = s->y0; hasY = true;
        } else if (const auto* s =
                       std::get_if<display::StrokePolygon>(&di)) {
            if (!s->points.empty()) { refY = s->points[0].y; hasY = true; }
        } else if (const auto* f = std::get_if<display::FillPolygon>(&di)) {
            if (!f->points.empty()) { refY = f->points[0].y; hasY = true; }
        } else if (const auto* e = std::get_if<display::FillEllipse>(&di)) {
            refY = e->rect.y; hasY = true;
        } else if (const auto* e = std::get_if<display::StrokeEllipse>(&di)) {
            refY = e->rect.y; hasY = true;
        } else if (const auto* pie = std::get_if<display::PieSlice>(&di)) {
            refY = pie->bounds.y; hasY = true;
        } else if (const auto* t = std::get_if<display::Text>(&di)) {
            refY = t->top; hasY = true;
        } else if (const auto* img = std::get_if<display::Image>(&di)) {
            refY = img->rect.y; hasY = true;
        } else if (const auto* clip = std::get_if<display::PushClip>(&di)) {
            refY = clip->rect.y; hasY = true;
        }
        // PopClip carries no place of its own; it closes the clip of
        // the block the cursor is already in.
        if (!hasY) continue;
        while (block + 1 < blocks.size() &&
               blocks[block + 1].y <= refY) {
            ++block;
        }
        // The record's band owns every item whose extent sits in it.
        if (refY >= blocks[block].y) {
            if (byBlock[block].first > i) byBlock[block].first = i;
            byBlock[block].second = i + 1;
        }
    }
    for (size_t b = 0; b < blocks.size(); ++b) {
        const uint32_t node = blocks[b].nodeIndex;
        if (node >= nodeCount) continue;
        // One node owns one record, so the node's span is the span of
        // its record, empty ranges included.
        (*byNode)[node] = byBlock[b];
    }
}

// Fill the selection highlight for a UTF-16 range of a text layout.
//
// HitTestTextRange returns one DWRITE_HIT_TEST_METRICS per text position,
// so passing a fixed 64-entry stack array silently truncated any selection
// longer than 64 characters. Source view lays the whole document out as a
// single layout, so Ctrl+A there highlighted only the first 64 characters
// and read as "select all does nothing". Rendered view had the same latent
// truncation for long paragraphs, code blocks and table cells.
//
// Walk the layout line by line and touch only the lines that intersect the
// viewport. That keeps the highlight correct for a selection of any length
// and bounds the work per frame to what is actually on screen, which a
// document-wide hit test over a million characters could not.
static void FillSelectionHighlight(ID2D1RenderTarget* rt,
                                   IDWriteTextLayout* layout,
                                   ID2D1SolidColorBrush* brush,
                                   UINT32 u16Start, UINT32 u16End,
                                   float originX, float originY,
                                   float scrollY, float viewportH) {
    if (!rt || !layout || !brush) return;
    if (u16End <= u16Start) return;

    DWRITE_TEXT_METRICS tm = {};
    layout->GetMetrics(&tm);
    if (tm.lineCount == 0) return;

    // GetLineMetrics fills a caller-supplied array and reports how many
    // lines it wrote, so ask once with a zero-size call to size the array.
    UINT32 lineCount = 0;
    layout->GetLineMetrics(nullptr, 0, &lineCount);
    if (lineCount == 0) return;
    std::vector<DWRITE_LINE_METRICS> lines(lineCount);
    if (FAILED(layout->GetLineMetrics(lines.data(), lineCount, &lineCount)))
        return;

    const float viewBottom = scrollY + (viewportH > 0.0f ? viewportH : 0.0f);
    std::vector<DWRITE_HIT_TEST_METRICS> htm;
    float lineTop = originY;
    UINT32 lineStart = 0;
    for (UINT32 i = 0; i < lineCount; ++i) {
        const DWRITE_LINE_METRICS& lm = lines[i];
        const float lineBottom = lineTop + lm.height;
        const UINT32 lineEnd = lineStart + lm.length;

        // Visible band test, in document coordinates.
        if (lineBottom >= scrollY && lineTop <= viewBottom) {
            const UINT32 selLo = u16Start > lineStart ? u16Start : lineStart;
            const UINT32 selHi = u16End < lineEnd ? u16End : lineEnd;
            if (selHi > selLo) {
                const UINT32 count = selHi - selLo;
                htm.resize(count);
                UINT32 hitCount = 0;
                HRESULT hr = layout->HitTestTextRange(
                    selLo, count, originX, originY,
                    htm.data(), count, &hitCount);
                if (SUCCEEDED(hr)) {
                    for (UINT32 h = 0; h < hitCount; ++h) {
                        D2D1_RECT_F r = D2D1::RectF(
                            htm[h].left, htm[h].top,
                            htm[h].left + htm[h].width,
                            htm[h].top + htm[h].height);
                        rt->FillRectangle(r, brush);
                    }
                }
            }
        }
        lineStart = lineEnd;
        lineTop = lineBottom;
        // Lines are in increasing y, so nothing below can be visible.
        if (lineTop > viewBottom) break;
    }
}

// ---------------------------------------------------------------------------
// Search highlight helpers
// ---------------------------------------------------------------------------
//
// FillMatchHighlights runs per visible block, and a one character search in
// a big document can carry hundreds of matches. Everything expensive that
// does not depend on the individual match is therefore hoisted into one
// cache per block, matches that fall outside the viewport skip their hit
// test entirely, and the src to UTF-16 mapping uses a binary search when the
// block's mapping is non-decreasing (which it is everywhere the mapping is
// built in source order) instead of a linear scan per match.

struct LayoutLineCache {
    std::vector<DWRITE_LINE_METRICS> lines;
    std::vector<float> lineTop;     // line top, relative to the origin
    std::vector<UINT32> lineStart;  // UTF-16 index of each line start
    UINT32 totalU16 = 0;
    bool valid = false;
};

static void BuildLayoutLineCache(IDWriteTextLayout* layout,
                                 LayoutLineCache& cache) {
    cache = LayoutLineCache();
    if (!layout) return;
    UINT32 count = 0;
    // Sizing probe: DirectWrite reports the needed line count through the
    // E_NOT_SUFFICIENT_BUFFER failure path, so a failing HRESULT here is
    // the normal answer, not an error. Only a zero count means there is
    // nothing to cache. (Checking FAILED() on the probe rejected the count
    // every time, left the cache invalid, and silently dropped every fill
    // that depended on it, search matches and markers alike.)
    layout->GetLineMetrics(nullptr, 0, &count);
    if (count == 0) return;
    cache.lines.resize(count);
    if (FAILED(layout->GetLineMetrics(cache.lines.data(), count, &count)) ||
        count == 0) {
        return;
    }
    cache.lines.resize(count);
    cache.lineTop.resize(count);
    cache.lineStart.resize(count);
    float y = 0.0f;
    UINT32 u = 0;
    for (UINT32 i = 0; i < count; ++i) {
        cache.lineTop[i] = y;
        cache.lineStart[i] = u;
        y += cache.lines[i].height;
        u += cache.lines[i].length;
    }
    cache.totalU16 = u;
    cache.valid = true;
}

// First index with value >= target. Exact for a non-decreasing vector, and
// the same answer the old linear scan gave; callers only use it after
// checking the mapping really is non-decreasing.
static UINT32 LowerBoundMapping(const std::vector<uint32_t>& mapping,
                                uint32_t target) {
    size_t lo = 0;
    size_t hi = mapping.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (mapping[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    return static_cast<UINT32>(lo);
}

static bool IsNonDecreasingMapping(const std::vector<uint32_t>& mapping) {
    for (size_t i = 1; i < mapping.size(); ++i) {
        if (mapping[i] < mapping[i - 1]) return false;
    }
    return true;
}

// Same fill as FillSelectionHighlight, but for a range drawn from the
// per-block line cache: no per-match layout setup, and a match that sits
// outside the visible band never reaches HitTestTextRange.
static void FillRangeFromCache(ID2D1RenderTarget* rt,
                               IDWriteTextLayout* layout,
                               const LayoutLineCache& cache,
                               ID2D1SolidColorBrush* brush,
                               UINT32 u16Start, UINT32 u16End,
                               float originX, float originY,
                               float scrollY, float viewportH,
                               ID2D1SolidColorBrush* underlineBrush) {
    if (!rt || !layout || !brush || !cache.valid) return;
    if (u16End <= u16Start) return;
    if (u16Start >= cache.totalU16) return;
    if (u16End > cache.totalU16) u16End = cache.totalU16;

    const float viewBottom = scrollY + (viewportH > 0.0f ? viewportH : 0.0f);
    const size_t lineCount = cache.lines.size();

    // Line that contains the match start: the last line whose first
    // character is at or before u16Start (a match commonly starts in the
    // middle of a line, so a plain lower bound would start too late).
    size_t first = 0;
    {
        size_t lo = 0, hi = lineCount;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (cache.lineStart[mid] <= u16Start) lo = mid + 1;
            else hi = mid;
        }
        first = (lo == 0) ? 0 : lo - 1;
    }

    // First line whose bottom is inside the visible band, never before the
    // match's first line: a match far above the viewport costs no hit test.
    size_t start = first;
    {
        size_t lo = first, hi = lineCount;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (originY + cache.lineTop[mid] + cache.lines[mid].height <
                scrollY) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo > start) start = lo;
    }

    std::vector<DWRITE_HIT_TEST_METRICS> htm;
    for (size_t i = start; i < lineCount; ++i) {
        const float lineTop = originY + cache.lineTop[i];
        if (lineTop > viewBottom) break;
        const UINT32 lineStart = cache.lineStart[i];
        const UINT32 lineEnd = lineStart + cache.lines[i].length;
        const UINT32 selLo = u16Start > lineStart ? u16Start : lineStart;
        const UINT32 selHi = u16End < lineEnd ? u16End : lineEnd;
        if (selHi > selLo) {
            const UINT32 count = selHi - selLo;
            htm.resize(count);
            UINT32 hitCount = 0;
            HRESULT hr = layout->HitTestTextRange(
                selLo, count, originX, originY,
                htm.data(), count, &hitCount);
            if (SUCCEEDED(hr)) {
                for (UINT32 h = 0; h < hitCount; ++h) {
                    D2D1_RECT_F r = D2D1::RectF(
                        htm[h].left, htm[h].top,
                        htm[h].left + htm[h].width,
                        htm[h].top + htm[h].height);
                    rt->FillRectangle(r, brush);
                    if (underlineBrush) {
                        // A thin line along the bottom keeps a mark from
                        // relying on colour alone.
                        D2D1_RECT_F line = r;
                        line.top = r.bottom - 2.0f;
                        if (line.top < r.top) line.top = r.top;
                        rt->FillRectangle(line, underlineBrush);
                    }
                }
            }
        }
        if (lineEnd >= u16End) break;
    }
}

Renderer::Renderer() {}
Renderer::~Renderer() { Release(); }

void Renderer::SetSearchMatches(const std::vector<TextMatch>* matches,
                                int currentIndex) {
    // A null pointer means no highlighting. Clearing here rather than at the
    // paint sites means a closed find bar can never leave a stale fill on
    // screen, and the paint paths only have to test the pointer.
    if (!matches) {
        searchMatches_ = nullptr;
        searchCurrentIndex_ = -1;
        return;
    }
    searchMatches_ = matches;
    searchCurrentIndex_ = currentIndex;
    // An index past the end, or one that only makes sense for a non-empty
    // list, would highlight nothing at all. Normalise it so every match still
    // gets the weaker fill instead of silently losing its highlight.
    if (!matches->empty() &&
        (currentIndex < 0 ||
         currentIndex >= static_cast<int>(matches->size()))) {
        searchCurrentIndex_ = -1;
    }
}

void Renderer::SetMarkers(const std::vector<TextMatch>* markers,
                          bool visible) {
    // The marker layer is driven by a borrowed pointer plus a visibility
    // flag, the same contract as the search highlights. A null pointer or
    // a hidden layer skips the pass entirely.
    markerRanges_ = markers;
    markersVisible_ = visible;
}

// Map a document byte offset to a UTF-16 index inside one block, using a
// LINEAR scan over u16ToSrc. These block mappings are not guaranteed to be
// sorted: a table cell can carry gaps where md4c split the text at a mark or
// an HTML tag, which is why the existing selection code scans too. A binary
// search over an unsorted map silently highlights the wrong characters.
static UINT32 MapSrcToU16Linear(const std::vector<uint32_t>& u16ToSrc,
                                UINT32 u16Len, uint32_t srcOff) {
    for (size_t i = 0; i < u16ToSrc.size(); ++i) {
        if (u16ToSrc[i] >= srcOff) return static_cast<UINT32>(i);
    }
    return u16Len;
}

void Renderer::FillMatchHighlights(ID2D1RenderTarget* rt,
                                   IDWriteTextLayout* layout,
                                   ID2D1SolidColorBrush* matchBrush,
                                   ID2D1SolidColorBrush* currentBrush,
                                   const std::vector<uint32_t>& u16ToSrc,
                                   UINT32 u16Len,
                                   uint32_t blockStart, uint32_t blockEnd,
                                   float originX, float originY,
                                   float scrollY, float viewportH) {
    if (!rt || !layout || !matchBrush || !currentBrush) return;
    if (!searchMatches_ || searchMatches_->empty()) return;
    if (u16ToSrc.empty() || u16Len == 0) return;

    // With hundreds of matches, most blocks are not touched at all. Check
    // first, so the per-block cache below is never built for them.
    bool touched = false;
    for (size_t i = 0; i < searchMatches_->size(); ++i) {
        const TextMatch& m = (*searchMatches_)[i];
        if (m.length == 0) continue;
        if (m.start + m.length <= blockStart || m.start >= blockEnd) continue;
        touched = true;
        break;
    }
    if (!touched) return;

    LayoutLineCache cache;
    BuildLayoutLineCache(layout, cache);

    // The binary search is only exact while the mapping is non-decreasing.
    // Every mapping is built in source order, so it is; the check costs one
    // pass and keeps the old per-call linear scan as the safe fallback.
    const bool monotone = IsNonDecreasingMapping(u16ToSrc);

    for (size_t i = 0; i < searchMatches_->size(); ++i) {
        const TextMatch& m = (*searchMatches_)[i];
        if (m.length == 0) continue;
        const uint32_t mStart = m.start;
        const uint32_t mEnd = m.start + m.length;
        // Matches are document ranges. Skip anything this block does not
        // cover rather than clipping, so a match is never drawn twice by two
        // adjacent blocks.
        if (mEnd <= blockStart || mStart >= blockEnd) continue;

        ID2D1SolidColorBrush* brush =
            (static_cast<int>(i) == searchCurrentIndex_) ? currentBrush
                                                         : matchBrush;
        UINT32 u16Start = monotone
            ? LowerBoundMapping(u16ToSrc, mStart)
            : MapSrcToU16Linear(u16ToSrc, u16Len, mStart);
        UINT32 u16End = (mEnd >= blockEnd)
            ? u16Len
            : (monotone ? LowerBoundMapping(u16ToSrc, mEnd)
                        : MapSrcToU16Linear(u16ToSrc, u16Len, mEnd));
        if (u16Start > u16Len) u16Start = u16Len;
        if (u16End > u16Len) u16End = u16Len;
        if (u16End <= u16Start) continue;
        FillRangeFromCache(rt, layout, cache, brush, u16Start, u16End,
                           originX, originY, scrollY, viewportH, nullptr);
    }
}

void Renderer::FillMarkerHighlights(ID2D1RenderTarget* rt,
                                    IDWriteTextLayout* layout,
                                    ID2D1SolidColorBrush* fillBrush,
                                    ID2D1SolidColorBrush* lineBrush,
                                    const std::vector<uint32_t>& u16ToSrc,
                                    UINT32 u16Len,
                                    uint32_t blockStart, uint32_t blockEnd,
                                    float originX, float originY,
                                    float scrollY, float viewportH) {
    if (!rt || !layout || !fillBrush || !lineBrush) return;
    if (!HasMarkers()) return;
    if (u16ToSrc.empty() || u16Len == 0) return;

    // Same shape as FillMatchHighlights: the cheap emptiness check first,
    // one line cache per block, binary mapping with the linear fallback,
    // and a viewport skip per range.
    bool touched = false;
    for (size_t i = 0; i < markerRanges_->size(); ++i) {
        const TextMatch& m = (*markerRanges_)[i];
        if (m.length == 0) continue;
        if (m.start + m.length <= blockStart || m.start >= blockEnd) continue;
        touched = true;
        break;
    }
    if (!touched) return;

    LayoutLineCache cache;
    BuildLayoutLineCache(layout, cache);
    const bool monotone = IsNonDecreasingMapping(u16ToSrc);

    for (size_t i = 0; i < markerRanges_->size(); ++i) {
        const TextMatch& m = (*markerRanges_)[i];
        if (m.length == 0) continue;
        const uint32_t mStart = m.start;
        const uint32_t mEnd = m.start + m.length;
        if (mEnd <= blockStart || mStart >= blockEnd) continue;
        UINT32 u16Start = monotone
            ? LowerBoundMapping(u16ToSrc, mStart)
            : MapSrcToU16Linear(u16ToSrc, u16Len, mStart);
        UINT32 u16End = (mEnd >= blockEnd)
            ? u16Len
            : (monotone ? LowerBoundMapping(u16ToSrc, mEnd)
                        : MapSrcToU16Linear(u16ToSrc, u16Len, mEnd));
        if (u16Start > u16Len) u16Start = u16Len;
        if (u16End > u16Len) u16End = u16Len;
        if (u16End <= u16Start) continue;
        FillRangeFromCache(rt, layout, cache, fillBrush, u16Start, u16End,
                           originX, originY, scrollY, viewportH, lineBrush);
    }
}

LayoutMetrics Renderer::ComputeMetrics() const {
    LayoutMetrics m = ScaleMetrics(BaseMetrics(), zoom_);
    // Override the content width cap based on the user's width setting.
    // Mode 3 (Full) means no cap, so we use a very large value.
    if (contentWidthMode_ == 0) {
        m.maxContentWidth = 800.0f * zoom_;      // Standard (default)
    } else if (contentWidthMode_ == 1) {
        m.maxContentWidth = 960.0f * zoom_;
    } else if (contentWidthMode_ == 2) {
        m.maxContentWidth = 1600.0f * zoom_;
    } else {
        m.maxContentWidth = 100000.0f;           // Full window width
    }
    return m;
}

std::shared_ptr<const layout::DocumentLayout> Renderer::SharedLayoutPass(
    const Document& doc, IDWriteFactory* dw,
    const LayoutMetrics& metrics,
    const Palette* palette, float contentWidth,
    std::vector<const layout::LaidBlock*>* byNode,
    const Selection* sel) {
    LayoutCacheSlot& slot = layoutSlots_[sel ? 1 : 0];
    LayoutCacheKey key;
    key.doc = &doc;
    if (layoutReuse_ && slot.layout && slot.key.doc == &doc) {
        // A scroll frame never edits the document (the app asks for
        // reuse only then), so the digest below, a walk of every node
        // and inline, would only confirm the last one.
        key.fingerprint = slot.key.fingerprint;
    } else {
        // A cheap digest of what the walk reads from the document, so an
        // edit made in place cannot hand back the old layout.
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        mix(doc.nodes.size());
        for (const Node& n : doc.nodes) {
            mix(static_cast<uint64_t>(n.block));
            mix(n.srcOffset);
            mix(n.srcLength);
            mix(n.raw.size());
            mix(n.children.size());
            mix(n.rows.size());
            for (const InlineBlock& c : n.children) {
                mix(c.text.size());
                mix(c.srcLength);
            }
        }
        key.fingerprint = h;
    }
    key.contentWidth = contentWidth;
    key.zoom = zoom_;
    key.editing = editing_;
    key.wrap = wrapEnabled_;
    key.standaloneSvg = standaloneSvg_;
    key.widthMode = contentWidthMode_;
    key.pictures = svg_cache_.size() + img_cache_.size() + img_size_cache_.size();
    key.caret = sel ? sel->active.offset : 0xffffffffu;
    if (!(layoutReuse_ && slot.layout && slot.key == key)) {
        slot.layout = RunSharedLayoutPass(doc, dw, metrics, palette,
                                          contentWidth, sel);
        slot.key = key;
    }
    std::shared_ptr<const layout::DocumentLayout> out = slot.layout;
    if (byNode) {
        byNode->assign(doc.nodes.size(), nullptr);
        for (const layout::LaidBlock& b : out->blocks) {
            if (b.nodeIndex < byNode->size()) {
                (*byNode)[b.nodeIndex] = &b;
            }
        }
    }
    return out;
}

std::shared_ptr<const layout::DocumentLayout> Renderer::RunSharedLayoutPass(
    const Document& doc, IDWriteFactory* dw,
    const LayoutMetrics& metrics,
    const Palette* palette, float contentWidth,
    const Selection* sel) {
    // The walk measures through the adapter, never the raw engine:
    // the adapter owns the zoom and the word splitting (the same two
    // duties the mac renderer hands its ZoomEngine; see WalkTextEngine
    // at the top of this file). The engine itself is the renderer's
    // own, or the test replacement SetWalkEngineForTesting installed.
    WalkTextEngine engine(TextEngineForWalk(), zoom_);
    layout::LayoutInput in;
    in.doc = &doc;
    in.metrics = &metrics;
    in.palette = palette;
    in.contentWidth = contentWidth;
    in.zoom = zoom_;
    in.editing = editing_;
    in.engine = &engine;
    // The image size answers, the same seam the mac renderer fills:
    // a "svg:" url questions a fence picture, a raster url questions
    // the intrinsic size the inline cache knows.
    in.imageSize = [this, &doc, sel, contentWidth](const std::string& url) {
        return ImageSizeForUrl(doc, url, sel, contentWidth);
    };
    // The flowchart relaid the painter shares with the walk: the very
    // layout object DrawMermaidBlock lays its hands on, its cache and
    // its measure format, so the charged band is the ink the frame
    // paints. A null factory keeps the parse provisional layout.
    in.flowchartRelaid = [this, dw](const Node& n) {
        return GetMermaidLayout(dw, n);
    };
    layout::DocumentLayout out = layout::LayoutDocument(in);

    // The one Text item the modules build by hand is the list marker:
    // its line box borrows the measured first line, but the run in it
    // never passed the engine, so unlike every other run it is still
    // in point space when the zoom is not one. The marker is the only
    // Text item of the list with no source length of its own, no
    // measured width on its line, and the clip of the gutter around
    // it, so it is found here once and scaled to the zoomed space the
    // replay draws the rest of the document at, the mac pass's own
    // fixup applied to both callers so the list it hands out stays
    // coherent.
    if (zoom_ != 1.0f) {
        bool insideMarkerClip = false;
        for (display::Item& item : out.list.items) {
            if (std::holds_alternative<display::PushClip>(item)) {
                insideMarkerClip = true;
            } else if (std::holds_alternative<display::PopClip>(item)) {
                insideMarkerClip = false;
            } else if (auto* text =
                           std::get_if<display::Text>(&item)) {
                if (insideMarkerClip && text->srcLength == 0 &&
                    text->line.width == 0.0f) {
                    for (platform::GlyphRun& run : text->line.runs) {
                        run.font.size *= zoom_;
                    }
                }
            }
        }
    }

    return std::make_shared<const layout::DocumentLayout>(std::move(out));
}

layout::ImageSize Renderer::ImageSizeForUrl(const Document& doc,
                                            const std::string& url,
                                            const Selection* sel,
                                            float drawW) {
    layout::ImageSize size;
    if (url.rfind("svg:", 0) == 0) {
        // The picture question of a ```svg fence arrives under the
        // "svg:" convention, the fence text itself. It answers only
        // when the picture form is a fact here: a standalone viewer,
        // the caret outside the fence and a parse with a positive
        // size. Anything else answers nothing and the walk lays the
        // fence out as the code specimen it is.
        for (const Node& n : doc.nodes) {
            if (n.block != BlockKind::CodeBlock || n.lang != "svg") {
                continue;
            }
            if (url != "svg:" + layout::Utf32ToUtf8(n.raw)) continue;
            svg::SvgDoc* docPicture = SvgPictureDocFor(n, sel, drawW);
            if (docPicture && docPicture->Width() > 0.0f &&
                docPicture->Height() > 0.0f) {
                size.width = docPicture->Width();
                size.height = docPicture->Height();
                size.known = true;
            }
            break;
        }
        return size;
    }
    // A raster image: the intrinsic size the inline cache knows. A
    // local file is read here, the way Measure always did; a remote
    // one stays unknown until the draw pass has fetched it, and the
    // walk charges the placeholder band until RememberImageSize
    // corrects the height on a later pass.
    float w = 0.0f;
    float h = 0.0f;
    if (InlineImageSize(url, &w, &h) && w > 0.0f && h > 0.0f) {
        size.width = w;
        size.height = h;
        size.known = true;
    }
    return size;
}

svg::SvgDoc* Renderer::SvgPictureDocFor(const Node& n, const Selection* sel,
                                        float drawW) {
    // The picture form of a ```svg fence: the standalone setting, the
    // svg language and a parse that produced a positive size. The caret
    // inside the fence keeps the code specimen, the edit view of the
    // raw text, exactly as the inline pass always read it.
    if (!standaloneSvg_ || n.block != BlockKind::CodeBlock ||
        n.lang != "svg" || !d2d_ctx5_) {
        return nullptr;
    }
    // Only while editing: a reader's caret (left behind by the source
    // view, or a click) must not turn the picture into its code.
    if (sel && editing_) {
        const uint32_t off = sel->active.offset;
        if (off >= n.srcOffset && off < n.srcOffset + n.srcLength) {
            return nullptr;
        }
    }
    const float availW = drawW - 2.0f * ComputeMetrics().codePad;
    svg::SvgDoc* doc = GetSvgDoc(n, availW);
    if (!doc || doc->Width() <= 0.0f || doc->Height() <= 0.0f) {
        return nullptr;
    }
    return doc;
}

platform::BitmapHandle Renderer::ImageHandleFor(
    const display::Image& item) {
    // The replay draws a raster image through this handle; the vector
    // pictures answer nothing here, their pixels are the renderer's
    // own SvgDoc paint in the overlay pass. The cache lookup is the
    // bitmap phase zero fetched this frame.
    if (item.url.rfind("svg:", 0) == 0) return nullptr;
    for (const auto& e : img_cache_) {
        if (e.url == item.url) return e.bmp;
    }
    return nullptr;
}

void Renderer::PaintSharedOverlays(
    ID2D1RenderTarget* rt, IDWriteFactory* dw, const Document& doc,
    const std::vector<const layout::LaidBlock*>& byNode,
    const layout::DocumentLayout& shared, const LayoutMetrics& m,
    float contentWidth, float originX, float topOffsetDip,
    const Selection* sel, float scrollY) {
    // Every band at the record's own y, the frame the walk measured:
    // padTop and the caller's top offset stack on it, so paint and
    // advance are the same number in both passes.
    // The walk's adapter, the same engine the records were measured
    // with: the wash rectangles below ask their geometry questions
    // through it, so the shapes answer the frame they will paint in.
    WalkTextEngine walkEngine(TextEngineForWalk(), zoom_);

    for (size_t nodeIdx = 0; nodeIdx < doc.nodes.size(); ++nodeIdx) {
        const Node& n = doc.nodes[nodeIdx];
        if (nodeIdx >= byNode.size()) continue;
        const layout::LaidBlock* b = byNode[nodeIdx];
        if (!b) continue;
        const float bandY = m.padTop + topOffsetDip + b->y;
        const float bandX = originX + b->x;
        const float bandW = b->width > 0.0f ? b->width : contentWidth;
        switch (n.block) {
            case BlockKind::CodeBlock: {
                // A standalone .svg fence in picture form: the page
                // image at natural size times the zoom, no card, the
                // walk's record geometry and the old DrawSvgBlock's
                // own numbers.
                if (n.lang == "svg" && standaloneSvg_) {
                    svg::SvgDoc* picture = SvgPictureDocFor(n, sel, bandW);
                    if (picture) {
                        picture->Draw(d2d_ctx5_, dw, bandX + m.codePad,
                                      bandY + m.codePad,
                                      picture->Width() * zoom_,
                                      picture->Height() * zoom_);
                        break;
                    }
                }
                // The code card is an opaque surface the replay also
                // fills: the card printer runs again above the list,
                // with its washes and glyphs and the caret record the
                // cache keeps for the block, so what ends up on screen
                // is exactly the card the per node painter always
                // drew, now at the walk's band.
                float outH = 0.0f;
                // The selection wash comes from the shared rect rule
                // now, the same shape the RectsForRange twin reports:
                // one rectangle per wrapped line of the walk's record,
                // placed by the frame the record was measured in.
                std::vector<platform::Rect> washRects;
                if (sel && !sel->Empty()) {
                    AppendBlockRangeRects(*b, n, m, walkEngine, originX,
                                          m.padTop + topOffsetDip,
                                          sel->Start(),
                                          sel->Start() + sel->Length(),
                                          &washRects);
                }
                DrawCodeBlock(rt, dw, n, bandX, bandY, bandW, outH, sel,
                              scrollY, washRects);
                break;
            }
            case BlockKind::Table: {
                // The rows are the same opaque surface story as the
                // code card: the grid, the washes per cell and the
                // per cell caret records all come from one painter.
                // The washes are the shared rule's rectangles, split
                // per cell below so the fill keeps its slot between
                // the match fills and the glyphs.
                float outH = 0.0f;
                std::vector<platform::Rect> washRects;
                if (sel && !sel->Empty()) {
                    AppendBlockRangeRects(*b, n, m, walkEngine, originX,
                                          m.padTop + topOffsetDip,
                                          sel->Start(),
                                          sel->Start() + sel->Length(),
                                          &washRects);
                }
                DrawTable(rt, dw, n, bandX, bandY, bandW, outH, sel,
                          scrollY, washRects,
                          b->table ? b->table.get() : nullptr);
                break;
            }
            case BlockKind::MermaidFlowchart: {
                // The three painters follow the zoom themselves and
                // measure against the column, the width the walk
                // measured them at, so they get the band's origin and
                // the content column, never the record's picture
                // width.
                DrawMermaidBlock(rt, dw, n, bandX, bandY, contentWidth);
                break;
            }
            case BlockKind::MermaidPie: {
                DrawMermaidPieBlock(rt, dw, n, bandX, bandY, contentWidth);
                break;
            }
            case BlockKind::MermaidSequence: {
                DrawMermaidSequenceBlock(rt, dw, n, bandX, bandY,
                                         contentWidth);
                break;
            }
            default:
                break;
        }
    }

    // The vector pictures of the text runs: an inline svg answers the
    // resolver with no bitmap (its cache entry holds the text, not
    // pixels), so the replay leaves the band it reserved, and the
    // renderer paints the picture itself at the walk's rect, the twin
    // of the fence picture above. A picture whose natural size the
    // walk never learned keeps its placeholder band unwritten this
    // frame; the fetch in the decode phase remembers the size, so the
    // layout that follows draws the real geometry.
    for (const display::Item& item : shared.list.items) {
        const auto* img = std::get_if<display::Image>(&item);
        if (!img || img->url.empty()) continue;
        // The fence pictures answered above; every other vector url
        // keeps the leading "svg:" namespace to itself.
        if (img->url.rfind("svg:", 0) == 0) continue;
        // A raster the decode phase never turned into a bitmap has
        // nothing to show either way; its band is the size the walk
        // charged, and its draw waits for the load.
        if (ImageHandleFor(*img)) continue;
        if (img->rect.width <= 0.0f || img->rect.height <= 0.0f) continue;
        if (img->naturalWidth <= 0.0f) continue;

        std::string urlLower = img->url;
        for (auto& ch : urlLower) {
            ch = static_cast<char>(
                tolower(static_cast<unsigned char>(ch)));
        }
        const bool isSvg =
            (urlLower.size() > 4 &&
             urlLower.substr(urlLower.size() - 4) == ".svg") ||
            (urlLower.find("data:image/svg+xml") == 0);
        if (!isSvg) continue;

        std::string svgText;
        bool svgTried = false;
        for (const auto& e : img_cache_) {
            if (e.url == img->url) {
                svgTried = true;
                if (!e.failed) svgText = e.svgText;
                break;
            }
        }
        if (!svgTried) {
            svgText = ImageHelper::LoadSvgText(img->url);
            if (img_cache_.size() >= 128) {
                if (img_cache_.front().bmp)
                    img_cache_.front().bmp->Release();
                img_cache_.erase(img_cache_.begin());
            }
            img_cache_.push_back(
                {img->url, nullptr, svgText.empty(), svgText});
        }
        if (svgText.empty()) continue;
        svg::SvgDoc picture;
        if (!picture.Load(d2d_ctx5_, svgText)) continue;
        if (picture.Width() <= 0.0f || picture.Height() <= 0.0f) continue;
        // List space is the column the walk owns, so the frame adds
        // the same origin the replay translated by, and the standing
        // scroll transform carries it to the view.
        picture.Draw(d2d_ctx5_, dw,
                     originX + img->rect.x,
                     m.padTop + topOffsetDip + img->rect.y,
                     img->rect.width, img->rect.height);
    }
}

bool Renderer::Init(IDWriteFactory* dw) {
    if (!dw) return false;
    dw_factory_ = dw;

    LayoutMetrics base = BaseMetrics();

    // Body format: Segoe UI, 15pt.
    HRESULT hr = dw->CreateTextFormat(
        base.bodyFont, nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        base.bodyFontSize * kPtToDip * zoom_,
        L"", &body_fmt_);
    if (FAILED(hr) || !body_fmt_) return false;
    auto applyLineBox = [this](IDWriteTextFormat* fmt, float lineHeightEm) {
        float natural = 0.0f;
        float ascent = 0.0f;
        if (!fmt || !FontBoxOf(fmt, &natural, &ascent) || natural <= 0.0f) {
            return;
        }
        const float height = lineHeightEm * fmt->GetFontSize();
        const float baseline = (height - natural) * 0.5f + ascent;
        fmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, height,
                            baseline);
    };
    applyLineBox(body_fmt_, base.bodyLineHeight);

    // Flowchart coordinates are kept in canonical DIPs. This format is
    // deliberately unzoomed, so zoom is applied once by the drawing path.
    hr = dw->CreateTextFormat(
        base.bodyFont, nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        base.bodyFontSize * kPtToDip,
        L"", &mermaid_measure_fmt_);
    if (SUCCEEDED(hr) && mermaid_measure_fmt_) {
        // The diagram labels keep their own 1.3 line box: the document's
        // 1.5 is a reading measure, not a diagram metric.
        mermaid_measure_fmt_->SetLineSpacing(
            DWRITE_LINE_SPACING_METHOD_PROPORTIONAL, 1.3f, 1.24f);
    }

    // Sequence autonumber digits: mermaid renders 12px sans-serif.
    hr = dw->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f * (4.0f / 3.0f) * zoom_,
        L"", &num_fmt_);
    if (FAILED(hr)) num_fmt_ = nullptr;

    // Code format: Cascadia Mono at GitHub's 85% size (falls back to
    // Consolas).
    hr = dw->CreateTextFormat(
        base.codeFont, nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        base.codeFontSize * kPtToDip * zoom_,
        L"", &code_fmt_);
    if (FAILED(hr) || !code_fmt_) return false;
    applyLineBox(code_fmt_, base.codeLineHeight);
    // Whole words only: the shared walk's canon (the same rule the
    // golden engine spells, a run never breaks inside itself) lets an
    // unbreakable token overflow the line instead of splitting it
    // mid word. The emergency break D2D takes by default would wrap a
    // long hash or minified line into a taller card than the walk
    // charged, so the band and the card drift apart; wrapping whole
    // words keeps the card's own DirectWrite measure equal to the
    // record the walk hands the frame. The source view sets its own
    // wrap explicitly on the layout and keeps the flat rule.
    code_fmt_->SetWordWrapping(DWRITE_WORD_WRAPPING_WHOLE_WORD);

    // Heading formats, scaled by level. The face follows the style:
    // the semibold body face, or the family the style names for the
    // level (the serif of the Wikipedia H1 and H2). The selection
    // washes measure with these, so they must match the drawn face.
    for (int lvl = 1; lvl <= 6; ++lvl) {
        std::wstring headingFamilyW;
        if (base.headingFamily[lvl])
            headingFamilyW.assign(base.headingFamily[lvl],
                                  base.headingFamily[lvl] +
                                      std::strlen(base.headingFamily[lvl]));
        const wchar_t* headingFace =
            base.headingFamily[lvl] ? headingFamilyW.c_str() : base.bodyFont;
        const DWRITE_FONT_WEIGHT headingWeight =
            base.headingBold[lvl]    ? DWRITE_FONT_WEIGHT_BOLD
            : base.headingFamily[lvl] ? DWRITE_FONT_WEIGHT_NORMAL
                                      : DWRITE_FONT_WEIGHT_SEMI_BOLD;
        hr = dw->CreateTextFormat(
            headingFace, nullptr,
            headingWeight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            base.headingSizes[lvl] * kPtToDip * zoom_,
            L"", &heading_fmt_[lvl]);
        if (FAILED(hr) || !heading_fmt_[lvl]) return false;
        applyLineBox(heading_fmt_[lvl], base.headingLineHeight);
    }

    return true;
}

void Renderer::Release() {
    auto rel = [](IDWriteTextFormat*& p) { if (p) { p->Release(); p = nullptr; } };
    // Formats are about to be recreated (zoom change) or torn down:
    // drop the line-height cache with them. Keys are format pointers,
    // and a newly allocated format could land on a freed one's
    // address, which would answer a different zoom's cached height.
    lineHeightCache_.clear();
    DropSourceLayout();
    rel(body_fmt_);
    rel(code_fmt_);
    rel(num_fmt_);
    rel(mermaid_measure_fmt_);
    mermaid_layout_cache_.Clear();
    for (int i = 1; i <= 6; ++i) rel(heading_fmt_[i]);
}

// Natural line height of the format: ascent + descent + lineGap from
// the font's own metrics, scaled from design units to the format's
// size. This is what the cursor-blinking guide calls "the current
// font's line height" for the caret: distinct from the em size (the
// height of one character) the code used before, and also distinct
// from the rendered line box, which additionally carries the app's
// line-spacing multiplier. A zero result means the metrics could
// not be resolved; the caller then falls back to the rendered line.
bool Renderer::FontBoxOf(IDWriteTextFormat* fmt, float* natural,
                         float* ascent) {
    if (!fmt) return false;
    IDWriteFontCollection* coll = nullptr;
    if (SUCCEEDED(fmt->GetFontCollection(&coll)) && coll) {
        IDWriteFontFamily* family = nullptr;
        UINT32 idx = 0; BOOL exists = FALSE;
        wchar_t famName[64];
        UINT32 nameLen = fmt->GetFontFamilyNameLength();
        if (nameLen < 64 &&
            SUCCEEDED(fmt->GetFontFamilyName(famName, 64)) &&
            SUCCEEDED(coll->FindFamilyName(famName, &idx, &exists)) &&
            exists &&
            SUCCEEDED(coll->GetFontFamily(idx, &family)) && family) {
            IDWriteFont* font = nullptr;
            if (SUCCEEDED(family->GetFirstMatchingFont(
                    fmt->GetFontWeight(), fmt->GetFontStretch(),
                    fmt->GetFontStyle(), &font)) && font) {
                DWRITE_FONT_METRICS m = {};
                font->GetMetrics(&m);
                const float units = static_cast<float>(
                    m.ascent + m.descent + m.lineGap);
                const float design =
                    static_cast<float>(m.designUnitsPerEm);
                if (design > 0.0f && units > 0.0f) {
                    const float size = fmt->GetFontSize();
                    if (natural) *natural = size * units / design;
                    if (ascent) {
                        *ascent = size * static_cast<float>(m.ascent) / design;
                    }
                    font->Release();
                    family->Release();
                    coll->Release();
                    return true;
                }
                font->Release();
            }
            family->Release();
        }
        coll->Release();
    }
    return false;
}

float Renderer::LineHeightOf(IDWriteTextFormat* fmt) {
    if (!fmt) return 0.0f;
    auto hit = lineHeightCache_.find(fmt);
    if (hit != lineHeightCache_.end()) return hit->second;
    float natural = 0.0f;
    if (!FontBoxOf(fmt, &natural, nullptr)) natural = 0.0f;
    lineHeightCache_[fmt] = natural;
    return natural;
}

void Renderer::SetZoom(float z) {
    // Clamp to the supported range: 25% (kMinZoom) .. 400% (kMaxZoom).
    zoom_ = zoom::Clamp(z);
}

void Renderer::SetWrap(bool w) {
    wrapEnabled_ = w;
}

void Renderer::SetContentWidthMode(int mode) {
    if (mode < 0 || mode > 3) mode = 0;
    contentWidthMode_ = mode;
}

float Renderer::ContentWidthDip(float viewportWidthDip) const {
    if (!wrapEnabled_) return kNoWrapContentWidthDip;
    LayoutMetrics m = ComputeMetrics();
    if (!(viewportWidthDip > 0.0f)) return m.maxContentWidth;
    float width = viewportWidthDip - 2.0f * m.padX;
    if (width > m.maxContentWidth) width = m.maxContentWidth;
    if (!(width > 0.0f)) width = 1.0f;
    // A picture wider than the column (a standalone .svg at natural size,
    // or an image at high zoom) extends the scrollable area. Without this
    // its right-hand part could not be reached.
    if (widestBlockRight_ > width) width = widestBlockRight_;
    return width;
}

float Renderer::BaseContentWidthDip() const {
    switch (contentWidthMode_) {
        case 1:  return 960.0f;
        case 2:  return 1600.0f;
        case 3:  return 0.0f;   // uncapped: no fixed column to fit
        default: return 800.0f;
    }
}

std::u16string Renderer::ToUtf16(const std::u32string& s32) {
    std::u16string out;
    out.reserve(s32.size());
    for (char32_t cp : s32) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

void Renderer::DrawCodeBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                              const Node& n, float x, float y,
                              float width, float& outH,
                              const Selection* sel, float scrollY,
                              const std::vector<platform::Rect>& washRects) {
    if (!rt || !dw || !code_fmt_) { outH = 0.0f; return; }

    LayoutMetrics m = ComputeMetrics();
    Palette pal = BasePalette();

    std::u32string raw = n.raw;
    while (!raw.empty() && (raw.back() == 0x0A || raw.back() == 0x0D)) {
        raw.pop_back();
    }
    std::u16string text16 = ToUtf16(raw);

    ID2D1SolidColorBrush* bgBrush = nullptr;
    rt->CreateSolidColorBrush(pal.codeBg, &bgBrush);
    ID2D1SolidColorBrush* codeBrush = nullptr;
    rt->CreateSolidColorBrush(pal.textPrimary, &codeBrush);

    float codeWidth = width - 2.0f * m.codePad;
    if (codeWidth <= 0) {
        outH = 0.0f;
        if (bgBrush) bgBrush->Release();
        if (codeBrush) codeBrush->Release();
        return;
    }

    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = dw->CreateTextLayout(
        reinterpret_cast<const WCHAR*>(text16.data()),
        static_cast<UINT32>(text16.size()),
        code_fmt_, codeWidth, 1.0e9f, &layout);
    if (FAILED(hr) || !layout) {
        if (bgBrush) bgBrush->Release();
        if (codeBrush) codeBrush->Release();
        outH = 0.0f;
        return;
    }

    DWRITE_TEXT_METRICS metrics = {};
    layout->GetMetrics(&metrics);

    float blockH = metrics.height + 2.0f * m.codePad;

    // Rounded background.
    D2D1_RECT_F bgRect = D2D1::RectF(x, y, x + width, y + blockH);
    if (m.codeRadius > 0.5f) {
        D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(bgRect,
            m.codeRadius, m.codeRadius);
        rt->FillRoundedRectangle(rrect, bgBrush);
    } else {
        rt->FillRectangle(bgRect, bgBrush);
    }

    // Search match highlights, under the text and under the selection so a
    // selection always stays readable on top of a match fill. The mapping is
    // rebuilt here rather than reusing the cache copy, because the cache is
    // optional and the highlight must not depend on hit-testing being on.
    if ((HasSearchMatches() || HasMarkers()) && !raw.empty() &&
        !text16.empty()) {
        std::vector<uint32_t> matchU16ToSrc;
        matchU16ToSrc.reserve(text16.size());
        uint32_t srcByte = n.contentOffset;
        for (char32_t cp : raw) {
            int utf8Len = (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                          (cp <= 0xFFFF) ? 3 : 4;
            int utf16Len = (cp <= 0xFFFF) ? 1 : 2;
            for (int u = 0; u < utf16Len; u++)
                matchU16ToSrc.push_back(srcByte);
            srcByte += utf8Len;
        }
        // Marker layer for this block, drawn behind the search fills.
        ID2D1SolidColorBrush* markerBrush = nullptr;
        ID2D1SolidColorBrush* markerLine = nullptr;
        if (HasMarkers()) {
            rt->CreateSolidColorBrush(pal.markerBg, &markerBrush);
            rt->CreateSolidColorBrush(pal.markerLine, &markerLine);
        }
        if (markerBrush && markerLine) {
            FillMarkerHighlights(rt, layout, markerBrush, markerLine,
                                 matchU16ToSrc,
                                 static_cast<UINT32>(text16.size()),
                                 n.contentOffset,
                                 n.contentOffset + n.contentLength,
                                 x + m.codePad, y + m.codePad,
                                 scrollY, rt->GetSize().height);
        }
        ID2D1SolidColorBrush* matchBrush = nullptr;
        ID2D1SolidColorBrush* currentBrush = nullptr;
        rt->CreateSolidColorBrush(pal.findMatchBg, &matchBrush);
        rt->CreateSolidColorBrush(pal.findCurrentMatchBg, &currentBrush);
        if (matchBrush && currentBrush) {
            const uint32_t contentStart = n.contentOffset;
            const uint32_t contentEnd = contentStart + n.contentLength;
            FillMatchHighlights(rt, layout, matchBrush, currentBrush,
                                matchU16ToSrc,
                                static_cast<UINT32>(text16.size()),
                                contentStart, contentEnd,
                                x + m.codePad, y + m.codePad,
                                scrollY, rt->GetSize().height);
        }
        if (markerBrush) markerBrush->Release();
        if (markerLine) markerLine->Release();
        if (matchBrush) matchBrush->Release();
        if (currentBrush) currentBrush->Release();
    }

    // The selection wash, from the shared rect rule: one rectangle
    // per wrapped line, the same shape the RectsForRange twin answers
    // outside the frame and the mac renderer paints from the same
    // records. The old per code point translation is gone; the wash
    // keeps its slot between the match fills and the glyphs.
    if (sel && !sel->Empty() && !washRects.empty()) {
        ID2D1SolidColorBrush* selBrush = nullptr;
        rt->CreateSolidColorBrush(pal.selectionBg, &selBrush);
        if (selBrush) {
            for (const platform::Rect& r : washRects) {
                const D2D1_RECT_F rect = D2D1::RectF(
                    r.x, r.y, r.x + r.width, r.y + r.height);
                rt->FillRectangle(rect, selBrush);
            }
            selBrush->Release();
        }
    }

    // Draw text.
    D2D1_POINT_2F origin = D2D1::Point2F(x + m.codePad, y + m.codePad);
    rt->DrawTextLayout(origin, layout, codeBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

    if (cache_) {
        // Build u16ToSrc mapping for the code block.
        std::vector<uint32_t> cu16ToSrc;
        std::vector<uint32_t> cu16ToSrcEnd;
        uint32_t srcByte = n.contentOffset;
        for (char32_t cp : raw) {
            int utf8Len = (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                          (cp <= 0xFFFF) ? 3 : 4;
            int utf16Len = (cp <= 0xFFFF) ? 1 : 2;
            for (int u = 0; u < utf16Len; u++) {
                cu16ToSrc.push_back(srcByte);
                cu16ToSrcEnd.push_back(srcByte +
                    static_cast<uint32_t>(utf8Len));
            }
            srcByte += utf8Len;
        }
        BlockLayout bl;
        bl.layout = layout;
        bl.x = x + m.codePad;
        bl.y = y + m.codePad;
        bl.width = codeWidth;
        bl.height = metrics.height;
        bl.srcOffset = n.srcOffset;
        bl.srcLength = n.srcLength;
        bl.textStartOffset = n.contentOffset;
        bl.nodeIndex = 0;
        bl.u16ToSrc = std::move(cu16ToSrc);
        bl.u16ToSrcEnd = std::move(cu16ToSrcEnd);
        // Caret height: the natural line height of the code font
        // (cursor-blinking guide), not the em size.
        bl.fontHeight = LineHeightOf(code_fmt_);
        cache_->Add(bl);
    } else {
        layout->Release();
    }
    if (bgBrush) bgBrush->Release();
    if (codeBrush) codeBrush->Release();
    outH = blockH;
}

void Renderer::DrawTable(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                          const Node& n, float x, float y,
                          float width, float& outH,
                          const Selection* sel, float scrollY,
                          const std::vector<platform::Rect>& washRects,
                          const layout::LaidTable* record) {
    if (!rt || !dw || !body_fmt_ || n.rows.empty()) { outH = 0.0f; return; }

    LayoutMetrics m = ComputeMetrics();
    Palette pal = BasePalette();

    ID2D1SolidColorBrush* textBrush = nullptr;
    rt->CreateSolidColorBrush(pal.textPrimary, &textBrush);
    ID2D1SolidColorBrush* altBg = nullptr;
    rt->CreateSolidColorBrush(pal.tableRowAlt, &altBg);
    ID2D1SolidColorBrush* hdrBg = nullptr;
    rt->CreateSolidColorBrush(pal.tableHeaderBg, &hdrBg);
    ID2D1SolidColorBrush* borderBrush = nullptr;
    rt->CreateSolidColorBrush(pal.tableBorder, &borderBrush);
    ID2D1SolidColorBrush* selBrush = nullptr;
    if (sel && !sel->Empty())
        rt->CreateSolidColorBrush(pal.selectionBg, &selBrush);
    // One pair of brushes for the whole table, released at the end, matching
    // the selBrush pattern. Both are null when find is closed, so the cells
    // below test the pointer instead of the match list.
    ID2D1SolidColorBrush* matchBrush = nullptr;
    ID2D1SolidColorBrush* currentBrush = nullptr;
    if (HasSearchMatches()) {
        rt->CreateSolidColorBrush(pal.findMatchBg, &matchBrush);
        rt->CreateSolidColorBrush(pal.findCurrentMatchBg, &currentBrush);
    }
    // Marker brushes for the whole table, null when the layer is hidden.
    ID2D1SolidColorBrush* markerBrush = nullptr;
    ID2D1SolidColorBrush* markerLine = nullptr;
    if (HasMarkers()) {
        rt->CreateSolidColorBrush(pal.markerBg, &markerBrush);
        rt->CreateSolidColorBrush(pal.markerLine, &markerLine);
    }

    size_t cols = 0;
    for (const auto& row : n.rows) {
        cols = (row.cells.size() > cols) ? row.cells.size() : cols;
    }
    if (cols == 0) {
        outH = 0.0f;
        if (textBrush) textBrush->Release();
        if (altBg) altBg->Release();
        if (hdrBg) hdrBg->Release();
        if (borderBrush) borderBrush->Release();
        if (selBrush) selBrush->Release();
        if (markerBrush) markerBrush->Release();
        if (markerLine) markerLine->Release();
        if (matchBrush) matchBrush->Release();
        if (currentBrush) currentBrush->Release();
        return;
    }

    // One geometry source: the walk's record carries the column
    // split that carries every column's longest word and the row
    // heights its cells measured, so this painter reads it instead
    // of re-measuring. The equal split stays as the fall back for
    // a recordless call.
    const layout::LaidTable* rec =
        (record && record->columnCount == cols &&
         !record->colWs.empty()) ? record : nullptr;
    std::vector<float> colXs(cols + 1, 0.0f);
    std::vector<float> colWs(cols, 0.0f);
    {
        const float equal = width / static_cast<float>(cols);
        float edge = 0.0f;
        for (size_t c = 0; c < cols; ++c) {
            const float w = rec ? TableColumnW(*rec, c) : equal;
            const float x0 = rec ? TableColumnX(*rec, c) : edge;
            colXs[c] = x0;
            colWs[c] = w;
            edge = x0 + w;
        }
        colXs[cols] = rec && rec->colXs.size() > cols
            ? rec->colXs[cols] : width;
    }
    float curY = y;

    for (size_t ri = 0; ri < n.rows.size(); ++ri) {
        const auto& row = n.rows[ri];
        float rowH = 0.0f;
        if (rec && ri < rec->rowHeights.size() &&
            rec->rowHeights[ri] > 0.0f) {
            // The record's heights already carry the cell padding.
            rowH = rec->rowHeights[ri];
        } else {
        for (size_t c = 0; c < row.cells.size() && c < cols; ++c) {
            std::u16string text16 = ToUtf16(row.cells[c].text);
            IDWriteTextLayout* layout = nullptr;
            HRESULT hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(text16.data()),
                static_cast<UINT32>(text16.size()),
                body_fmt_, colWs[c] - 2.0f * m.cellPadX, 1.0e9f, &layout);
            if (SUCCEEDED(hr) && layout) {
                // Apply the same inline formatting as the draw pass,
                // because code spans use a wider monospace font that
                // may wrap differently and need more vertical space.
                if (row.cells[c].isHeader) {
                    DWRITE_TEXT_RANGE r = {0,
                        static_cast<UINT32>(text16.size())};
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, r);
                }
                for (const auto& sp : row.cells[c].inlineSpans) {
                    DWRITE_TEXT_RANGE r = {sp.u16Start,
                        sp.u16End - sp.u16Start};
                    if (sp.bold)
                        layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                    if (sp.italic)
                        layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
                    if (sp.code) {
                        layout->SetFontFamilyName(m.codeFont, r);
                        layout->SetFontSize(
                            m.codeFontSize * kPtToDip * zoom_, r);
                    }
                    if (sp.strike)
                        layout->SetStrikethrough(true, r);
                }
                DWRITE_TEXT_METRICS tm = {};
                layout->GetMetrics(&tm);
                if (tm.height > rowH) rowH = tm.height;
                layout->Release();
            }
        }
        if (rowH == 0.0f) {
            rowH = m.bodyLineHeight * m.bodyFontSize * kPtToDip * zoom_;
        }
        rowH += 2.0f * m.cellPadY;
        }

        bool isHeaderRow = (!row.cells.empty() && row.cells[0].isHeader);
        if (isHeaderRow && m.tableHeaderFill && hdrBg) {
            rt->FillRectangle(
                D2D1::RectF(x, curY, x + width, curY + rowH), hdrBg);
        } else if (!isHeaderRow && altBg &&
                   (m.tableBodyFill || (m.tableZebra && ri % 2 == 1))) {
            // The style's row wash. GitHub's zebra: the first body row
            // is the first muted one, because the header is tr 1.
            D2D1_RECT_F bg = D2D1::RectF(x, curY, x + width, curY + rowH);
            rt->FillRectangle(bg, altBg);
        }

        for (size_t c = 0; c < row.cells.size() && c < cols; ++c) {
            float cellX = x + colXs[c];
            if (borderBrush && m.tableVerticalLines) {
                D2D1_POINT_2F p1 = D2D1::Point2F(cellX, curY);
                D2D1_POINT_2F p2 = D2D1::Point2F(cellX, curY + rowH);
                rt->DrawLine(p1, p2, borderBrush, 1.0f);
            }
            std::u16string text16 = ToUtf16(row.cells[c].text);
            IDWriteTextLayout* layout = nullptr;
            HRESULT hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(text16.data()),
                static_cast<UINT32>(text16.size()),
                body_fmt_, colWs[c] - 2.0f * m.cellPadX, 1.0e9f, &layout);
            if (SUCCEEDED(hr) && layout) {
                // Honour the column alignment written in the delimiter row
                // (`:---`, `:---:`, `---:`). DirectWrite lays the text out
                // inside the cell, so the selection highlight below follows
                // the same alignment automatically.
                {
                    // DWRITE_TEXT_ALIGNMENT is the horizontal one:
                    // LEADING is left, TRAILING is right. The paragraph
                    // enum only has NEAR, FAR and CENTER, and NEAR/FAR are
                    // vertical.
                    switch (layout::CellAlign(n, row, c, m)) {
                        case ColumnAlign::Left:
                            layout->SetTextAlignment(
                                DWRITE_TEXT_ALIGNMENT_LEADING);
                            break;
                        case ColumnAlign::Center:
                            layout->SetTextAlignment(
                                DWRITE_TEXT_ALIGNMENT_CENTER);
                            break;
                        case ColumnAlign::Right:
                            layout->SetTextAlignment(
                                DWRITE_TEXT_ALIGNMENT_TRAILING);
                            break;
                        case ColumnAlign::Default:
                            break;
                    }
                }
                if (row.cells[c].isHeader) {
                    DWRITE_TEXT_RANGE r = {0,
                        static_cast<UINT32>(text16.size())};
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, r);
                }
                // Apply inline formatting spans (bold, italic, code, strike).
                for (const auto& sp : row.cells[c].inlineSpans) {
                    DWRITE_TEXT_RANGE r = {sp.u16Start,
                        sp.u16End - sp.u16Start};
                    if (sp.bold)
                        layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, r);
                    if (sp.italic)
                        layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, r);
                    if (sp.code) {
                        // GitHub's 85% code size inside a table cell.
                        layout->SetFontFamilyName(m.codeFont, r);
                        layout->SetFontSize(
                            m.codeFontSize * kPtToDip * zoom_, r);
                    }
                    if (sp.strike)
                        layout->SetStrikethrough(true, r);
                }
                for (const auto& link : row.cells[c].links) {
                    UINT32 linkStart = static_cast<UINT32>(text16.size());
                    UINT32 linkEnd = 0;
                    for (size_t ui = 0; ui < row.cells[c].u16ToSrc.size(); ++ui) {
                        const uint32_t spanStart = row.cells[c].u16ToSrc[ui];
                        const uint32_t spanEnd = ui < row.cells[c].u16ToSrcEnd.size()
                            ? row.cells[c].u16ToSrcEnd[ui] : spanStart + 1;
                        const uint32_t linkEndSrc = link.srcOffset + link.srcLength;
                        if (spanEnd <= link.srcOffset || spanStart >= linkEndSrc)
                            continue;
                        linkStart = std::min<UINT32>(linkStart,
                            static_cast<UINT32>(ui));
                        linkEnd = std::max<UINT32>(linkEnd,
                            static_cast<UINT32>(ui + 1));
                    }
                    if (linkEnd > linkStart) {
                        DWRITE_TEXT_RANGE range = {linkStart,
                            linkEnd - linkStart};
                        if (m.linkUnderline)
                            layout->SetUnderline(TRUE, range);
                    }
                }
                D2D1_POINT_2F cellOrigin = D2D1::Point2F(
                    cellX + m.cellPadX, curY + m.cellPadY);
                // Marker layer for this cell, behind the search fills.
                if (markerBrush && markerLine &&
                    !row.cells[c].text.empty() && !text16.empty() &&
                    !row.cells[c].u16ToSrc.empty()) {
                    const auto& cell = row.cells[c];
                    const uint32_t cellStart = cell.srcOffset;
                    const uint32_t cellEnd = cell.srcEnd > cellStart
                        ? cell.srcEnd : cellStart;
                    FillMarkerHighlights(rt, layout, markerBrush, markerLine,
                                         cell.u16ToSrc,
                                         static_cast<UINT32>(text16.size()),
                                         cellStart, cellEnd,
                                         cellOrigin.x, cellOrigin.y,
                                         scrollY, rt->GetSize().height);
                }
                // Search match highlights for this cell, drawn before the
                // selection so the selection stays on top.
                if (matchBrush && currentBrush &&
                    !row.cells[c].text.empty() && !text16.empty() &&
                    !row.cells[c].u16ToSrc.empty()) {
                    const auto& cell = row.cells[c];
                    const uint32_t cellStart = cell.srcOffset;
                    const uint32_t cellEnd = cell.srcEnd > cellStart
                        ? cell.srcEnd : cellStart;
                    FillMatchHighlights(rt, layout, matchBrush, currentBrush,
                                        cell.u16ToSrc,
                                        static_cast<UINT32>(text16.size()),
                                        cellStart, cellEnd,
                                        cellOrigin.x, cellOrigin.y,
                                        scrollY, rt->GetSize().height);
                }
                // The selection wash for this cell, from the shared
                // rect rule: the whole table's rectangles were computed
                // once above, so the ones inside this cell's band are
                // the cell's share. The fill keeps the old slot between
                // the match fills and the glyphs.
                if (selBrush && sel && !sel->Empty() &&
                    !washRects.empty()) {
                    for (const platform::Rect& r : washRects) {
                        if (r.x < cellX || r.x >= cellX + colWs[c]) continue;
                        if (r.y < curY || r.y >= curY + rowH) continue;
                        const D2D1_RECT_F rect = D2D1::RectF(
                            r.x, r.y, r.x + r.width, r.y + r.height);
                        rt->FillRectangle(rect, selBrush);
                    }
                }
                // The glyphs come from the walk's own lines, the
                // same lines the caret and the record keep, placed
                // by the same centre the clones read, so what
                // paints is what was measured: one geometry, one
                // place. A table without record lines, a stack
                // without a measured walk, falls back to the card's
                // own layout, the paint that always worked there.
                bool painted = false;
                if (rec && ri < rec->cells.size() &&
                    c < rec->cells[ri].size() &&
                    !rec->cells[ri][c].lines.empty()) {
                    const float innerCW = colWs[c] - 2.0f * m.cellPadX;
                    const float clipH = rowH - 2.0f * m.cellPadY;
                    if (innerCW > 0.0f && clipH > 0.0f) {
                        rt->PushAxisAlignedClip(D2D1::RectF(
                            cellX + m.cellPadX,
                            curY + m.cellPadY,
                            cellX + m.cellPadX + innerCW,
                            curY + m.cellPadY + clipH),
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                        float textH = 0.0f;
                        for (const platform::TextLine& line :
                             rec->cells[ri][c].lines)
                            textH += line.height;
                        float lineY = curY +
                            (std::max)(m.cellPadY,
                                       (rowH - textH) * 0.5f);
                        const ColumnAlign align =
                            layout::CellAlign(n, n.rows[ri], c, m);
                        platform::D2DTarget player(
                            rt, layoutEngine_.Factory());
                        size_t paintedRuns = 0;
                        for (const platform::TextLine& line :
                             rec->cells[ri][c].lines) {
                            float lineX = cellX + m.cellPadX;

                            if (align == ColumnAlign::Center)
                                lineX += (innerCW - line.width) * 0.5f;
                            else if (align == ColumnAlign::Right)
                                lineX += innerCW - line.width;
                            const float baseline = lineY + line.ascent;
                            for (size_t k = 0; k < line.runs.size();
                                 ++k) {
                                const platform::GlyphRun& run =
                                    line.runs[k];
                                const float end =
                                    k + 1 < line.runs.size()
                                        ? line.runs[k + 1].x
                                        : line.width;
                                const float width = end - run.x;
                                const float x = lineX + run.x;
                                const platform::Color color{
                                    run.r, run.g, run.b, run.a};
                                if (!run.text.empty()) ++paintedRuns;

                                player.DrawText(
                                    run.text, run.font, x, baseline,
                                    color);
                                if (run.underline)
                                    player.StrokeLine(
                                        x, baseline + 2.0f,
                                        x + width, baseline + 2.0f,
                                        color, 1.0f);
                                if (run.strike)
                                    player.StrokeLine(
                                        x,
                                        baseline - 0.30f * run.font.size,
                                        x + width,
                                        baseline - 0.30f * run.font.size,
                                        color, 1.0f);
                            }
                            lineY += line.height;
                        }
                        rt->PopAxisAlignedClip();
                        // A walk whose engine could not split the
                        // piece leaves lines with no runs: the
                        // record paint drew nothing, and the empty
                        // record wins. Fall back to the card's own
                        // layout in that case rather than leave a
                        // cell without glyphs.
                        painted = paintedRuns > 0;
                    }
                }
                if (!painted) {

                    rt->DrawTextLayout(cellOrigin, layout, textBrush,
                                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                if (cache_) {
                    // Add cell layout to cache for hit-testing (double-click,
                    // selection, caret placement in edit mode).
                    // Use the u16ToSrc mapping from the parser, which correctly
                    // handles gaps from md4c mark splits (e.g., ':' as a
                    // permissive URL autolink mark).
                    BlockLayout bl;
                    bl.layout = layout;
                    bl.x = cellOrigin.x;
                    bl.y = cellOrigin.y;
                    bl.width = colWs[c] - 2.0f * m.cellPadX;
                    bl.height = rowH - 2.0f * m.cellPadY;
                    bl.srcOffset = row.cells[c].srcOffset;
                    bl.srcLength = row.cells[c].srcEnd > bl.srcOffset
                        ? row.cells[c].srcEnd - bl.srcOffset : 0;
                    // Compute extended cell range from the source mapping.
                    // opening markers and forward past closing markers
                    // so the caret works when placed between hidden
                    // marker characters (* ` ~).
                    bl.srcCellStart = bl.srcOffset;
                    bl.srcCellEnd = bl.srcOffset + bl.srcLength;
                    if (srcText_) {
                        const auto& src = *srcText_;
                        // Scan backward past opening markers.
                        uint32_t st = bl.srcOffset;
                        while (st > 0 &&
                               (src[st - 1] == '*' || src[st - 1] == '`' ||
                                src[st - 1] == '~'))
                            st--;
                        bl.srcCellStart = st;
                        // Scan forward past closing markers.
                        uint32_t e = bl.srcOffset + bl.srcLength;
                        while (e < src.size() &&
                               (src[e] == '*' || src[e] == '`' ||
                                src[e] == '~'))
                            e++;
                        bl.srcCellEnd = e;
                    }
                    bl.textStartOffset = row.cells[c].srcOffset;
                    bl.nodeIndex = 0;
                    bl.isTableCell = true;
                    bl.u16ToSrc = row.cells[c].u16ToSrc; // copy parser's mapping
                    bl.u16ToSrcEnd = row.cells[c].u16ToSrcEnd;
                    bl.fontHeight = LineHeightOf(body_fmt_);
                    cache_->Add(bl);
                } else {
                    layout->Release();
                }
            }
        }
        if (borderBrush && m.tableVerticalLines) {
            D2D1_POINT_2F p1 = D2D1::Point2F(x + width, curY);
            D2D1_POINT_2F p2 = D2D1::Point2F(x + width, curY + rowH);
            rt->DrawLine(p1, p2, borderBrush, 1.0f);
        }
        if (borderBrush) {
            // The top edge is part of the full grid only; a style with
            // row hairlines draws the bottom of each row and no more.
            if (m.tableVerticalLines)
                rt->DrawLine(
                    D2D1::Point2F(x, curY), D2D1::Point2F(x + width, curY),
                    borderBrush, 1.0f);
            rt->DrawLine(
                D2D1::Point2F(x, curY + rowH),
                D2D1::Point2F(x + width, curY + rowH),
                borderBrush, 1.0f);
        }
        curY += rowH;
    }

    if (textBrush) textBrush->Release();
    if (altBg) altBg->Release();
        if (hdrBg) hdrBg->Release();
    if (borderBrush) borderBrush->Release();
    if (selBrush) selBrush->Release();
    if (markerBrush) markerBrush->Release();
    if (markerLine) markerLine->Release();
    if (matchBrush) matchBrush->Release();
    if (currentBrush) currentBrush->Release();
    outH = curY - y;
}

// ---------------------------------------------------------------------------
// Batch 3.3: the selection rect twin over the shared walk records
// ---------------------------------------------------------------------------

// The engine the walk and the twin measure through: the test
// replacement when one is installed, the renderer's own DirectWrite
// engine otherwise. The zoom rides the WalkTextEngine adapter around
// the answer either way, so the caller wraps this in one.
platform::TextEngine* Renderer::TextEngineForWalk() {
    return walkEngineForTesting_ ? walkEngineForTesting_
                                 : static_cast<platform::TextEngine*>(
                                       &layoutEngine_);
}

// The source map of one block's drawn text. The builders live in the
// shared header (layout/selection_rects.h), spelled exactly the way
// the mac record adopts them in its AppendBlockFromLaid, so both
// ports map the same bytes: the inline children per drawn byte, the
// blank placeholder positions the caret keeps for empty paragraphs
// and bare list items, the raw text of a code card.
void Renderer::BuildBlockMapping(const Node& n,
                                 std::vector<uint32_t>* visToSrc,
                                 std::vector<uint32_t>* visToSrcEnd) const {
    switch (n.block) {
        case BlockKind::Heading:
        case BlockKind::Paragraph:
            layout::AppendInlineMapping(n.children, visToSrc, visToSrcEnd);
            if (visToSrc->empty() && n.virtualEmptyParagraph) {
                // The placeholder line maps to the position the empty
                // paragraph keeps for its caret.
                layout::FinishMapping(n.contentOffset, visToSrc,
                                      visToSrcEnd);
            }
            break;

        case BlockKind::List:
            layout::AppendInlineMapping(n.children, visToSrc, visToSrcEnd);
            if (visToSrc->empty()) {
                // The marker keeps the item addressable.
                layout::FinishMapping(n.contentOffset, visToSrc,
                                      visToSrcEnd);
            }
            break;

        case BlockKind::BlockQuote:
            layout::AppendInlineMapping(n.children, visToSrc, visToSrcEnd);
            break;

        case BlockKind::CodeBlock: {
            // The fence's text lives in raw; the inline children are
            // empty for a code block.
            std::u32string raw32 = n.raw;
            while (!raw32.empty() &&
                   (raw32.back() == U'\n' || raw32.back() == U'\r')) {
                raw32.pop_back();
            }
            const std::string code = layout::Utf32ToUtf8(raw32);
            layout::AppendCodeMapping(code, n.contentOffset, visToSrc,
                                      visToSrcEnd);
            break;
        }

        default:
            break;
    }
}

// The frame placement of a record's own text, the mirror of the mac
// TextX/TextTop pair: an indented list opens its text after the
// marker gutter, a code card one pad inside its surface, and the
// images a text block stacked above its text push the top down. The
// numbers are the walk's own placement, the same placement the replay
// paints the glyphs at, so the washes the rule answers sit under the
// drawn text by construction.
void Renderer::TextOriginOf(const layout::LaidBlock& b,
                            const LayoutMetrics& m, float originX,
                            float frameTop, float* x, float* top) {
    *x = originX + b.x;
    if (b.kind == BlockKind::List) {
        // b.markerWidth is the measured gutter (widened past the fixed
        // formula for a wide ordinal like "10."); a block with no
        // marker (an empty placeholder row) still falls back to the
        // fixed reservation.
        *x += b.markerWidth > 0.0f
                  ? b.markerWidth
                  : m.listIndent * m.markerRatio + m.markerGutter;
    } else if (b.kind == BlockKind::CodeBlock) {
        *x += m.codePad;
    }
    *top = frameTop + b.y + b.imagesHeight;
    if (b.kind == BlockKind::CodeBlock) {
        *top = frameTop + b.y + m.codePad;
    }
}

// The rect rule over one whole record: the block's own text at the
// record's placement, a table's cells at the record's cell geometry
// (the centered lineY of a short cell in a wrapped row, the walk's
// own placement again), both through the shared AppendRangeRects the
// mac twin answers from.
void Renderer::AppendBlockRangeRects(
    const layout::LaidBlock& b, const Node& n, const LayoutMetrics& m,
    const platform::TextEngine& engine, float originX, float frameTop,
    uint32_t start, uint32_t end,
    std::vector<platform::Rect>* rects) const {
    if (!b.lines.empty()) {
        std::vector<uint32_t> visToSrc;
        std::vector<uint32_t> visToSrcEnd;
        BuildBlockMapping(n, &visToSrc, &visToSrcEnd);
        if (!visToSrc.empty()) {
            float x = 0.0f;
            float top = 0.0f;
            TextOriginOf(b, m, originX, frameTop, &x, &top);
            layout::AppendRangeRects(b.lines, visToSrc, engine, x, top,
                                     start, end, rects,
                                     ColumnAlign::Default, 0.0f, false);
        }
    }
    if (b.kind == BlockKind::Table && b.table) {
        // The cells: the walk's record owns the measured lines, the
        // parser's node owns the byte map. A row centers a short cell
        // against a taller one, the lineY the walk places the glyphs
        // at, so the rectangle band follows the glyphs rather than the
        // cell's top padding: this body used to read the paddings and
        // answered a band above the text of every short cell in a
        // wrapped row.
        for (size_t r = 0;
             r < b.table->cells.size() && r < n.rows.size(); ++r) {
            const layout::LaidTable& t = *b.table;
            const float rowH = r < t.rowHeights.size()
                ? t.rowHeights[r] : 0.0f;
            const size_t columns =
                (std::min)(t.cells[r].size(), n.rows[r].cells.size());
            for (size_t c = 0; c < columns; ++c) {
                const layout::LaidTableCell& cell = t.cells[r][c];
                if (cell.lines.empty()) continue;
                std::vector<uint32_t> cellVis;
                std::vector<uint32_t> cellVisEnd;
                layout::AppendCellMapping(n.rows[r].cells[c], &cellVis,
                                          &cellVisEnd);
                const ColumnAlign align =
                    layout::CellAlign(n, n.rows[r], c, m);
                const float innerW = layout::TableColumnW(t, c) -
                    2.0f * m.cellPadX;
                float textH = 0.0f;
                for (const platform::TextLine& line : cell.lines) {
                    textH += line.height;
                }
                const float lineY = t.cells[r][c].y +
                    (std::max)(m.cellPadY, (rowH - textH) * 0.5f);
                layout::AppendRangeRects(
                    cell.lines, cellVis, engine,
                    originX + b.x + layout::TableColumnX(t, c) +
                        m.cellPadX,
                    frameTop + b.y + lineY, start, end, rects,
                    align, innerW, false);
            }
        }
    }
}

std::vector<platform::Rect> Renderer::RectsForRange(
    IDWriteFactory* dw, const Document& doc, float widthDip,
    float topOffsetDip, uint32_t start, uint32_t end, const Selection* sel) {
    std::vector<platform::Rect> rects;
    if (!dw || widthDip <= 0.0f) return rects;

    // The same frame the paint pass builds: the same metrics, the
    // same content column, so the records answer with the geometry
    // the last frame painted.
    LayoutMetrics m = ComputeMetrics();
    Palette pal = BasePalette();
    float avail = widthDip - 2.0f * m.padX;
    float contentWidth = wrapEnabled_ ? avail : kNoWrapContentWidthDip;
    if (wrapEnabled_ && contentWidth > m.maxContentWidth) {
        contentWidth = m.maxContentWidth;
    }
    if (contentWidth <= 0.0f) contentWidth = 1.0f;
    const float originX = wrapEnabled_
        ? (widthDip - contentWidth) * 0.5f
        : m.padX;

    std::vector<const layout::LaidBlock*> byNode;
    const auto sharedHold =
        SharedLayoutPass(doc, dw, m, &pal, contentWidth, &byNode, sel);
    const layout::DocumentLayout& shared = *sharedHold;

    // The engine the records were measured through: the walk measures
    // through the zooming adapter, so the rule's geometry questions
    // go to the same adapter, the same pairing the mac rule keeps
    // with its own engine.
    WalkTextEngine engine(TextEngineForWalk(), zoom_);
    const float frameTop = m.padTop + topOffsetDip;
    for (const layout::LaidBlock& b : shared.blocks) {
        if (b.nodeIndex >= doc.nodes.size()) continue;
        const Node& n = doc.nodes[b.nodeIndex];
        AppendBlockRangeRects(b, n, m, engine, originX, frameTop,
                              start, end, &rects);
    }
    return rects;
}

float Renderer::Measure(IDWriteFactory* dw, const Document& doc,
                           float widthDip, float topOffsetDip) {
    if (!dw) return 0.0f;
    LayoutMetrics m = ComputeMetrics();
    widestBlockRight_ = 0.0f;
    if (widthDip <= 0.0f) return m.padTop + topOffsetDip;

    float avail = widthDip - 2.0f * m.padX;
    float contentWidth = wrapEnabled_ ? avail : kNoWrapContentWidthDip;
    if (wrapEnabled_ && contentWidth > m.maxContentWidth)
        contentWidth = m.maxContentWidth;
    if (contentWidth <= 0.0f) contentWidth = 1.0f;

    float originX = wrapEnabled_
        ? (widthDip - contentWidth) * 0.5f
        : m.padX;

    // Batch 3: every owned kind measures through the one shared pass.
    // The walk runs the same gaps and band heights Render paints from,
    // so the scroll range and the frame cannot drift apart anymore;
    // its display items are built but not consumed, Measure is height
    // only.
    const Palette measurePal = BasePalette();
    const auto sharedHold =
        SharedLayoutPass(doc, dw, m, &measurePal, contentWidth, nullptr,
                         nullptr);
    const layout::DocumentLayout& shared = *sharedHold;
    // The same layout and width give the same height and scroll range,
    // so a scroll frame answers from the last walk instead of redoing
    // the span merge and the block loop below.
    if (sharedHold == measureHold_ && widthDip == measureWidth_ &&
        topOffsetDip == measureTop_) {
        widestBlockRight_ = measureWidest_;
        return measureHeight_;
    }
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    MergeNodeItemSpans(shared.blocks, shared.list, doc.nodes.size(),
                       &spans);

    // The scrollable width: a picture can be wider than the column. A
    // standalone .svg fence counts natural size times the zoom and the
    // two code pads, the numbers its picture is drawn at; an inline
    // image counts its fitted width, both plus the trailing pad the
    // reader scrolls to. The reserved fallback rect of an image whose
    // size is still unknown charges room but never widens the range.
    for (size_t bi = 0; bi < shared.blocks.size(); ++bi) {
        const layout::LaidBlock& b = shared.blocks[bi];
        float right = 0.0f;
        if (b.kind == BlockKind::CodeBlock &&
            b.nodeIndex < doc.nodes.size()) {
            const Node& n = doc.nodes[b.nodeIndex];
            if (n.lang == "svg" && standaloneSvg_) {
                if (svg::SvgDoc* d = GetSvgDoc(n, contentWidth)) {
                    if (d->Width() > 0.0f && d->Height() > 0.0f) {
                        right = originX + d->Width() * zoom_ +
                                2.0f * m.codePad + m.padX;
                    }
                }
            }
        }
        if (right <= 0.0f && b.nodeIndex < doc.nodes.size()) {
            for (uint32_t i = spans[b.nodeIndex].first;
                 i < spans[b.nodeIndex].second; ++i) {
                const auto* img = std::get_if<display::Image>(
                    &shared.list.items[i]);
                if (!img || img->naturalWidth <= 0.0f) continue;
                const float r = originX + img->rect.x + img->rect.width +
                                 m.padX;
                if (r > right) right = r;
            }
        }
        if (right > widestBlockRight_) widestBlockRight_ = right;
    }

    // The column starts one padTop under the top of the view and the
    // shared walk measures from the column's own top, so the height is
    // the pad, the walked height, and the doubled pad the bottom
    // margin keeps.
    const float height = m.padTop + topOffsetDip + shared.list.height +
                         m.padTop * 2.0f;
    measureHold_ = sharedHold;
    measureWidth_ = widthDip;
    measureTop_ = topOffsetDip;
    measureWidest_ = widestBlockRight_;
    measureHeight_ = height;
    return height;
}

float Renderer::Render(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                       const Document& doc, float widthDip, float scrollY,
                       float topOffsetDip, float scrollX,
                       const Selection* sel) {
    if (!rt || !dw) return 0.0f;
    LayoutMetrics m = ComputeMetrics();
    Palette pal = BasePalette();
    // Viewport height in DIPs, used to limit selection highlighting to the
    // lines actually on screen.
    const float viewH = rt->GetSize().height;
    if (widthDip <= 0.0f) return m.padTop;

    float avail = widthDip - 2.0f * m.padX;
    float contentWidth = wrapEnabled_ ? avail : kNoWrapContentWidthDip;
    if (wrapEnabled_ && contentWidth > m.maxContentWidth)
        contentWidth = m.maxContentWidth;
    if (contentWidth <= 0.0f) contentWidth = 1.0f;

    float originX = wrapEnabled_
        ? (widthDip - contentWidth) * 0.5f
        : m.padX;

    // The brushes shared by the passes below. The glyphs themselves
    // are the replay's now, so the body, link and quote brushes the
    // per node text painter kept are gone; what stays are the washes
    // and the fills the caret side still paints from (the code card
    // and the table build and release their own).
    ID2D1SolidColorBrush* selBrush = nullptr;
    if (sel && !sel->Empty()) {
        rt->CreateSolidColorBrush(pal.selectionBg, &selBrush);
    }

    // Search match brushes, created once per Render and released at the end.
    // Both stay null when find is closed, so no block pays for them.
    ID2D1SolidColorBrush* matchBrush = nullptr;
    ID2D1SolidColorBrush* currentBrush = nullptr;
    if (HasSearchMatches()) {
        rt->CreateSolidColorBrush(pal.findMatchBg, &matchBrush);
        rt->CreateSolidColorBrush(pal.findCurrentMatchBg, &currentBrush);
    }
    // Marker brushes for the same lifetime, null when the layer is hidden.
    ID2D1SolidColorBrush* markerBrush = nullptr;
    ID2D1SolidColorBrush* markerLine = nullptr;
    if (HasMarkers()) {
        rt->CreateSolidColorBrush(pal.markerBg, &markerBrush);
        rt->CreateSolidColorBrush(pal.markerLine, &markerLine);
    }
    // Clip to content area (below the ribbon).
    D2D1_SIZE_F clipSize = rt->GetSize();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->PushAxisAlignedClip(
        D2D1::RectF(0.0f, topOffsetDip, clipSize.width, clipSize.height),
        (D2D1_ANTIALIAS_MODE)0);

    // Scroll both axes: the content column exceeds the viewport width at
    // high zoom, so a vertical-only offset left the right-hand part of
    // every line unreachable.
    rt->SetTransform(D2D1::Matrix3x2F::Translation(-scrollX, -scrollY));

    // Clear the layout cache before rebuilding.
    if (cache_) cache_->Clear();

    // One shared pass for the frame: every owned kind's band, gap and
    // display item comes from it. The per node loop below keeps only
    // the caret's DirectWrite side of the text blocks and the washes
    // that sit under the replayed glyphs; the code cards, the tables
    // and the diagram painters re-run after the replay, and the images
    // are decoded before the walk reads their sizes.
    std::vector<const layout::LaidBlock*> sharedByNode;
    const auto sharedHold =
        SharedLayoutPass(doc, dw, m, &pal, contentWidth, &sharedByNode, sel);
    const layout::DocumentLayout& shared = *sharedHold;

    // The engine the walk and the rect twin ask their geometry
    // questions through, the same adapter those records were measured
    // with: the renderer's own engine, or the test replacement.
    WalkTextEngine twinEngine(TextEngineForWalk(), zoom_);

    // Phase zero, before the walk's image questions are answered: the
    // inline images of the text nodes are fetched here, where the per
    // node painter always fetched them, so the size the seam answers
    // is the size on screen this frame and the placeholder never
    // waits for a repaint that may not come. drewImage remembers which
    // nodes have a drawn image, the old test that kept the caret
    // record, the chips and the washes off an image bearing block.
    std::vector<char> nodeDrewImage(doc.nodes.size(), 0);
    for (size_t nodeIdx = 0; nodeIdx < doc.nodes.size(); ++nodeIdx) {
        const Node& n = doc.nodes[nodeIdx];
        if (n.block != BlockKind::Paragraph &&
            n.block != BlockKind::BlockQuote &&
            n.block != BlockKind::List && n.block != BlockKind::Heading) {
            continue;
        }
        float columnDrawW = contentWidth;
        if (n.block == BlockKind::BlockQuote) {
            columnDrawW -= m.quoteIndent * (n.depth + 1);
        } else if (n.block == BlockKind::List) {
            columnDrawW -= m.listIndent * (n.depth + 1);
        }
        for (const auto& ib : n.children) {
            if (ib.kind != InlineKind::Image || ib.url.empty()) continue;
            // Check if this is an SVG image, by extension or data url.
            std::string urlLower = ib.url;
            for (auto& ch : urlLower)
                ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            bool isSvg = (urlLower.size() > 4 &&
                urlLower.substr(urlLower.size() - 4) == ".svg") ||
                (urlLower.find("data:image/svg+xml") == 0);
            if (isSvg) {
                // SVG source is fetched once per view and parsed at
                // every paint, the rule the old inline pass followed:
                // no bitmap, the text itself is the cache.
                if (!d2d_ctx5_) continue;
                std::string svgText;
                bool svgTried = false;
                for (const auto& e : img_cache_) {
                    if (e.url == ib.url) {
                        svgTried = true;
                        if (!e.failed) {
                            svgText = e.svgText;
                        }
                        break;
                    }
                }
                if (!svgTried) {
                    svgText = ImageHelper::LoadSvgText(ib.url);
                    if (img_cache_.size() >= 128) {
                        if (img_cache_.front().bmp)
                            img_cache_.front().bmp->Release();
                        img_cache_.erase(img_cache_.begin());
                    }
                    img_cache_.push_back(
                        {ib.url, nullptr, svgText.empty(), svgText});
                }
                if (!svgText.empty()) {
                    svg::SvgDoc svgDoc;
                    if (svgDoc.Load(d2d_ctx5_, svgText) &&
                        svgDoc.Width() > 0.0f && svgDoc.Height() > 0.0f) {
                        RememberImageSize(ib.url, svgDoc.Width(),
                                          svgDoc.Height());
                        nodeDrewImage[nodeIdx] = 1;
                    }
                }
            } else {
                // Inline raster image: fetch once per URL, reuse across
                // repaints (the scroll ticks used to re-fetch it). The
                // outcome is cached either way: the failure entry is
                // what stops a dead URL being fetched again.
                ID2D1Bitmap* bmp = nullptr;
                bool alreadyTried = false;
                for (const auto& e : img_cache_) {
                    if (e.url == ib.url) {
                        bmp = e.bmp;
                        alreadyTried = true;
                        break;
                    }
                }
                if (!bmp && !alreadyTried) {
                    bmp = ImageHelper::LoadBitmapFromUrl(
                        rt, ib.url, columnDrawW);
                    if (img_cache_.size() >= 128) {
                        if (img_cache_.front().bmp)
                            img_cache_.front().bmp->Release();
                        img_cache_.erase(img_cache_.begin());
                    }
                    img_cache_.push_back(
                        {ib.url, bmp, bmp == nullptr, std::string()});
                }
                if (bmp) {
                    D2D1_SIZE_F bmpSize = bmp->GetSize();
                    RememberImageSize(ib.url, bmpSize.width,
                                      bmpSize.height);
                    nodeDrewImage[nodeIdx] = 1;
                }
            }
        }
    }

    // Phase one, the caret's DirectWrite side: the per node text block
    // runs exactly as it always did, the same DirectWrite layout, the
    // same source mapping and the same highlight rectangles, minus
    // every glyph, marker, bar, checkbox and image the replay paints
    // at the walk's own positions, and minus the band advance, which
    // the records own. The band top is the record's: the walk's gaps
    // are the gaps today's curY walk made.
    // The inline code washes come from the runs the replay draws, so
    // each wash sits exactly under its glyphs. They were once placed from
    // a second DirectWrite layout, which measured the line a little
    // differently and drifted off the text. A wash covers the term and
    // nothing else: the padding stays under half a space, so it never
    // reaches the neighbouring words or spaces. They go down first, under
    // the search and selection fills, as before.
    {
        ID2D1SolidColorBrush* codeBg = nullptr;
        rt->CreateSolidColorBrush(pal.inlineCodeBg, &codeBg);
        if (codeBg) {
            const float codeEm = m.codeFontSize * kPtToDip * zoom_;
            const float padX = m.inlineCodePadX * codeEm;
            const float originY = m.padTop + topOffsetDip;
            for (const display::Item& item : shared.list.items) {
                const auto* t = std::get_if<display::Text>(&item);
                if (!t) continue;
                const float top = originY + t->top;
                if (top > scrollY + viewH || top + t->line.height < scrollY)
                    continue;
                const auto& runs = t->line.runs;
                for (size_t i = 0; i < runs.size(); ++i) {
                    if (!runs[i].font.monospace || runs[i].text.empty())
                        continue;
                    const float right = i + 1 < runs.size()
                                            ? runs[i + 1].x
                                            : t->line.width;
                    const float w = right - runs[i].x;
                    if (w <= 0.0f) continue;
                    const float boxH = std::min(t->line.height, 1.6f * codeEm);
                    const float left = originX + t->x + runs[i].x;
                    const float boxTop =
                        top + (t->line.height - boxH) * 0.5f;
                    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
                        D2D1::RectF(left - padX, boxTop, left + w + padX,
                                    boxTop + boxH),
                        6.0f * zoom_, 6.0f * zoom_);
                    rt->FillRoundedRectangle(rr, codeBg);
                }
            }
            codeBg->Release();
        }
    }

    for (size_t nodeIdx = 0; nodeIdx < doc.nodes.size(); ++nodeIdx) {
        const Node& n = doc.nodes[nodeIdx];
        if (n.block != BlockKind::Paragraph &&
            n.block != BlockKind::BlockQuote &&
            n.block != BlockKind::List && n.block != BlockKind::Heading) {
            continue;
        }
        const layout::LaidBlock* b = sharedByNode[nodeIdx];
        if (!b) continue;
        const float curY = m.padTop + topOffsetDip + b->y;
        float drawX = originX + b->x;
        float drawW = b->width > 0.0f ? b->width : contentWidth;

        IDWriteTextFormat* fmt = nullptr;
        if (n.block == BlockKind::Heading) {
            // Only the caret's DirectWrite side: fmt feeds the cache
            // and washes; the glyphs and rule come from the list.
            int lvl = n.level;
            if (lvl < 1) lvl = 1;
            if (lvl > 6) lvl = 6;
            fmt = heading_fmt_[lvl];
        } else {
            fmt = body_fmt_;
        }
        if (!fmt) {
            continue;
        }

        std::u32string text32;
        float markerW = 0.0f;
        // The marker text and the ordered counter are the walk's now:
        // it resolves them into display items (the clipped gutter run,
        // the task checkbox stroke), so this pass only keeps the
        // gutter width the caret layout reserves.
        bool hasMarker = false;
        if (n.block == BlockKind::List) {
            markerW = (b && b->markerWidth > 0.0f)
                          ? b->markerWidth
                          : m.listIndent * m.markerRatio + m.markerGutter;
            hasMarker = (n.taskMark < 0);
        }

        struct SpanRange {
            UINT32 start;
            UINT32 length;
            bool em;
            bool strong;
            bool code;
            bool link;
            bool strike;
        };
        std::u16string text16;
        std::vector<SpanRange> spans;
        // Map each UTF-16 position to UTF-8 source byte offset.
        // Walk each inline block codepoint by codepoint to build this.
        std::vector<uint32_t> u16ToSrc;
        std::vector<uint32_t> u16ToSrcEnd;

        for (const auto& ib : n.children) {
            if (ib.kind == InlineKind::Image) continue;
            std::u16string part16 = ToUtf16(ib.text);
            UINT32 start = static_cast<UINT32>(text16.size());
            text16 += part16;
            UINT32 length = static_cast<UINT32>(part16.size());
            // Walk the UTF-32 codepoints and for each, compute its UTF-8
            // byte size and its UTF-16 code unit count, advancing both.
            uint32_t srcByte = ib.srcOffset;
            size_t u16Idx = start;
            for (char32_t cp : ib.text) {
                // UTF-8 byte length of this codepoint
                int utf8Len = (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                              (cp <= 0xFFFF) ? 3 : 4;
                // UTF-16 code unit count
                int utf16Len = (cp <= 0xFFFF) ? 1 : 2;
                uint32_t sourceEnd = srcByte + static_cast<uint32_t>(utf8Len);
                if (ib.text.size() == 1 && ib.srcLength >
                    static_cast<uint32_t>(utf8Len)) {
                    sourceEnd = ib.srcOffset + ib.srcLength;
                }
                for (int u = 0; u < utf16Len; u++) {
                    u16ToSrc.push_back(srcByte);
                    u16ToSrcEnd.push_back(sourceEnd);
                }
                srcByte += utf8Len;
                u16Idx += utf16Len;
            }
            if (length > 0) {
                spans.push_back({start, length, ib.em, ib.strong,
                                 ib.code, ib.kind == InlineKind::Link,
                                 ib.strike});
            }
        }

        // Image-only paragraphs have no text; they must still reach the
        // image-drawing loop below, so only skip when there are no images.
        bool hasImage = false;
        for (const auto& ib : n.children) {
            if (ib.kind == InlineKind::Image && !ib.url.empty()) {
                hasImage = true;
                break;
            }
        }
        if (text16.empty() && !hasMarker && !hasImage &&
            !n.virtualEmptyParagraph) {
            continue;
        }

        // A placeholder supplies a line metric, but both sides map to the
        // same canonical source position. It is layout data, not source text
        // or a special-case document interpretation in mouse hit-testing.
        const bool emptyCaretBlock = text16.empty() && !hasImage &&
            (n.virtualEmptyParagraph || n.block == BlockKind::List);
        const std::u16string layoutText16 = emptyCaretBlock ? u" " : text16;
        if (emptyCaretBlock) {
            u16ToSrc.push_back(n.contentOffset);
            u16ToSrcEnd.push_back(n.contentOffset);
        }

        float textX = drawX + markerW;
        float textW = drawW - markerW;

        IDWriteTextLayout* layout = nullptr;
        HRESULT hr = dw->CreateTextLayout(
            reinterpret_cast<const WCHAR*>(layoutText16.data()),
            static_cast<UINT32>(layoutText16.size()),
            fmt, textW > 0 ? textW : drawW, 1.0e9f, &layout);
        if (FAILED(hr) || !layout) {
            continue;
        }

        LayoutMetrics base = BaseMetrics();
        for (const auto& s : spans) {
            DWRITE_TEXT_RANGE range = {s.start, s.length};
            if (s.strong) {
                layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
            }
            if (s.em) {
                layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
            }
            if (s.code) {
                layout->SetFontFamilyName(base.codeFont, range);
                layout->SetFontSize(
                    base.codeFontSize * kPtToDip * zoom_, range);
            }
            if (s.link && base.linkUnderline) {
                // Underline is span metadata the hit tests never read;
                // the colour itself is the replay's run property, so no
                // drawing effect is attached anymore.
                layout->SetUnderline(TRUE, range);
            }
            if (s.strike) {
                layout->SetStrikethrough(TRUE, range);
            }
        }

        DWRITE_TEXT_METRICS metrics = {};
        layout->GetMetrics(&metrics);

        // While reading, the blank line next to a list takes the thin
        // strip Measure charges for it, the height the cache keeps for
        // this record.
        const float blankAdvance =
            (!editing_ && n.virtualEmptyParagraph) ? kReadBlankLineStripDip
                                                   : metrics.height;

        // A block whose image drew keeps no caret record: the image
        // owns the band the way it always did (the old pass released
        // the layout there and moved on past the washes). What such a
        // block shows on screen, the fitted bitmap and the text below
        // it, is the walk's and the resolver's own work now.
        if (nodeDrewImage[nodeIdx]) {
            layout->Release();
            continue;
        }


        // The task checkbox, the counter and the bullet marker are the
        // walk's display items now (the stroke pair, the clipped gutter
        // run), so they paint once, under the glyph replay, at the
        // exact origin the walk's own scale chose.

        // Marker layer first, then the search fills on top of it.
        if (markerBrush && markerLine && !u16ToSrc.empty() &&
            !text16.empty()) {
            const uint32_t blockStart = n.srcOffset;
            const uint32_t blockEnd = blockStart + n.srcLength;
            FillMarkerHighlights(rt, layout, markerBrush, markerLine,
                                 u16ToSrc,
                                 static_cast<UINT32>(text16.size()),
                                 blockStart, blockEnd,
                                 textX, curY, scrollY, viewH);
        }

        // Search match highlights, drawn before the selection highlight so
        // selection and text stay on top of the match fills.
        if (matchBrush && currentBrush && !u16ToSrc.empty() &&
            !text16.empty()) {
            const uint32_t blockStart = n.srcOffset;
            const uint32_t blockEnd = blockStart + n.srcLength;
            FillMatchHighlights(rt, layout, matchBrush, currentBrush,
                                u16ToSrc,
                                static_cast<UINT32>(text16.size()),
                                blockStart, blockEnd,
                                textX, curY, scrollY, viewH);
        }

        // Draw the selection wash, Batch 3.3: the same shared rect rule
        // the mac twin paints from (layout/selection_rects.h), answered
        // over this frame's own walk record, so both ports hold one
        // selection shape over the same records. The wash sits under
        // the replayed glyphs exactly where the old per layout fill
        // did: the placement below is the walk's own text placement,
        // the one the replay paints the lines at, and the band top
        // counts the images the record stacked above the text.
        if (selBrush && sel && !sel->Empty() && !b->lines.empty()) {
            std::vector<uint32_t> visToSrc;
            std::vector<uint32_t> visToSrcEnd;
            BuildBlockMapping(n, &visToSrc, &visToSrcEnd);
            if (!visToSrc.empty()) {
                float washX = 0.0f;
                float washTop = 0.0f;
                TextOriginOf(*b, m, originX,
                             m.padTop + topOffsetDip, &washX, &washTop);
                std::vector<platform::Rect> washRects;
                layout::AppendRangeRects(
                    b->lines, visToSrc, twinEngine, washX, washTop,
                    sel->Start(), sel->Start() + sel->Length(),
                    &washRects, ColumnAlign::Default, 0.0f, false);
                for (const platform::Rect& r : washRects) {
                    rt->FillRectangle(
                        D2D1::RectF(r.x, r.y, r.x + r.width,
                                    r.y + r.height),
                        selBrush);
                }
            }
        }

        // No glyph draw here: the replay below paints every text run at
        // the walk's own positions, one translate for the frame. The
        // DirectWrite layout's only remaining job is the caret cache
        // record above, the same layout, the same maps, the same
        // coordinates as before.

        if (cache_) {
            BlockLayout bl;
            bl.layout = layout;
            bl.x = textX;
            bl.y = curY;
            bl.width = textW > 0 ? textW : drawW;
            bl.height = blankAdvance;
            bl.srcOffset = n.srcOffset;
            bl.srcLength = n.srcLength;
            bl.textStartOffset = n.contentOffset;
            bl.nodeIndex = nodeIdx;
            bl.u16ToSrc = std::move(u16ToSrc);
            bl.u16ToSrcEnd = std::move(u16ToSrcEnd);
            // Caret height: the natural line height of this block's
            // format (cursor-blinking guide), not the em size.
            bl.fontHeight = LineHeightOf(fmt);
            cache_->Add(bl);
        } else {
            layout->Release();
        }
    }

    // Phase two, the replay: the whole display list under one
    // translate, the exact structure the mac twin uses. Every band
    // sits at padTop + topOffset + record.y, exactly where the per
    // node pass puts the caret bands, so the glyphs land on the same
    // lines the cache points at; the scroll-only translate keeps every
    // item at its walked width and position.
    {
        const float originY = m.padTop + topOffsetDip;
        platform::D2DTarget player(rt);
        rt->SetTransform(D2D1::Matrix3x2F::Translation(
            originX - scrollX, originY - scrollY));
        display::Play(shared.list, &player,
                      [this](const display::Image& item) {
                          return ImageHandleFor(item);
                      });
        rt->SetTransform(D2D1::Matrix3x2F::Translation(-scrollX, -scrollY));
    }

    // Phase three, the opaque layers above the glyph pass: cards and
    // cells that cover their own bands, so they re-run on the raw
    // target, in view coordinates, each at RecordY.
    PaintSharedOverlays(rt, dw, doc, sharedByNode, shared, m, contentWidth,
                        originX, topOffsetDip, sel, scrollY);

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->PopAxisAlignedClip();

    if (selBrush) selBrush->Release();
    if (markerBrush) markerBrush->Release();
    if (markerLine) markerLine->Release();
    if (matchBrush) matchBrush->Release();
    if (currentBrush) currentBrush->Release();
    // The same extent the old curY walk returned: the last band's
    // bottom on the frame, gaps included, so Measure and this pass
    // still answer callers with one number.
    if (shared.blocks.empty()) return m.padTop + topOffsetDip;
    const layout::LaidBlock& last = shared.blocks.back();
    return m.padTop + topOffsetDip + last.y + last.height;
}

// --- Source view: render raw markdown as monospace text with word wrap ---

static std::u16string Utf8ToUtf16(const std::string& s) {
    std::u16string out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char16_t>(c);
            i += 1;
        } else if (c < 0xC0) {
            i += 1; // skip continuation byte
        } else if (c < 0xE0) {
            if (i + 1 < s.size()) {
                char16_t ch = ((c & 0x1F) << 6) |
                    (static_cast<unsigned char>(s[i+1]) & 0x3F);
                out += ch;
                i += 2;
            } else { i += 1; }
        } else if (c < 0xF0) {
            if (i + 2 < s.size()) {
                char16_t ch = ((c & 0x0F) << 12) |
                    ((static_cast<unsigned char>(s[i+1]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(s[i+2]) & 0x3F);
                out += ch;
                i += 3;
            } else { i += 1; }
        } else {
            if (i + 3 < s.size()) {
                uint32_t cp = ((c & 0x07) << 18) |
                    ((static_cast<unsigned char>(s[i+1]) & 0x3F) << 12) |
                    ((static_cast<unsigned char>(s[i+2]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(s[i+3]) & 0x3F);
                cp -= 0x10000;
                out += static_cast<char16_t>(0xD800 + (cp >> 10));
                out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
                i += 4;
            } else { i += 1; }
        }
    }
    return out;
}

void Renderer::DropSourceLayout() {
    if (srcState_.layout) srcState_.layout->Release();
    srcState_ = SourceLayoutState();
}

bool Renderer::EnsureSourceLayout(IDWriteFactory* dw, const std::string& src,
                                  float widthDip, float textW,
                                  float contentWidth, bool forceRebuild) {
    SourceLayoutState& st = srcState_;
    const bool same = st.layout && !forceRebuild && st.width == widthDip &&
                      st.zoom == zoom_ && st.wrap == wrapEnabled_ &&
                      st.widthMode == contentWidthMode_ &&
                      st.srcSize == src.size() && st.format == code_fmt_;
    if (same) return true;
    DropSourceLayout();
    st.text16 = Utf8ToUtf16(src);
    if (st.text16.empty()) return false;
    IDWriteTextLayout* layout = nullptr;
    const HRESULT hr = dw->CreateTextLayout(
        reinterpret_cast<const WCHAR*>(st.text16.data()),
        static_cast<UINT32>(st.text16.size()),
        code_fmt_, textW > 0 ? textW : contentWidth, 1.0e9f, &layout);
    if (FAILED(hr) || !layout) { st = SourceLayoutState(); return false; }
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    DWRITE_TEXT_METRICS tm = {};
    layout->GetMetrics(&tm);
    st.layout = layout;
    st.height = tm.height;
    st.u16ToSrc.reserve(st.text16.size());
    st.u16ToSrcEnd.reserve(st.text16.size());
    uint32_t bi = 0;
    for (size_t si = 0; si < src.size();) {
        const unsigned char c = static_cast<unsigned char>(src[si]);
        const int utf8Len = (c < 0x80) ? 1 : (c < 0xC0) ? 1 :
                            (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        const int utf16Len = (c < 0x80) ? 1 : (c < 0xE0) ? 1 :
                             (c < 0xF0) ? 1 : 2;
        for (int u = 0; u < utf16Len; ++u) {
            st.u16ToSrc.push_back(bi);
            st.u16ToSrcEnd.push_back(bi + static_cast<uint32_t>(utf8Len));
        }
        bi += utf8Len;
        si += utf8Len;
    }
    st.width = widthDip;
    st.zoom = zoom_;
    st.wrap = wrapEnabled_;
    st.widthMode = contentWidthMode_;
    st.srcSize = src.size();
    st.format = code_fmt_;
    return true;
}

float Renderer::MeasureSourceView(IDWriteFactory* dw, const std::string& src,
                                   float widthDip, float topOffsetDip) {
    if (!dw || !code_fmt_) return 0.0f;

    // The source view has no pictures, so it must not inherit the scroll
    // range a rendered document left behind.
    widestBlockRight_ = 0.0f;
    LayoutMetrics m = ComputeMetrics();
    float avail = widthDip - 2.0f * m.padX;
    float contentWidth = wrapEnabled_ ? avail : kNoWrapContentWidthDip;
    if (wrapEnabled_ && contentWidth > m.maxContentWidth)
        contentWidth = m.maxContentWidth;
    if (contentWidth <= 0.0f) contentWidth = 1.0f;
    float originX = wrapEnabled_
        ? (widthDip - contentWidth) * 0.5f
        : m.padX;
    float textW = contentWidth - 2.0f * m.cellPadX;

    // A scroll frame (reuse granted) keeps the last layout; every other
    // frame builds it afresh, and the render that follows uses that one.
    if (!EnsureSourceLayout(dw, src, widthDip, textW, contentWidth,
                            !layoutReuse_))
        return topOffsetDip + 20.0f;
    srcState_.fresh = true;
    float totalH = m.padTop + topOffsetDip + srcState_.height + m.padTop;
    return totalH;
}

float Renderer::RenderSourceView(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                                  const std::string& src, float widthDip,
                                  float scrollY, float topOffsetDip,
                                  float scrollX,
                                  const Selection* sel) {
    if (!rt || !dw || !code_fmt_) return topOffsetDip;

    // Own the cache lifecycle like Render() does: clear stale blocks so
    // repeated calls never accumulate layout-owned entries.
    if (cache_) cache_->Clear();

    Palette pal = BasePalette();
    LayoutMetrics m = ComputeMetrics();
    const float viewH = rt->GetSize().height;
    float avail = widthDip - 2.0f * m.padX;
    float contentWidth = wrapEnabled_ ? avail : kNoWrapContentWidthDip;
    if (wrapEnabled_ && contentWidth > m.maxContentWidth)
        contentWidth = m.maxContentWidth;
    if (contentWidth <= 0.0f) contentWidth = 1.0f;
    float originX = wrapEnabled_
        ? (widthDip - contentWidth) * 0.5f
        : m.padX;
    float textW = contentWidth - 2.0f * m.cellPadX;
    float textX = originX + m.cellPadX;
    float textY = m.padTop + topOffsetDip;

    // Reuse what this frame's measure just built, or the last layout on a
    // scroll frame. Anything else (a render with no measure before it)
    // builds afresh, so an edit can never show the old text.
    const bool allowed = srcState_.fresh || layoutReuse_;
    srcState_.fresh = false;
    if (!EnsureSourceLayout(dw, src, widthDip, textW, contentWidth, !allowed))
        return topOffsetDip + 20.0f;
    const std::u16string& text16 = srcState_.text16;
    IDWriteTextLayout* layout = srcState_.layout;
    DWRITE_TEXT_METRICS tm = {};
    layout->GetMetrics(&tm);

    // Clip to the content area (document coordinates; transform handles scrollY).
    D2D1_RECT_F clipRect = D2D1::RectF(
        originX, textY, originX + contentWidth, textY + tm.height + 50.0f);
    rt->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_ALIASED);

    // Translate for scrolling.
    rt->SetTransform(D2D1::Matrix3x2F::Translation(-scrollX, -scrollY));

    // Draw text.
    ID2D1SolidColorBrush* textBrush = nullptr;
    rt->CreateSolidColorBrush(pal.textPrimary, &textBrush);
    ID2D1SolidColorBrush* selBrush = nullptr;
    rt->CreateSolidColorBrush(pal.selectionBg, &selBrush);
    // Search match brushes, created once and released at the end like
    // selBrush. Null when find is closed, so a closed find bar costs nothing.
    ID2D1SolidColorBrush* matchBrush = nullptr;
    ID2D1SolidColorBrush* currentBrush = nullptr;
    if (HasSearchMatches()) {
        rt->CreateSolidColorBrush(pal.findMatchBg, &matchBrush);
        rt->CreateSolidColorBrush(pal.findCurrentMatchBg, &currentBrush);
    }
    // Marker brushes for the same lifetime, null when the layer is hidden.
    ID2D1SolidColorBrush* markerBrush = nullptr;
    ID2D1SolidColorBrush* markerLine = nullptr;
    if (HasMarkers()) {
        rt->CreateSolidColorBrush(pal.markerBg, &markerBrush);
        rt->CreateSolidColorBrush(pal.markerLine, &markerLine);
    }

    const std::vector<uint32_t>& u16ToSrcMap = srcState_.u16ToSrc;
    // Marker layer across the whole source, behind the search fills.
    if (markerBrush && markerLine && !u16ToSrcMap.empty() &&
        !text16.empty()) {
        FillMarkerHighlights(rt, layout, markerBrush, markerLine,
                             u16ToSrcMap, static_cast<UINT32>(text16.size()),
                             0, static_cast<uint32_t>(src.size()),
                             textX, textY, scrollY, viewH);
    }
    // Search match highlights, drawn before the selection highlight so
    // selection and text stay on top of the match fills. Source view covers
    // the whole document, so the block range is the whole source.
    if (matchBrush && currentBrush && !u16ToSrcMap.empty() &&
        !text16.empty()) {
        FillMatchHighlights(rt, layout, matchBrush, currentBrush,
                            u16ToSrcMap, static_cast<UINT32>(text16.size()),
                            0, static_cast<uint32_t>(src.size()),
                            textX, textY, scrollY, viewH);
    }
    if (selBrush && sel && !sel->Empty() && !u16ToSrcMap.empty()) {
        uint32_t selStart = sel->Start();
        uint32_t selEnd = selStart + sel->Length();
        // Binary search: find first u16 index where u16ToSrc[idx] >= srcOff.
        auto findU16 = [&](uint32_t srcOff) -> UINT32 {
            if (srcOff <= u16ToSrcMap[0]) return 0;
            size_t lo = 0, hi = u16ToSrcMap.size();
            while (lo < hi) {
                size_t mid = (lo + hi) / 2;
                if (u16ToSrcMap[mid] < srcOff) lo = mid + 1;
                else hi = mid;
            }
            return (lo < u16ToSrcMap.size()) ?
                static_cast<UINT32>(lo) :
                static_cast<UINT32>(text16.size());
        };
        UINT32 u16Start = findU16(selStart);
        UINT32 u16End = (selEnd >= static_cast<uint32_t>(src.size())) ?
            static_cast<UINT32>(text16.size()) : findU16(selEnd);
        FillSelectionHighlight(rt, layout, selBrush, u16Start, u16End,
                               textX, textY, scrollY, viewH);
    }

    // Draw the text in document coordinates (transform handles scrollY).
    rt->DrawTextLayout(D2D1::Point2F(textX, textY),
                       layout, textBrush,
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);

    // Add block layout to cache for hit-testing and caret placement.
    if (cache_) {
        BlockLayout bl;
        bl.layout = layout;
        bl.x = textX;
        bl.y = textY;
        bl.width = textW > 0 ? textW : contentWidth;
        bl.height = tm.height;
        // Extend the block's y range to cover the full viewport so
        // that margin clicks at any visible y find this block even
        // when the text is shorter than the viewport.
        D2D1_SIZE_F rtSize = rt->GetSize();
        float viewportH = rtSize.height;
        if (bl.height < viewportH) bl.height = viewportH + m.padTop;
        bl.srcOffset = 0;
        bl.srcLength = static_cast<uint32_t>(src.size());
        bl.textStartOffset = 0;
        bl.nodeIndex = 0;
        bl.fontHeight = LineHeightOf(code_fmt_);
        bl.u16ToSrc = srcState_.u16ToSrc;
        bl.u16ToSrcEnd = srcState_.u16ToSrcEnd;
        // The cache holds its own reference; this renderer keeps one.
        layout->AddRef();
        cache_->Add(bl);
    }

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->PopAxisAlignedClip();

    if (textBrush) textBrush->Release();
    if (selBrush) selBrush->Release();
    if (markerBrush) markerBrush->Release();
    if (markerLine) markerLine->Release();
    if (matchBrush) matchBrush->Release();
    if (currentBrush) currentBrush->Release();
    return textY + tm.height;
}


// --- SVG block rendering ---

// Convert a UTF-32 string (from Node::raw) to UTF-8.
static std::string U32ToUtf8(const std::u32string& s) {
    std::string out;
    for (char32_t cp : s) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

void Renderer::ClearSvgCache() {
    for (auto& e : svg_cache_) {
        e.doc.Release();
    }
    svg_cache_.clear();
    // Inline bitmaps belong to the render target; drop them whenever the
    // target is recreated or the document changes.
    for (auto& e : img_cache_) {
        if (e.bmp) e.bmp->Release();
    }
    img_cache_.clear();
    // Intrinsic sizes are read from the files on disk, so a document change
    // (or a file the watcher reloaded) can make them stale. Dropping them
    // costs one header read per picture on the next measure.
    img_size_cache_.clear();
}

// Intrinsic size of an inline image, cached per URL. Measure needs it before
// the draw pass has run, so a local file is read here; a remote image stays
// unknown until the draw pass has fetched it, and the placeholder height is
// corrected on the next paint.
bool Renderer::InlineImageSize(const std::string& url, float* w, float* h) {
    for (const auto& e : img_size_cache_) {
        if (e.url != url) continue;
        if (!e.known) return false;
        *w = e.w;
        *h = e.h;
        return true;
    }
    float iw = 0.0f, ih = 0.0f;
    bool known = false;
    std::string lower = url;
    for (auto& c : lower) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    // A remote picture is not fetched during measure: that would block the
    // UI thread on the network on the first paint. The draw pass fetches it
    // and RememberImageSize corrects the height on the next measure.
    if (lower.find("http://") == 0 || lower.find("https://") == 0) {
        return false;
    }
    const bool isSvg =
        (lower.size() > 4 && lower.substr(lower.size() - 4) == ".svg") ||
        (lower.find("data:image/svg+xml") == 0);
    if (isSvg) {
        const std::string text = ImageHelper::LoadSvgText(url);
        if (!text.empty()) known = svg::ReadSize(text, &iw, &ih);
    } else {
        known = ImageHelper::LoadImageSize(url, &iw, &ih);
    }
    if (img_size_cache_.size() >= 128) {
        img_size_cache_.erase(img_size_cache_.begin());
    }
    img_size_cache_.push_back({url, iw, ih, known});
    if (!known) return false;
    *w = iw;
    *h = ih;
    return true;
}

void Renderer::RememberImageSize(const std::string& url, float w, float h) {
    if (!(w > 0.0f) || !(h > 0.0f)) return;
    for (auto& e : img_size_cache_) {
        if (e.url == url) {
            e.w = w;
            e.h = h;
            e.known = true;
            return;
        }
    }
    if (img_size_cache_.size() >= 128) {
        img_size_cache_.erase(img_size_cache_.begin());
    }
    img_size_cache_.push_back({url, w, h, true});
}

svg::SvgDoc* Renderer::GetSvgDoc(const Node& n, float availW) {
    if (!d2d_ctx5_) return nullptr;

    // Check the cache by where the fence sits and by what it says
    const uint64_t rawHash = Fnv1aUtf32(n.raw);
    for (auto& e : svg_cache_) {
        if (e.srcOffset == n.srcOffset && e.rawHash == rawHash) {
            return &e.doc;
        }
    }

    // Cache miss: parse the SVG
    std::string utf8 = U32ToUtf8(n.raw);
    if (utf8.empty()) return nullptr;

    SvgCacheEntry entry;
    entry.srcOffset = n.srcOffset;
    entry.rawHash = rawHash;
    bool loaded = entry.doc.Load(d2d_ctx5_, utf8);
    diag::TraceFmt("SVGLOAD off=%u bytes=%zu ok=%d w=%.1f h=%.1f",
                   n.srcOffset, utf8.size(), loaded ? 1 : 0,
                   entry.doc.Width(), entry.doc.Height());
    if (!loaded) {
        return nullptr;
    }
    // A reader who opens a hundred documents should not keep a hundred
    // drawings alive.
    if (svg_cache_.size() >= 32) svg_cache_.clear();
    svg_cache_.push_back(std::move(entry));
    return &svg_cache_.back().doc;
}

// -----------------------------------------------------------------------------
// Mermaid flowchart rendering (Task 12).
// -----------------------------------------------------------------------------

// Dotted edge dash pattern (D2D1_DASH_STYLE_CUSTOM takes float pairs:
// dash, gap in stroke-width multiples). Mermaid's `.flowchart .edgePath
// [style*='dotted']` renders ~3:3 dots at 1px stroke; we use 1.5/2.2 to read
// as dots at 1.5px stroke.
static const float kMermaidDash[] = {1.0f, 2.0f};
static const UINT32 kMermaidDashSize = 2;

// Note: layout is computed once at parse time (parser.cpp) and stored on
// Node::mermaid_layout, so we never re-run layout on WM_PAINT. Height is
// derived through mermaid::MeasureLayoutHeight so measure and paint agree.
std::shared_ptr<mermaid::LaidOutFlowchart> Renderer::GetMermaidLayout(
        IDWriteFactory* dw, const Node& n) const {
    if (!n.mermaid_flowchart || n.mermaid_source.empty() || !dw ||
        !mermaid_measure_fmt_) return n.mermaid_layout;
    if (auto cached = mermaid_layout_cache_.Find(n.mermaid_source, zoom_))
        return cached;
    auto laid = std::make_shared<mermaid::LaidOutFlowchart>(
        mermaid::LayoutFlowchartWithDWrite(*n.mermaid_flowchart, dw,
                                            mermaid_measure_fmt_));
    if (!laid->nodes.empty()) {
        mermaid_layout_cache_.Put(n.mermaid_source, zoom_, laid);
        return laid;
    }
    return n.mermaid_layout;
}

static void DrawArrowHead(ID2D1RenderTarget* rt, ID2D1Factory* fac,
                          ID2D1SolidColorBrush* brush,
                          const mermaid::Point& a, const mermaid::Point& b,
                          float ox, float oy, float scale) {
    if (!rt || !fac || !brush) return;
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6) return;
    dx /= len; dy /= len;
    const float head_len = 10.0f;
    const float head_w = 6.0f;
    D2D1_POINT_2F tip = {
        ox + static_cast<float>(b.x * scale),
        oy + static_cast<float>(b.y * scale)
    };
    double bx = b.x - dx * (head_len / (scale > 1e-6 ? scale : 1.0f));
    double by = b.y - dy * (head_len / (scale > 1e-6 ? scale : 1.0f));
    // Perpendicular in world units.
    double px = -dy;
    double py = dx;
    double half = head_w / (scale > 1e-6 ? scale : 1.0f);
    D2D1_POINT_2F left = {
        ox + static_cast<float>((bx + px * half) * scale),
        oy + static_cast<float>((by + py * half) * scale)
    };
    D2D1_POINT_2F right = {
        ox + static_cast<float>((bx - px * half) * scale),
        oy + static_cast<float>((by - py * half) * scale)
    };
    ID2D1PathGeometry* geom = nullptr;
    if (FAILED(fac->CreatePathGeometry(&geom)) || !geom) return;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geom->Open(&sink)) || !sink) { geom->Release(); return; }
    sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
    D2D1_POINT_2F pts[2] = { left, right };
    sink->AddLines(pts, 2);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    rt->FillGeometry(geom, brush);
    geom->Release();
}

void Renderer::DrawMermaidBlock(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                                 const Node& n, float x, float y,
                                 float width) {
    if (!rt || !dw) return;
    const auto laid = GetMermaidLayout(dw, n);
    if (!laid) return;
    const auto& lo = *laid;
    const float pad = mermaid::kMermaidBlockPad;
    // Fit the diagram into the text column: scale down (never up) when the
    // natural canvas is wider than the available width.
    const float availW =
        width > 2.0f * pad ? width - 2.0f * pad : 0.0f;
    // Lineær zoom: scale = min(zoom, availW/naturalW); aldrig op-skaler.
    float scale = zoom_;
    if (lo.width > 0 && availW > 0) {
        float fit_scale = availW / static_cast<float>(lo.width);
        if (fit_scale < scale) scale = fit_scale;
    }
    float ox = x;
    float oy = y + pad;
    // Text must shrink by the same factor the geometry shrank by: clone the
    // base format once at the fitted size and use it for all diagram text.
    const float textScale = scale / (zoom_ > 0.001f ? zoom_ : 1.0f);
    IDWriteTextFormat* scale_fmt = body_fmt_;
    IDWriteTextFormat* scaled_fmt = nullptr;
    if (body_fmt_ && textScale != 1.0f && textScale > 0.01f) {
        WCHAR fam[64] = L"";
        if (SUCCEEDED(body_fmt_->GetFontFamilyName(fam, 64))) {
            DWRITE_FONT_WEIGHT wght = body_fmt_->GetFontWeight();
            DWRITE_FONT_STYLE style = body_fmt_->GetFontStyle();
            DWRITE_FONT_STRETCH stretch = body_fmt_->GetFontStretch();
            float size = body_fmt_->GetFontSize();
            dw->CreateTextFormat(fam, nullptr, wght, style, stretch,
                                 size * textScale, L"", &scaled_fmt);
        }
        if (scaled_fmt) scale_fmt = scaled_fmt;
    }

    Palette pal = BasePalette();
    ID2D1SolidColorBrush* edgeBrush = nullptr;
    rt->CreateSolidColorBrush(pal.mermaidText, &edgeBrush);
    ID2D1SolidColorBrush* nodeFill = nullptr;
    rt->CreateSolidColorBrush(pal.mermaidNodeFill, &nodeFill);
    ID2D1SolidColorBrush* nodeBorder = nullptr;
    rt->CreateSolidColorBrush(pal.mermaidNodeBorder, &nodeBorder);
    ID2D1SolidColorBrush* textBrush = nullptr;
    rt->CreateSolidColorBrush(pal.mermaidText, &textBrush);

    ID2D1Factory* fac = nullptr;
    rt->GetFactory(&fac);

    // Swimlane bands first, so nodes/edges overlay.
    if (!lo.lanes.empty()) {
        ID2D1SolidColorBrush* laneFill = nullptr;
        ID2D1SolidColorBrush* laneBorder = nullptr;
        ID2D1SolidColorBrush* laneTitle = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.96f, 0.96f, 1.0f), &laneFill);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.60f, 0.60f, 0.60f, 1.0f), &laneBorder);
        rt->CreateSolidColorBrush(pal.textPrimary, &laneTitle);
        for (const auto& lb : lo.lanes) {
            float lx = ox + static_cast<float>(lb.x * scale);
            float ly = oy + static_cast<float>(lb.y * scale);
            float lw = static_cast<float>(lb.width  * scale);
            float lh = static_cast<float>(lb.height * scale);
            D2D1_RECT_F r = D2D1::RectF(lx, ly, lx + lw, ly + lh);
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 6.0f * scale, 6.0f * scale);
            if (laneFill)   rt->FillRoundedRectangle(rr, laneFill);
            if (laneBorder) rt->DrawRoundedRectangle(rr, laneBorder, 1.0f);

            if (!lb.title.empty() && body_fmt_ && laneTitle) {
                std::u16string t16;
                for (unsigned char c : lb.title) t16.push_back(static_cast<char16_t>(c));
                IDWriteTextLayout* tl = nullptr;
                if (SUCCEEDED(dw->CreateTextLayout(
                        reinterpret_cast<const WCHAR*>(t16.data()),
                        static_cast<UINT32>(t16.size()),
                        scale_fmt, lw, 20.0f * scale, &tl)) && tl) {
                    rt->DrawTextLayout(D2D1::Point2F(lx + 6.0f * scale, ly + 2.0f * scale),
                                       tl, laneTitle,
                                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    tl->Release();
                }
            }
        }
        if (laneFill)   laneFill->Release();
        if (laneBorder) laneBorder->Release();
        if (laneTitle)  laneTitle->Release();
    }

    // Edges first.
    if (fac && edgeBrush) {
        ID2D1SolidColorBrush* labelBrush = nullptr;
        rt->CreateSolidColorBrush(pal.mermaidText, &labelBrush);
        for (const auto& e : lo.edges) {
            if (e.route.size() < 2) continue;
            // Stroke style: dotted edges use a dash pattern; thick edges
            // draw at ~2.5x width, matching mermaid's visual weight.
            const bool dotted = (e.style == mermaid::LineStyle::Dotted);
            const bool thick = (e.style == mermaid::LineStyle::Thick);
            const float stroke_w = thick ? 3.5f : 1.5f;
            ID2D1StrokeStyle* stroke = nullptr;
            if (dotted && fac) {
                // Square dashes, like mermaid's stroke-dasharray 3 3 look.
                fac->CreateStrokeStyle(
                    D2D1_STROKE_STYLE_PROPERTIES{
                        D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                        D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_MITER, 8.0f,
                        D2D1_DASH_STYLE_CUSTOM, 0.0f,
                    },
                    kMermaidDash, kMermaidDashSize, &stroke);
            }
            ID2D1PathGeometry* path = nullptr;
            if (FAILED(fac->CreatePathGeometry(&path)) || !path) { if (stroke) stroke->Release(); continue; }
            ID2D1GeometrySink* sink = nullptr;
            if (FAILED(path->Open(&sink)) || !sink) { path->Release(); if (stroke) stroke->Release(); continue; }
            D2D1_POINT_2F p0 = {
                ox + static_cast<float>(e.route[0].x * scale),
                oy + static_cast<float>(e.route[0].y * scale)
            };
            sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t i = 1; i < e.route.size(); ++i) {
                D2D1_POINT_2F pi = {
                    ox + static_cast<float>(e.route[i].x * scale),
                    oy + static_cast<float>(e.route[i].y * scale)
                };
                sink->AddLine(pi);
            }
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            rt->DrawGeometry(path, edgeBrush, stroke_w, stroke);
            path->Release();
            if (stroke) stroke->Release();
            if (e.head == mermaid::Head::Arrow) {
                DrawArrowHead(rt, fac, edgeBrush,
                              e.route[e.route.size() - 2], e.route.back(),
                              ox, oy, scale);
            }

            // Edge label: centered at the polyline midpoint, with an opaque
            // backing rect so the line does not strike through the glyphs.
            if (!e.label.empty() && labelBrush && body_fmt_) {
                size_t mid_seg = (e.route.size() - 1) / 2;
                double mx = (e.route[mid_seg].x + e.route[mid_seg + 1].x) * 0.5;
                double my = (e.route[mid_seg].y + e.route[mid_seg + 1].y) * 0.5;
                std::u16string m16;
                {
                    const std::string& s = e.label;
                    for (size_t i = 0; i < s.size(); ) {
                        unsigned char c = static_cast<unsigned char>(s[i]);
                        uint32_t cp = 0;
                        int n_b = 1;
                        if (c < 0x80) { cp = c; n_b = 1; }
                        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
                            cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F); n_b = 2;
                        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
                            cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) |
                                 (s[i+2] & 0x3F); n_b = 3;
                        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
                            cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) |
                                 ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F); n_b = 4;
                        } else { cp = '?'; n_b = 1; }
                        if (cp <= 0xFFFF) {
                            m16.push_back(static_cast<char16_t>(cp));
                        } else {
                            cp -= 0x10000;
                            m16.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                            m16.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
                        }
                        i += n_b;
                    }
                }
                IDWriteTextLayout* tl = nullptr;
                float max_lbl_w = 220.0f * scale;
                if (SUCCEEDED(dw->CreateTextLayout(
                        reinterpret_cast<const WCHAR*>(m16.data()),
                        static_cast<UINT32>(m16.size()),
                        scale_fmt, max_lbl_w, 28.0f * scale, &tl)) && tl) {
                    DWRITE_TEXT_METRICS tm{};
                    tl->GetMetrics(&tm);
                    float lbl_w = tm.widthIncludingTrailingWhitespace;
                    float lbl_h = tm.height;
                    float lx = ox + static_cast<float>(mx * scale) - lbl_w * 0.5f;
                    float ly = oy + static_cast<float>(my * scale) - lbl_h * 0.5f;
                    D2D1_RECT_F bg = D2D1::RectF(lx - 2.0f, ly - 2.0f,
                                                 lx + lbl_w + 2.0f, ly + lbl_h + 2.0f);
                    ID2D1SolidColorBrush* bgBrush = nullptr;
                    rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &bgBrush);
                    if (bgBrush) {
                        rt->FillRectangle(bg, bgBrush);
                        bgBrush->Release();
                    }
                    rt->DrawTextLayout(D2D1::Point2F(lx, ly), tl, labelBrush,
                                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    tl->Release();
                }
            }
        }
        if (labelBrush) labelBrush->Release();
    }

    // Nodes.
    for (const auto& nd : lo.nodes) {
        if (nd.is_dummy) continue;
        float nx = ox + static_cast<float>((nd.x - nd.width / 2.0f) * scale);
        float ny = oy + static_cast<float>((nd.y - nd.height / 2.0f) * scale);
        float nw = static_cast<float>(nd.width * scale);
        float nh = static_cast<float>(nd.height * scale);
        D2D1_RECT_F rect = D2D1::RectF(nx, ny, nx + nw, ny + nh);

        switch (nd.shape) {
            case mermaid::NodeShape::Round: {
                D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 6.0f * scale, 6.0f * scale);
                if (nodeFill) rt->FillRoundedRectangle(rr, nodeFill);
                if (nodeBorder) rt->DrawRoundedRectangle(rr, nodeBorder, 1.0f);
                break;
            }
            case mermaid::NodeShape::Stadium: {
                float r = nh * 0.5f;
                D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, r, r);
                if (nodeFill) rt->FillRoundedRectangle(rr, nodeFill);
                if (nodeBorder) rt->DrawRoundedRectangle(rr, nodeBorder, 1.0f);
                break;
            }
            case mermaid::NodeShape::Circle: {
                D2D1_ELLIPSE el = D2D1::Ellipse(
                    D2D1::Point2F(nx + nw * 0.5f, ny + nh * 0.5f),
                    nw * 0.5f, nh * 0.5f);
                if (nodeFill) rt->FillEllipse(el, nodeFill);
                if (nodeBorder) rt->DrawEllipse(el, nodeBorder, 1.0f);
                break;
            }
            case mermaid::NodeShape::Diamond: {
                if (fac) {
                    ID2D1PathGeometry* geom = nullptr;
                    if (SUCCEEDED(fac->CreatePathGeometry(&geom)) && geom) {
                        ID2D1GeometrySink* sink = nullptr;
                        if (SUCCEEDED(geom->Open(&sink)) && sink) {
                            D2D1_POINT_2F top = D2D1::Point2F(nx + nw * 0.5f, ny);
                            D2D1_POINT_2F right = D2D1::Point2F(nx + nw, ny + nh * 0.5f);
                            D2D1_POINT_2F bot = D2D1::Point2F(nx + nw * 0.5f, ny + nh);
                            D2D1_POINT_2F left = D2D1::Point2F(nx, ny + nh * 0.5f);
                            sink->BeginFigure(top, D2D1_FIGURE_BEGIN_FILLED);
                            D2D1_POINT_2F pts[3] = { right, bot, left };
                            sink->AddLines(pts, 3);
                            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                            sink->Close();
                            sink->Release();
                            if (nodeFill) rt->FillGeometry(geom, nodeFill);
                            if (nodeBorder) rt->DrawGeometry(geom, nodeBorder, 1.0f);
                        }
                        geom->Release();
                    }
                }
                break;
            }
            case mermaid::NodeShape::Rect:
            default: {
                if (nodeFill) rt->FillRectangle(rect, nodeFill);
                if (nodeBorder) rt->DrawRectangle(rect, nodeBorder, 1.0f);
                break;
            }
        }

        // Label text, centered. Mermaid line-break separators (<br/> etc.)
        // split into separate lines; the DWrite layout renders them as a
        // multi-line block that matches SplitLabelLines sizing.
        if (!nd.label.empty() && body_fmt_ && textBrush) {
            std::vector<std::string> lines =
                mermaid::SplitLabelLines(nd.label);
            if (lines.empty()) lines.push_back(nd.label);
            std::u16string text16;
            for (size_t li = 0; li < lines.size(); ++li) {
                if (li > 0) text16.push_back(u'\n');
                const std::string& s = lines[li];
                for (size_t i = 0; i < s.size(); ) {
                    unsigned char c = static_cast<unsigned char>(s[i]);
                    uint32_t cp = 0;
                    int n_bytes = 1;
                    if (c < 0x80) { cp = c; n_bytes = 1; }
                    else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
                        cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F);
                        n_bytes = 2;
                    } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
                        cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) |
                             (s[i+2] & 0x3F);
                        n_bytes = 3;
                    } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
                        cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) |
                             ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F);
                        n_bytes = 4;
                    } else { cp = '?'; n_bytes = 1; }
                    if (cp <= 0xFFFF) {
                        text16.push_back(static_cast<char16_t>(cp));
                    } else {
                        cp -= 0x10000;
                        text16.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                        text16.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
                    }
                    i += n_bytes;
                }
            }
            IDWriteTextLayout* layout = nullptr;
            HRESULT hr = dw->CreateTextLayout(
                reinterpret_cast<const WCHAR*>(text16.data()),
                static_cast<UINT32>(text16.size()),
                scale_fmt, nw, nh, &layout);
            if (SUCCEEDED(hr) && layout) {
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(nx, ny), layout, textBrush,
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
                layout->Release();
            }
        }
    }

    if (scaled_fmt) scaled_fmt->Release();
    if (fac) fac->Release();
    if (edgeBrush) edgeBrush->Release();
    if (nodeFill) nodeFill->Release();
    if (nodeBorder) nodeBorder->Release();
    if (textBrush) textBrush->Release();
}
