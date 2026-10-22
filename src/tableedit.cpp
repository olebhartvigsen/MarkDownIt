// The table editing commands, shared by every platform.
//
// The Windows build had these in its window class and the mac build
// would have had them in its app class, and then the two would drift
// apart. They live here instead, so both platforms change a table the
// same way.

#include "tableedit.h"

#include "navigation.h"

namespace tableedit {

const Node* ContainingTable(const Document& doc, uint32_t offset) {
    for (const Node& n : doc.nodes) {
        if (n.block != BlockKind::Table) continue;
        // srcOffset + srcLength is the first byte after the table, so
        // the span is half-open.
        if (offset >= n.srcOffset && offset < n.srcOffset + n.srcLength)
            return &n;
    }
    return nullptr;
}

bool Locate(const std::string& text, const Node* table, uint32_t offset,
            TableLocation* out) {
    if (!table || !out) return false;
    const uint32_t tblStart = table->srcOffset;
    const uint32_t tblEnd = tblStart + table->srcLength;
    if (offset < tblStart || offset > tblEnd) return false;

    // The widest row decides the column count, because a table may
    // have rows with a different number of cells.
    int numCols = 0;
    for (const auto& r : table->rows) {
        if (static_cast<int>(r.cells.size()) > numCols)
            numCols = static_cast<int>(r.cells.size());
    }
    if (numCols <= 0) return false;

    // Walk the lines of the table's own source.
    int rowIdx = 0;
    uint32_t lineStart = tblStart;
    for (uint32_t i = tblStart; i <= tblEnd; ++i) {
        if (i == tblEnd || text[i] == '\n') {
            const uint32_t lineEnd = i;  // not including the newline
            if (offset >= lineStart && offset <= (i == tblEnd ? i : i + 1)) {
                // The column is the number of pipes before the caret.
                int col = 0;
                for (uint32_t j = lineStart; j < offset && j < lineEnd; ++j) {
                    if (text[j] == '|') ++col;
                }
                // The first pipe opens the first cell, so it does not
                // count as a separator.
                col = col > 0 ? col - 1 : 0;
                if (col >= numCols) col = numCols - 1;

                out->rowStart = lineStart;
                out->rowEnd = (i == tblEnd) ? i : i + 1;
                out->columnIndex = col;
                out->rowIndex = rowIdx;
                out->numCols = numCols;
                out->numRows = static_cast<int>(table->rows.size());
                return true;
            }
            if (i < tblEnd) lineStart = i + 1;
            ++rowIdx;
        }
    }
    return false;
}

TableEdit AddRow(const std::string& text, const TableLocation& loc, bool below) {
    TableEdit edit;

    // The header line and the dash delimiter are what make this a
    // table. Inserting above the header, or between the header and the
    // delimiter, would leave a block md4c no longer reads as a table.
    const TableCapabilities caps =
        TableCapabilitiesFor(loc.rowIndex, loc.numCols);
    if (below ? !caps.addRowBelow : !caps.addRowAbove) return edit;

    // The new row has the same number of columns as the table.
    std::string newRow = "|";
    for (int c = 0; c < loc.numCols; ++c) newRow += "        |";
    newRow += "\n";

    // Below: right after the current row's end. Above: at the current
    // row's start.
    uint32_t insertPos = below ? loc.rowEnd : loc.rowStart;
    if (below && insertPos < text.size() && text[insertPos] == '\n')
        insertPos += 1;

    // A table at the end of the document may have no trailing newline,
    // and then the row would be appended onto the last row's line. The
    // new row starts on a line of its own.
    std::string prefix;
    if (insertPos > 0 && text[insertPos - 1] != '\n') prefix = "\n";

    edit.ok = true;
    edit.start = insertPos;
    edit.length = 0;
    edit.text = prefix + newRow;

    const uint32_t base = insertPos + static_cast<uint32_t>(prefix.size());
    // Below keeps the caret at the start of the row after the new one.
    // Above puts it inside the new row's first cell, one position past
    // the leading pipe.
    if (below) {
        edit.caret = base + static_cast<uint32_t>(newRow.size());
    } else {
        edit.caret = base + 1;
    }
    return edit;
}

TableEdit RemoveRow(const std::string& text, const TableLocation& loc) {
    TableEdit edit;

    // The caret's line sits on the header (line 0) or the delimiter
    // (line 1): those lines carry the table's structure and are never
    // removed, whatever the size of the table.
    if (!TableCapabilitiesFor(loc.rowIndex, loc.numCols).removeRow) {
        return edit;
    }

    edit.ok = true;
    edit.start = loc.rowStart;
    edit.length = loc.rowEnd - loc.rowStart;
    edit.text.clear();
    edit.caret = loc.rowStart;
    return edit;
}

std::string BuildGrid(int cols, int rows) {
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    std::string table;
    // The header.
    for (int c = 0; c < cols; ++c) {
        table += "| Column ";
        table += std::to_string(c + 1);
        table += " ";
    }
    table += "|\n";
    // The dash delimiter.
    for (int c = 0; c < cols; ++c) table += "|--------";
    table += "|\n";
    // The body rows.
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) table += "|        ";
        table += "|\n";
    }
    return table;
}

}  // namespace tableedit
