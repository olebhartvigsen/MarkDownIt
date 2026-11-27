#include "windows/text_range_source_win.h"
#include "windows/text_engine_win.h"

#include <algorithm>

namespace platform {

namespace {

// The native object behind a block, or null when the block is not one of
// this adapter's own.
IDWriteTextLayout* NativeLayoutOf(const TextLayoutHandle& block) {
    const DWriteTextLayout* dwrite =
        dynamic_cast<const DWriteTextLayout*>(block.get());
    return dwrite ? dwrite->Layout() : nullptr;
}

}  // namespace

DWriteRangeSource::DWriteRangeSource(const TextLayoutHandle& block,
                                     float originX, float originY)
    : DWriteRangeSource(NativeLayoutOf(block), originX, originY) {}

DWriteRangeSource::DWriteRangeSource(IDWriteTextLayout* layout, float originX,
                                     float originY)
    : layout_(layout), originX_(originX), originY_(originY) {
    BuildCache();
}

DWriteRangeSource::~DWriteRangeSource() = default;

void DWriteRangeSource::BuildCache() {
    lines_.clear();
    totalU16_ = 0;
    valid_ = false;
    if (!layout_) return;

    UINT32 count = 0;
    // Sizing probe: DirectWrite reports the needed line count through the
    // E_NOT_SUFFICIENT_BUFFER failure path, so a failing HRESULT here is
    // the normal answer, not an error. Only a zero count means there is
    // nothing to cache. (Checking FAILED() on the probe rejected the count
    // every time, left the cache invalid, and silently dropped every fill
    // that depended on it, search matches and markers alike.)
    layout_->GetLineMetrics(nullptr, 0, &count);
    if (count == 0) return;

    std::vector<DWRITE_LINE_METRICS> metrics(count);
    if (FAILED(layout_->GetLineMetrics(metrics.data(), count, &count)) ||
        count == 0) {
        return;
    }
    lines_.resize(count);
    float y = 0.0f;
    UINT32 u = 0;
    for (UINT32 i = 0; i < count; ++i) {
        lines_[i].metrics = metrics[i];
        lines_[i].top = y;
        lines_[i].start = u;
        y += metrics[i].height;
        u += metrics[i].length;
    }
    totalU16_ = u;
    valid_ = true;
}

void DWriteRangeSource::AppendRangeRects(uint32_t start, uint32_t end,
                                         float cullTop, float cullBottom,
                                         std::vector<Rect>* out) {
    if (!out || !valid_ || !layout_) return;
    if (end <= start) return;
    if (start >= totalU16_) return;
    if (end > totalU16_) end = totalU16_;

    const size_t lineCount = lines_.size();

    // The line that contains the range start: the last line whose first
    // character is at or before it (a range commonly starts in the middle of
    // a line, so a plain lower bound would start too late).
    size_t first = 0;
    {
        size_t lo = 0, hi = lineCount;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (lines_[mid].start <= start) lo = mid + 1;
            else hi = mid;
        }
        first = (lo == 0) ? 0 : lo - 1;
    }

    // The first line whose bottom is inside the band, never before the
    // range's first line: a range far above the viewport costs no hit test.
    size_t begin = first;
    {
        size_t lo = first, hi = lineCount;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (originY_ + lines_[mid].top + lines_[mid].metrics.height <
                cullTop) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo > begin) begin = lo;
    }

    std::vector<DWRITE_HIT_TEST_METRICS> hits;
    for (size_t i = begin; i < lineCount; ++i) {
        const float lineTop = originY_ + lines_[i].top;
        if (lineTop > cullBottom) break;
        const UINT32 lineStart = lines_[i].start;
        const UINT32 lineEnd = lineStart + lines_[i].metrics.length;
        const UINT32 selLo = start > lineStart ? start : lineStart;
        const UINT32 selHi = end < lineEnd ? end : lineEnd;
        if (selHi > selLo) {
            const UINT32 count = selHi - selLo;
            hits.resize(count);
            UINT32 hitCount = 0;
            if (SUCCEEDED(layout_->HitTestTextRange(
                    selLo, count, originX_, originY_, hits.data(), count,
                    &hitCount))) {
                for (UINT32 h = 0; h < hitCount; ++h) {
                    Rect rect;
                    rect.x = hits[h].left;
                    rect.y = hits[h].top;
                    rect.width = hits[h].width;
                    rect.height = hits[h].height;
                    out->push_back(rect);
                }
            }
        }
        if (lineEnd >= end) break;
    }
}

}  // namespace platform
