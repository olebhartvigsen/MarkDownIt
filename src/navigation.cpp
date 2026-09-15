#include "navigation.h"
#include "editcontroller.h"
#include <algorithm>

// Check if a source offset is a hidden markdown marker character
// (not part of the rendered text) using the layout cache's u16ToSrc
// mapping. If the offset is not in any block's u16ToSrc, it's hidden.
static bool isHiddenMarker(const std::string& s, uint32_t offset,
                           const LayoutCache* cache) {
    if (!cache) return false;
    // Quick check: is the char a markdown marker?
    if (offset >= s.size()) return false;
    char c = s[offset];
    if (c != '*' && c != '`' && c != '~') return false;
    // Check if this offset is in the rendered text of any block.
    // The u16ToSrc vector maps rendered UTF-16 positions to source
    // byte offsets. If offset appears in any block's u16ToSrc, it's
    // rendered (visible). Otherwise it's a hidden marker.
    const auto& blocks = cache->Blocks();
    for (const auto& bl : blocks) {
        for (uint32_t srcOff : bl.u16ToSrc) {
            if (srcOff == offset) return false;  // rendered → not hidden
            if (srcOff > offset) break;          // sorted, no point
        }
    }
    return true;  // marker char not found in any rendered text
}

// Skip forward past hidden marker characters starting at offset.
static uint32_t skipMarkersRight(const std::string& s, uint32_t offset,
                                 const LayoutCache* cache) {
    while (offset < s.size() && isHiddenMarker(s, offset, cache))
        offset = NextGraphemeBoundary(s, offset);
    return offset;
}

// Skip backward past hidden marker characters before offset.
static uint32_t skipMarkersLeft(const std::string& s, uint32_t offset,
                                const LayoutCache* cache) {
    while (offset > 0) {
        uint32_t prev = PrevGraphemeBoundary(s, offset);
        if (prev == offset) break;
        if (!isHiddenMarker(s, prev, cache)) break;
        offset = prev;
    }
    return offset;
}

uint32_t MoveLeft(const TextBuffer& buf, uint32_t offset,
                  const LayoutCache* layoutCache) {
    uint32_t prev = PrevGraphemeBoundary(buf.Text(), offset);
    // If we landed on a hidden marker, keep skipping left past markers.
    if (layoutCache)
        prev = skipMarkersLeft(buf.Text(), prev, layoutCache);
    // Skip over unrendered gap bytes (blank lines between blocks,
    // code-fence lines) so the caret always lands on visible text.
    if (layoutCache) {
        while (prev > 0 && !layoutCache->OffsetIsRendered(prev)) {
            uint32_t p = PrevGraphemeBoundary(buf.Text(), prev);
            if (p == prev) break;
            prev = skipMarkersLeft(buf.Text(), p, layoutCache);
        }
    }
    return prev;
}

uint32_t MoveRight(const TextBuffer& buf, uint32_t offset,
                   const LayoutCache* layoutCache) {
    const std::string& s = buf.Text();
    uint32_t next = NextGraphemeBoundary(s, offset);
    // If we landed on a hidden marker, keep skipping right past markers.
    if (layoutCache)
        next = skipMarkersRight(s, next, layoutCache);
    // Skip over unrendered gap bytes (blank lines between blocks,
    // code-fence lines) so the caret always lands on visible text.
    if (layoutCache) {
        while (next < s.size() && !layoutCache->OffsetIsRendered(next)) {
            uint32_t n = NextGraphemeBoundary(s, next);
            if (n == next) break;
            next = skipMarkersRight(s, n, layoutCache);
        }
    }
    return next;
}

namespace {
struct NavCp { char32_t value; uint32_t next; };
static NavCp DecodeNav(const std::string& s, uint32_t at) {
    if (at >= s.size()) return {0, at};
    unsigned char b = static_cast<unsigned char>(s[at]);
    if (b < 0x80) return {b, at + 1};
    if ((b & 0xE0) == 0xC0 && at + 1 < s.size())
        return {static_cast<char32_t>((b & 0x1F) << 6 | (static_cast<unsigned char>(s[at+1]) & 0x3F)), at + 2};
    if ((b & 0xF0) == 0xE0 && at + 2 < s.size())
        return {static_cast<char32_t>((b & 0x0F) << 12 | (static_cast<unsigned char>(s[at+1]) & 0x3F) << 6 | (static_cast<unsigned char>(s[at+2]) & 0x3F)), at + 3};
    if ((b & 0xF8) == 0xF0 && at + 3 < s.size())
        return {static_cast<char32_t>((b & 7) << 18 | (static_cast<unsigned char>(s[at+1]) & 0x3F) << 12 | (static_cast<unsigned char>(s[at+2]) & 0x3F) << 6 | (static_cast<unsigned char>(s[at+3]) & 0x3F)), at + 4};
    return {0xFFFD, at + 1};
}
static bool UnicodeSpace(char32_t cp) {
    return cp == 0x0009 || cp == 0x000A || cp == 0x000B || cp == 0x000C ||
           cp == 0x000D || cp == 0x0020 || cp == 0x0085 || cp == 0x00A0 ||
           (cp >= 0x1680 && cp <= 0x180E) || (cp >= 0x2000 && cp <= 0x200A) ||
           cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}
static bool UnicodeMark(char32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF);
}
static bool UnicodePunctuation(char32_t cp) {
    return (cp >= 0x0021 && cp <= 0x002F) ||
           (cp >= 0x003A && cp <= 0x0040) ||
           (cp >= 0x005B && cp <= 0x0060) ||
           (cp >= 0x007B && cp <= 0x007E) ||
           (cp >= 0x2000 && cp <= 0x206F) ||
           (cp >= 0x2E00 && cp <= 0x2E7F) ||
           (cp >= 0x3000 && cp <= 0x303F) ||
           (cp >= 0xFE10 && cp <= 0xFE1F) ||
           (cp >= 0xFE30 && cp <= 0xFE6F) ||
           (cp >= 0xFF01 && cp <= 0xFF65);
}

static bool UnicodeWord(char32_t cp) {
    if (UnicodePunctuation(cp)) return false;
    return UnicodeMark(cp) ||
           (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') ||
           (cp >= 'a' && cp <= 'z') || (cp == '_') ||
           (cp >= 0x00C0 && cp <= 0x02FF) || (cp >= 0x0370 && cp <= 0x052F) ||
           (cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0x0900 && cp <= 0x1FFF) ||
           (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7FF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x10000 && cp <= 0x1EFFF);
}
enum class WordKind { Space, Word, Other };
static WordKind Kind(char32_t cp) {
    if (UnicodeSpace(cp)) return WordKind::Space;
    if (UnicodeWord(cp)) return WordKind::Word;
    return WordKind::Other;
}
}

uint32_t MoveWordLeft(const TextBuffer& buf, uint32_t offset) {
    const std::string& s = buf.Text();
    if (offset == 0) return 0;
    uint32_t i = offset;
    while (i > 0) {
        uint32_t p = PrevGraphemeBoundary(s, i);
        NavCp cp = DecodeNav(s, p);
        if (Kind(cp.value) != WordKind::Space) break;
        i = p;
    }
    uint32_t p = PrevGraphemeBoundary(s, i);
    if (p == i) return i;
    WordKind wanted = Kind(DecodeNav(s, p).value);
    while (i > 0) {
        p = PrevGraphemeBoundary(s, i);
        if (Kind(DecodeNav(s, p).value) != wanted) break;
        i = p;
    }
    return i;
}

uint32_t MoveWordRight(const TextBuffer& buf, uint32_t offset) {
    const std::string& s = buf.Text();
    uint32_t n = static_cast<uint32_t>(s.size());
    uint32_t i = offset > n ? n : offset;
    while (i < n) {
        NavCp cp = DecodeNav(s, i);
        if (Kind(cp.value) != WordKind::Space) break;
        i = NextGraphemeBoundary(s, i);
    }
    if (i >= n) return n;
    WordKind wanted = Kind(DecodeNav(s, i).value);
    while (i < n && Kind(DecodeNav(s, i).value) == wanted)
        i = NextGraphemeBoundary(s, i);
    return i;
}
void WordSpanAt(const TextBuffer& buf, uint32_t offset,
                uint32_t* outStart, uint32_t* outEnd) {
    const std::string& s = buf.Text();
    uint32_t n = static_cast<uint32_t>(s.size());
    if (!outStart || !outEnd || n == 0) return;
    uint32_t at = std::min(offset, n);
    if (at == n) at = PrevGraphemeBoundary(s, n);
    else if (at > 0) {
        uint32_t prev = PrevGraphemeBoundary(s, at);
        if (prev < at && NextGraphemeBoundary(s, prev) != at)
            at = prev;
    }
    NavCp current = DecodeNav(s, at);
    WordKind wanted = Kind(current.value);
    uint32_t start = at;
    while (start > 0) {
        uint32_t prev = PrevGraphemeBoundary(s, start);
        if (Kind(DecodeNav(s, prev).value) != wanted) break;
        start = prev;
    }
    uint32_t end = NextGraphemeBoundary(s, at);
    while (end < n && Kind(DecodeNav(s, end).value) == wanted)
        end = NextGraphemeBoundary(s, end);
    *outStart = start;
    *outEnd = end;
}
uint32_t MoveVertical(const LayoutCache& lc, uint32_t offset,
                      int direction, float* desiredX,
                      float scrollY, float lineHeight) {
    float x, y, h;
    if (!lc.OffsetToCaretRect(offset, &x, &y, &h)) return offset;

    if (*desiredX < 0) *desiredX = x;

    // Blocks can be separated by large vertical gaps (blank lines,
    // heading spacing). One lineHeight step may land between blocks
    // where PointToOffset fails; progressively scan further in the
    // move direction until we hit rendered text or run out of doc.
    uint32_t newOffset = UINT32_MAX;
    float targetY = y;
    for (int step = 1; step <= 12; ++step) {
        targetY += static_cast<float>(direction) * lineHeight;
        uint32_t cand = lc.PointToOffset(*desiredX, targetY);
        if (cand == UINT32_MAX) {
            // The x may fall outside narrower blocks (list items start
            // after their marker gutter, code blocks are inset). Find
            // the block by y alone and hit-test with x clamped inside.
            int blkIdx = lc.FindBlockAtY(targetY);
            if (blkIdx < 0) continue;
            const auto& blocks = lc.Blocks();
            const auto& bl = blocks[blkIdx];
            float cx = *desiredX;
            if (cx < bl.x) cx = bl.x;
            if (cx >= bl.x + bl.width) cx = bl.x + bl.width - 1.0f;
            cand = lc.PointToOffset(cx, targetY);
            if (cand == UINT32_MAX) continue;
        }
        // Reject hits that are still on the current visual line. A
        // tall block (H1-H3 use much larger fonts than body text)
        // makes one generic lineHeight step land back inside the
        // same block; moving must actually leave the current line.
        float cx, cy, ch;
        if (!lc.OffsetToCaretRect(cand, &cx, &cy, &ch)) continue;
        bool movedPast = (direction < 0)
            ? (cy < y - h * 0.5f)
            : (cy > y + h * 0.5f);
        if (!movedPast) continue;
        newOffset = cand;
        break;
    }
    // No block found in that direction: clamp to doc start/end so the
    // caret still moves to the first/last line of the document.
    if (newOffset == UINT32_MAX) {
        const auto& blocks = lc.Blocks();
        if (blocks.empty()) return offset;
        if (direction < 0) {
            newOffset = blocks.front().srcOffset;
        } else {
            const auto& last = blocks.back();
            if (last.u16ToSrc.empty()) {
                newOffset = last.srcOffset + last.srcLength;
            } else if (lc.SourceText()) {
                newOffset = NextGraphemeBoundary(
                    *lc.SourceText(), last.u16ToSrc.back());
            } else {
                newOffset = last.srcOffset + last.srcLength;
            }
        }
    }
    return newOffset;
}

uint32_t MoveLineStart(const LayoutCache& lc, uint32_t offset) {
    float x, y, h;
    if (!lc.OffsetToCaretRect(offset, &x, &y, &h)) return offset;
    // Hit test at the leftmost edge of this block.
    int blockIdx = lc.BlockForOffset(offset);
    if (blockIdx < 0) return offset;
    const auto& bl = lc.Blocks()[blockIdx];
    uint32_t newOffset = lc.PointToOffset(bl.x, y);
    if (newOffset == UINT32_MAX) return offset;
    return newOffset;
}

uint32_t MoveLineEnd(const LayoutCache& lc, uint32_t offset) {
    float x, y, h;
    if (!lc.OffsetToCaretRect(offset, &x, &y, &h)) return offset;
    int blockIdx = lc.BlockForOffset(offset);
    if (blockIdx < 0) return offset;
    const auto& bl = lc.Blocks()[blockIdx];
    uint32_t newOffset = lc.PointToOffset(bl.x + bl.width - 1, y);
    if (newOffset == UINT32_MAX) return offset;
    return newOffset;
}


namespace {
static uint32_t TableCellSourceEnd(const TableCell& cell) {
    if (!cell.u16ToSrc.empty()) {
        const uint32_t last = cell.u16ToSrc.back();
        // u16ToSrc contains the same source offset twice for a non-BMP
        // character. Use the final mapped code point, not +1 byte.
        const char32_t cp = cell.text.empty() ? U' ' : cell.text.back();
        const uint32_t length = cp <= 0x7F ? 1 : cp <= 0x7FF ? 2 :
                                cp <= 0xFFFF ? 3 : 4;
        return last + length;
    }
    uint32_t bytes = 0;
    for (char32_t cp : cell.text) {
        bytes += cp <= 0x7F ? 1 : cp <= 0x7FF ? 2 :
                 cp <= 0xFFFF ? 3 : 4;
    }
    return cell.srcOffset + bytes;
}
}

bool IsOffsetInTable(const Document& doc, uint32_t offset) {
    for (const Node& node : doc.nodes) {
        if (node.block != BlockKind::Table) continue;
        if (offset >= node.srcOffset && offset <= node.srcOffset + node.srcLength)
            return true;
        for (const TableRow& row : node.rows) {
            for (const TableCell& cell : row.cells) {
                if (offset >= cell.srcOffset &&
                    offset <= TableCellSourceEnd(cell)) return true;
            }
        }
    }
    return false;
}

bool MoveTableCell(const Document& doc, uint32_t offset, bool backwards,
                   uint32_t* destination) {
    if (!destination) return false;
    struct CellRef { uint32_t start; uint32_t end; };
    for (const Node& node : doc.nodes) {
        if (node.block != BlockKind::Table) continue;
        std::vector<CellRef> cells;
        for (const TableRow& row : node.rows) {
            for (const TableCell& cell : row.cells) {
                cells.push_back({cell.srcOffset, TableCellSourceEnd(cell)});
            }
        }

        // Empty cells are represented by a zero-width range. At a shared
        // boundary, forward movement belongs to the earlier cell and
        // backward movement belongs to the later cell.
        size_t current = cells.size();
        if (backwards) {
            for (size_t i = 0; i < cells.size(); ++i) {
                if (offset >= cells[i].start && offset <= cells[i].end)
                    current = i;
            }
        } else {
            for (size_t i = 0; i < cells.size(); ++i) {
                if (offset >= cells[i].start && offset <= cells[i].end) {
                    current = i;
                    break;
                }
            }
        }
        if (current == cells.size()) continue;

        if (backwards) {
            if (current == 0) return false;
            *destination = cells[current - 1].end;
        } else {
            if (current + 1 >= cells.size()) return false;
            *destination = cells[current + 1].start;
        }
        return true;
    }
    return false;
}
