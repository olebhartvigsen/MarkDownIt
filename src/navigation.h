#pragma once
#include "layoutcache.h"
#include "textbuffer.h"
#include <cstdint>
#include <cstddef>
#include <string>

// Move left by one grapheme cluster, skipping hidden markdown markers.
// layoutCache gives access to u16ToSrc mapping to know which source
// offsets are rendered vs. hidden markers. May be null (raw move).
uint32_t MoveLeft(const TextBuffer& buf, uint32_t offset,
                  const LayoutCache* layoutCache = nullptr);

// Move right by one grapheme cluster, skipping hidden markdown markers.
uint32_t MoveRight(const TextBuffer& buf, uint32_t offset,
                   const LayoutCache* layoutCache = nullptr);

// Move left by one word (Ctrl+Left).
uint32_t MoveWordLeft(const TextBuffer& buf, uint32_t offset);

// Move right by one word (Ctrl+Right).
uint32_t MoveWordRight(const TextBuffer& buf, uint32_t offset);

// Return the contiguous Unicode word/space/punctuation span containing offset.
void WordSpanAt(const TextBuffer& buf, uint32_t offset,
                uint32_t* outStart, uint32_t* outEnd);
bool IsOffsetInTable(const Document& doc, uint32_t offset);

// Move to the adjacent Markdown table cell. Returns false outside a table
// or at the requested document edge.
bool MoveTableCell(const Document& doc, uint32_t offset, bool backwards,
                   uint32_t* destination);

// Move up or down by one rendered line.
// direction: -1 for up, +1 for down.
// desiredX: in/out. On first vertical move, set to caret x.
//          On subsequent moves, keep the same desiredX.
// Returns the new offset.
uint32_t MoveVertical(const LayoutCache& lc, uint32_t offset,
                      int direction, float* desiredX,
                      float scrollY, float lineHeight);

// Move to the start of the visual line containing offset.
uint32_t MoveLineStart(const LayoutCache& lc, uint32_t offset);

// Move to the end of the visual line containing offset.
uint32_t MoveLineEnd(const LayoutCache& lc, uint32_t offset);

// ─── Table cell queries ───────────────────────────────────────────────
// One addressable table cell: stable [row][col] identity plus the
// source range of its content (between the delimiting pipes).
struct TableCellRef {
    size_t tableIndex = SIZE_MAX;  // index into Document::nodes
    size_t rowIndex = 0;
    size_t columnIndex = 0;
    uint32_t srcOffset = 0;        // first content byte (after leading pipe)
    uint32_t srcEnd = 0;           // one past last content byte
    bool isHeader = false;
    bool separatorRow = false;
};

// Resolve the cell containing a source offset, or the nearest cell when
// the offset sits in hidden syntax (pipes, dashes, padding). Nearest is
// searched within the same row first, then the same table. Returns false
// only when offset belongs to no table.
bool TableCellAtOffset(const Document& doc, const std::string& source,
                       uint32_t offset, TableCellRef* out);

// Count of columns the widest row of the table declares.
size_t TableColumnCount(const Document& doc, size_t tableIndex);

// Compose the source text of a blank row with columnCount cells.
std::string TableBlankRow(int columnCount);

// Compose the source text of the table's delimiter row (dashes only,
// default alignment) for a fresh column count.
std::string TableDelimitersFor(int columnCount);

// Find a table's delimiter row source range. The delimiter row is the
// physical line of dashes-only cells (md4c emits no TR for it). Returns
// false when no delimiter line is found inside the table block.
bool TableDelimiterRange(const Document& doc, const std::string& source,
                         size_t tableIndex, uint32_t* outStart,
                         uint32_t* outEnd);

// Sanitize pasted text for insertion inside a table cell: tables are
// single-line Markdown syntax, so pipes are dropped and row breaks fold
// to single spaces (a nested table cannot be expressed).
std::string SanitizePasteForTableCell(const std::string& pasted);
