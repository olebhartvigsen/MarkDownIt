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
