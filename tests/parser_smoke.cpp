#include "gtest_lite.h"
#include "parser.h"

// Smoke test: three top-level blocks (H1, H2, Paragraph).
TEST(ParserSmoke, HeadingsAndParagraph) {
    Document doc;
    bool ok = ParseMarkdown("# Title\n## Sub\npara", doc);
    ASSERT_TRUE(ok);
    EXPECT_EQ(doc.nodes.size(), 3u);

    EXPECT_EQ(static_cast<int>(doc.nodes[0].block),
              static_cast<int>(BlockKind::Heading));
    EXPECT_EQ(doc.nodes[0].level, 1);

    EXPECT_EQ(static_cast<int>(doc.nodes[1].block),
              static_cast<int>(BlockKind::Heading));
    EXPECT_EQ(doc.nodes[1].level, 2);

    EXPECT_EQ(static_cast<int>(doc.nodes[2].block),
              static_cast<int>(BlockKind::Paragraph));
}

// Verify heading text is captured as a UTF-32 inline child.
TEST(ParserSmoke, HeadingText) {
    Document doc;
    bool ok = ParseMarkdown("# Hello", doc);
    ASSERT_TRUE(ok);
    EXPECT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].children.size(), 1u);
    // "Hello" as 5 UTF-32 code points
    EXPECT_EQ(doc.nodes[0].children[0].text.size(), 5u);
    EXPECT_EQ(doc.nodes[0].children[0].text[0], U'H');
}

// Title captured from the first H1.
TEST(ParserSmoke, TitleCapture) {
    Document doc;
    bool ok = ParseMarkdown("# My Doc\n\nSome body text.", doc);
    ASSERT_TRUE(ok);
    EXPECT_EQ(doc.title.size(), 6u);
    EXPECT_EQ(doc.title[0], U'M');
}

// Paragraph with inline emphasis and strong.
TEST(ParserSmoke, InlineSpans) {
    Document doc;
    bool ok = ParseMarkdown("This has *italic* and **bold**.", doc);
    ASSERT_TRUE(ok);
    EXPECT_EQ(doc.nodes.size(), 1u);
    // Should have multiple inline children.
    EXPECT_TRUE(doc.nodes[0].children.size() >= 5u);

    bool found_em = false, found_strong = false;
    for (const auto& ib : doc.nodes[0].children) {
        if (ib.em) found_em = true;
        if (ib.strong) found_strong = true;
    }
    EXPECT_TRUE(found_em);
    EXPECT_TRUE(found_strong);
}

// Code block: raw text goes to node.raw.
TEST(ParserSmoke, CodeBlock) {
    Document doc;
    bool ok = ParseMarkdown("```c++\nint x = 0;\n```", doc);
    ASSERT_TRUE(ok);
    EXPECT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(static_cast<int>(doc.nodes[0].block),
              static_cast<int>(BlockKind::CodeBlock));
    EXPECT_FALSE(doc.nodes[0].raw.empty());
}

// Link: URL captured.
TEST(ParserSmoke, Link) {
    Document doc;
    bool ok = ParseMarkdown("[click](https://example.com)", doc);
    ASSERT_TRUE(ok);
    EXPECT_EQ(doc.nodes.size(), 1u);
    bool found_link = false;
    for (const auto& ib : doc.nodes[0].children) {
        if (ib.kind == InlineKind::Link) {
            found_link = true;
            EXPECT_EQ(ib.url, std::string("https://example.com"));
        }
    }
    EXPECT_TRUE(found_link);
}

// Empty string produces no nodes.
TEST(ParserSmoke, Empty) {
    Document doc;
    bool ok = ParseMarkdown("", doc);
    ASSERT_TRUE(ok);
    EXPECT_EQ(doc.nodes.size(), 0u);
}

// Source offsets on nodes.
TEST(ParserOffsets, HeadingCarriesSourceRange) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("# Title\n\nBody text\n", doc));
    ASSERT_EQ(doc.nodes.size(), 2u);
    EXPECT_EQ(doc.nodes[0].srcOffset, 0u);
    EXPECT_EQ(doc.nodes[0].contentOffset, 2u);   // after "# "
    EXPECT_EQ(doc.nodes[1].srcOffset, 9u);
}

// Source offsets on inline spans.
TEST(ParserOffsets, InlineSpanHasOffset) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("# Hello", doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    ASSERT_EQ(doc.nodes[0].children.size(), 1u);
    // "Hello" starts at byte offset 2 in the source (after "# ")
    EXPECT_EQ(doc.nodes[0].children[0].srcOffset, 2u);
    EXPECT_EQ(doc.nodes[0].children[0].srcLength, 5u);
}

