// The shared table commands, tested without a window.
//
// The Windows build and the mac build both change a table through this
// module, so the rules live here and are checked here: the header and
// the dash delimiter are structural, a row may not be added above the
// header, and a table never loses its shape.

#include "gtest_lite.h"
#include "parser.h"
#include "tableedit.h"

#include <string>

namespace {

const std::string kTable =
    "| a | b |\n"
    "|---|---|\n"
    "| 1 | 2 |\n";

Document ParseDoc(const std::string& md) {
    Document doc;
    ParseMarkdown(md, doc);
    return doc;
}

// The table of the document, and where the caret is in it.
struct Placed {
    tableedit::TableLocation loc;
    bool found = false;
};

Placed Place(const std::string& md, uint32_t offset) {
    const Document doc = ParseDoc(md);
    Placed placed;
    const Node* table = tableedit::ContainingTable(doc, offset);
    if (!table) return placed;
    placed.found = tableedit::Locate(md, table, offset, &placed.loc);
    return placed;
}

// The text with the edit of the command applied.
std::string Apply(const std::string& md, const tableedit::TableEdit& edit) {
    return md.substr(0, edit.start) + edit.text + md.substr(edit.start + edit.length);
}

}  // namespace

TEST(TableEdit, TheTableIsFound) {
    const Placed placed = Place(kTable, 2);
    EXPECT_TRUE(placed.found);
    EXPECT_EQ(placed.loc.numCols, 2);
    EXPECT_EQ(placed.loc.rowIndex, 0);
    EXPECT_EQ(placed.loc.columnIndex, 0);
}

TEST(TableEdit, TheCaretOutsideTheTableIsNotInIt) {
    const std::string md = std::string(kTable) + "\nafter\n";
    const Document doc = ParseDoc(md);
    // The table ends before "after", so a caret there is not in it.
    EXPECT_TRUE(tableedit::ContainingTable(doc, 0) != nullptr);
    EXPECT_TRUE(tableedit::ContainingTable(doc, md.size() - 1) == nullptr);
}

TEST(TableEdit, ARowGoesBelowTheBodyRow) {
    const Placed placed = Place(kTable, 22);  // the "| 1 | 2 |" row
    EXPECT_TRUE(placed.found);
    EXPECT_EQ(placed.loc.rowIndex, 2);

    const tableedit::TableEdit edit =
        tableedit::AddRow(kTable, placed.loc, true);
    EXPECT_TRUE(edit.ok);
    EXPECT_EQ(edit.length, 0u);

    const std::string after = Apply(kTable, edit);
    EXPECT_EQ(after,
              std::string("| a | b |\n"
                          "|---|---|\n"
                          "| 1 | 2 |\n"
                          "|        |        |\n"));
    // The row reparses as a table with two body rows, not as loose text.
    const Document doc = ParseDoc(after);
    int tables = 0;
    for (const Node& n : doc.nodes) {
        if (n.block == BlockKind::Table) {
            ++tables;
            EXPECT_EQ(n.rows.size(), 3u);
        }
    }
    EXPECT_EQ(tables, 1);
}

TEST(TableEdit, ARowDoesNotGoAboveTheHeader) {
    // The header is row 0. A row above it would leave a block md4c no
    // longer reads as a table.
    const Placed placed = Place(kTable, 2);
    EXPECT_TRUE(placed.found);
    EXPECT_EQ(placed.loc.rowIndex, 0);

    EXPECT_FALSE(tableedit::AddRow(kTable, placed.loc, false).ok);
    // Below the header would land between the header and the delimiter,
    // so it is refused as well.
    EXPECT_FALSE(tableedit::AddRow(kTable, placed.loc, true).ok);
}

TEST(TableEdit, ARowDoesNotGoBetweenTheHeaderAndTheDelimiter) {
    // The delimiter is row 1. A row between the header and the
    // delimiter would break the table apart.
    const Placed placed = Place(kTable, 12);
    EXPECT_TRUE(placed.found);
    EXPECT_EQ(placed.loc.rowIndex, 1);

    EXPECT_FALSE(tableedit::AddRow(kTable, placed.loc, false).ok);
    // Below the delimiter is fine, because the table keeps its shape.
    EXPECT_TRUE(tableedit::AddRow(kTable, placed.loc, true).ok);
}

TEST(TableEdit, TheHeaderAndTheDelimiterAreNeverRemoved) {
    for (uint32_t offset : {2u, 12u}) {
        const Placed placed = Place(kTable, offset);
        EXPECT_TRUE(placed.found);
        EXPECT_FALSE(tableedit::RemoveRow(kTable, placed.loc).ok);
    }
}

TEST(TableEdit, ABodyRowIsRemoved) {
    const Placed placed = Place(kTable, 22);
    EXPECT_TRUE(placed.found);

    const tableedit::TableEdit edit =
        tableedit::RemoveRow(kTable, placed.loc);
    EXPECT_TRUE(edit.ok);
    EXPECT_EQ(Apply(kTable, edit),
              std::string("| a | b |\n"
                          "|---|---|\n"));
}

TEST(TableEdit, ATableAtTheEndWithoutANewlineStillGetsItsOwnLine) {
    // A table with no trailing newline: the new row must not be
    // appended onto the last row's line.
    const std::string md = "| a | b |\n|---|---|\n| 1 | 2 |";
    const Placed placed = Place(md, 22);
    EXPECT_TRUE(placed.found);
    EXPECT_EQ(placed.loc.rowIndex, 2);

    const tableedit::TableEdit edit = tableedit::AddRow(md, placed.loc, true);
    EXPECT_TRUE(edit.ok);
    EXPECT_EQ(Apply(md, edit),
              std::string("| a | b |\n"
                          "|---|---|\n"
                          "| 1 | 2 |\n"
                          "|        |        |\n"));
}

TEST(TableEdit, TheCaretLandsInTheNewRow) {
    const Placed placed = Place(kTable, 22);
    EXPECT_TRUE(placed.found);

    const tableedit::TableEdit edit = tableedit::AddRow(kTable, placed.loc, true);
    EXPECT_TRUE(edit.ok);
    // The new row is the last row, so the caret lands just past it.
    const size_t rowLength = std::string("|        |        |\n").size();
    EXPECT_EQ(edit.caret, static_cast<uint32_t>(kTable.size() + rowLength));

    const tableedit::TableEdit above =
        tableedit::AddRow(kTable, placed.loc, false);
    EXPECT_TRUE(above.ok);
    // Above, the caret is inside the new row's first cell, one position
    // past the leading pipe. The new row starts where the body row did.
    EXPECT_EQ(above.caret, static_cast<uint32_t>(20 + 1));
}

TEST(TableEdit, AGridHasAHeaderAndADelimiter) {
    const std::string grid = tableedit::BuildGrid(2, 1);
    EXPECT_EQ(grid,
              std::string("| Column 1 | Column 2 |\n"
                          "|--------|--------|\n"
                          "|        |        |\n"));

    // The grid reparses as a table with the right shape.
    const Document doc = ParseDoc(grid);
    int tables = 0;
    for (const Node& n : doc.nodes) {
        if (n.block == BlockKind::Table) {
            ++tables;
            EXPECT_EQ(n.rows.size(), 2u);
        }
    }
    EXPECT_EQ(tables, 1);
}
