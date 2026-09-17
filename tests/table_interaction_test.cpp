#include "gtest_lite.h"
#include "parser.h"
#include "navigation.h"
#include "editcontroller.h"
#include "textbuffer.h"

namespace {
Document ParseDoc(const std::string& md) {
    Document doc;
    ParseMarkdown(md, doc);
    return doc;
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
