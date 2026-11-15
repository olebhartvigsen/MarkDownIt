// The per-column filters: what a query matches, which rows survive, what the
// column menu offers, and what the table document says about it.

#include "gtest_lite.h"

#include "csv.h"
#include "csvdocument.h"
#include "csvfilter.h"
#include "documentfile.h"

#include <string>
#include <vector>

namespace {

const char* kTable =
    "name,city,size\n"
    "Ole,Aarhus,12\n"
    "Anna,Aalborg,7\n"
    "Bo,Aarhus,31\n"
    "Carla,Copenhagen,12\n";

}  // namespace

TEST(CsvFilter, FoldLowersLettersAndKeepsAccentsApart) {
    EXPECT_EQ(csvfilter::Fold("Aarhus"), std::string("aarhus"));
    EXPECT_EQ(csvfilter::Fold("AARHUS"), std::string("aarhus"));
    // Danish letters fold as letters, not as two ASCII letters.
    EXPECT_EQ(csvfilter::Fold("\xC3\x86"), std::string("\xC3\xA6"));
    EXPECT_TRUE(csvfilter::Fold("ae") != csvfilter::Fold("\xC3\xA6"));
    // An accent is a different letter here, not the same one written twice.
    EXPECT_TRUE(csvfilter::Fold("a") != csvfilter::Fold("\xC3\xA5"));
}

TEST(CsvFilter, AQueryIsFoundInsideACellAndAValueMustEqualIt) {
    EXPECT_TRUE(csvfilter::Matches("Aarhus", csvfilter::Fold("aar")));
    EXPECT_TRUE(csvfilter::Matches("Aarhus", csvfilter::Fold("AAR")));
    EXPECT_FALSE(csvfilter::Matches("Aarhus", csvfilter::Fold("aarhus ")));
    EXPECT_FALSE(csvfilter::Matches("Aarhus", csvfilter::Fold("Aalborg")));

    EXPECT_TRUE(csvfilter::Matches("12", csvfilter::Fold("12"), true));
    EXPECT_FALSE(csvfilter::Matches("12.5", csvfilter::Fold("12"), true));
    EXPECT_TRUE(csvfilter::Matches("12.5", csvfilter::Fold("12"), false));
}

TEST(CsvFilter, OneFilterPerColumnAndAnEmptyOneClears) {
    csvfilter::State state;
    EXPECT_FALSE(state.Any());
    state.Set(1, "Aarhus");
    state.Set(2, "12", true);
    EXPECT_TRUE(state.Active(1));
    EXPECT_TRUE(state.Active(2));
    EXPECT_FALSE(state.Active(0));
    EXPECT_EQ(state.filters.size(), static_cast<size_t>(2));

    // Setting the same column again replaces, it does not add.
    state.Set(1, "Aalborg");
    EXPECT_EQ(state.filters.size(), static_cast<size_t>(2));
    EXPECT_EQ(state.For(1)->query, std::string("Aalborg"));

    state.Set(1, "");
    EXPECT_FALSE(state.Active(1));
    EXPECT_EQ(state.filters.size(), static_cast<size_t>(1));
    state.ClearAll();
    EXPECT_FALSE(state.Any());
}

TEST(CsvFilter, TheHeaderSurvivesWhateverTheFiltersSay) {
    const csv::Table table = csv::Parse(kTable);
    ASSERT_TRUE(table.hasHeader);

    csvfilter::State state;
    state.Set(1, "Aarhus");
    const std::vector<std::size_t> rows = csvfilter::VisibleRows(table, state);
    // The header and the two Aarhus records.
    ASSERT_EQ(rows.size(), static_cast<size_t>(3));
    EXPECT_EQ(rows[0], static_cast<std::size_t>(0));
    EXPECT_EQ(rows[1], static_cast<std::size_t>(1));
    EXPECT_EQ(rows[2], static_cast<std::size_t>(3));

    // Two filters are an AND.
    state.Set(2, "12", true);
    const std::vector<std::size_t> both = csvfilter::VisibleRows(table, state);
    ASSERT_EQ(both.size(), static_cast<size_t>(2));
    EXPECT_EQ(both[1], static_cast<std::size_t>(1));

    // Nothing matches: the header is still there, so the buttons are too.
    state.Set(2, "99", true);
    const std::vector<std::size_t> none = csvfilter::VisibleRows(table, state);
    ASSERT_EQ(none.size(), static_cast<size_t>(1));
    EXPECT_EQ(none[0], static_cast<std::size_t>(0));
}

TEST(CsvFilter, ValuesCountAndOrderAndSkipTheHeader) {
    const csv::Table table = csv::Parse(kTable);
    const std::vector<csvfilter::Value> cities =
        csvfilter::Values(table, 1, 10);
    ASSERT_EQ(cities.size(), static_cast<size_t>(3));
    // The most frequent first, then the alphabetical tie break.
    EXPECT_EQ(cities[0].text, std::string("Aarhus"));
    EXPECT_EQ(cities[0].count, static_cast<size_t>(2));
    EXPECT_EQ(cities[1].text, std::string("Aalborg"));
    EXPECT_EQ(cities[2].text, std::string("Copenhagen"));

    // The most common value leads, and the ties read in numeric order, so a
    // menu of figures reads like a column of figures.
    const std::vector<csvfilter::Value> sizes = csvfilter::Values(table, 2, 10);
    ASSERT_EQ(sizes.size(), static_cast<size_t>(3));
    EXPECT_EQ(sizes[0].text, std::string("12"));
    EXPECT_EQ(sizes[0].count, static_cast<size_t>(2));
    EXPECT_EQ(sizes[1].text, std::string("7"));
    EXPECT_EQ(sizes[2].text, std::string("31"));

    // The limit is the menu's, not the table's.
    EXPECT_EQ(csvfilter::Values(table, 1, 2).size(), static_cast<size_t>(2));
}

TEST(CsvFilter, TheDocumentShowsOnlyTheRowsTheFiltersKeep) {
    csvfilter::State state;
    state.Set(1, "Aarhus");
    const Document doc = csvdoc::BuildDocument(kTable, "people.csv", state);
    ASSERT_EQ(doc.nodes.size(), static_cast<size_t>(2));
    EXPECT_EQ(doc.nodes[0].block, BlockKind::Table);
    // The header and the two records that pass.
    EXPECT_EQ(doc.nodes[0].rows.size(), static_cast<size_t>(3));
    EXPECT_TRUE(doc.nodes[0].rows[0].cells[0].isHeader);
    EXPECT_EQ(doc.nodes[0].rows[1].cells[0].text, std::u32string(U"Ole"));
    EXPECT_EQ(doc.nodes[0].rows[2].cells[0].text, std::u32string(U"Bo"));
    // The column's button is filled, because that column is filtered.
    ASSERT_EQ(doc.nodes[0].tableFilterActive.size(), static_cast<size_t>(3));
    EXPECT_FALSE(doc.nodes[0].tableFilterActive[0]);
    EXPECT_TRUE(doc.nodes[0].tableFilterActive[1]);
    EXPECT_FALSE(doc.nodes[0].tableFilterActive[2]);
    // And the line under the table says what is missing.
    ASSERT_TRUE(!doc.nodes[1].children.empty());
    const std::u32string notice = doc.nodes[1].children[0].text;
    EXPECT_TRUE(notice.find(U"Showing 2 of 4 rows.") != std::u32string::npos);
}

TEST(CsvFilter, AFilterThatMatchesNothingSaysSo) {
    csvfilter::State state;
    state.Set(1, "nowhere");
    const Document doc = csvdoc::BuildDocument(kTable, "people.csv", state);
    ASSERT_EQ(doc.nodes.size(), static_cast<size_t>(2));
    EXPECT_EQ(doc.nodes[0].rows.size(), static_cast<size_t>(1));  // header only
    const std::u32string notice = doc.nodes[1].children[0].text;
    EXPECT_TRUE(notice.find(U"Showing 0 of 4 rows.") != std::u32string::npos);
    EXPECT_TRUE(notice.find(U"No row matches") != std::u32string::npos);
}

TEST(CsvFilter, TheMenuOffersClearThenTheValuesWithTheActiveOneTicked) {
    csvfilter::State state;
    state.Set(1, "Aarhus", true);
    const std::vector<csvdoc::FilterMenuItem> menu =
        csvdoc::FilterMenu(kTable, "people.csv", 1, state, 10);
    ASSERT_TRUE(menu.size() >= 4);
    EXPECT_TRUE(menu[0].clear);
    EXPECT_FALSE(menu[0].checked);  // a column is filtered, so "show all" is not on
    EXPECT_TRUE(menu[1].separator);
    // A value that appears more than once says so, so a menu of a wide
    // column tells the reader which values carry the table.
    EXPECT_EQ(menu[1].label, std::string("Aarhus (2)"));
    EXPECT_TRUE(menu[1].checked);
    EXPECT_EQ(menu[1].query, std::string("Aarhus"));
    EXPECT_FALSE(menu[2].checked);

    // With no filter, the clear row is the ticked one.
    const std::vector<csvdoc::FilterMenuItem> plain =
        csvdoc::FilterMenu(kTable, "people.csv", 1, csvfilter::State{}, 10);
    ASSERT_TRUE(!plain.empty());
    EXPECT_TRUE(plain[0].checked);
}
