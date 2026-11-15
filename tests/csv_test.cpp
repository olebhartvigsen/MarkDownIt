// The CSV reader: RFC 4180 quoting, the line ends and marks files carry, the
// delimiter sniff, the header guess and the caps. Every case here is a shape
// a real export has been seen in.

#include "csv.h"
#include "gtest_lite.h"

#include <string>
#include <vector>

namespace {

std::string Row(const csv::Table& table, std::size_t row) {
    std::string joined;
    for (const std::string& cell : table.rows[row]) {
        if (!joined.empty()) joined += "|";
        joined += cell;
    }
    return joined;
}

}  // namespace

TEST(Csv, QuotedFieldsKeepTheirOwnDelimitersAndQuotes) {
    const csv::Table table =
        csv::Parse("name,note\n\"Ole, obh\",\"he said \"\"hi\"\"\"\n");
    // A header and one record; the quote is data and does not end a field.
    ASSERT_EQ(table.rows.size(), 2u);
    EXPECT_EQ(Row(table, 1), "Ole, obh|he said \"hi\"");
    EXPECT_EQ(table.delimiter, ',');
    EXPECT_TRUE(table.hasHeader);
}

TEST(Csv, AQuotedFieldMayHoldALineEnd) {
    const csv::Table table = csv::Parse("a,b\n\"one\ntwo\",three\n");
    ASSERT_EQ(table.rows.size(), 2u);
    EXPECT_EQ(table.rows[1][0], "one\ntwo");
    EXPECT_EQ(table.rows[1][1], "three");
}

TEST(Csv, EveryLineEndEndsARecord) {
    const csv::Table lf = csv::Parse("a,b\n1,2\n");
    const csv::Table crlf = csv::Parse("a,b\r\n1,2\r\n");
    const csv::Table cr = csv::Parse("a,b\r1,2\r");
    ASSERT_EQ(lf.rows.size(), 2u);
    ASSERT_EQ(crlf.rows.size(), 2u);
    ASSERT_EQ(cr.rows.size(), 2u);
    EXPECT_EQ(Row(lf, 1), Row(crlf, 1));
    EXPECT_EQ(Row(lf, 1), Row(cr, 1));
}

TEST(Csv, TheByteOrderMarkIsNotPartOfTheFirstCell) {
    const csv::Table table = csv::Parse("\xEF\xBB\xBFname,city\nOle,Aarhus\n");
    ASSERT_EQ(table.rows.size(), 2u);
    EXPECT_EQ(table.rows[0][0], "name");
    EXPECT_TRUE(table.hasHeader);
}

TEST(Csv, AUtf16FileIsReportedRatherThanMangled) {
    // Built byte by byte: a literal would have its hex escape eat the next
    // character and its NULs end the string.
    std::string raw;
    raw.push_back('\xFF');
    raw.push_back('\xFE');
    raw.push_back('a');
    raw.push_back('\0');
    raw.push_back(',');
    raw.push_back('\0');
    raw.push_back('b');
    raw.push_back('\0');
    const csv::Table table = csv::Parse(raw);
    EXPECT_TRUE(table.unsupportedEncoding);
    EXPECT_TRUE(table.rows.empty());
}

TEST(Csv, RaggedRowsArePaddedToTheWidest) {
    const csv::Table table = csv::Parse("a,b,c\n1,2\n3,4,5,6\n");
    ASSERT_EQ(table.rows.size(), 3u);
    for (const std::vector<std::string>& row : table.rows) {
        EXPECT_EQ(row.size(), 4u);
    }
    EXPECT_EQ(table.rows[1][2], "");
}

TEST(Csv, TheDelimiterIsSniffedFromTheRecords) {
    EXPECT_EQ(csv::SniffDelimiter("a;b;c\n1;2;3\n"), ';');
    EXPECT_EQ(csv::SniffDelimiter("a\tb\tc\n1\t2\t3\n"), '\t');
    EXPECT_EQ(csv::SniffDelimiter("a|b|c\n1|2|3\n"), '|');
    EXPECT_EQ(csv::SniffDelimiter("a,b,c\n1,2,3\n"), ',');

    const csv::Table semicolons = csv::Parse("a;b;c\n1;2;3\n");
    EXPECT_EQ(semicolons.delimiter, ';');
    EXPECT_EQ(semicolons.rows[1][2], "3");
}

TEST(Csv, AHintFromTheExtensionBeatsTheSniff) {
    // A .tsv file whose cells happen to hold commas is still tab separated.
    const csv::Table table = csv::Parse("a\tb\n1,5\t2,5\n", '\t');
    EXPECT_EQ(table.delimiter, '\t');
    ASSERT_EQ(table.rows.size(), 2u);
    EXPECT_EQ(table.rows[1][1], "2,5");
}

TEST(Csv, ANumberIsPlainDigitsAndOnePoint) {
    EXPECT_TRUE(csv::LooksNumeric("12"));
    EXPECT_TRUE(csv::LooksNumeric("-3.5"));
    EXPECT_TRUE(csv::LooksNumeric("+0.25"));
    EXPECT_TRUE(csv::LooksNumeric("1e-3"));
    EXPECT_TRUE(csv::LooksNumeric(" 42 "));
    EXPECT_FALSE(csv::LooksNumeric(""));
    EXPECT_FALSE(csv::LooksNumeric("12,5"));
    EXPECT_FALSE(csv::LooksNumeric("1,234"));
    EXPECT_FALSE(csv::LooksNumeric("$99"));
    EXPECT_FALSE(csv::LooksNumeric("12%"));
    EXPECT_FALSE(csv::LooksNumeric("1.2.3"));
    EXPECT_FALSE(csv::LooksNumeric("abc"));
}

TEST(Csv, TheHeaderRowIsANameRowAndNotData) {
    EXPECT_TRUE(csv::LooksLikeHeader({{"name", "city"}, {"Ole", "Aarhus"}}));
    // A row of numbers is data, so there is no header.
    EXPECT_FALSE(csv::LooksLikeHeader({{"1", "2"}, {"3", "4"}}));
    // A repeated name is data too.
    EXPECT_FALSE(csv::LooksLikeHeader({{"a", "a"}, {"1", "2"}}));
    // An empty name cell is not a name.
    EXPECT_FALSE(csv::LooksLikeHeader({{"name", ""}, {"1", "2"}}));
    // One record alone has nothing to name.
    EXPECT_FALSE(csv::LooksLikeHeader({{"name", "city"}}));
}

TEST(Csv, TheRowCapKeepsCounting) {
    std::string text = "a,b\n";
    for (std::size_t i = 0; i < csv::kMaxRows + 5; ++i) {
        text += std::to_string(i) + ",x\n";
    }
    const csv::Table table = csv::Parse(text);
    EXPECT_EQ(table.rows.size(), csv::kMaxRows);
    EXPECT_TRUE(table.rowsTruncated);
    // The header plus the rows the file held, five of them past the cap.
    EXPECT_EQ(table.sourceRows, csv::kMaxRows + 6);
}

TEST(Csv, TheColumnCapCutsEachRow) {
    std::string text;
    for (int column = 0; column < 70; ++column) {
        if (column) text += ",";
        text += "c" + std::to_string(column);
    }
    text += "\n";
    const csv::Table table = csv::Parse(text);
    EXPECT_TRUE(table.columnsTruncated);
    ASSERT_EQ(table.rows.size(), 1u);
    EXPECT_EQ(table.rows[0].size(), csv::kMaxColumns);
}

TEST(Csv, ACellOverTheCapIsCutAndReported) {
    std::string cell(csv::kMaxCellBytes + 10, 'x');
    const csv::Table table = csv::Parse("a\n" + cell + "\n");
    EXPECT_TRUE(table.cellsTruncated);
    ASSERT_EQ(table.rows.size(), 2u);
    EXPECT_EQ(table.rows[1][0].size(), csv::kMaxCellBytes);
}

TEST(Csv, AnEmptyFileIsAnEmptyTable) {
    const csv::Table table = csv::Parse("");
    EXPECT_TRUE(table.rows.empty());
    EXPECT_FALSE(table.hasHeader);
    EXPECT_FALSE(table.rowsTruncated);
}
