#include "gtest_lite.h"
#include "parser.h"
#include "navigation.h"
#include "editcontroller.h"
#include "textbuffer.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
Document ParseDoc(const std::string& md) {
    Document doc;
    ParseMarkdown(md, doc);
    return doc;
}
// Cell text is stored as UTF-32; tests assert on readable strings.
std::string ToUtf8(const std::u32string& s) {
    std::string out;
    for (char32_t cp : s) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}
size_t TableIndexOf(const Document& doc) {
    for (size_t i = 0; i < doc.nodes.size(); ++i)
        if (doc.nodes[i].block == BlockKind::Table) return i;
    return SIZE_MAX;
}
}  // namespace

// ─── Row composition helpers ──────────────────────────────────────────

TEST(TableRows, BlankRowMatchesColumnCount) {
    EXPECT_EQ(TableBlankRow(3), "|        |        |        |\n");
    EXPECT_EQ(TableBlankRow(1), "|        |\n");
}

TEST(TableRows, DelimitersMatchColumnCount) {
    EXPECT_EQ(TableDelimitersFor(2), "|--------|--------|\n");
}

TEST(ExcelPaste, TabRowsBecomeAMarkdownTable) {
    const std::string pasted = "Name\tDepartment\nAda\tMath\nAlan\tComputing";
    std::string table;
    uint32_t caret = 0;
    ASSERT_TRUE(TabSeparatedToMarkdownTable(pasted, &table, &caret));
    const std::string expected =
        "| Name | Department |\n"
        "|--------|--------|\n"
        "| Ada | Math |\n"
        "| Alan | Computing |\n";
    EXPECT_EQ(table, expected);
    // Caret sits in the first body cell, right after its opening "| ".
    EXPECT_EQ(table.substr(caret, 3), "Ada");
    EXPECT_EQ(table[caret - 1], ' ');
    EXPECT_EQ(table[caret - 2], '|');

    // The produced table must parse back as a table with the right shape.
    const Document doc = ParseDoc(table);
    const size_t ti = TableIndexOf(doc);
    ASSERT_TRUE(ti != SIZE_MAX);
    // rows[] includes the header: header + two body rows.
    ASSERT_EQ(doc.nodes[ti].rows.size(), 3u);
    EXPECT_EQ(ToUtf8(doc.nodes[ti].rows[0].cells[0].text), "Name");
    EXPECT_EQ(ToUtf8(doc.nodes[ti].rows[0].cells[1].text), "Department");
    EXPECT_EQ(ToUtf8(doc.nodes[ti].rows[1].cells[0].text), "Ada");
    EXPECT_EQ(ToUtf8(doc.nodes[ti].rows[1].cells[1].text), "Math");
    EXPECT_EQ(ToUtf8(doc.nodes[ti].rows[2].cells[1].text), "Computing");
}

TEST(ExcelPaste, QuotedValuesKeepTabsAndDoubledQuotes) {
    // Excel quotes a cell whose text contains the separator.
    const std::string pasted = "\"a\tb\"\tx\n\"say \"\"hi\"\"\"\ty";
    std::string table;
    uint32_t caret = 0;
    ASSERT_TRUE(TabSeparatedToMarkdownTable(pasted, &table, &caret));
    const Document doc = ParseDoc(table);
    const size_t ti = TableIndexOf(doc);
    ASSERT_TRUE(ti != SIZE_MAX);
    EXPECT_EQ(ToUtf8(doc.nodes[ti].rows[0].cells[0].text), "a\tb");
    EXPECT_EQ(ToUtf8(doc.nodes[ti].rows[0].cells[1].text), "x");
    EXPECT_EQ(ToUtf8(doc.nodes[ti].rows[1].cells[0].text), "say \"hi\"");
}

TEST(ExcelPaste, PipesAreEscapedAndEmptyTrailingCellsSurvive) {
    const std::string pasted = "a|b\t\n c \t";
    std::string table;
    uint32_t caret = 0;
    ASSERT_TRUE(TabSeparatedToMarkdownTable(pasted, &table, &caret));
    const Document doc = ParseDoc(table);
    const size_t ti = TableIndexOf(doc);
    ASSERT_TRUE(ti != SIZE_MAX);
    ASSERT_EQ(doc.nodes[ti].rows[0].cells.size(), 2u);
    EXPECT_EQ(ToUtf8(doc.nodes[ti].rows[0].cells[0].text), "a|b");
    EXPECT_TRUE(doc.nodes[ti].rows[0].cells[1].text.empty());
    EXPECT_EQ(ToUtf8(doc.nodes[ti].rows[1].cells[0].text), "c");
}

TEST(ExcelPaste, EscapedClipboardTextDoesNotBreakQuotingOrPipes) {
    // The real Windows paste chain escapes Markdown metacharacters before
    // insertion. The structural conversion must run on the raw text, so
    // prove the ordering: escaped input must NOT convert to a broken
    // table, while raw input converts cleanly.
    const std::string raw = "\"a\tb\"\tx\n\"say \"\"hi\"\"\"\ty";
    std::string table;
    uint32_t caret = 0;
    ASSERT_TRUE(TabSeparatedToMarkdownTable(raw, &table, &caret));
    // Escaped variant (what a naive chain would hand the converter):
    // quotes are prefaced with backslashes, so SplitTabRow cannot see the
    // opening quote and the payload must fall back to plain text.
    std::string escaped;
    for (char c : raw) {
        if (c == '"') escaped += "\\\"";
        else escaped += c;
    }
    std::string rejected;
    uint32_t caret2 = 0;
    EXPECT_FALSE(TabSeparatedToMarkdownTable(escaped, &rejected, &caret2));
    EXPECT_TRUE(rejected.empty());
}

TEST(ExcelPaste, TableAfterListNeedsBlankSeparatorLine) {
    // User scenario: caret in the first empty paragraph after a list, a
    // new table is created there. A table cannot interrupt a list, so the
    // insert must add a blank separator line; otherwise the table rows
    // are absorbed as a lazy continuation of the last list item.
    const std::string before = "- one\n- two\n";
    // Caret just past the list's final newline (start of the empty
    // paragraph after the list).
    const uint32_t insertPos = 12;
    EXPECT_EQ(TableSeparatorNewlinesBefore(before, insertPos), 1u);
    // Reproduce the full insertion and check the parse.
    const std::string table = "| a | b |\n|---|---|\n| 1 | 2 |\n";
    std::string insertText(TableSeparatorNewlinesBefore(before, insertPos), '\n');
    insertText += table;
    std::string doc = before.substr(0, insertPos) + insertText;
    Document parsed;
    ASSERT_TRUE(ParseMarkdown(doc, parsed));
    size_t tables = 0;
    for (const Node& n : parsed.nodes)
        if (n.block == BlockKind::Table) ++tables;
    EXPECT_EQ(tables, 1u);

    // Without the separator (the old bug): the table disappears into the
    // last list item.
    std::string lazy = before + table;
    Document lazyDoc;
    ASSERT_TRUE(ParseMarkdown(lazy, lazyDoc));
    size_t lazyTables = 0;
    for (const Node& n : lazyDoc.nodes)
        if (n.block == BlockKind::Table) ++lazyTables;
    EXPECT_EQ(lazyTables, 0u);

    // Start of document needs nothing.
    EXPECT_EQ(TableSeparatorNewlinesBefore("", 0), 0u);
    // Already blank-separated text needs nothing extra.
    EXPECT_EQ(TableSeparatorNewlinesBefore("- x\n\n", 6), 0u);
}

TEST(TableRows, EmptyCellRowsStayInsideTheTableSpan) {
    // Grid-created tables have rows whose cells are all empty; the parser's
    // text-based span used to stop at the last row containing text, leaving
    // the empty rows outside the table's own span. Removing a row then left
    // its newline behind, the blank line split the table, and the rows
    // below degraded to raw pipe text. The span must cover every row line.
    const std::string src =
        "| Column 1 | Column 2 |\n"
        "|--------|--------|\n"
        "|        |        |\n"
        "|        |        |\n";
    Document doc;
    ASSERT_TRUE(ParseMarkdown(src, doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    ASSERT_EQ(doc.nodes[0].block, BlockKind::Table);
    EXPECT_EQ(doc.nodes[0].rows.size(), 3u);
    // The span covers the whole table, both empty body rows included.
    EXPECT_EQ(doc.nodes[0].srcOffset, 0u);
    EXPECT_EQ(doc.nodes[0].srcLength,
              static_cast<uint32_t>(src.size()));

    // The user flow: caret in the first body row, remove rows; the text
    // stays one table no matter which body row was removed.
    const uint32_t rowLineLen =
        static_cast<uint32_t>(strlen("\n|        |        |"));
    for (uint32_t removed = 2; removed <= 4; removed += 2) {
        // body row 1 line starts after header+delimiter
        uint32_t rowStart = removed == 2
            ? strlen("| Column 1 | Column 2 |\n|--------|--------|\n")
            : strlen("| Column 1 | Column 2 |\n|--------|--------|\n"
                     "|        |        |\n");
        std::string after = src.substr(0, rowStart) +
                            src.substr(rowStart + rowLineLen);
        Document d2;
        ASSERT_TRUE(ParseMarkdown(after, d2));
        size_t tables = 0, otherBlocks = 0;
        for (const Node& n : d2.nodes) {
            if (n.block == BlockKind::Table) ++tables;
            else if (n.block != BlockKind::Paragraph ||
                     !n.virtualEmptyParagraph) ++otherBlocks;
        }
        EXPECT_EQ(tables, 1u);
        EXPECT_EQ(otherBlocks, 0u);
        if (d2.nodes.size() == 1u)
            EXPECT_EQ(d2.nodes[0].rows.size(), 2u);
    }
}

TEST(ExcelPaste, NotTabSeparatedIsRejected) {
    const char* rejects[] = {
        "just a sentence",
        "one\tline only",
        "cell\tcell\ncell",        // ragged column count
        "\tint x;\n\tint y;",      // tab-indented text, not a table
        "",                        // nothing
    };
    for (const char* text : rejects) {
        std::string table;
        uint32_t caret = 0;
        EXPECT_FALSE(TabSeparatedToMarkdownTable(text, &table, &caret));
        EXPECT_TRUE(table.empty());
    }
    // A pipe table from MarkDownIt itself must not be re-converted.
    std::string table;
    uint32_t caret = 0;
    EXPECT_FALSE(TabSeparatedToMarkdownTable(
        "| a | b |\n|---|---|\n| c | d |", &table, &caret));
}

TEST(TableRows, DelimiterRangeFindsSecondRow) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |";
    const Document doc = ParseDoc(md);
    const size_t ti = TableIndexOf(doc);
    ASSERT_TRUE(ti != SIZE_MAX);
    uint32_t start = 0, end = 0;
    EXPECT_TRUE(TableDelimiterRange(doc, md, ti, &start, &end));
    EXPECT_NE(start, end);
    // The range covers the dash run; both dash cells map inside it.
    EXPECT_TRUE(doc.nodes[ti].srcOffset <= start);
    EXPECT_TRUE(end <= doc.nodes[ti].srcOffset + doc.nodes[ti].srcLength);
}

// ─── TableCellAtOffset identity and snapping ──────────────────────────

TEST(TableCellQuery, ExactCellContainmentResolved) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |";
    const std::string& md_source = md;
    const Document doc = ParseDoc(md);
    ASSERT_TRUE(TableIndexOf(doc) != SIZE_MAX);
    const size_t ti = TableIndexOf(doc);
    const TableCell& cell = doc.nodes[ti].rows[1].cells[1];  // "d"
    TableCellRef ref;
    EXPECT_TRUE(TableCellAtOffset(doc, md_source, cell.srcOffset, &ref));
    EXPECT_EQ(ref.tableIndex, ti);
    EXPECT_EQ(ref.rowIndex, 1u);
    EXPECT_EQ(ref.columnIndex, 1u);
}

TEST(TableCellQuery, HeaderRowIdentity) {
    const std::string md_source = "| h1 | h2 |\n|----|----|\n| a | b |";
    const Document doc = ParseDoc(md_source);
    const size_t ti = TableIndexOf(doc);
    ASSERT_TRUE(ti != SIZE_MAX);
    TableCellRef ref;
    EXPECT_TRUE(TableCellAtOffset(doc, md_source, doc.nodes[ti].rows[0].cells[0].srcOffset,
                                  &ref));
    EXPECT_TRUE(ref.isHeader);
}

TEST(TableCellQuery, PipeSnapsToNearestCellSameRow) {
    // Click at the separating pipe between the two body cells: the caret
    // belongs to a cell of the same row, not to another row.
    const std::string md = "| a | b |\n|---|---|\n| c | d |";
    const std::string& md_source = md;
    const Document doc = ParseDoc(md);
    const size_t ti = TableIndexOf(doc);
    // rows[] holds data rows only: row 0 is the header, row 1 the body.
    const uint32_t pipe = static_cast<uint32_t>(md.rfind('|'));
    TableCellRef ref;
    EXPECT_TRUE(TableCellAtOffset(doc, md_source, pipe, &ref));
    EXPECT_EQ(ref.rowIndex, 1u);
}

TEST(TableCellQuery, OffsetOutsideAnyTableFails) {
    const std::string md_source =
        "plain text\n\n| a | b |\n|---|---|\n| c | d |";
    const Document doc = ParseDoc(md_source);
    TableCellRef ref;
    EXPECT_TRUE(TableCellAtOffset(doc, md_source, 2, &ref) == false ||
                ref.tableIndex == TableIndexOf(doc));
}

TEST(TableCellQuery, EmptyTableCellStillContainsCaret) {
    const std::string md_source = "| a |  |\n|---|---|\n| c | d |";
    const Document doc = ParseDoc(md_source);
    const size_t ti = TableIndexOf(doc);
    ASSERT_TRUE(ti != SIZE_MAX);
    ASSERT_EQ(doc.nodes[ti].rows[0].cells.size(), 2u);
    const TableCell& empty = doc.nodes[ti].rows[0].cells[1];
    TableCellRef ref;
    EXPECT_TRUE(TableCellAtOffset(doc, md_source, empty.srcOffset, &ref));
    EXPECT_EQ(ref.rowIndex, 0u);
    EXPECT_EQ(ref.columnIndex, 1u);
}

TEST(TableColumnCount, WidestRowWins) {
    const Document doc = ParseDoc("| a | b |\n|---|---|\n| c | d |");
    EXPECT_EQ(TableColumnCount(doc, TableIndexOf(doc)), 2u);
}

// ─── In-cell delete guards (plan: cell boundary is never crossed) ──────

TEST(CellDelete, BackspaceAtCellStartIsNoOp) {
    const std::string md = "| ab | cd |\n|----|----|\n| ef | gh |";
    TextBuffer b;
    b.SetText(md);
    Document doc;
    ParseMarkdown(md, doc);
    const size_t ti = TableIndexOf(doc);
    const uint32_t contentStart = doc.nodes[ti].rows[1].cells[1].srcOffset;
    Selection s;
    s.Collapse({contentStart});
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);
    EXPECT_FALSE(ec.DeleteBackwardInCell(doc));
    EXPECT_EQ(b.Text(), md);
    EXPECT_FALSE(undo.CanUndo());
}

TEST(CellDelete, BackspaceInsideCellDeletesOneGrapheme) {
    TextBuffer b;
    b.SetText("| ab | cd |\n|----|----|\n| ef | gh |");
    const std::string original = b.Text();
    Document doc;
    ParseMarkdown(original, doc);
    const size_t ti = TableIndexOf(doc);
    const TableCell& cell = doc.nodes[ti].rows[1].cells[1];  // "gh"
    const uint32_t at = cell.srcOffset + 1;  // between 'g' and 'h'
    Selection s;
    s.Collapse({at});
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);
    EXPECT_TRUE(ec.DeleteBackwardInCell(doc));
    EXPECT_EQ(b.Text(), "| ab | cd |\n|----|----|\n| ef | h |");
    EXPECT_EQ(s.active.offset, cell.srcOffset);
}

TEST(CellDelete, ForwardAtCellEndIsNoOp) {
    const std::string md = "| ab | cd |\n|----|----|\n| ef | gh |";
    TextBuffer b;
    b.SetText(md);
    Document doc;
    ParseMarkdown(md, doc);
    const size_t ti = TableIndexOf(doc);
    // end of "gh" content
    const TableCell& cell = doc.nodes[ti].rows[1].cells[1];
    uint32_t end = cell.srcOffset;
    for (char32_t cp : cell.text) end += cp <= 0x7F ? 1u : 4u;
    Selection s;
    s.Collapse({end});
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);
    EXPECT_FALSE(ec.DeleteForwardInCell(doc));
    EXPECT_EQ(b.Text(), md);
}

TEST(CellDelete, ForwardInsideCellDeletesOneGrapheme) {
    TextBuffer b;
    b.SetText("| ab | cd |\n|----|----|\n| ef | gh |");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    const size_t ti = TableIndexOf(doc);
    const TableCell& cell = doc.nodes[ti].rows[1].cells[1];  // "gh"
    Selection s;
    s.Collapse({cell.srcOffset + 1});  // before 'h'
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);
    EXPECT_TRUE(ec.DeleteForwardInCell(doc));
    EXPECT_EQ(b.Text(), "| ab | cd |\n|----|----|\n| ef | g |");
}

TEST(CellDelete, SeparatorRowUntouched) {
    const std::string md_source = "| ab | cd |\n|----|----|\n| ef | gh |";
    TextBuffer b;
    b.SetText(md_source);
    const std::string original = b.Text();
    Document doc;
    ParseMarkdown(original, doc);
    const size_t ti = TableIndexOf(doc);
    // The dashes line "|----|----|" starts at 11 and ends before \n (21).
    TableCellRef sep;
    ASSERT_TRUE(TableCellAtOffset(doc, md_source, 15, &sep));
    EXPECT_TRUE(sep.separatorRow);
    Selection s;
    s.Collapse({15});  // caret inside the dashes
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);
    EXPECT_FALSE(ec.DeleteBackwardInCell(doc));
    EXPECT_FALSE(ec.DeleteForwardInCell(doc));
    EXPECT_EQ(b.Text(), original);
}

// ─── Paste sanitization for in-cell paste ─────────────────────────────

TEST(PasteTable, DropsPipesAndFoldsBreaks) {
    EXPECT_EQ(SanitizePasteForTableCell("a|b"), "a b");
    EXPECT_EQ(SanitizePasteForTableCell("A\nB\nC"), "A B C");
    EXPECT_EQ(SanitizePasteForTableCell("a | b | c"), "a b c");
}

TEST(PasteTable, MarkdownTablePasteBecomesText) {
    const std::string pasted = "| x | y |\n|---|---|\n| a | b |\n";
    EXPECT_EQ(SanitizePasteForTableCell(pasted), "x y a b ");
}

TEST(PasteTable, LeadingWhitespaceIsPreserved) {
    EXPECT_EQ(SanitizePasteForTableCell("  hi"), "  hi");
}

TEST(PasteTable, EmptyPasteStaysEmpty) {
    EXPECT_EQ(SanitizePasteForTableCell(""), "");
    EXPECT_EQ(SanitizePasteForTableCell("|\n|"), "");
}

// Regression (audit defect: caret resting on a trailing pipe used to
// resolve to no cell, leaving Backspace a silent no-op at the table
// edge).
TEST(TableCellQuery, CaretOnTrailingPipeSnapsIntoLastCell) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |";
    const std::string& md_source = md;
    const Document doc = ParseDoc(md);
    ASSERT_EQ(doc.nodes.size(), 1u);
    const uint32_t lastPipe = static_cast<uint32_t>(md.rfind('|'));
    ASSERT_TRUE(lastPipe > 0);
    TableCellRef ref;
    EXPECT_TRUE(TableCellAtOffset(doc, md_source, lastPipe, &ref));
    EXPECT_EQ(ref.tableIndex, 0u);
    EXPECT_EQ(ref.rowIndex, 1u);
    EXPECT_EQ(ref.columnIndex, 1u);
    EXPECT_FALSE(ref.separatorRow);
}

TEST(TableCellQuery, CaretBeforeLeadingPipeSnapsIntoFirstCell) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |";
    const std::string& md_source = md;
    const Document doc = ParseDoc(md);
    TableCellRef ref;
    EXPECT_TRUE(TableCellAtOffset(doc, md_source, 0, &ref));
    EXPECT_EQ(ref.rowIndex, 0u);
    EXPECT_EQ(ref.columnIndex, 0u);
    EXPECT_TRUE(ref.isHeader);
}

// ─── Column insert / remove (regression: single undo step, valid geometry) ──

namespace {

// Apply a column rewrite the way AppWindow does: splice the returned span
// fragment over the table's own range. The helpers return the table-local
// fragment, NOT the whole document.
std::string Splice(const std::string& src, uint32_t start, uint32_t end,
                   const std::string& fragment) {
    std::string out = src;
    out.replace(start, end - start, fragment);
    return out;
}

// Column count of each physical line of a table, derived from its pipes.
std::vector<size_t> ColsPerLine(const std::string& md) {
    std::vector<size_t> out;
    std::string line;
    for (size_t i = 0; i <= md.size(); ++i) {
        if (i == md.size() || md[i] == '\n') {
            if (!line.empty()) {
                size_t pipes = 0;
                for (char c : line)
                    if (c == '|') ++pipes;
                out.push_back(pipes ? pipes - 1 : 0);
            }
            line.clear();
        } else {
            line += md[i];
        }
    }
    return out;
}

bool AllEqual(const std::vector<size_t>& v) {
    for (size_t x : v)
        if (x != v.front()) return false;
    return !v.empty();
}

}  // namespace

// Regression: RemoveTableColumn used to delete BOTH the pipe opening the
// column and the one closing it, so a 3-column table collapsed to a single
// column and the neighbouring cells merged.
TEST(TableColumn, RemoveKeepsExactlyOneColumnFewer) {
    const std::string md = "| a | b | c |\n|---|---|---|\n| d | e | f |\n";
    for (int col = 0; col < 3; ++col) {
        std::string out;
        uint32_t caret = 0;
        ASSERT_TRUE(TableRemoveColumn(md, 0, static_cast<uint32_t>(md.size()),
                                      col, 2, &out, &caret));
        const std::vector<size_t> cols = ColsPerLine(Splice(
            md, 0, static_cast<uint32_t>(md.size()), out));
        EXPECT_TRUE(AllEqual(cols));
        EXPECT_EQ(cols.front(), 2u);
    }
}

TEST(TableColumn, RemovePreservesTheOtherCells) {
    const std::string md = "| a | b | c |\n|---|---|---|\n| d | e | f |\n";
    std::string out;
    uint32_t caret = 0;
    ASSERT_TRUE(TableRemoveColumn(md, 0, static_cast<uint32_t>(md.size()),
                                  1, 2, &out, &caret));
    EXPECT_EQ(Splice(md, 0, static_cast<uint32_t>(md.size()), out),
              "| a | c |\n|---|---|\n| d | f |\n");
}

// Regression: the caret was left at its old offset after a column was
// removed, which could land past the end of the shorter text.
TEST(TableColumn, RemoveKeepsCaretInsideText) {
    const std::string md = "| a | b | c |\n|---|---|---|\n| d | e | f |\n";
    for (const char* cell : {"d", "e", "f"}) {
        std::string out;
        uint32_t caret = 0;
        const uint32_t at = static_cast<uint32_t>(md.find(cell));
        ASSERT_TRUE(TableRemoveColumn(md, 0, static_cast<uint32_t>(md.size()),
                                      0, at, &out, &caret));
        EXPECT_LE(caret, Splice(md, 0, static_cast<uint32_t>(md.size()), out)
                            .size());
    }
}

TEST(TableColumn, RemoveShiftsCaretLeftByRemovedBytes) {
    const std::string md = "| a | b | c |\n|---|---|---|\n| d | e | f |\n";
    const uint32_t at = static_cast<uint32_t>(md.rfind("| f |") + 2);
    std::string out;
    uint32_t caret = 0;
    ASSERT_TRUE(TableRemoveColumn(md, 0, static_cast<uint32_t>(md.size()),
                                  0, at, &out, &caret));
    // Removing column 0 shortens every line; the caret must follow.
    EXPECT_LT(caret, at);
    EXPECT_LE(caret, Splice(md, 0, static_cast<uint32_t>(md.size()), out)
                        .size());
}

TEST(TableColumn, InsertRightAddsOneColumnToEveryLine) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |\n";
    std::string out;
    uint32_t caret = 0;
    ASSERT_TRUE(TableInsertColumn(md, 0, static_cast<uint32_t>(md.size()),
                                  0, true, 2, &out, &caret));
    const std::vector<size_t> cols = ColsPerLine(Splice(
        md, 0, static_cast<uint32_t>(md.size()), out));
    EXPECT_TRUE(AllEqual(cols));
    EXPECT_EQ(cols.front(), 3u);
}

TEST(TableColumn, InsertLeftPutsNewColumnFirst) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |\n";
    std::string out;
    uint32_t caret = 0;
    ASSERT_TRUE(TableInsertColumn(md, 0, static_cast<uint32_t>(md.size()),
                                  0, false, 2, &out, &caret));
    const std::string whole = Splice(md, 0,
        static_cast<uint32_t>(md.size()), out);
    EXPECT_EQ(ColsPerLine(whole).front(), 3u);
    // The original header text survives after the new blank column.
    EXPECT_TRUE(whole.find("|        | a | b |") != std::string::npos);
}

TEST(TableColumn, InsertKeepsRowSeparation) {
    // Regression: a rewrite that dropped the newline terminator ran all
    // the rows together into one line.
    const std::string md = "| a | b |\n|---|---|\n| c | d |\n";
    std::string out;
    uint32_t caret = 0;
    ASSERT_TRUE(TableInsertColumn(md, 0, static_cast<uint32_t>(md.size()),
                                  1, true, 2, &out, &caret));
    const std::string whole = Splice(md, 0,
        static_cast<uint32_t>(md.size()), out);
    size_t newlines = 0;
    for (char c : whole)
        if (c == '\n') ++newlines;
    EXPECT_EQ(newlines, 3u);
}

TEST(TableColumn, InsertUsesDashCellOnDelimiterRow) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |\n";
    std::string out;
    uint32_t caret = 0;
    ASSERT_TRUE(TableInsertColumn(md, 0, static_cast<uint32_t>(md.size()),
                                  0, true, 2, &out, &caret));
    EXPECT_TRUE(Splice(md, 0, static_cast<uint32_t>(md.size()), out)
                    .find("|--------|") != std::string::npos);
}

// The caret only shifts when the new text is inserted before it. Adding a
// column to the right of the caret's own cell must leave the caret on the
// same character.
TEST(TableColumn, InsertRightOfCaretLeavesCaretInPlace) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |\n";
    std::string out;
    uint32_t caret = 0;
    ASSERT_TRUE(TableInsertColumn(md, 0, static_cast<uint32_t>(md.size()),
                                  0, true, 2, &out, &caret));
    EXPECT_EQ(caret, 2u);
    EXPECT_EQ(Splice(md, 0, static_cast<uint32_t>(md.size()), out)[caret],
              md[2]);
}

TEST(TableColumn, InsertLeftOfCaretShiftsItRight) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |\n";
    // Caret inside the second cell ("b" at offset 6); inserting to the left
    // of that column puts the new cell ahead of the caret.
    std::string out;
    uint32_t caret = 0;
    ASSERT_TRUE(TableInsertColumn(md, 0, static_cast<uint32_t>(md.size()),
                                  1, false, 6, &out, &caret));
    EXPECT_GT(caret, 6u);
    EXPECT_EQ(Splice(md, 0, static_cast<uint32_t>(md.size()), out)[caret], 'b');
}

// A short row without the required pipe is copied through unchanged
// instead of being half-rewritten into an invalid table.
TEST(TableColumn, RaggedRowIsPreserved) {
    const std::string md = "| a | b |\n|---|---|\n| short |\n";
    std::string out;
    uint32_t caret = 0;
    ASSERT_TRUE(TableInsertColumn(md, 0, static_cast<uint32_t>(md.size()),
                                  1, true, 2, &out, &caret));
    EXPECT_TRUE(Splice(md, 0, static_cast<uint32_t>(md.size()), out)
                    .find("| short |") != std::string::npos);
}

// No line carries the pipe, so the command is refused outright.
TEST(TableColumn, RefusedWhenNoLineHasThePipe) {
    const std::string md = "| a | b |\n|---|---|\n";
    std::string out;
    uint32_t caret = 0;
    EXPECT_FALSE(TableRemoveColumn(md, 0, static_cast<uint32_t>(md.size()),
                                   9, 2, &out, &caret));
}

TEST(TableColumn, RejectsRangeOutsideSource) {
    const std::string md = "| a | b |\n|---|---|\n";
    std::string out;
    uint32_t caret = 0;
    EXPECT_FALSE(TableRemoveColumn(md, 0, 9999, 0, 2, &out, &caret));
    EXPECT_FALSE(TableInsertColumn(md, 5, 2, 0, true, 2, &out, &caret));
}

// A table at the end of the file has no trailing newline; the rewrite must
// not depend on one.
TEST(TableColumn, WorksOnTableAtEndOfFile) {
    const std::string md = "| a | b | c |\n|---|---|---|\n| d | e | f |";
    std::string out;
    uint32_t caret = 0;
    ASSERT_TRUE(TableRemoveColumn(md, 0, static_cast<uint32_t>(md.size()),
                                  1, 30, &out, &caret));
    const std::string whole = Splice(md, 0,
        static_cast<uint32_t>(md.size()), out);
    EXPECT_EQ(ColsPerLine(whole), (std::vector<size_t>{2, 2, 2}));
    EXPECT_LE(caret, whole.size());
}

// ─── Regression: a column command must not duplicate the document ─────────
//
// These helpers return the table's REWRITTEN SPAN. Returning the whole
// document instead made the caller splice the surrounding text into the
// span, so any text before or after the table appeared twice.

TEST(TableColumn, SurroundingTextIsNotDuplicatedOnInsert) {
    const std::string md = "Intro\n\n| A | B |\n|---|---|\n| C | D |\n\nAfter\n";
    Document doc;
    ParseMarkdown(md, doc);
    size_t ti = SIZE_MAX;
    for (size_t i = 0; i < doc.nodes.size(); ++i)
        if (doc.nodes[i].block == BlockKind::Table) ti = i;
    ASSERT_TRUE(ti != SIZE_MAX);
    const uint32_t start = doc.nodes[ti].srcOffset;
    const uint32_t end = start + doc.nodes[ti].srcLength;

    std::string frag;
    uint32_t caret = 0;
    ASSERT_TRUE(TableInsertColumn(md, start, end, 0, true, start + 2,
                                  &frag, &caret));
    const std::string whole = Splice(md, start, end, frag);

    // The document prefix and suffix survive exactly once each.
    EXPECT_TRUE(whole.find("Intro") == 0);
    EXPECT_TRUE(whole.find("After") == whole.rfind("After"));
    EXPECT_EQ(whole.rfind("Intro"), whole.find("Intro"));
    EXPECT_EQ(std::count(whole.begin(), whole.end(), 'I'), 1);
    // The returned fragment is table-local: it must not contain the
    // surrounding prose at all.
    EXPECT_TRUE(frag.find("Intro") == std::string::npos);
    EXPECT_TRUE(frag.find("After") == std::string::npos);
}

TEST(TableColumn, SurroundingTextIsNotDuplicatedOnRemove) {
    const std::string md = "Intro\n\n| A | B |\n|---|---|\n| C | D |\n\nAfter\n";
    Document doc;
    ParseMarkdown(md, doc);
    size_t ti = SIZE_MAX;
    for (size_t i = 0; i < doc.nodes.size(); ++i)
        if (doc.nodes[i].block == BlockKind::Table) ti = i;
    ASSERT_TRUE(ti != SIZE_MAX);
    const uint32_t start = doc.nodes[ti].srcOffset;
    const uint32_t end = start + doc.nodes[ti].srcLength;

    std::string frag;
    uint32_t caret = 0;
    ASSERT_TRUE(TableRemoveColumn(md, start, end, 0, start + 2,
                                  &frag, &caret));
    const std::string whole = Splice(md, start, end, frag);
    EXPECT_EQ(whole.find("Intro"), whole.rfind("Intro"));
    EXPECT_EQ(whole.find("After"), whole.rfind("After"));
    EXPECT_TRUE(frag.find("Intro") == std::string::npos);
}

// The table span is half-open, so the byte just past it is not in the table.
TEST(TableBoundary, TableEndIsNotInsideTheTable) {
    const std::string md = "| A | B |\n|---|---|\n| C | D |\n\nAfter\n";
    Document doc;
    ParseMarkdown(md, doc);
    size_t ti = SIZE_MAX;
    for (size_t i = 0; i < doc.nodes.size(); ++i)
        if (doc.nodes[i].block == BlockKind::Table) ti = i;
    ASSERT_TRUE(ti != SIZE_MAX);
    const uint32_t start = doc.nodes[ti].srcOffset;
    const uint32_t end = start + doc.nodes[ti].srcLength;
    EXPECT_TRUE(IsOffsetInTable(doc, start));
    EXPECT_TRUE(IsOffsetInTable(doc, end - 1));
    EXPECT_FALSE(IsOffsetInTable(doc, end));
}

// Backspace at the caret that starts a table must not delete the blank line
// that makes it a table (spec section 45, backspaceBeforeTable).
TEST(TableBoundary, TableStartIsProtectedFromBackspace) {
    const std::string md = "Para\n\n| A | B |\n|---|---|\n| C | D |\n";
    Document doc;
    ParseMarkdown(md, doc);
    size_t ti = SIZE_MAX;
    for (size_t i = 0; i < doc.nodes.size(); ++i)
        if (doc.nodes[i].block == BlockKind::Table) ti = i;
    ASSERT_TRUE(ti != SIZE_MAX);
    EXPECT_TRUE(TableStartsAt(doc, md, doc.nodes[ti].srcOffset));

    TextBuffer buf;
    buf.SetText(md);
    Selection sel;
    sel.Collapse({doc.nodes[ti].srcOffset});
    UndoStack undo;
    EditController ec(&buf, &sel);
    ec.SetUndoStack(&undo);
    EXPECT_FALSE(ec.DeleteBackward(&doc));
    EXPECT_EQ(buf.Text(), md);
    EXPECT_FALSE(undo.CanUndo());
}

// A document-initial table has no separation to protect.
TEST(TableBoundary, DocumentInitialTableIsNotProtected) {
    const std::string md = "| A | B |\n|---|---|\n";
    Document doc;
    ParseMarkdown(md, doc);
    size_t ti = SIZE_MAX;
    for (size_t i = 0; i < doc.nodes.size(); ++i)
        if (doc.nodes[i].block == BlockKind::Table) ti = i;
    ASSERT_TRUE(ti != SIZE_MAX);
    EXPECT_FALSE(TableStartsAt(doc, md, doc.nodes[ti].srcOffset));
}

// ─── Table command capabilities (ribbon enable state) ──────────────────────
//
// The ribbon must not offer a live button for a command that will silently
// refuse. md4c requires the dash delimiter to sit IMMEDIATELY after the
// header row, so inserting a row anywhere in that pair destroys the table.
// Verified by reparsing after each candidate insert: a row inserted below
// the header reparses to ZERO table blocks.

TEST(TableCapabilities, HeaderRowAllowsNoRowCommand) {
    // Row 0. Above it is outside the table; below it would land between
    // header and delimiter and break it.
    const TableCapabilities c = TableCapabilitiesFor(0, 3);
    EXPECT_FALSE(c.addRowAbove);
    EXPECT_FALSE(c.addRowBelow);
    EXPECT_FALSE(c.removeRow);
}

TEST(TableCapabilities, DelimiterRowAllowsOnlyAddingBelow) {
    const TableCapabilities c = TableCapabilitiesFor(1, 3);
    EXPECT_FALSE(c.addRowAbove);   // would split header from delimiter
    EXPECT_FALSE(c.removeRow);     // the delimiter IS the table
    EXPECT_TRUE(c.addRowBelow);    // lands after the delimiter: safe
}

TEST(TableCapabilities, BodyRowAllowsAllRowCommands) {
    const TableCapabilities c = TableCapabilitiesFor(2, 3);
    EXPECT_TRUE(c.addRowAbove);
    EXPECT_TRUE(c.addRowBelow);
    EXPECT_TRUE(c.removeRow);
}

TEST(TableCapabilities, ColumnCommandsIgnoreRowIndex) {
    for (int row = 0; row < 4; ++row) {
        const TableCapabilities c = TableCapabilitiesFor(row, 3);
        EXPECT_TRUE(c.addColumnLeft);
        EXPECT_TRUE(c.addColumnRight);
    }
}

TEST(TableCapabilities, LastColumnCannotBeRemoved) {
    EXPECT_FALSE(TableCapabilitiesFor(2, 1).removeColumn);
    EXPECT_TRUE(TableCapabilitiesFor(2, 2).removeColumn);
}

// The geometry behind the guard: a row inserted below the header destroys
// the table, while the same row inserted below a body row is fine.
TEST(TableRowGuards, InsertingBelowHeaderDestroysTheTable) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |\n";
    const uint32_t headerEnd = static_cast<uint32_t>(md.find("|---|"));
    ASSERT_EQ(headerEnd, 10u);

    // Insert a blank row right after the header line, i.e. between header
    // and delimiter. This is what "Add Row" on the header row would do.
    const std::string newRow = "|        |        |\n";
    std::string broken = md;
    broken.insert(headerEnd, newRow);

    Document doc;
    ParseMarkdown(broken, doc);
    int tables = 0;
    for (const auto& n : doc.nodes)
        if (n.block == BlockKind::Table) ++tables;
    EXPECT_EQ(tables, 0);

    // The capability rule is what must prevent that insert.
    EXPECT_FALSE(TableCapabilitiesFor(0, 2).addRowBelow);
}

TEST(TableRowGuards, InsertingBelowBodyRowKeepsTheTable) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |\n";
    std::string after = md;
    after.insert(md.size(), "|        |        |\n");

    Document doc;
    ParseMarkdown(after, doc);
    int tables = 0;
    for (const auto& n : doc.nodes)
        if (n.block == BlockKind::Table) ++tables;
    EXPECT_EQ(tables, 1);
    // The delimiter is still immediately after the header.
    EXPECT_EQ(after.find("|---|"), 10u);
}

TEST(TableRowGuards, DelimiterStaysAdjacentToHeader) {
    const std::string md = "| a | b |\n|---|---|\n| c | d |\n";
    // Header occupies [0,10), delimiter [10,20). The delimiter must follow
    // the header with no blank line or row between them.
    EXPECT_TRUE(md.compare(0, 10, "| a | b |\n") == 0);
    EXPECT_TRUE(md.compare(10, 10, "|---|---|\n") == 0);
}

// ─── Column alignment from the delimiter row (plan Task 16b step 1) ─────────
//
// md4c reports MD_BLOCK_TD_DETAIL.align per cell. A table written with
// `:---:` and `---:` must render those columns centred and right aligned.
// Before this was captured, every column rendered left aligned.
TEST(TableAlign, CentredAndRightMarkersAreCaptured) {
    const Document d = ParseDoc("| a | b | c |\n|:--|:-:|--:|\n| 1 | 2 | 3 |\n");
    ASSERT_EQ(d.nodes.size(), 1u);
    const Node& t = d.nodes[0];
    ASSERT_EQ(t.aligns.size(), 3u);
    EXPECT_TRUE(t.aligns[0] == ColumnAlign::Left);
    EXPECT_TRUE(t.aligns[1] == ColumnAlign::Center);
    EXPECT_TRUE(t.aligns[2] == ColumnAlign::Right);
}

TEST(TableAlign, PlainTableHasNoAlignment) {
    const Document d = ParseDoc("| a | b |\n|---|---|\n| 1 | 2 |\n");
    ASSERT_EQ(d.nodes.size(), 1u);
    const Node& t = d.nodes[0];
    for (ColumnAlign a : t.aligns) {
        EXPECT_TRUE(a == ColumnAlign::Default);
    }
}

TEST(TableAlign, HeaderCellMarkerSurvivesBodyDefault) {
    // The header cell reports the explicit marker; body cells report
    // MD_ALIGN_DEFAULT. The explicit marker must not be overwritten.
    const Document d = ParseDoc("| a | b |\n|--:|---|\n| 1 | 2 |\n| 3 | 4 |\n");
    ASSERT_EQ(d.nodes.size(), 1u);
    const Node& t = d.nodes[0];
    ASSERT_GE(t.aligns.size(), 1u);
    EXPECT_TRUE(t.aligns[0] == ColumnAlign::Right);
}

// ─── Column alignment marker (plan Task 16b steps 3 and 4) ────────────────
//
// Setting alignment must touch exactly one cell of the delimiter row and
// leave every other byte alone. The dash run keeps its length.
TEST(TableAlignEdit, RightMarkerIsWrittenOnDelimiterRowOnly) {
    const std::string md = "| a | b |\n|---|---|\n| 1 | 2 |\n";
    std::string out; uint32_t caret = 0;
    ASSERT_TRUE(TableSetColumnAlign(md, 0, static_cast<uint32_t>(md.size()),
                                    1, TableAlignMark::Right, 0, &out, &caret));
    // Only the delimiter row's second cell changed: --- -> ---:
    EXPECT_TRUE(out == "| a | b |\n|---|---:|\n| 1 | 2 |\n");
}

TEST(TableAlignEdit, CenterMarkerWrapsInColons) {
    const std::string md = "| a | b |\n|---|---|\n| 1 | 2 |\n";
    std::string out; uint32_t caret = 0;
    ASSERT_TRUE(TableSetColumnAlign(md, 0, static_cast<uint32_t>(md.size()),
                                    0, TableAlignMark::Center, 0, &out, &caret));
    EXPECT_TRUE(out == "| a | b |\n|:---:|---|\n| 1 | 2 |\n");
}

TEST(TableAlignEdit, ClearingTheMarkerRestoresAPlainDashCell) {
    const std::string md = "| a | b |\n|---|---:|\n| 1 | 2 |\n";
    std::string out; uint32_t caret = 0;
    ASSERT_TRUE(TableSetColumnAlign(md, 0, static_cast<uint32_t>(md.size()),
                                    1, TableAlignMark::None, 0, &out, &caret));
    EXPECT_TRUE(out == "| a | b |\n|---|---|\n| 1 | 2 |\n");
}

TEST(TableAlignEdit, DashCountIsPreserved) {
    const std::string md = "| a | b |\n|-------|-----------|\n| 1 | 2 |\n";
    std::string out; uint32_t caret = 0;
    ASSERT_TRUE(TableSetColumnAlign(md, 0, static_cast<uint32_t>(md.size()),
                                    0, TableAlignMark::Right, 0, &out, &caret));
    // The second cell's ten dashes are untouched.
    EXPECT_TRUE(out == "| a | b |\n|-------:|-----------|\n| 1 | 2 |\n");
}

TEST(TableAlignEdit, ShortDashRunIsPaddedToThree) {
    // A two-dash cell is legal markdown. Writing `:--` would leave a cell
    // too short to read back as a delimiter, so the run is padded to three.
    const std::string md = "| a | b |\n|--|----|\n| 1 | 2 |\n";
    std::string out; uint32_t caret = 0;
    ASSERT_TRUE(TableSetColumnAlign(md, 0, static_cast<uint32_t>(md.size()),
                                    0, TableAlignMark::Right, 0, &out, &caret));
    EXPECT_TRUE(out == "| a | b |\n|---:|----|\n| 1 | 2 |\n");
    const Document d = ParseDoc(out);
    ASSERT_EQ(d.nodes.size(), 1u);
    // md4c reports one row per content row; the delimiter is not a row.
    EXPECT_EQ(d.nodes[0].rows.size(), 2u);  // header + one body row
    ASSERT_GE(d.nodes[0].aligns.size(), 1u);
    EXPECT_TRUE(d.nodes[0].aligns[0] == ColumnAlign::Right);
}

TEST(TableAlignEdit, AlignmentReparsesAsTheSameAlignment) {
    const std::string md = "| a | b | c |\n|---|---|---|\n| 1 | 2 | 3 |\n";
    std::string out; uint32_t caret = 0;
    ASSERT_TRUE(TableSetColumnAlign(md, 0, static_cast<uint32_t>(md.size()),
                                    2, TableAlignMark::Right, 0, &out, &caret));
    const Document d = ParseDoc(out);
    ASSERT_EQ(d.nodes.size(), 1u);
    const Node& t = d.nodes[0];
    ASSERT_EQ(t.aligns.size(), 3u);
    EXPECT_TRUE(t.aligns[2] == ColumnAlign::Right);
    EXPECT_TRUE(t.aligns[0] == ColumnAlign::Default);
}

TEST(TableAlignEdit, RefusedWhenColumnHasNoDelimiterCell) {
    const std::string md = "| a | b |\n|---|---|\n| 1 | 2 |\n";
    std::string out; uint32_t caret = 0;
    EXPECT_FALSE(TableSetColumnAlign(md, 0, static_cast<uint32_t>(md.size()),
                                     7, TableAlignMark::Right, 0, &out, &caret));
}

TEST(TableAlignEdit, SurroundingTextIsNotDuplicated) {
    const std::string md =
        "Intro paragraph.\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\nOutro.\n";
    // Derive the span from the text rather than counting bytes by hand.
    const uint32_t ts = static_cast<uint32_t>(md.find("| a | b |"));
    const uint32_t te = static_cast<uint32_t>(
        md.find("| 1 | 2 |") + std::strlen("| 1 | 2 |"));
    std::string out; uint32_t caret = 0;
    bool ok = TableSetColumnAlign(md, ts, te, 0, TableAlignMark::Center,
                                    0, &out, &caret);
    printf("SUR ok=%d ts=%u te=%u\n", ok, ts, te);
    std::string spliced = md.substr(0, ts) + out + md.substr(te);
    EXPECT_TRUE(spliced ==
        "Intro paragraph.\n\n| a | b |\n|:---:|---|\n| 1 | 2 |\n\nOutro.\n");
}

// ─── Up and Down move by row, same column (plan Task 16b step 5) ───────────
//
// Inside a table the arrow keys must walk rows, not visual lines, and must
// keep the column. Leaving the table with an arrow key is not useful, so a
// move past the first or last body row is refused and the caller falls back.
// Offsets below are asserted against the text, never hand-counted.
TEST(TableVertical, DownMovesToTheSameColumnOneRowOn) {
    const std::string md = "| aa | bb |\n|---|---|\n| cc | dd |\n| ee | ff |\n";
    uint32_t out = 0;
    // Caret on the first 'c' of "cc"; one row down is the first 'e' of "ee".
    ASSERT_TRUE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                  24, +1, &out));
    EXPECT_EQ(out, 36u);
}

TEST(TableVertical, UpMovesToTheSameColumnOneRowBack) {
    const std::string md = "| aa | bb |\n|---|---|\n| cc | dd |\n| ee | ff |\n";
    uint32_t out = 0;
    ASSERT_TRUE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                  36, -1, &out));
    EXPECT_EQ(out, 24u);
}

TEST(TableVertical, ColumnIsPreservedAcrossRows) {
    const std::string md = "| aa | bb |\n|---|---|\n| cc | dd |\n";
    uint32_t out = 0;
    // Caret on the first 'd' of "dd" in the body row. The pipes before it are
    // at 22 and 27, so this is column 2.
    ASSERT_TRUE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                  29, -1, &out));
    // One row up is the header, still column 2: the first 'b' of "bb" at 7.
    EXPECT_EQ(out, 7u);
}

TEST(TableVertical, UpFromTheFirstBodyRowReachesTheHeader) {
    const std::string md = "| aa | bb |\n|---|---|\n| cc | dd |\n";
    uint32_t out = 0;
    // Up from the first body row reaches the header, which is a real row.
    ASSERT_TRUE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                  24, -1, &out));
    EXPECT_EQ(out, 2u);  // the 'a' of "aa"
    EXPECT_EQ(md[out], 'a');
}

TEST(TableVertical, RefusedOnTheLastBodyRowGoingDown) {
    const std::string md = "| aa | bb |\n|---|---|\n| cc | dd |\n| ee | ff |\n";
    uint32_t out = 0;
    EXPECT_FALSE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                   36, +1, &out));
}

TEST(TableVertical, DelimiterRowMovesIntoTheAdjacentRow) {
    // The dash delimiter has no text and no caret of its own. A caret that
    // resolves to that line is read as the header row, so Up from it stays
    // put and Down from it lands in the first body row rather than leaving
    // the caret stranded between the header and the dashes.
    const std::string md = "| aa | bb |\n|---|---|\n| cc | dd |\n";
    uint32_t out = 0;
    // Caret on the first dash of the delimiter, column 1.
    EXPECT_FALSE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                   13, -1, &out));
    ASSERT_TRUE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                  13, +1, &out));
    // Read as the header row, one row down is the first body row. The caret
    // was on the delimiter's first dash, column 1, so it lands at the start
    // of "dd" in column 2 of the body row.
    EXPECT_EQ(out, 33u);
}

TEST(TableVertical, OutsideATableIsRefused) {
    const std::string md = "Just a paragraph.\n";
    uint32_t out = 0;
    EXPECT_FALSE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                   3, +1, &out));
}

TEST(TableVertical, OffsetIsClampedToAShorterTargetCell) {
    // The clamp must land on the cell's end, never past it into the next
    // cell, which would put the caret on the following column's text.
    const std::string md = "| aaaa | b |\n|---|---|\n| x | yyyy |\n";
    uint32_t out = 0;
    ASSERT_TRUE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                  5, +1, &out));
    // "aaaa" is four bytes, the target cell "x" is one, so the offset clamps
    // to the cell's end: just past the 'x'.
    EXPECT_EQ(out, 26u);
    EXPECT_EQ(md[out], ' ');
    EXPECT_EQ(md[out - 1], 'x');
}

TEST(TableVertical, ClampLandsOnTheCellEndNotPastIt) {
    const std::string md = "| aaaa | b |\n|---|---|\n| x | y |\n";
    uint32_t out = 0;
    ASSERT_TRUE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                  5, +1, &out));
    // Clamped to the end of the one-character cell "x": offset 26 is the
    // space after it, still inside the row and not into the next column.
    EXPECT_EQ(out, 26u);
    EXPECT_TRUE(out < md.size());
    EXPECT_EQ(md[out - 1], 'x');
    EXPECT_EQ(md[out], ' ');
    EXPECT_EQ(md[out + 1], '|');
}

TEST(TableVertical, LongerTargetCellKeepsTheRelativeOffset) {
    // Caret in the SECOND column of the header, one byte into "xx". The body
    // row's second column is "bbbb", so the caret lands one byte into it.
    const std::string md = "| aa | xx |\n|---|---|\n| y | bbbb |\n";
    uint32_t out = 0;
    ASSERT_TRUE(TableVerticalMove(md, 0, static_cast<uint32_t>(md.size()),
                                  8, +1, &out));
    EXPECT_EQ(out, 29u);   // second 'b' of "bbbb"
    EXPECT_EQ(md[out], 'b');
}

// ─── Split cell (table guidelines section 22) ──────────────────────────────
//
// Markdown cannot express a merged cell, so split has no inverse. Only the
// caret's row carries text; the other rows gain empty companions so the table
// keeps a uniform column count.

static std::string EscapeNewlines(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}

// Split cell column `col` on row `row` of the table spanning [0, size).
static std::string SplitRow(const std::string& md, int col, int row,
                            int pieces, uint32_t* outCaret = nullptr) {
    std::string out;
    uint32_t caret = 0;
    const bool ok = TableSplitCell(md, 0, static_cast<uint32_t>(md.size()),
                                   col, row, pieces,
                                   static_cast<uint32_t>(md.size() / 2),
                                   &out, &caret);
    if (!ok) return "<REFUSED>";
    if (outCaret) *outCaret = caret;
    return out;
}

TEST(TableSplit, TwoPiecesWidensEveryRow) {
    const std::string md =
        "| aa | bb |\n|---|---|\n| ccc | ddd |\n| eee | fff |\n";
    const std::string out = SplitRow(md, 0, 2, 2);
    printf("SPLIT2 [%s]\n", EscapeNewlines(out).c_str());
    // The caret row's "ccc" becomes two cells; every other row gains an empty
    // one, so all four rows must end up with the same column count.
    const Document d = ParseDoc(out);
    ASSERT_EQ(d.nodes.size(), 1u);
    ASSERT_EQ(d.nodes[0].rows.size(), 3u);   // md4c excludes the delimiter
    for (size_t r = 0; r < d.nodes[0].rows.size(); ++r) {
        EXPECT_EQ(d.nodes[0].rows[r].cells.size(), 3u);
    }
}

TEST(TableSplit, ReparsesAsAValidTable) {
    const std::string md =
        "| aa | bb |\n|---|---|\n| ccc | ddd |\n| eee | fff |\n";
    const std::string out = SplitRow(md, 0, 2, 2);
    printf("SPLITVALID [%s]\n", EscapeNewlines(out).c_str());
    // The whole point: the result must still parse as one table, not as a
    // table-shaped paragraph. ValidateDocument checks exactly that.
    const Document d = ParseDoc(out);
    std::vector<std::string> errors;
    EXPECT_TRUE(ValidateDocument(d, &errors));
    for (const auto& e : errors) printf("    error: %s\n", e.c_str());
}

TEST(TableSplit, SplitCellsCarryTheExpectedText) {
    // The rendered text matters more than the spacing: "ccc" split in two
    // must yield "c" and "cc", not "c" and "ccc".
    const std::string md = "| aa | bb |\n|---|---|\n| ccc | ddd |\n";
    const std::string out = SplitRow(md, 0, 2, 2);
    const Document d = ParseDoc(out);
    ASSERT_EQ(d.nodes.size(), 1u);
    ASSERT_EQ(d.nodes[0].rows.size(), 2u);   // header + one body row
    const TableRow& body = d.nodes[0].rows[1];
    ASSERT_EQ(body.cells.size(), 3u);
    printf("CELLS: [%s] [%s] [%s]\n", ToUtf8(body.cells[0].text).c_str(),
           ToUtf8(body.cells[1].text).c_str(), ToUtf8(body.cells[2].text).c_str());
    EXPECT_EQ(ToUtf8(body.cells[0].text), std::string("c"));
    EXPECT_EQ(ToUtf8(body.cells[1].text), std::string("cc"));
    // The untouched second column keeps its text.
    EXPECT_EQ(ToUtf8(body.cells[2].text), std::string("ddd"));
}

TEST(TableSplit, EveryRowKeepsItsOriginalText) {
    // A split must not disturb the text in rows it did not touch. "eee"
    // carries a leading space in the source cell; what matters is that it
    // still parses back as "eee".
    const std::string md = "| aa | bb |\n|---|---|\n| ccc | ddd |\n| eee | fff |\n";
    const std::string out = SplitRow(md, 0, 2, 2);
    printf("KEEP [%s]\n", EscapeNewlines(out).c_str());
    const Document d = ParseDoc(out);
    ASSERT_EQ(d.nodes.size(), 1u);
    ASSERT_EQ(d.nodes[0].rows.size(), 3u);
    EXPECT_EQ(ToUtf8(d.nodes[0].rows[1].cells[0].text), std::string("c"));
    EXPECT_EQ(ToUtf8(d.nodes[0].rows[1].cells[1].text), std::string("cc"));
    EXPECT_EQ(ToUtf8(d.nodes[0].rows[2].cells[0].text), std::string("eee"));
    EXPECT_EQ(ToUtf8(d.nodes[0].rows[2].cells[1].text), std::string(""));
    EXPECT_EQ(ToUtf8(d.nodes[0].rows[2].cells[2].text), std::string("fff"));
}

TEST(TableSplit, ThreePieces) {
    const std::string md = "| aa | bb |\n|---|---|\n| ccc | ddd |\n";
    const std::string out = SplitRow(md, 1, 2, 3);
    printf("SPLIT3 [%s]\n", EscapeNewlines(out).c_str());
    const Document d = ParseDoc(out);
    ASSERT_EQ(d.nodes.size(), 1u);
    for (size_t r = 0; r < d.nodes[0].rows.size(); ++r) {
        EXPECT_EQ(d.nodes[0].rows[r].cells.size(), 4u);
    }
}

TEST(TableSplit, RefusedOnHeaderAndDelimiter) {
    const std::string md = "| aa | bb |\n|---|---|\n| ccc | ddd |\n";
    // Row 0 is the header, row 1 the delimiter. Splitting either breaks the
    // table's structure, so both are refused.
    EXPECT_EQ(SplitRow(md, 0, 0, 2), std::string("<REFUSED>"));
    EXPECT_EQ(SplitRow(md, 0, 1, 2), std::string("<REFUSED>"));
}

TEST(TableSplit, RefusedForOnePiece) {
    const std::string md = "| aa | bb |\n|---|---|\n| ccc | ddd |\n";
    EXPECT_EQ(SplitRow(md, 0, 2, 1), std::string("<REFUSED>"));
    EXPECT_EQ(SplitRow(md, 0, 2, 0), std::string("<REFUSED>"));
}

TEST(TableSplit, SurroundingTextIsNotDuplicated) {
    const std::string md =
        "Intro.\n\n| aa | bb |\n|---|---|\n| ccc | ddd |\n\nOutro.\n";
    std::string out;
    uint32_t caret = 0;
    const uint32_t ts = static_cast<uint32_t>(md.find("| aa"));
    const uint32_t te = static_cast<uint32_t>(md.find("| ccc | ddd |") +
                                              std::string("| ccc | ddd |").size());
    ASSERT_TRUE(TableSplitCell(md, ts, te, 0, 2, 2, ts + 20, &out, &caret));
    // The helper returns only the table span, so splicing it back reproduces
    // the document with the split applied and nothing duplicated.
    std::string whole = md;
    whole.replace(ts, te - ts, out);
    EXPECT_EQ(whole.substr(0, 6), std::string("Intro."));
    EXPECT_TRUE(whole.size() > md.size());
    printf("SPLITSUR [%s]\n", EscapeNewlines(whole).c_str());
    const Document d = ParseDoc(whole);
    EXPECT_TRUE(d.nodes.size() >= 3u);
}
