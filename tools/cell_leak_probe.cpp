// Probe: does TableCellAtOffset swallow prose offsets after a table?
// Repro of the prose double-click path without any Windows dependencies.
#include "../../tests/gtest_lite.h"
#include "../../src/parser.h"
#include "../../src/navigation.h"
#include "../../src/textbuffer.h"
#include <cstdio>
#include <string>

namespace {

TextBuffer Buf(const std::string& s) {
    TextBuffer b;
    b.SetText(s);
    return b;
}

void DumpOffsets(const Document& doc, const std::string& label) {
    std::printf("=== %s ===\n", label.c_str());
    for (size_t i = 0; i < doc.nodes.size(); ++i) {
        const Node& n = doc.nodes[i];
        std::printf("node %zu kind=%d src=[%u..%u) len=%u content=[%u..%u)\n",
                    i, (int)n.block, n.srcOffset,
                    n.srcOffset + n.srcLength, n.srcLength,
                    n.contentOffset, n.contentOffset + n.contentLength);
        if (n.block == BlockKind::Table) {
            for (size_t r = 0; r < n.rows.size(); ++r) {
                for (size_t c = 0; c < n.rows[r].cells.size(); ++c) {
                    const TableCell& cell = n.rows[r].cells[c];
                    std::printf("    cell r%zu c%zu src=[%u..%u) text='%s'\n",
                                r, c, cell.srcOffset, cell.srcEnd,
                                std::string(cell.text.begin(), cell.text.end()).c_str());
                }
            }
        }
    }
}

}  // namespace

int main() {
    const std::string md =
        "First paragraph.\n"
        "\n"
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "Last paragraph word.\n";

    Document doc;
    ParseMarkdown(md, doc);
    DumpOffsets(doc, "table-in-middle");

    TextBuffer buf = Buf(md);

    // For every byte offset, ask TableCellAtOffset like OnLButtonDblClk does.
    // The prose double-click path (app.cpp:1579-1596) treats a TRUE return
    // with !separatorRow as a whole-cell selection. A TRUE return on a
    // prose offset means prose double-click selects table garbage instead.
    const std::string proseWord = "paragraph";
    const size_t proseAt = md.find(proseWord);
    const size_t lastAt = md.find("word.");
    struct Case { size_t off; const char* what; };
    std::vector<Case> cases;
    cases.push_back({proseAt, "prose 'First paragraph.' word start"});
    cases.push_back({proseAt + 5, "prose 'First paragraph.' mid-word"});
    cases.push_back({lastAt, "prose 'Last paragraph word.' word start"});
    cases.push_back({lastAt + 2, "prose 'Last paragraph word.' mid-word"});
    // Table cells themselves:
    cases.push_back({proseAt + md.find("| A | B |") - proseAt, "unused"});
    cases.pop_back();
    cases.push_back({md.find(" A "), "header cell 'A' text"});
    cases.push_back({md.find(" 1 "), "body cell '1' text"});

    for (const Case& c : cases) {
        TableCellRef ref;
        bool hit = TableCellAtOffset(doc, md, (uint32_t)c.off, &ref);
        std::printf("TableCellAtOffset(off=%zu '%s') -> %s", c.off, c.what,
                    hit ? "TRUE" : "false");
        if (hit) {
            std::printf("  tableIdx=%zu row=%zu col=%zu src=[%u..%u) sep=%d\n",
                        ref.tableIndex, ref.rowIndex, ref.columnIndex,
                        ref.srcOffset, ref.srcEnd, (int)ref.separatorRow);
        } else {
            std::printf("\n");
        }
    }

    // Worst case sweep: every offset in the doc. Count prose offsets that
    // report inTable.
    int falsePositives = 0;
    for (size_t off = 0; off < md.size(); ++off) {
        TableCellRef ref;
        if (TableCellAtOffset(doc, md, (uint32_t)off, &ref)) {
            bool inProse = (off < md.find("| A | B |")) ||
                           (off > md.find("Last paragraph word."));
            if (inProse) {
                if (falsePositives < 12) {
                    std::printf("LEAK: off=%zu (prose) -> cell src=[%u..%u)\n",
                                off, ref.srcOffset, ref.srcEnd);
                }
                falsePositives++;
            }
        }
    }
    std::printf("total prose offsets claimed to be in-table: %d / %zu\n",
                falsePositives, md.size());
    return 0;
}
