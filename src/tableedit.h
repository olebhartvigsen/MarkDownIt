#pragma once

// The table editing commands, shared by every platform.
//
// The Windows build has the table commands in its window class and the
// mac build would have had them in its app class, and then the two
// would drift apart. The commands live here instead, so both platforms
// change a table the same way.
//
// The commands do not touch the buffer. They work out what the change
// is and hand it back, and the caller splices it in. That keeps one
// table operation one undo step, which is the rule the table
// guidelines set (section 46).

#include <cstdint>
#include <string>

#include "dom.h"

struct TableLocation;

namespace tableedit {

// Where the caret is inside a table.
//
// The rows are the physical lines of the table's source, so row 0 is
// the header and row 1 is the dash delimiter.
struct TableLocation {
    uint32_t rowStart = 0;  // the row's first character
    uint32_t rowEnd = 0;    // past the row's newline, or the end of the table
    int columnIndex = 0;    // the 0-based column the caret is in
    int rowIndex = 0;       // the 0-based row
    int numCols = 0;        // the number of columns
    int numRows = 0;        // the number of rows
};

// The change a table command makes.
//
// The caller splices `text` over `length` bytes at `start`, and puts
// the caret at `caret` afterwards.
struct TableEdit {
    bool ok = false;
    uint32_t start = 0;
    uint32_t length = 0;
    std::string text;
    uint32_t caret = 0;
};

// The table that holds the offset, or null when the offset is not in a
// table. The span is half-open, so a caret resting just past the table
// is not inside it.
const Node* ContainingTable(const Document& doc, uint32_t offset);

// Puts the caret's place in the table. False when the offset is not in
// the table's own source span.
bool Locate(const std::string& text, const Node* table, uint32_t offset,
            TableLocation* out);

// Adds a row above or below the caret's row.
//
// The header line and the dash delimiter are what make a Markdown
// table, so a row may not be added above the header or between the
// header and the delimiter.
TableEdit AddRow(const std::string& text, const TableLocation& loc, bool below);

// Removes the caret's row. The header and the delimiter are never
// removed, whatever the size of the table.
TableEdit RemoveRow(const std::string& text, const TableLocation& loc);

// The text of a new table with the given number of columns and rows.
// The first row is the header and the second is the dash delimiter.
std::string BuildGrid(int cols, int rows);

}  // namespace tableedit
