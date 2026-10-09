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

// Paragraph selection follows document semantics: a list item's marker and
// terminating line break are structural, so selecting its text retains both.
bool GetBlockSelectionRange(const Document* doc, const LayoutCache& cache,
                            int blockIndex, uint32_t* start, uint32_t* end);

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

// Insert or remove a column across every line of a table's source span.
//
// Both rewrite the whole span in one pass and report the caret to restore,
// so the caller can apply the result as a single buffer edit (one undo
// step, spec section 46). The returned text is the table's REWRITTEN SPAN
// ONLY, not the whole document: the caller splices it over [tableStart,
// tableEnd). Returning the full document here would paste the surrounding
// text into the span and duplicate it. `caretColumn` selects the insertion
// point:
//   AddColumn:   right inserts after the pipe closing the caret's cell,
//                left after the pipe that opens it.
//   RemoveColumn: the opening pipe is kept and only the closing pipe is
//                dropped, so the table loses exactly one column.
// Returns false (leaving *outText/*outCaret untouched) when no line carries
// the required pipe, so a ragged table is never half-rewritten.
bool TableInsertColumn(const std::string& source, uint32_t tableStart,
                       uint32_t tableEnd, int caretColumn, bool right,
                       uint32_t caretOffset, std::string* outText,
                       uint32_t* outCaret);

bool TableRemoveColumn(const std::string& source, uint32_t tableStart,
                       uint32_t tableEnd, int caretColumn,
                       uint32_t caretOffset, std::string* outText,
                       uint32_t* outCaret);

// True when a table block begins exactly at `offset`, so the blank-line
// separation in front of it must not be deleted (spec section 45). Used by
// Backspace to keep a table from being merged into the paragraph above.
bool TableStartsAt(const Document& doc, const std::string& source,
                   uint32_t offset);

// Which table commands can act on the caret's current row.
//
// The header line and the dash delimiter are what MAKE a Markdown table:
// inserting a row above the header, between header and delimiter, or
// removing either line turns the block into ordinary text. The ribbon asks
// this so it greys out a command instead of showing a live button that
// silently does nothing, and the commands themselves call it so the button
// and the behaviour can never disagree.
struct TableCapabilities {
    bool addRowAbove = false;   // header and delimiter rows are structural
    bool addRowBelow = false;   // never blocked: a row below is always valid
    bool removeRow = false;     // false on header and delimiter
    bool addColumnLeft = false;
    bool addColumnRight = false;
    bool removeColumn = false;  // false when only one column remains
    bool splitCell = false;     // false on header and delimiter rows
};

// `rowIndex` is the caret's physical line within the table (0 = header,
// 1 = delimiter), `numCols` the table's column count.
TableCapabilities TableCapabilitiesFor(int rowIndex, int numCols);

// Alignment marker to write into a table's delimiter row. None clears it.
// Split the caret's cell into `pieces` columns (table guidelines section 22).
// Returns false for the header and delimiter rows, for pieces < 2, and for a
// row without the caret column's delimiters. On success *outText receives only
// the rewritten table span and *outCaret the shifted caret; the caller splices
// that span exactly once so the whole split is one undo step.
bool TableSplitCell(const std::string& source, uint32_t tableStart,
                    uint32_t tableEnd, int caretColumn, int caretRow,
                    int pieces, uint32_t caretOffset, std::string* outText,
                    uint32_t* outCaret);

enum class TableAlignMark { None, Left, Center, Right };

// Rewrite only cell `column` of the delimiter row of the table spanning
// [tableStart, tableEnd). `*outText` receives the rewritten table span only,
// not the whole document. Returns false when the column has no delimiter
// cell. Single-call friendly: the caller splices once, so one table
// operation stays one undo step.
bool TableSetColumnAlign(const std::string& source, uint32_t tableStart,
                         uint32_t tableEnd, int column,
                         TableAlignMark mark, uint32_t caretOffset,
                         std::string* outText, uint32_t* outCaret);

// Move the caret one table row up or down, staying in the same column and
// preserving the byte offset within the cell where the target cell allows it.
// Returns false when the caret is not in a table or the move would leave it,
// so the caller can fall back to visual line motion.
bool TableVerticalMove(const std::string& source, uint32_t tableStart,
                       uint32_t tableEnd, uint32_t caretOffset,
                       int direction, uint32_t* outOffset);

// Convert tab-separated clipboard rows (Excel, sheets) into a Markdown
// table in one Paste. The caller already guarantees that the caret is not
// inside a table cell. Cells honor Excel quoting: `"a\tb"` is one value.
// Returns the table text and the caret offset for its first body cell.
// Returns false (empty output) when the text is not tab-separated rows.
bool TabSeparatedToMarkdownTable(const std::string& pasted,
                                  std::string* outTable,
                                  uint32_t* outFirstCellCaret);

// Newline bytes that must precede a block table inserted at insertPos so
// the table never becomes a lazy continuation of the previous block (a
// list item or paragraph absorbs table rows as plain text). 0 at the
// document start; otherwise enough to reach a blank separator line.
uint32_t TableSeparatorNewlinesBefore(const std::string& text,
                                      uint32_t insertPos);
