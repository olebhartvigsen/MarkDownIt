#include "navigation.h"
#include "editcontroller.h"
#include <algorithm>
#include <vector>

namespace {
// Excel quoting: a value wrapped in double quotes keeps tabs, and a
// doubled quote inside is a literal quote.
std::vector<std::string> SplitTabRow(const std::string& line) {
    std::vector<std::string> cells;
    std::string value;
    bool quoted = false, wasQuoted = false, any = false;
    auto cellDone = [&]() {
        cells.push_back(value);
        value.clear();
        quoted = wasQuoted = false;
        any = false;
    };
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { value += '"'; ++i; }
                else quoted = false;
            } else {
                value += c;
            }
        } else if (c == '"' && value.empty() && !wasQuoted && !any) {
            quoted = wasQuoted = true;
        } else if (c == '\t') {
            cellDone();
        } else {
            value += c;
            any = true;
        }
    }
    cells.push_back(value);
    return cells;
}

// Trim one space from each padded side of a cell, matching TableBlankRow's
// `[padding][content][padding]` rhythm without collapsing user spacing.
std::string TrimCell(const std::string& cell) {
    size_t start = 0, end = cell.size();
    if (start < end && cell[start] == ' ') ++start;
    if (start < end && cell[end - 1] == ' ') --end;
    return cell.substr(start, end - start);
}
} // namespace

bool TabSeparatedToMarkdownTable(const std::string& pasted,
                                  std::string* outTable,
                                  uint32_t* outFirstCellCaret) {
    if (!outTable || !outFirstCellCaret) return false;
    outTable->clear();
    *outFirstCellCaret = 0;

    // Split into logical lines, accepting CRLF and lone CR like the parser.
    std::vector<std::string> lines;
    std::string line;
    for (char c : pasted) {
        if (c == '\n' || c == '\r') {
            if (!line.empty()) { lines.push_back(line); line.clear(); }
        } else {
            line += c;
        }
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.size() < 2 || lines.size() > 4096) return false;

    // Every line must be tab separated with the same column count, so a
    // plain sentence with a stray tab is left alone. Trailing empty cells
    // are real cells (Excel copies the full rectangle), so nothing is
    // trimmed: ragged input is rejected rather than corrupted.
    std::vector<std::vector<std::string>> rows;
    size_t columns = 0;
    for (const std::string& row : lines) {
        if (row.find('\t') == std::string::npos) return false;
        std::vector<std::string> cells = SplitTabRow(row);
        if (columns == 0) {
            columns = cells.size();
            if (columns < 2 || columns > 64) return false;
        } else if (cells.size() != columns) {
            return false;
        }
        rows.push_back(std::move(cells));
    }
    if (columns < 2) return false;

    // Guard against plain indented text: uniform leading tabs before every
    // line (an indented code block) leave the whole first column empty,
    // which real spreadsheet data does not. Paste it as text instead.
    bool anyFirstCell = false;
    for (const std::vector<std::string>& row : rows)
        if (!row.front().empty()) { anyFirstCell = true; break; }
    if (!anyFirstCell) return false;

    // First data row becomes the header; Excel rows after it are body rows.
    std::string table;
    auto appendRow = [&](const std::vector<std::string>& cells) {
        for (const std::string& cell : cells) {
            std::string escaped;
            for (char c : TrimCell(cell)) {
                if (c == '|') escaped += "\\|";
                else escaped += c;
            }
            table += "| ";
            table += escaped;
            table += " ";
        }
        table += "|\n";
    };
    appendRow(rows.front());
    table += TableDelimitersFor(static_cast<int>(columns));
    for (size_t i = 1; i < rows.size(); ++i) appendRow(rows[i]);

    // Caret belongs in the first body cell, right after its opening "| ".
    size_t delimEnd = table.find('\n');
    if (delimEnd == std::string::npos) return false;
    delimEnd = table.find('\n', delimEnd + 1);
    if (delimEnd == std::string::npos) return false;
    *outTable = table;
    *outFirstCellCaret = static_cast<uint32_t>(delimEnd + 3);
    return true;
}

bool GetBlockSelectionRange(const Document* doc, const LayoutCache& cache,
                            int blockIndex, uint32_t* start, uint32_t* end) {
    if (!cache.GetRenderedBlockRange(blockIndex, start, end)) return false;
    const auto& block = cache.Blocks()[static_cast<size_t>(blockIndex)];
    if (doc && block.nodeIndex < doc->nodes.size()) {
        const Node& node = doc->nodes[block.nodeIndex];
        if (node.block == BlockKind::List) {
            // Include closing inline syntax in the selected item, but not its
            // newline or the next item's marker. Otherwise deletion nests the
            // next item under the empty prefix left on this source line.
            *start = node.contentOffset;
            const std::string* source = cache.SourceText();
            if (source && node.srcOffset < source->size()) {
                uint32_t cursor = node.srcOffset;
                const uint32_t limit = std::min<uint32_t>(
                    node.contentOffset, static_cast<uint32_t>(source->size()));
                while (cursor < limit &&
                       ((*source)[cursor] == ' ' || (*source)[cursor] == '\t')) ++cursor;
                // Skip blockquote markers first, mirroring the parser's
                // own scan, so a quoted item's '>' is never mistaken for
                // the bullet: the selection must still skip the '-'.
                while (cursor < limit && (*source)[cursor] == '>') {
                    ++cursor;
                    while (cursor < limit &&
                           ((*source)[cursor] == ' ' || (*source)[cursor] == '\t')) ++cursor;
                }
                if (node.ordered) {
                    while (cursor < limit && (*source)[cursor] >= '0' &&
                           (*source)[cursor] <= '9') ++cursor;
                }
                if (cursor < limit) ++cursor; // bullet or ordered delimiter
                while (cursor < limit &&
                       ((*source)[cursor] == ' ' || (*source)[cursor] == '\t')) ++cursor;
                *start = cursor; // before any opening inline syntax
            }
            *end = node.srcOffset + node.srcLength;
        }
    }
    return *start <= *end;
}

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
// Source byte length of a table cell's text content.
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
        // srcOffset + srcLength is the first byte AFTER the table, so the
        // span is half-open. Treating the end as inside made a caret
        // resting just past the table paste through the in-cell sanitizer
        // and run table commands against a table it was not in.
        if (offset >= node.srcOffset && offset < node.srcOffset + node.srcLength)
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

// ─── Table cell queries ───────────────────────────────────────────────

// Source byte length of a table cell's text content (already trackable
// through the u16ToSrc mapping; mirrored here for the query API).
static uint32_t TableQueryCellEnd(const TableCell& cell) {
    if (!cell.u16ToSrc.empty()) {
        const uint32_t last = cell.u16ToSrc.back();
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

size_t TableColumnCount(const Document& doc, size_t tableIndex) {
    if (tableIndex >= doc.nodes.size()) return 0;
    const Node& node = doc.nodes[tableIndex];
    if (node.block != BlockKind::Table) return 0;
    size_t cols = 0;
    for (const TableRow& row : node.rows)
        cols = row.cells.size() > cols ? row.cells.size() : cols;
    return cols;
}

std::string TableBlankRow(int columnCount) {
    std::string row = "|";
    for (int c = 0; c < columnCount; ++c) row += "        |";
    row += "\n";
    return row;
}

std::string TableDelimitersFor(int columnCount) {
    std::string row = "|";
    for (int c = 0; c < columnCount; ++c) row += "--------|";
    row += "\n";
    return row;
}

bool TableDelimiterRange(const Document& doc, const std::string& source,
                         size_t tableIndex, uint32_t* outStart,
                         uint32_t* outEnd) {
    if (tableIndex >= doc.nodes.size()) return false;
    const Node& node = doc.nodes[tableIndex];
    if (node.block != BlockKind::Table || node.rows.size() < 1) return false;
    const uint32_t tblStart = node.srcOffset;
    const uint32_t tblEnd = tblStart + node.srcLength;
    // The delimiter row is the physical line whose cells are dashes only
    // (md4c does not emit a TR for it, so rows[] has no entry). Scan the
    // table block line by line.
    uint32_t lineStart = tblStart;
    while (lineStart < tblEnd) {
        uint32_t lineEnd = lineStart;
        while (lineEnd < tblEnd && source[lineEnd] != '\n') ++lineEnd;
        // Trim indentation and trailing whitespace/CR.
        uint32_t c = lineStart;
        while (c < lineEnd && (source[c] == ' ' || source[c] == '\t')) ++c;
        uint32_t e = lineEnd;
        if (e > c && source[e - 1] == '\r') --e;
        while (e > c && (source[e - 1] == ' ' || source[e - 1] == '\t')) --e;
        bool looksDelimiter = (e > c) && (source[c] == '|' || source[c] == '-');
        bool anyDash = false;
        for (uint32_t k = c; looksDelimiter && k < e; ++k) {
            const char ch = source[k];
            if (ch == '-' || ch == ':' || ch == ' ' || ch == '|' ||
                ch == '\t') {
                if (ch == '-') anyDash = true;
                continue;
            }
            looksDelimiter = false;
        }
        if (looksDelimiter && anyDash && lineEnd < tblEnd) {
            // Full physical line including its newline terminator.
            if (outStart) *outStart = lineStart;
            if (outEnd) *outEnd = lineEnd + 1;
            return true;
        }
        lineStart = lineEnd + 1;
    }
    return false;
}

bool TableCellAtOffset(const Document& doc, const std::string& source,
                       uint32_t offset, TableCellRef* out) {
    for (size_t ti = 0; ti < doc.nodes.size(); ++ti) {
        const Node& node = doc.nodes[ti];
        if (node.block != BlockKind::Table) continue;
        const uint32_t tblStart = node.srcOffset;
        const uint32_t tblEnd = tblStart + node.srcLength;

        bool inTable = (offset >= tblStart && offset <= tblEnd);
        if (!inTable) {
            for (const TableRow& row : node.rows) {
                for (const TableCell& cell : row.cells) {
                    if (offset >= cell.srcOffset &&
                        offset <= TableQueryCellEnd(cell)) {
                        inTable = true;
                        break;
                    }
                }
                if (inTable) break;
            }
        }
        if (!inTable) continue;

        // Offsets inside the dashes-only delimiter line resolve to the
        // special separator row (md4c emits no TR for it).
        uint32_t delimStart = 0, delimEnd = 0;
        const bool hasDelimiter =
            TableDelimiterRange(doc, source, ti, &delimStart, &delimEnd);
        if (hasDelimiter && offset >= delimStart && offset <= delimEnd) {
            if (node.rows.empty() || node.rows.front().cells.empty()) {
                // Degenerate table with no resolvable cell.
                TableCellRef ref;
                ref.tableIndex = ti;
                ref.rowIndex = SIZE_MAX;
                ref.columnIndex = 0;
                ref.srcOffset = delimStart;
                ref.srcEnd = delimEnd;
                ref.separatorRow = true;
                if (out) *out = ref;
                return true;
            }
            const TableRow& row = node.rows.front();
            TableCellRef ref;
            ref.tableIndex = ti;
            ref.rowIndex = SIZE_MAX;  // sentinel: not a data row
            ref.columnIndex = 0;
            ref.srcOffset = delimStart;
            ref.srcEnd = delimEnd;
            ref.isHeader = row.cells.front().isHeader;
            ref.separatorRow = true;
            if (out) *out = ref;
            return true;
        }

        // Exact cell containment wins: this includes the zero-width
        // ranges of empty cells (srcOffset == srcEnd).
        for (size_t ri = 0; ri < node.rows.size(); ++ri) {
            const TableRow& row = node.rows[ri];
            for (size_t ci = 0; ci < row.cells.size(); ++ci) {
                const TableCell& cell = row.cells[ci];
                const uint32_t cellEnd = TableQueryCellEnd(cell);
                if (offset >= cell.srcOffset && offset <= cellEnd) {
                    TableCellRef ref;
                    ref.tableIndex = ti;
                    ref.rowIndex = ri;
                    ref.columnIndex = ci;
                    ref.srcOffset = cell.srcOffset;
                    ref.srcEnd = cellEnd;
                    ref.isHeader = cell.isHeader;
                    ref.separatorRow = hasDelimiter &&
                        offset >= delimStart && offset <= delimEnd;
                    if (out) *out = ref;
                    return true;
                }
            }
        }

        // Hidden syntax inside one row band (a pipe, the dash run of the
        // delimiter row, trailing padding): snap to the nearest cell of
        // that row, preferring the earlier cell of a tied distance.
        for (size_t ri = 0; ri < node.rows.size(); ++ri) {
            const TableRow& row = node.rows[ri];
            if (row.cells.empty()) continue;
            uint32_t rowStart = row.cells.front().srcOffset;
            uint32_t rowEnd = TableQueryCellEnd(row.cells.back());
            for (const TableCell& cell : row.cells) {
                rowStart = std::min(rowStart, cell.srcOffset);
                rowEnd = std::max(rowEnd, TableQueryCellEnd(cell));
            }
            // Click/caret may sit on the row's outer pipes or the
            // padding around them (spaces, dashes): the physical row
            // line up to its outer pipes is this row's surface, not
            // neighboring syntax.
            uint32_t bandStart = rowStart;
            uint32_t bandEnd = rowEnd;
            while (bandStart > node.srcOffset &&
                   source[bandStart - 1] != '\n' &&
                   source[bandStart - 1] != '|')
                --bandStart;
            if (bandStart > node.srcOffset &&
                source[bandStart - 1] == '|')
                --bandStart;
            while (bandEnd < node.srcOffset + node.srcLength &&
                   source[bandEnd] != '\n' &&
                   source[bandEnd] != '|')
                ++bandEnd;
            if (bandEnd < node.srcOffset + node.srcLength &&
                source[bandEnd] == '|')
                ++bandEnd;
            if (offset >= bandStart && offset <= bandEnd) {
                size_t bestIdx = 0;
                uint32_t bestDist = UINT32_MAX;
                for (size_t ci = 0; ci < row.cells.size(); ++ci) {
                    const TableCell& cell = row.cells[ci];
                    const uint32_t cellEnd = TableQueryCellEnd(cell);
                    // Distance from offset to the cell's [start, end) span.
                    const uint32_t dist =
                        offset < cell.srcOffset ? cell.srcOffset - offset :
                        offset > cellEnd ? offset - cellEnd : 0;
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestIdx = ci;
                    }
                }
                const TableCell& cell = row.cells[bestIdx];
                TableCellRef ref;
                ref.tableIndex = ti;
                ref.rowIndex = ri;
                ref.columnIndex = bestIdx;
                ref.srcOffset = cell.srcOffset;
                ref.srcEnd = TableQueryCellEnd(cell);
                ref.isHeader = cell.isHeader;
                ref.separatorRow = hasDelimiter &&
                    offset >= delimStart && offset <= delimEnd;
                if (out) *out = ref;
                return true;
            }
        }

        // In the table's leading/trailing syntax (between tables rows or
        // outside the cell grid but inside the block): fall back to the
        // first cell of the nearest row by offset.
        size_t nearestRow = node.rows.size();
        for (size_t ri = 0; ri < node.rows.size(); ++ri) {
            const TableRow& row = node.rows[ri];
            if (row.cells.empty()) continue;
            nearestRow = ri;
            uint32_t rowEnd = TableQueryCellEnd(row.cells.back());
            for (const TableCell& cell : row.cells)
                rowEnd = std::max(rowEnd, TableQueryCellEnd(cell));
            if (offset <= rowEnd) break;
        }
        if (nearestRow == node.rows.size()) continue;
        const TableRow& row = node.rows[nearestRow];
        if (row.cells.empty()) continue;
        TableCellRef ref;
        ref.tableIndex = ti;
        ref.rowIndex = nearestRow;
        ref.columnIndex = 0;
        ref.srcOffset = row.cells.front().srcOffset;
        ref.srcEnd = TableQueryCellEnd(row.cells.front());
        ref.isHeader = row.cells.front().isHeader;
        ref.separatorRow = hasDelimiter &&
            offset >= delimStart && offset <= delimEnd;
        if (out) *out = ref;
        return true;
    }
    return false;
}

std::string SanitizePasteForTableCell(const std::string& pasted) {
    // A line of dashes (with optional pipes/colons) is a Markdown table
    // delimiter row; inside a cell it is meaningless syntax. Drop it.
    auto isDelimiterLine = [](const std::string& line) {
        bool anyDash = false;
        for (char c : line) {
            if (c == '-') { anyDash = true; continue; }
            if (c == '|' || c == ':' || c == ' ' || c == '\r' ||
                c == '\t') continue;
            return false;
        }
        return anyDash;
    };

    std::string out;
    bool pendingSpace = false;
    std::string pendingLine;
    auto flushLine = [&](const std::string& line) {
        if (isDelimiterLine(line)) return;
        for (char c : line) {
            if (c == '|' || c == '\t' || c == '\r') {
                // A pipe separated outer cells; keep the words apart.
                pendingSpace = true;
                continue;
            }
            if (c == ' ') {
                // While a fold is pending, extra spaces collapse into it;
                // otherwise spacing is the user's own text.
                if (!pendingSpace) out += ' ';
                continue;
            }
            if (pendingSpace) {
                if (!out.empty() && out.back() != ' ') out += ' ';
                pendingSpace = false;
            }
            out += c;
        }
    };
    for (char c : pasted) {
        if (c == '\n') {
            flushLine(pendingLine);
            pendingLine.clear();
            pendingSpace = true;
        } else {
            pendingLine += c;
        }
    }
    flushLine(pendingLine);
    return out;
}

// ---------------------------------------------------------------------------
// Column insertion and removal over a table's source span.
//
// A Markdown table row is "| cell | cell |" and md4c delimits columns with
// pipes, so a column is added or removed by rewriting each physical line of
// the table's span. Both helpers build the whole replacement text and hand it
// back with a caret offset, which lets the caller apply one buffer edit and
// record one undo entry (spec section 46). Rewriting line by line with
// separate edits pushed an undo entry per row, so a single Ctrl+Z undid one
// line and left the table ragged.
// ---------------------------------------------------------------------------

namespace {

// Source offsets of every physical line start inside [tableStart, tableEnd).
std::vector<uint32_t> TableLineStarts(const std::string& s, uint32_t tableStart,
                                      uint32_t tableEnd) {
    std::vector<uint32_t> starts;
    if (tableStart >= s.size() || tableStart > tableEnd) return starts;
    starts.push_back(tableStart);
    for (uint32_t i = tableStart; i < tableEnd && i < s.size(); ++i)
        if (s[i] == '\n' && i + 1 < tableEnd) starts.push_back(i + 1);
    return starts;
}

// One past the last byte of the line, excluding its newline.
uint32_t TableLineEnd(const std::string& s, uint32_t lineStart,
                      uint32_t tableEnd) {
    uint32_t j = lineStart;
    while (j <= tableEnd && j < s.size()) {
        if (j == tableEnd || s[j] == '\n') break;
        ++j;
    }
    return j;
}

// A line's source span including its newline terminator, so copying it
// through a rewrite preserves the line breaks between rows.
uint32_t TableLineStop(const std::string& s, uint32_t lineEnd,
                       uint32_t tableEnd) {
    return (lineEnd < tableEnd) ? lineEnd + 1 : lineEnd;
}

// True when the line carries no cell content, only the dash run and pipes
// (the delimiter row), which needs a dash cell rather than a blank one.
bool TableLineIsSeparator(const std::string& s, uint32_t lineStart,
                          uint32_t lineEnd) {
    for (uint32_t j = lineStart; j < lineEnd; ++j) {
        const char c = s[j];
        if (c != '|' && c != '-' && c != ' ' && c != ':' && c != '\r')
            return false;
    }
    return true;
}

}  // namespace

bool TableInsertColumn(const std::string& source, uint32_t tableStart,
                       uint32_t tableEnd, int caretColumn, bool right,
                       uint32_t caretOffset, std::string* outText,
                       uint32_t* outCaret) {
    if (!outText || !outCaret) return false;
    if (tableStart > tableEnd || tableEnd > source.size()) return false;

    // Right inserts after the pipe closing the caret's cell, left after the
    // pipe that opens it. The new cell carries its own trailing pipe, so the
    // row grammar "|" + "        |" gains exactly one delimiter.
    const int afterPipe = caretColumn + (right ? 2 : 1);

    const std::vector<uint32_t> starts =
        TableLineStarts(source, tableStart, tableEnd);
    std::string rebuilt;
    rebuilt.reserve(source.size() + starts.size() * 16);
    uint32_t insertedBeforeCaret = 0;
    bool changed = false;

    for (const uint32_t lineS : starts) {
        const uint32_t lineE = TableLineEnd(source, lineS, tableEnd);
        const uint32_t lineStop = TableLineStop(source, lineE, tableEnd);

        int pipeSeen = 0;
        uint32_t insertAfter = 0;
        bool found = false;
        for (uint32_t j = lineS; j < lineE; ++j) {
            if (source[j] == '|') {
                ++pipeSeen;
                if (pipeSeen == afterPipe) {
                    insertAfter = j + 1;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            // Ragged line without that pipe: copy it through unchanged so a
            // short row keeps its own shape instead of gaining a cell.
            rebuilt.append(source, lineS, lineStop - lineS);
            continue;
        }
        changed = true;

        const std::string cell = TableLineIsSeparator(source, lineS, lineE)
            ? "--------|" : "        |";
        rebuilt.append(source, lineS, insertAfter - lineS);
        rebuilt += cell;
        rebuilt.append(source, insertAfter, lineStop - insertAfter);
        if (insertAfter <= caretOffset) {
            insertedBeforeCaret += static_cast<uint32_t>(cell.size());
        }
    }
    if (!changed) return false;

    // Return only the rewritten table fragment. The caller splices it over
    // the table's own span; returning the whole document here would paste
    // the surrounding text into the span and duplicate it.
    *outText = rebuilt;
    *outCaret = caretOffset + insertedBeforeCaret;
    return true;
}

bool TableRemoveColumn(const std::string& source, uint32_t tableStart,
                       uint32_t tableEnd, int caretColumn,
                       uint32_t caretOffset, std::string* outText,
                       uint32_t* outCaret) {
    if (!outText || !outCaret) return false;
    if (tableStart > tableEnd || tableEnd > source.size()) return false;

    const std::vector<uint32_t> starts =
        TableLineStarts(source, tableStart, tableEnd);
    std::string rebuilt;
    rebuilt.reserve(source.size());
    uint32_t removedBeforeCaret = 0;
    bool changed = false;

    for (const uint32_t lineS : starts) {
        const uint32_t lineE = TableLineEnd(source, lineS, tableEnd);
        const uint32_t lineStop = TableLineStop(source, lineE, tableEnd);

        int pipeSeen = 0;
        uint32_t openPipe = 0, closePipe = 0;
        bool found = false;
        for (uint32_t j = lineS; j < lineE; ++j) {
            if (source[j] == '|') {
                ++pipeSeen;
                if (pipeSeen == caretColumn + 1) openPipe = j;
                if (pipeSeen == caretColumn + 2) {
                    closePipe = j;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            rebuilt.append(source, lineS, lineStop - lineS);
            continue;
        }
        changed = true;

        // Keep the pipe that opens the column and drop the one that closes
        // it. Removing both would delete a second column's worth of
        // delimiter and merge the neighbouring cells.
        const uint32_t cutStart = openPipe + 1;
        const uint32_t cutEnd = closePipe + 1;
        rebuilt.append(source, lineS, cutStart - lineS);
        rebuilt.append(source, cutEnd, lineStop - cutEnd);

        // Bytes removed ahead of the caret shift it left, so the caret stays
        // in the same cell instead of jumping a column to the right.
        if (cutStart < caretOffset) {
            const uint32_t removed = cutEnd - cutStart;
            removedBeforeCaret += (caretOffset <= cutEnd)
                ? (caretOffset - cutStart)
                : removed;
        }
    }
    if (!changed) return false;

    // Return only the rewritten table fragment; see TableInsertColumn.
    *outCaret = caretOffset > removedBeforeCaret
        ? caretOffset - removedBeforeCaret
        : tableStart;
    if (*outCaret < tableStart) *outCaret = tableStart;
    *outText = rebuilt;
    return true;
}

bool TableStartsAt(const Document& doc, const std::string& source,
                   uint32_t offset) {
    for (const Node& node : doc.nodes) {
        if (node.block != BlockKind::Table) continue;
        if (node.srcOffset != offset) continue;
        // Require real separation in front of the table: a table that
        // begins the document has nothing to protect.
        if (offset == 0) return false;
        // The blank line run before the table is what makes it a table
        // rather than table text. When the previous non-newline byte is a
        // pipe the table is directly under a row it extends, so no guard.
        return offset > 0 && source[offset - 1] == '\n';
    }
    return false;
}

// Split the caret's cell into `pieces` columns (table guidelines section 22).
// Only the caret's own ROW carries text; every other row of the table gains
// empty companions of the same width, so the table keeps a uniform column
// count (section 60's invariant).
//
// Splitting one cell while the rest of the table kept its shape would leave
// the row with one more cell than the others, and md4c pads short rows on the
// way back in. Mirroring the width is what makes the result reparse to exactly
// the intended geometry.
//
// Markdown cannot express a merged cell, so this has no inverse here. One
// direction is all the format supports, and this is it.
bool TableSplitCell(const std::string& source, uint32_t tableStart,
                    uint32_t tableEnd, int caretColumn, int caretRow,
                    int pieces, uint32_t caretOffset, std::string* outText,
                    uint32_t* outCaret) {
    if (!outText || !outCaret) return false;
    if (tableStart > tableEnd || tableEnd > source.size()) return false;
    // A cell cannot be split into fewer than two pieces.
    if (pieces < 2) return false;
    // Rows 0 and 1 are the header and the dash delimiter. They must keep the
    // same column count as each other and as the body, and the delimiter must
    // stay immediately after the header, so neither is a split candidate.
    if (caretRow < 2) return false;

    const std::vector<uint32_t> starts =
        TableLineStarts(source, tableStart, tableEnd);
    if (caretRow >= static_cast<int>(starts.size())) return false;

    std::string rebuilt;
    rebuilt.reserve(source.size() + 64);
    uint32_t insertedBeforeCaret = 0;
    bool changed = false;

    for (size_t rowIdx = 0; rowIdx < starts.size(); ++rowIdx) {
        const uint32_t lineS = starts[rowIdx];
        const uint32_t lineE = TableLineEnd(source, lineS, tableEnd);
        const uint32_t lineStop = TableLineStop(source, lineE, tableEnd);
        const bool isCaretRow = static_cast<int>(rowIdx) == caretRow;

        // Opening and closing pipe of the caret column. A hand-edited ragged
        // row may lack them; that row is copied through rather than guessed at.
        int pipeSeen = 0;
        uint32_t openPipe = 0, closePipe = 0;
        bool found = false;
        for (uint32_t j = lineS; j < lineE; ++j) {
            if (source[j] != '|') continue;
            ++pipeSeen;
            if (pipeSeen == caretColumn + 1) openPipe = j;
            if (pipeSeen == caretColumn + 2) { closePipe = j; found = true; break; }
        }
        if (!found) {
            rebuilt.append(source, lineS, lineStop - lineS);
            continue;
        }
        changed = true;

        // The cell's content, padding trimmed. Only the caret row has any.
        uint32_t coreStart = openPipe + 1;
        uint32_t coreEnd = closePipe;
        while (coreStart < coreEnd &&
               (source[coreStart] == ' ' || source[coreStart] == '\t'))
            ++coreStart;
        while (coreEnd > coreStart &&
               (source[coreEnd - 1] == ' ' || source[coreEnd - 1] == '\t'))
            --coreEnd;
        const uint32_t coreLen = coreEnd - coreStart;

        // Width the cell occupied, padding included, so the row keeps its
        // rhythm. The empty companions are drawn at this width. On the
        // delimiter row the same width is spent on dashes instead of spaces,
        // which keeps every delimiter cell the same length.
        uint32_t width = closePipe - (openPipe + 1);
        if (width < 3) width = 3;  // " ---" is the smallest readable cell

        // Build the replacement for this row's caret column: `pieces` cells
        // laid end to end, each exactly `width` wide, with a `|` between each
        // pair. Every cell is written as [padding][content][padding], which is
        // why an empty cell must still emit its spaces: `||` would read as two
        // adjacent empty cells and change the column count on reparse.
        const size_t n = static_cast<size_t>(pieces);
        const bool isDelimiter = TableLineIsSeparator(source, lineS, lineE);

        // One cell, exactly `width` wide, carrying `content` with a space of
        // padding on each side. The padding is what makes the source read as
        // markdown (`| content |`) rather than as a bare `|content|`, and it is
        // what the rest of this file's helpers assume a cell looks like.
        auto makeCell = [&](const std::string& content) {
            std::string c(static_cast<size_t>(width), ' ');
            const size_t room = width >= 2 ? static_cast<size_t>(width) - 2 : 0;
            const size_t take = content.size() < room ? content.size() : room;
            if (take > 0) c.replace(1, take, content, 0, take);
            return c;
        };

        // Divide the text for the caret row: whole characters left to right,
        // and any remainder into the last piece, so no piece ever ends mid
        // UTF-8 sequence.
        std::vector<std::string> content;
        content.reserve(n);
        if (isCaretRow && coreLen > 0) {
            const uint32_t per = coreLen / static_cast<uint32_t>(n);
            uint32_t used = 0;
            for (size_t p = 0; p + 1 < n; ++p) {
                content.push_back(std::string(source, coreStart + used, per));
                used += per;
            }
            content.push_back(std::string(source, coreStart + used, coreLen - used));
        } else if (isDelimiter) {
            for (size_t p = 0; p < n; ++p) content.push_back(std::string());
        } else if (!isCaretRow) {
            // Not the caret row and not the delimiter: the original cell text
            // stays in the first piece, the rest are empty. coreStart/coreEnd
            // are the padding-trimmed bounds, so pass the content and let
            // makeCell re-pad it. Passing the raw body instead would double the
            // padding, which truncates the text: " eee " in a 5 wide cell has
            // room for three characters, so " eee " loses its last letter.
            content.push_back(std::string(source, coreStart, coreLen));
            for (size_t p = 1; p < n; ++p) content.push_back(std::string());
        } else {
            for (size_t p = 0; p < n; ++p) content.push_back(std::string());
        }

        std::string replacement;
        for (size_t p = 0; p < n; ++p) {
            if (p > 0) replacement += '|';
            if (isDelimiter) replacement.append(static_cast<size_t>(width), '-');
            else replacement += makeCell(content[p]);
        }
        const std::string companions = replacement;

        rebuilt.append(source, lineS, openPipe + 1 - lineS);
        rebuilt += companions;
        rebuilt.append(source, closePipe, lineStop - closePipe);

        if (lineS <= caretOffset && caretOffset <= lineStop) {
            insertedBeforeCaret += static_cast<uint32_t>(companions.size());
        }
    }
    if (!changed) return false;

    // Return only the rewritten table fragment; see TableInsertColumn.
    *outText = rebuilt;
    *outCaret = caretOffset + insertedBeforeCaret;
    return true;
}

TableCapabilities TableCapabilitiesFor(int rowIndex, int numCols) {
    TableCapabilities caps;
    // Row 0 is the header and row 1 the dash delimiter; together they are
    // what makes the block a table. md4c requires the delimiter to sit
    // IMMEDIATELY after the header, so:
    //  - inserting above the header or between header and delimiter breaks it
    //  - inserting below the header would land between header and delimiter
    //  - inserting below the delimiter is fine, and so is any body row
    const bool headerRow = rowIndex == 0;
    const bool delimiterRow = rowIndex == 1;
    caps.addRowAbove = !headerRow && !delimiterRow;
    caps.addRowBelow = !headerRow;
    caps.removeRow = !headerRow && !delimiterRow;
    caps.addColumnLeft = true;
    caps.addColumnRight = true;
    // Removing the last column would leave an invalid zero-column table.
    caps.removeColumn = numCols > 1;
    // A split adds a column, so every row must gain a cell. The header and
    // delimiter carry the table's shape and stay as they are.
    caps.splitCell = !headerRow && !delimiterRow;
    return caps;
}

// Set or clear the alignment marker on one column of a table's delimiter
// row. Only that one cell of that one row changes: the dash run keeps its
// length and every other line is copied byte for byte, so the user's table
// formatting survives a change of alignment. Returns false when the target
// column has no cell in the delimiter row.
bool TableSetColumnAlign(const std::string& source, uint32_t tableStart,
                         uint32_t tableEnd, int column,
                         TableAlignMark mark, uint32_t caretOffset,
                         std::string* outText, uint32_t* outCaret) {
    if (!outText || !outCaret) return false;
    if (tableStart > tableEnd || tableEnd > source.size()) return false;
    if (column < 0) return false;

    const std::vector<uint32_t> starts =
        TableLineStarts(source, tableStart, tableEnd);
    if (starts.empty()) return false;

    // The delimiter row is the one whose cells are all dashes. md4c accepts
    // a table only when header and delimiter are adjacent, so exactly one
    // such line exists and it is line index 1.
    const uint32_t lineS = starts[1];
    const uint32_t lineE = TableLineEnd(source, lineS, tableEnd);
    const uint32_t lineStop = TableLineStop(source, lineE, tableEnd);
    if (!TableLineIsSeparator(source, lineS, lineE)) return false;

    // Locate cell `column` in the delimiter row: it spans from just after
    // the (column+1)-th pipe to just before the (column+2)-th pipe.
    uint32_t cellStart = 0, cellEnd = 0;
    int pipeSeen = 0;
    bool haveStart = false, haveEnd = false;
    for (uint32_t j = lineS; j < lineE; ++j) {
        if (source[j] != '|') continue;
        ++pipeSeen;
        if (pipeSeen == column + 1) { cellStart = j + 1; haveStart = true; }
        else if (pipeSeen == column + 2) { cellEnd = j; haveEnd = true; }
        if (haveStart && haveEnd) break;
    }
    if (!haveStart || !haveEnd || cellEnd < cellStart) return false;

    // Only emit a colon where the marker calls for one. No substitute space:
    // a compact cell like `---` must stay compact, and `:---` needs no pad.
    const char lead  = (mark == TableAlignMark::Left ||
                        mark == TableAlignMark::Center) ? ':' : '\0';
    const char trail = (mark == TableAlignMark::Right ||
                        mark == TableAlignMark::Center) ? ':' : '\0';

    // Replace the whole cell content between its pipes: `[coreStart,
    // coreEnd)`. Order matters. Strip the marker colons first from the
    // outer edges, then the padding spaces, then any remaining colons.
    // Doing it in this order keeps a cell like `:---:` down to its dash run
    // and leaves a compact `---` compact.
    uint32_t coreStart = cellStart;
    uint32_t coreEnd   = cellEnd;
    while (coreStart < coreEnd && source[coreStart] == ':') ++coreStart;
    while (coreEnd > coreStart && source[coreEnd - 1] == ':') --coreEnd;
    while (coreStart < coreEnd &&
           (source[coreStart] == ' ' || source[coreStart] == '\t'))
        ++coreStart;
    while (coreEnd > coreStart &&
           (source[coreEnd - 1] == ' ' || source[coreEnd - 1] == '\t'))
        --coreEnd;
    while (coreStart < coreEnd && source[coreStart] == ':') ++coreStart;
    while (coreEnd > coreStart && source[coreEnd - 1] == ':') --coreEnd;

    // Keep the run of dashes, but never fewer than three: `:-` is not a cell
    // md4c reads back as a delimiter.
    uint32_t dashCount = 0;
    for (uint32_t j = coreStart; j < coreEnd; ++j)
        if (source[j] == '-') ++dashCount;
    if (dashCount < 3) dashCount = 3;

    std::string rebuilt;
    rebuilt.reserve(source.size() + 4);
    rebuilt.append(source, tableStart, cellStart - tableStart);
    if (lead) rebuilt.push_back(lead);
    rebuilt.append(dashCount, '-');
    if (trail) rebuilt.push_back(trail);
    rebuilt.append(source, cellEnd, tableEnd - cellEnd);

    *outText = rebuilt;
    *outCaret = caretOffset;
    return true;
}

// Move the caret one table row up (direction -1) or down (+1), staying in the
// same column. Returns false when the caret is not in a table, or when the
// move would leave it: Up from the first row and Down from the last row stay
// put rather than escaping the table, because escaping a grid with an arrow
// key is almost never what the user meant. The byte offset within the cell is
// preserved where possible, and clamped to the target cell's text.
bool TableVerticalMove(const std::string& source, uint32_t tableStart,
                       uint32_t tableEnd, uint32_t caretOffset, int direction,
                       uint32_t* outOffset) {
    if (!outOffset) return false;
    if (tableStart > tableEnd || tableEnd > source.size()) return false;
    if (direction != -1 && direction != 1) return false;

    // Split the block into its physical lines and locate the caret's line.
    std::vector<uint32_t> lineStart, lineEnd;
    for (uint32_t s = tableStart; s < tableEnd;) {
        uint32_t e = s;
        while (e < tableEnd && source[e] != '\n') ++e;
        lineStart.push_back(s);
        lineEnd.push_back(e);
        s = (e < tableEnd) ? e + 1 : tableEnd;
    }
    if (lineStart.size() < 3) return false;  // header + delimiter + body

    int row = -1;
    for (size_t i = 0; i < lineStart.size(); ++i) {
        if (caretOffset >= lineStart[i] && caretOffset <= lineEnd[i]) {
            row = static_cast<int>(i);
            break;
        }
    }
    if (row < 0) return false;

    // Row 1 is the dash delimiter. It carries no text and has no caret, so a
    // caret that lands on it (the caret is inclusive of both line ends) is
    // treated as belonging to the row above it, the header.
    if (row == 1) row = 0;

    // The header row is a legitimate caret row, so row 0 is a valid target.
    // Row 1 is the dash delimiter and has no caret of its own, so it is
    // skipped by stepping over it rather than by forbidding the move.
    int target = row + direction;
    if (target == 1) target = direction > 0 ? 2 : 0;
    if (target < 0 || target >= static_cast<int>(lineStart.size())) return false;
    if (target == 1) return false;

    // Count pipes on the current line to learn the column, then keep the same
    // byte offset inside that column of the target line.
    const uint32_t curS = lineStart[static_cast<size_t>(row)];
    const uint32_t curE = lineEnd[static_cast<size_t>(row)];
    // The caret sits inside cell N, where N is the number of pipes that open
    // it. A caret sitting exactly on a pipe belongs to the cell it closes.
    int pipesBefore = 0;
    for (uint32_t j = curS; j < caretOffset && j < curE; ++j)
        if (source[j] == '|') ++pipesBefore;
    // Pipes before the caret open its cell, so the cell is opened by pipe
    // number `pipesBefore` (1-based). A caret before the row's first pipe has
    // no cell.
    const int column = pipesBefore;
    if (column < 1) return false;
    // Byte offset of the caret inside its cell text.
    uint32_t cellTextStart = 0;
    {
        int ps = 0;
        bool found = false;
        for (uint32_t j = curS; j < curE; ++j) {
            if (source[j] != '|') continue;
            ++ps;
            if (ps == column) { cellTextStart = j + 1; found = true; break; }
        }
        if (!found) return false;  // caret is not inside a real cell
    }
    // Skip the cell's leading padding so the offset is measured from the first
    // real character, matching the target cell, which is trimmed below.
    while (cellTextStart < curE &&
           (source[cellTextStart] == ' ' || source[cellTextStart] == '\t'))
        ++cellTextStart;
    if (cellTextStart > caretOffset) cellTextStart = caretOffset;
    const uint32_t offsetInCell = caretOffset - cellTextStart;

    const uint32_t tgtS = lineStart[static_cast<size_t>(target)];
    const uint32_t tgtE = lineEnd[static_cast<size_t>(target)];
    uint32_t tgtCellStart = tgtS, tgtCellEnd = tgtE;
    {
        int ps = 0;
        bool gotStart = false;
        for (uint32_t j = tgtS; j < tgtE; ++j) {
            if (source[j] != '|') continue;
            ++ps;
            if (ps == column && !gotStart) { tgtCellStart = j + 1; gotStart = true; }
            else if (ps == column + 1) { tgtCellEnd = j; break; }
        }
        if (!gotStart) return false;  // target row is short: stay put
    }

    // Trim the target cell's padding so the offset lands on real text.
    while (tgtCellStart < tgtCellEnd &&
           (source[tgtCellStart] == ' ' || source[tgtCellStart] == '\t'))
        ++tgtCellStart;
    while (tgtCellEnd > tgtCellStart &&
           (source[tgtCellEnd - 1] == ' ' || source[tgtCellEnd - 1] == '\t'))
        --tgtCellEnd;
    const uint32_t cellLen = tgtCellEnd - tgtCellStart;
    const uint32_t clamped = offsetInCell < cellLen ? offsetInCell : cellLen;

    *outOffset = tgtCellStart + clamped;
    return true;
}
