// The bridge from a CSV file to the node list the table view draws: one table
// node, the header row flagged, the numeric columns right, and a notice when
// a cap left something out.

#include "csvdocument.h"
#include "gtest_lite.h"

#include <string>

namespace {

std::string Utf8(const std::u32string& text) {
    std::string out;
    for (char32_t code : text) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    return out;
}

const Node* FirstTable(const Document& doc) {
    for (const Node& node : doc.nodes) {
        if (node.block == BlockKind::Table) return &node;
    }
    return nullptr;
}

}  // namespace

TEST(CsvDocument, ThePathDecidesTheKind) {
    EXPECT_TRUE(csvdoc::IsCsvPath("data.csv"));
    EXPECT_TRUE(csvdoc::IsCsvPath("DATA.CSV"));
    EXPECT_TRUE(csvdoc::IsCsvPath("/tmp/export.tsv"));
    EXPECT_TRUE(csvdoc::IsCsvPath("sheet.tab"));
    EXPECT_FALSE(csvdoc::IsCsvPath("notes.md"));
    EXPECT_FALSE(csvdoc::IsCsvPath("diagram.svg"));
    EXPECT_FALSE(csvdoc::IsCsvPath("csv"));
}

TEST(CsvDocument, TheTabExtensionsAskForATab) {
    EXPECT_EQ(csvdoc::DelimiterHintForPath("data.tsv"), '\t');
    EXPECT_EQ(csvdoc::DelimiterHintForPath("data.tab"), '\t');
    // A .csv file may be comma or semicolon, so it is sniffed.
    EXPECT_EQ(csvdoc::DelimiterHintForPath("data.csv"), 0);
}

TEST(CsvDocument, AFileBecomesOneTableNode) {
    const Document doc = csvdoc::BuildDocument(
        "name,price\nOle,12.5\nAnna,3\n", "prices.csv");
    ASSERT_EQ(doc.nodes.size(), 1u);
    const Node& table = doc.nodes[0];
    EXPECT_EQ(table.block, BlockKind::Table);
    EXPECT_TRUE(table.tableMonospace);
    ASSERT_EQ(table.rows.size(), 3u);
    EXPECT_TRUE(table.rows[0].cells[0].isHeader);
    EXPECT_FALSE(table.rows[1].cells[0].isHeader);
    EXPECT_EQ(Utf8(table.rows[1].cells[0].text), "Ole");
    EXPECT_EQ(Utf8(table.rows[1].cells[1].text), "12.5");
    // The name column is text, the price column is numbers.
    ASSERT_EQ(table.aligns.size(), 2u);
    EXPECT_EQ(table.aligns[0], ColumnAlign::Left);
    EXPECT_EQ(table.aligns[1], ColumnAlign::Right);
}

TEST(CsvDocument, AFileWithoutAHeaderIsAllData) {
    const Document doc = csvdoc::BuildDocument("1,2\n3,4\n", "numbers.csv");
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].rows.size(), 2u);
    EXPECT_FALSE(doc.nodes[0].rows[0].cells[0].isHeader);
    EXPECT_TRUE(doc.nodes[0].rows[0].cells[0].isHeader == false);
    // Both columns are numbers.
    EXPECT_EQ(doc.nodes[0].aligns[0], ColumnAlign::Right);
    EXPECT_EQ(doc.nodes[0].aligns[1], ColumnAlign::Right);
}

TEST(CsvDocument, FourRowsInFiveDecideANumericColumn) {
    const Document doc = csvdoc::BuildDocument(
        "v\n1\n2\n3\n4\nn/a\n", "mixed.csv");
    const Node* table = FirstTable(doc);
    ASSERT_TRUE(table != nullptr); // the header is v, then four numbers
    EXPECT_EQ(table->aligns[0], ColumnAlign::Right);

    const Document thin = csvdoc::BuildDocument(
        "v\n1\n2\nn/a\nn/a\n", "mixed.csv");
    ASSERT_TRUE(FirstTable(thin) != nullptr);
    EXPECT_EQ(FirstTable(thin)->aligns[0], ColumnAlign::Left);
}

TEST(CsvDocument, ACappedFileSaysWhatItLeftOut) {
    std::string text = "a,b\n";
    for (std::size_t i = 0; i < csv::kMaxRows + 3; ++i) {
        text += std::to_string(i) + ",x\n";
    }
    const Document doc = csvdoc::BuildDocument(text, "big.csv");
    ASSERT_EQ(doc.nodes.size(), 2u);
    EXPECT_EQ(doc.nodes[0].block, BlockKind::Table);
    EXPECT_EQ(doc.nodes[1].block, BlockKind::Paragraph);
    const std::string notice = Utf8(doc.nodes[1].children[0].text);
    EXPECT_TRUE(notice.find("Showing the first 20,000 of 20,004 rows.") !=
                std::string::npos);
}

TEST(CsvDocument, AnEmptyFileStillHasAPage) {
    const Document doc = csvdoc::BuildDocument("", "empty.csv");
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].block, BlockKind::Paragraph);
    EXPECT_EQ(Utf8(doc.nodes[0].children[0].text), "This file holds no rows.");
}

TEST(CsvDocument, AnUnreadableEncodingIsExplained) {
    std::string raw;
    raw.push_back('\xFF');
    raw.push_back('\xFE');
    raw.push_back('a');
    raw.push_back('\0');
    raw.push_back(',');
    raw.push_back('\0');
    raw.push_back('b');
    raw.push_back('\0');
    const Document doc = csvdoc::BuildDocument(raw, "old.csv");
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].block, BlockKind::Paragraph);
    EXPECT_TRUE(Utf8(doc.nodes[0].children[0].text).find("not UTF-8") !=
                std::string::npos);
}
