// Outline model tests: heading collection from a parsed Document,
// collapse filtering and active-heading lookup, all without a window
// and without any Windows API. The local gate builds this file together
// with the shared test main:
//   g++ -std=c++17 -Wall -Wextra -I tests -I src -I third_party/md4c
//       tests/test_main.cpp tests/outline_test.cpp src/outline.cpp
//       src/parser.cpp third_party/md4c/md4c.c

#include "gtest_lite.h"
#include "outline.h"
#include "dom.h"
#include "parser.h"

#include <string>

namespace {

Document Parse(const char* md) {
    Document doc;
    ParseMarkdown(std::string(md), doc);
    return doc;
}

}  // namespace

TEST(Outline, CollectsLevelTextAndOffset) {
    Document doc = Parse("# Title\n\n## Sub\n\ntext\n\n### Deep\n");
    std::vector<OutlineItem> items = CollectHeadings(doc);
    ASSERT_EQ(3u, items.size());
    EXPECT_EQ(1, items[0].level);
    EXPECT_EQ(U"Title", items[0].text);
    EXPECT_EQ(2, items[1].level);
    EXPECT_EQ(U"Sub", items[1].text);
    EXPECT_EQ(3, items[2].level);
    EXPECT_EQ(U"Deep", items[2].text);
}

TEST(Outline, OffsetIsTheHeadingStartInSource) {
    // contentOffset lands after the ATX markers: "# Title" at 0 makes the
    // heading text begin at 2; "## Sub" at 9 begins its text at 12.
    Document doc = Parse("# Title\n\n## Sub\n");
    std::vector<OutlineItem> items = CollectHeadings(doc);
    ASSERT_EQ(2u, items.size());
    EXPECT_EQ(2u, items[0].offset);
    EXPECT_EQ(12u, items[1].offset);
}

TEST(Outline, SkipsNonHeadingBlocks) {
    Document doc = Parse(
        "plain\n\n- list\n\n> quote\n\n    indented code\n");
    std::vector<OutlineItem> items = CollectHeadings(doc);
    EXPECT_TRUE(items.empty());
}

TEST(Outline, EmptyDocumentYieldsNoHeadings) {
    Document doc;
    std::vector<OutlineItem> items = CollectHeadings(doc);
    EXPECT_TRUE(items.empty());
}

TEST(Outline, SetextAndInlineFormattingSimplify) {
    Document doc = Parse("Title\n=====\n\n## **bold** and `code`\n");
    std::vector<OutlineItem> items = CollectHeadings(doc);
    ASSERT_EQ(2u, items.size());
    EXPECT_EQ(1, items[0].level);  // setext H1 via the underline
    EXPECT_EQ(U"Title", items[0].text);
    EXPECT_EQ(2, items[1].level);
    // Inline formatting contributes its plain text only.
    EXPECT_EQ(U"bold and code", ConcatenateInlineText(items[1]));
}

TEST(Outline, LinkContributesTextNotUrl) {
    Document doc = Parse("# See [the guide](https://example.com)\n");
    std::vector<OutlineItem> items = CollectHeadings(doc);
    ASSERT_EQ(1u, items.size());
    EXPECT_EQ(U"See the guide", ConcatenateInlineText(items[0]));
}

TEST(Outline, VisibleItemsRespectCollapsedBranches) {
    std::vector<OutlineItem> items = {
        {1, 0, U"a"}, {2, 10, U"b"}, {3, 20, U"c"},
        {2, 30, U"d"}, {1, 40, U"e"}
    };
    OutlineCollapse st;
    st.Toggle(1);  // collapse "b": hides "c" only (its subtree)
    std::vector<int> vis = VisibleItems(items, st);
    ASSERT_EQ(4u, vis.size());
    EXPECT_EQ(0, vis[0]);
    EXPECT_EQ(1, vis[1]);
    EXPECT_EQ(3, vis[2]);
    EXPECT_EQ(4, vis[3]);

    st.Toggle(0);  // collapse "a": hides "b", "c" and "d" down to next H1
    vis = VisibleItems(items, st);
    ASSERT_EQ(2u, vis.size());
    EXPECT_EQ(0, vis[0]);
    EXPECT_EQ(4, vis[1]);
}

TEST(Outline, CollapseOutsideRangeIsHarmless) {
    std::vector<OutlineItem> items = { {1, 0, U"a"} };
    OutlineCollapse st;
    st.Toggle(5);  // no such index; the state records it, nothing breaks
    std::vector<int> vis = VisibleItems(items, st);
    ASSERT_EQ(1u, vis.size());
}

TEST(Outline, ItemIndexForOffsetOwnership) {
    std::vector<OutlineItem> items = {
        {1, 0, U"a"}, {2, 10, U"b"}, {2, 30, U"d"}, {1, 40, U"e"}
    };
    // A heading owns every offset from its own start up to the next
    // heading's start (spec 6.1/21: the section the cursor is in).
    EXPECT_EQ(0, ItemIndexForOffset(items, 0));
    EXPECT_EQ(0, ItemIndexForOffset(items, 9));
    EXPECT_EQ(1, ItemIndexForOffset(items, 15));
    EXPECT_EQ(2, ItemIndexForOffset(items, 32));
    EXPECT_EQ(2, ItemIndexForOffset(items, 39));
    EXPECT_EQ(3, ItemIndexForOffset(items, 40));
    // The last section owns the document tail (spec 6.1: the heading of
    // the section the cursor is in stays active past the last heading).
    EXPECT_EQ(3, ItemIndexForOffset(items, 200));
    EXPECT_EQ(-1, ItemIndexForOffset({}, 0));
    EXPECT_EQ(-1, ItemIndexForOffset({}, 500));
}
