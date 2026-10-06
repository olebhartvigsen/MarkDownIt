#include "layoutcache.h"
#include "editcontroller.h"
#include "crash_trace.h"
#include <algorithm>
#include <cmath>

LayoutCache::~LayoutCache() {
    Clear();
}

void LayoutCache::Clear() {
    for (auto& bl : blocks_) {
        if (bl.layout) {
            bl.layout->Release();
            bl.layout = nullptr;
        }
    }
    blocks_.clear();
    spanIndexBuilt_ = false;
}

void LayoutCache::Add(BlockLayout bl) {
    blocks_.push_back(bl);
    spanIndexBuilt_ = false;
}

void LayoutCache::EnsureSpanIndex() const {
    if (spanIndexBuilt_) return;
    spanIndexBuilt_ = true;
    spanIndexUsable_ = false;
    spanIndex_.clear();

    size_t total = 0;
    for (const auto& bl : blocks_) total += bl.u16ToSrc.size();
    if (total == 0) return;

    spanIndex_.reserve(total);
    // Blocks are added in document order and their mappings ascend, so the
    // collected spans are usually already sorted; the sort is only paid
    // when some block breaks that order.
    bool sorted = true;
    uint64_t previous = 0;
    for (const auto& bl : blocks_) {
        for (size_t i = 0; i < bl.u16ToSrc.size(); ++i) {
            const uint32_t start = bl.u16ToSrc[i];
            const uint32_t end = i < bl.u16ToSrcEnd.size()
                ? bl.u16ToSrcEnd[i] : start + 1;
            if (end <= start) continue;
            const uint64_t packed =
                (static_cast<uint64_t>(start) << 32) | end;
            if (!spanIndex_.empty() && packed < previous) sorted = false;
            previous = packed;
            spanIndex_.push_back(packed);
        }
    }
    if (!sorted) {
        std::sort(spanIndex_.begin(), spanIndex_.end());
    }
    // Overlapping spans would make "the" containing span ambiguous, and the
    // scan this replaces resolved that by block order. Refuse the index
    // when that happens so results cannot change.
    for (size_t i = 1; i < spanIndex_.size(); ++i) {
        const uint32_t previousEnd =
            static_cast<uint32_t>(spanIndex_[i - 1] & 0xFFFFFFFFu);
        const uint32_t currentStart = static_cast<uint32_t>(spanIndex_[i] >> 32);
        if (currentStart < previousEnd) return;
    }
    spanIndexUsable_ = true;
}

int LayoutCache::HitTestBlock(float x, float y) const {
    for (size_t i = 0; i < blocks_.size(); ++i) {
        const auto& bl = blocks_[i];
        if (x >= bl.x && x < bl.x + bl.width &&
            y >= bl.y && y < bl.y + bl.height) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int LayoutCache::FindNearestBlockInRow(float x, float y) const {
    // All blocks sharing the y band are candidates (table row cells,
    // parallel columns). Nearest by horizontal distance keeps padding
    // and side whitespace inside the clicked column's cell.
    int best = -1;
    float bestDist = 0.0f;
    for (size_t i = 0; i < blocks_.size(); ++i) {
        const auto& bl = blocks_[i];
        if (y < bl.y || y >= bl.y + bl.height) continue;
        const float dist =
            x < bl.x ? bl.x - x :
            x >= bl.x + bl.width ? x - (bl.x + bl.width) : 0.0f;
        if (best < 0 || dist < bestDist) {
            best = static_cast<int>(i);
            bestDist = dist;
        }
    }
    return best;
}

int LayoutCache::FindBlockAtY(float y) const {
    // Find the topmost block whose y-range contains y.
    // Do NOT snap to nearest block; if y is in a gap between blocks,
    // return -1 so the caller can clear the selection.
    for (size_t i = 0; i < blocks_.size(); ++i) {
        const auto& bl = blocks_[i];
        if (y >= bl.y && y < bl.y + bl.height) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool LayoutCache::GetLineRangeAtY(int blockIndex, float y,
                                  uint32_t* outStart, uint32_t* outEnd,
                                  float* outLineTop) const {
    if (blockIndex < 0 || blockIndex >= static_cast<int>(blocks_.size()))
        return false;
    const auto& bl = blocks_[blockIndex];
    if (!bl.layout) return false;

    // Get line metrics from the DirectWrite layout. The buffer must be
    // sized for the whole block: GetLineMetrics with a fixed 64-entry
    // buffer silently truncates (E_NOT_SUFFICIENT_BUFFER is returned for
    // layouts with more lines), which broke line selection in long
    // code blocks. Query the count first, then allocate to fit.
    UINT32 lineCount = 0;
    HRESULT hr = bl.layout->GetLineMetrics(nullptr, 0, &lineCount);
    if (FAILED(hr) && lineCount == 0) return false;
    if (lineCount == 0) return false;
    if (lineCount > 4096) lineCount = 4096;  // sanity cap
    std::vector<DWRITE_LINE_METRICS> metrics(static_cast<size_t>(lineCount));
    hr = bl.layout->GetLineMetrics(metrics.data(), lineCount, &lineCount);
    if (FAILED(hr) || lineCount == 0) return false;

    // Convert y to layout-local coordinate.
    float localY = y - bl.y;

    // Find which visual line contains localY.
    float lineTop = 0.0f;
    float lineBottom = 0.0f;
    int lineIdx = 0;
    for (uint32_t i = 0; i < lineCount; ++i) {
        lineBottom = lineTop + metrics[i].height;
        if (localY >= lineTop && localY < lineBottom) {
            lineIdx = static_cast<int>(i);
            break;
        }
        lineTop = lineBottom;
    }
    // If y is beyond the last line, clamp to last line.
    if (localY >= lineBottom) {
        lineIdx = static_cast<int>(lineCount) - 1;
        // Recompute lineTop for the last line
        lineTop = 0.0f;
        for (uint32_t i = 0; i + 1 < lineCount; ++i)
            lineTop += metrics[i].height;
    }

    if (outLineTop) *outLineTop = lineTop;

    // Now find the text position range of this visual line.
    // Use HitTestTextPosition on the first and last positions.
    // The line's text range can be found using GetLineMetrics
    // which unfortunately doesn't give us text positions directly.
    // Instead, use HitTestPoint on left edge and right edge of the line.

    // First, find the start position: hit-test the left edge of the line.
    float lineY = lineTop + metrics[lineIdx].height * 0.5f;
    float lineLocalY = lineY;

    DWRITE_HIT_TEST_METRICS htmStart = {}, htmEnd = {};
    BOOL isTrailing, isInside;

    // Hit-test the left edge to get the start of the line.
    bl.layout->HitTestPoint(0.0f, lineLocalY,
        &isTrailing, &isInside, &htmStart);

    // Hit-test the right edge (full width) to get the end of the line.
    bl.layout->HitTestPoint(bl.width, lineLocalY,
        &isTrailing, &isInside, &htmEnd);

    uint32_t u16Start = htmStart.textPosition;
    uint32_t u16End = htmEnd.textPosition + htmEnd.length;

    // Convert UTF-16 positions to UTF-8 source offsets via u16ToSrc.
    if (!bl.u16ToSrc.empty()) {
        if (u16Start < bl.u16ToSrc.size())
            *outStart = bl.u16ToSrc[u16Start];
        else
            *outStart = bl.srcOffset + bl.srcLength;

        if (u16End < bl.u16ToSrc.size())
            *outEnd = bl.u16ToSrc[u16End];
        else if (u16End > 0 && u16End - 1 < bl.u16ToSrc.size()) {
            uint32_t lastStart = bl.u16ToSrc[u16End - 1];
            if (srcText_ && lastStart < srcText_->size())
                *outEnd = NextGraphemeBoundary(*srcText_, lastStart);
            else
                *outEnd = bl.srcOffset + bl.srcLength;
        } else
            *outEnd = bl.srcOffset + bl.srcLength;
    } else {
        // Fallback: linear approximation.
        *outStart = bl.textStartOffset + u16Start;
        *outEnd = bl.textStartOffset + u16End;
    }

    return true;
}

int LayoutCache::BlockForOffset(uint32_t offset) const {
    int best = -1;
    uint32_t bestLen = 0xFFFFFFFF;
    for (size_t i = 0; i < blocks_.size(); ++i) {
        const auto& bl = blocks_[i];
        uint32_t startOff = (bl.srcCellStart < bl.srcOffset && bl.srcCellStart > 0)
            ? bl.srcCellStart : bl.srcOffset;
        uint32_t endOff = (bl.srcCellEnd > bl.srcOffset + bl.srcLength)
            ? bl.srcCellEnd : bl.srcOffset + bl.srcLength;
        if (offset >= startOff && offset <= endOff) {
            uint32_t len = endOff - startOff;
            if (len < bestLen) {
                bestLen = len;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

bool LayoutCache::OffsetIsRendered(uint32_t offset) const {
    // Only offsets explicitly present in the UTF-16-to-source map are
    // rendered. A block's source interval also contains Markdown syntax,
    // list prefixes and fence markers, so the interval itself is not enough.
    for (const auto& bl : blocks_) {
        for (uint32_t srcOff : bl.u16ToSrc) {
            if (srcOff == offset) return true;
            if (srcOff > offset) break;
        }
    }
    return false;
}

bool LayoutCache::RangeIsRendered(uint32_t start, uint32_t length) const {
    const uint32_t end = start + length;
    if (length == 0 || end < start) return false;

    // No source mapping means the visible layout is not ready. Callers must
    // repaint synchronously before filtering matches.
    bool hasMapping = false;
    for (const auto& bl : blocks_) {
        if (!bl.u16ToSrc.empty()) {
            hasMapping = true;
            break;
        }
    }
    if (!hasMapping) return false;
    if (!srcText_ || end > srcText_->size()) return false;

    // Find operations call this once per candidate match, so the per-byte
    // work has to be logarithmic, not a scan of every block.
    EnsureSpanIndex();
    if (spanIndexUsable_) {
        for (uint32_t offset = start; offset < end; ++offset) {
            const unsigned char byte =
                static_cast<unsigned char>((*srcText_)[offset]);
            if ((byte & 0xC0u) == 0x80u) continue;
            // The span containing offset is the last one starting at or
            // before it; the usable index holds at most one per start.
            const uint64_t key =
                (static_cast<uint64_t>(offset) << 32) | 0xFFFFFFFFu;
            auto it = std::upper_bound(spanIndex_.begin(), spanIndex_.end(),
                                       key);
            if (it == spanIndex_.begin()) return false;
            --it;
            const uint32_t spanStart = static_cast<uint32_t>(*it >> 32);
            const uint32_t spanEnd = static_cast<uint32_t>(*it & 0xFFFFFFFFu);
            if (offset < spanStart || offset >= spanEnd) return false;
            if (start > spanStart || end < spanEnd) return false;
        }
        return true;
    }

    for (uint32_t offset = start; offset < end; ++offset) {
        const unsigned char byte =
            static_cast<unsigned char>((*srcText_)[offset]);
        if ((byte & 0xC0u) == 0x80u) continue;
        bool covered = false;
        for (const auto& bl : blocks_) {
            for (size_t i = 0; i < bl.u16ToSrc.size(); ++i) {
                const uint32_t spanStart = bl.u16ToSrc[i];
                const uint32_t spanEnd = i < bl.u16ToSrcEnd.size()
                    ? bl.u16ToSrcEnd[i] : spanStart + 1;
                if (spanStart <= offset && offset < spanEnd) {
                    if (start > spanStart || end < spanEnd) return false;
                    covered = true;
                    break;
                }
            }
            if (covered) break;
        }
        if (!covered) return false;
    }
    return true;
}

bool LayoutCache::GetRenderedBlockRange(int blockIndex,
                                         uint32_t* outStart,
                                         uint32_t* outEnd) const {
    if (blockIndex < 0 || static_cast<size_t>(blockIndex) >= blocks_.size() ||
        !outStart || !outEnd) return false;
    const auto& bl = blocks_[static_cast<size_t>(blockIndex)];
    if (bl.u16ToSrc.empty()) return false;
    *outStart = bl.u16ToSrc.front();
    if (!bl.u16ToSrcEnd.empty()) {
        *outEnd = bl.u16ToSrcEnd.back();
    } else {
        *outEnd = bl.u16ToSrc.back() + 1;
    }
    if (srcText_ && *outEnd < srcText_->size()) {
        if ((*srcText_)[*outEnd] == '\r') ++*outEnd;
        if (*outEnd < srcText_->size() && (*srcText_)[*outEnd] == '\n') ++*outEnd;
    }
    return *outStart <= *outEnd;
}

uint32_t LayoutCache::NormalizeToRenderedCaret(uint32_t offset) const {
    uint32_t previous = UINT32_MAX;
    uint32_t next = UINT32_MAX;
    for (const auto& bl : blocks_) {
        for (size_t i = 0; i < bl.u16ToSrc.size(); ++i) {
            const uint32_t start = bl.u16ToSrc[i];
            const uint32_t end = i < bl.u16ToSrcEnd.size()
                ? bl.u16ToSrcEnd[i] : start + 1;
            if (offset == start) return offset;
            if (offset == end) {
                uint32_t normalized = end;
                if (srcText_) {
                    while (normalized < srcText_->size() &&
                           ((*srcText_)[normalized] == '*' ||
                            (*srcText_)[normalized] == '_' ||
                            (*srcText_)[normalized] == '`' ||
                            (*srcText_)[normalized] == '~')) {
                        ++normalized;
                    }
                    // Link text ends before `](destination)`. Keep a click at
                    // that visual end outside the hidden link syntax as well.
                    if (normalized + 1 < srcText_->size() &&
                        (*srcText_)[normalized] == ']' &&
                        (*srcText_)[normalized + 1] == '(') {
                        uint32_t cursor = normalized + 2;
                        int depth = 1;
                        while (cursor < srcText_->size() && depth > 0) {
                            const char c = (*srcText_)[cursor++];
                            if (c == '\\' && cursor < srcText_->size()) {
                                ++cursor;
                            } else if (c == '(') {
                                ++depth;
                            } else if (c == ')') {
                                --depth;
                            }
                        }
                        if (depth == 0) normalized = cursor;
                    }
                }
                return normalized;
            }
            if (offset > start && offset < end) return end;
            if (end < offset && (previous == UINT32_MAX || end > previous))
                previous = end;
            if (start > offset && (next == UINT32_MAX || start < next))
                next = start;
        }
    }
    if (previous == UINT32_MAX) return next;
    if (next == UINT32_MAX) return previous;
    return offset - previous <= next - offset ? previous : next;
}

uint32_t LayoutCache::PointToOffset(float x, float y) const {
    const int idx = HitTestBlock(x, y);
    return idx < 0 ? UINT32_MAX : PointToOffsetInBlock(idx, x, y);
}

uint32_t LayoutCache::PointToOffsetAtOrAfterBlock(float x, float y) const {
    // Sibling blocks can share one y band (table cells of a row), so
    // the y lookup alone always lands on the leftmost column. Resolve
    // the x-span first and keep the row only as a fallback for clicks
    // in the whitespace beside the block.
    int idx = HitTestBlock(x, y);
    if (idx < 0) {
        // Between/around sibling spans: the click belongs to the block
        // it is nearest to (cell padding belongs to that cell, not to
        // the leftmost neighbor).
        idx = FindNearestBlockInRow(x, y);
        if (idx < 0) return UINT32_MAX;
        if (x < blocks_[static_cast<size_t>(idx)].x) {
            return UINT32_MAX;
        }
    }
    const uint32_t candidate = PointToOffsetInBlock(idx, x, y);
    if (candidate == UINT32_MAX) return UINT32_MAX;
    const uint32_t normalized = NormalizeToRenderedCaret(candidate);
    return normalized == UINT32_MAX ? candidate : normalized;
}

uint32_t LayoutCache::PointToOffsetInBlock(int blockIndex, float x,
                                            float y) const {
    if (blockIndex < 0 || blockIndex >= static_cast<int>(blocks_.size()))
        return UINT32_MAX;
    const auto& bl = blocks_[static_cast<size_t>(blockIndex)];
    if (!bl.layout) return bl.textStartOffset;

    // DirectWrite clamps a point beyond a visual line to that line's final
    // insertion position. This preserves a paragraph-end click instead of
    // treating right-side whitespace as a left-margin line selection.
    const float localX = x - bl.x;
    const float localY = y - bl.y;

    DWRITE_HIT_TEST_METRICS htm = {};
    BOOL isTrailingHit = FALSE;
    BOOL isInside = FALSE;
    bl.layout->HitTestPoint(
        localX, localY, &isTrailingHit, &isInside, &htm);

    // htm.textPosition is a UTF-16 code-unit position within the layout.
    // Use the u16ToSrc mapping to convert to a UTF-8 source offset.
    if (!bl.u16ToSrc.empty()) {
        const size_t pos = std::min<size_t>(htm.textPosition,
                                             bl.u16ToSrc.size() - 1);
        const uint32_t srcOff = bl.u16ToSrc[pos];
        if (htm.textPosition >= bl.u16ToSrc.size() || isTrailingHit) {
            if (pos < bl.u16ToSrcEnd.size())
                return bl.u16ToSrcEnd[pos];
            if (pos + 1 < bl.u16ToSrc.size() &&
                bl.u16ToSrc[pos + 1] > srcOff)
                return bl.u16ToSrc[pos + 1];
        }
        return srcOff;
    }
    // Fallback: linear approximation (for blocks without mapping).
    return bl.textStartOffset + htm.textPosition;
}

bool LayoutCache::OffsetToCaretRect(uint32_t offset,
                                     float* outX, float* outY,
                                     float* outH) const {
    int idx = BlockForOffset(offset);
    if (idx < 0) return false;

    const auto& bl = blocks_[idx];
    if (!bl.layout) return false;

    // Convert source offset to UTF-16 position within this layout
    // using the u16ToSrc mapping (reverse lookup).
    uint32_t u16Pos = 0;
    if (!bl.u16ToSrc.empty()) {
        // Binary search for the first u16 index whose src offset >= offset.
        size_t lo = 0, hi = bl.u16ToSrc.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (bl.u16ToSrc[mid] < offset) lo = mid + 1;
            else hi = mid;
        }
        u16Pos = (lo < bl.u16ToSrc.size())
            ? static_cast<uint32_t>(lo)
            : static_cast<uint32_t>(bl.u16ToSrc.size());
    } else {
        // Fallback: linear approximation.
        u16Pos = (offset >= bl.textStartOffset)
            ? offset - bl.textStartOffset : 0;
    }

    DWRITE_HIT_TEST_METRICS htm = {};
    FLOAT caretX = 0.0f, caretY = 0.0f;
    bl.layout->HitTestTextPosition(
        u16Pos, FALSE, &caretX, &caretY, &htm);

    if (outX) *outX = bl.x + caretX;
    // Vertically center the caret on the text within the line.
    // caretY is the top of the line; the text occupies roughly the
    // em-height portion, centered by the line-spacing/baseline ratio.
    if (outY) {
        float caretH = (bl.fontHeight > 0.0f) ? bl.fontHeight : htm.height;
        float lineH = htm.height;
        *outY = bl.y + caretY + (lineH - caretH) * 0.5f;
    }
    // Use the font's em height for the caret, not the full line height
    // (which includes leading/line-spacing and makes the caret too tall).
    if (outH) *outH = (bl.fontHeight > 0.0f) ? bl.fontHeight : htm.height;
    return true;
}

// --- UTF-8 / UTF-16 conversion helpers ---

// Count UTF-16 code units needed for the first byteOffset bytes of s.
uint32_t Utf8OffsetToUtf16(const std::string& s, uint32_t byteOffset) {
    uint32_t u16 = 0;
    uint32_t i = 0;
    while (i < byteOffset && i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            i += 1;
        } else if ((b & 0xE0) == 0xC0) {
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            // Surrogate pair: 4 UTF-8 bytes -> 2 UTF-16 code units.
            i += 4;
            u16++; // account for the extra UTF-16 unit
        } else {
            i += 1;
        }
        u16++;
    }
    return u16;
}

// Convert a UTF-16 code-unit offset back to a UTF-8 byte offset in s.
uint32_t Utf16OffsetToUtf8(const std::string& s, uint32_t u16Offset) {
    uint32_t u16 = 0;
    uint32_t i = 0;
    while (u16 < u16Offset && i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            i += 1;
        } else if ((b & 0xE0) == 0xC0) {
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            i += 4;
            u16++; // surrogate pair: 2 UTF-16 units
        } else {
            i += 1;
        }
        u16++;
    }
    return i;
}
