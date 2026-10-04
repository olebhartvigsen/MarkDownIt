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

// Even an empty document owns one logical paragraph and caret position.
TEST(ParserSmoke, EmptyDocumentHasVirtualParagraph) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("", doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(static_cast<int>(doc.nodes[0].block),
              static_cast<int>(BlockKind::Paragraph));
    EXPECT_TRUE(doc.nodes[0].virtualEmptyParagraph);
    EXPECT_EQ(doc.nodes[0].contentOffset, 0u);
}

TEST(ParserOffsets, ExtraBlankLinesCreateVirtualParagraph) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("first\n\n\n\nsecond", doc));
    ASSERT_EQ(doc.nodes.size(), 3u);
    EXPECT_FALSE(doc.nodes[0].virtualEmptyParagraph);
    EXPECT_TRUE(doc.nodes[1].virtualEmptyParagraph);
    EXPECT_EQ(doc.nodes[1].contentOffset, 7u);
    EXPECT_FALSE(doc.nodes[2].virtualEmptyParagraph);
    EXPECT_EQ(doc.nodes[2].contentOffset, 9u);
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

// Inline raw HTML <br> variants become a newline, not literal tag text.
TEST(ParserSmoke, BrTagBreaksLine) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("line one<BR>line two", doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    // Text, newline, text: three inline children.
    ASSERT_EQ(doc.nodes[0].children.size(), 3u);
    EXPECT_EQ(doc.nodes[0].children[0].text, std::u32string(U"line one"));
    EXPECT_EQ(doc.nodes[0].children[1].text, std::u32string(1, U'\n'));
    EXPECT_EQ(doc.nodes[0].children[2].text, std::u32string(U"line two"));
    // Source offsets stay on the tag itself: caret mapped at the tag start.
    EXPECT_EQ(doc.nodes[0].children[1].srcOffset, 8u);
    EXPECT_EQ(doc.nodes[0].children[1].srcLength, 4u);
}

TEST(ParserSmoke, BrVariantsBreakLines) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("a<br>b<br />c", doc));
    ASSERT_EQ(doc.nodes[0].children.size(), 5u);
    EXPECT_EQ(doc.nodes[0].children[1].text, std::u32string(1, U'\n'));
    EXPECT_EQ(doc.nodes[0].children[3].text, std::u32string(1, U'\n'));
}

// Br inside a heading breaks too.
TEST(ParserSmoke, BrTagBreaksLineInHeading) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("# Head<BR>Line", doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    ASSERT_EQ(doc.nodes[0].children.size(), 3u);
    EXPECT_EQ(doc.nodes[0].children[1].text, std::u32string(1, U'\n'));
}

// Raw HTML that is not a br variant still passes through as literal text.
TEST(ParserSmoke, OtherRawHtmlStaysLiteral) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("a <span>x</span> b", doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    std::u32string text;
    for (const auto& child : doc.nodes[0].children) text += child.text;
    EXPECT_TRUE(text.find(U'<') != std::u32string::npos);
    EXPECT_TRUE(text.find(U"span") != std::u32string::npos);
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

// Fence info string (lang) captured into Node::lang.
TEST(ParserSmoke, FenceLang) {
    Document doc;
    bool ok = ParseMarkdown("```mermaid\ngraph TD\n```\n", doc);
    ASSERT_TRUE(ok);
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].lang, std::string("mermaid"));
}

// Fence info string with extra options: "mermaid theme=dark".
TEST(ParserSmoke, FenceLangTruncated) {
    Document doc;
    bool ok = ParseMarkdown("```mermaid theme=dark\nA --> B\n```\n", doc);
    ASSERT_TRUE(ok);
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].lang, std::string("mermaid"));
}

// No info string: lang stays empty.
TEST(ParserSmoke, FenceLangEmpty) {
    Document doc;
    bool ok = ParseMarkdown("```\nplain code\n```\n", doc);
    ASSERT_TRUE(ok);
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].lang, std::string(""));
}

// HTML entities decode to Unicode scalars and reject invalid scalar values.
TEST(ParserSmoke, EntityDecoding) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("&copy; &#x1F600; &#x110000;", doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    std::u32string text;
    for (const auto& child : doc.nodes[0].children) text += child.text;
    EXPECT_TRUE(text.find(U'\u00A9') != std::u32string::npos);
    EXPECT_TRUE(text.find(U'\U0001F600') != std::u32string::npos);
    EXPECT_TRUE(text.find(U'\uFFFD') != std::u32string::npos);
}

// Table cells retain the complete source span for entities and links.
TEST(ParserSmoke, TableCellSourceMapping) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("| A | B |\n|---|---|\n| &amp; | [go](https://example.com) |", doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    ASSERT_EQ(doc.nodes[0].rows.size(), 2u);
    ASSERT_EQ(doc.nodes[0].rows[1].cells.size(), 2u);
    const auto& entity = doc.nodes[0].rows[1].cells[0];
    const auto& link = doc.nodes[0].rows[1].cells[1];
    EXPECT_TRUE(entity.srcEnd > entity.srcOffset);
    EXPECT_EQ(entity.u16ToSrc.size(), entity.u16ToSrcEnd.size());
    EXPECT_TRUE(!link.links.empty());
}



TEST(ParserOffsets, ParagraphRangeIncludesTrailingInlineSyntax) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("**bold** [link](https://example.com)", doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].srcOffset, 0u);
    EXPECT_EQ(doc.nodes[0].srcLength, 36u);
    EXPECT_TRUE(doc.nodes[0].srcOffset + doc.nodes[0].srcLength == 36u);
}

// Table guidelines §60: the validator accepts a wellformed table.
TEST(ParserSmoke, ValidateDocumentAcceptsWellformedTable) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("| A | B | C |\n|---|---|---|\n| 1 | 2 | 3 |\n| 4 | 5 | 6 |", doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    ASSERT_TRUE(doc.nodes[0].block == BlockKind::Table);
    std::vector<std::string> errors;
    EXPECT_TRUE(ValidateDocument(doc, &errors));
    EXPECT_TRUE(errors.empty());
    // Null errors pointer is allowed.
    EXPECT_TRUE(ValidateDocument(doc, nullptr));
}

// Table guidelines §60: md4c pads ragged source rows, so a ragged
// source table still parses into a uniform, valid model.
TEST(ParserSmoke, ValidateDocumentAcceptsRaggedSourceAfterParse) {
    Document doc;
    // Middle row has one column in source; the parser must normalize.
    ASSERT_TRUE(ParseMarkdown("| A | B |\n|---|---|\n| only |\n| 1 | 2 |", doc));
    ASSERT_EQ(doc.nodes.size(), 1u);
    std::vector<std::string> errors;
    EXPECT_TRUE(ValidateDocument(doc, &errors));
    EXPECT_EQ(errors.size(), 0u);
}

// Table guidelines §60: hand-built model violations are rejected, so
// the validator guards the invariants against future model changes.
TEST(ParserSmoke, ValidateDocumentRejectsBrokenTableInvariants) {
    // Ragged model: second row has one cell fewer.
    {
        Document doc;
        Node tbl;
        tbl.block = BlockKind::Table;
        tbl.srcOffset = 0;
        tbl.srcLength = 64;
        TableRow header;
        header.cells.resize(2);
        header.cells[0].srcOffset = 2; header.cells[0].srcEnd = 5;
        header.cells[1].srcOffset = 8; header.cells[1].srcEnd = 11;
        TableRow ragged;
        ragged.cells.resize(1);
        ragged.cells[0].srcOffset = 20; ragged.cells[0].srcEnd = 24;
        tbl.rows.push_back(header);
        tbl.rows.push_back(ragged);
        doc.nodes.push_back(tbl);
        std::vector<std::string> errors;
        EXPECT_FALSE(ValidateDocument(doc, &errors));
        ASSERT_GE(errors.size(), 1u);
        EXPECT_TRUE(errors[0].find("expected 2") != std::string::npos);
    }
    // Cell source span outside the table's own range.
    {
        Document doc;
        Node tbl;
        tbl.block = BlockKind::Table;
        tbl.srcOffset = 0;
        tbl.srcLength = 32;
        TableRow row;
        row.cells.resize(1);
        row.cells[0].srcOffset = 40;
        row.cells[0].srcEnd = 48;
        tbl.rows.push_back(row);
        doc.nodes.push_back(tbl);
        std::vector<std::string> errors;
        EXPECT_FALSE(ValidateDocument(doc, &errors));
        ASSERT_GE(errors.size(), 1u);
        EXPECT_TRUE(errors[0].find("outside the table range") != std::string::npos);
    }
    // Table without rows.
    {
        Document doc;
        Node tbl;
        tbl.block = BlockKind::Table;
        tbl.srcOffset = 0;
        tbl.srcLength = 16;
        doc.nodes.push_back(tbl);
        std::vector<std::string> errors;
        EXPECT_FALSE(ValidateDocument(doc, &errors));
        ASSERT_GE(errors.size(), 1u);
        EXPECT_TRUE(errors[0].find("has no rows") != std::string::npos);
    }
}


// --- Standalone-SVG fence behaviour -------------------------------------
// A ```svg fence is a picture only when the document is a standalone .svg
// file. In a .md file it is code the author wrote, so it must stay code.
// The renderer gates on Renderer::StandaloneSvg(), which the app sets from
// the file extension in LoadDocumentText.

TEST(SvgFence, SvgFenceParsesAsCodeBlock) {
    const std::string md =
        "Before\n\n```svg\n<svg width=\"10\" height=\"10\"/>\n```\n\nAfter\n";
    Document d;
    ParseMarkdown(md, d);
    int codeBlocks = 0;
    for (const auto& n : d.nodes) {
        if (n.block == BlockKind::CodeBlock) ++codeBlocks;
    }
    // It stays a code block either way; only the drawing differs. This
    // guards against a future change that promotes the fence to a
    // non-code block kind, which would bypass the gate entirely.
    EXPECT_TRUE(codeBlocks >= 1);
}

TEST(SvgFence, SvgFenceCarriesSvgLang) {
    const std::string md = "```svg\n<svg/>\n```\n";
    Document d;
    ParseMarkdown(md, d);
    bool sawSvgLang = false;
    for (const auto& n : d.nodes) {
        if (n.block == BlockKind::CodeBlock && n.lang == "svg") sawSvgLang = true;
    }
    EXPECT_TRUE(sawSvgLang);
}

// A standalone .svg is wrapped in a fence so the markdown pipeline can
// parse it; the raw content is what gets rendered. Mirrors the
// WrapLangFence/HasLangFence pair in app.cpp.
TEST(SvgFence, WrappedStandaloneSvgParsesToOneSvgBlock) {
    const std::string svgFile =
        "<svg width=\"120\" height=\"60\"><text x=\"5\" y=\"20\">Hi</text></svg>";
    const std::string wrapped = "```svg\n" + svgFile + "\n```\n";
    Document d;
    ParseMarkdown(wrapped, d);
    int codeBlocks = 0;
    for (const auto& n : d.nodes) {
        if (n.block == BlockKind::CodeBlock) ++codeBlocks;
    }
    EXPECT_EQ(codeBlocks, 1);
}
