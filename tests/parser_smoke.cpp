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


// --- CR-only line endings ------------------------------------------------
// A document with a classic-Mac CR line ending used to hang ParseMarkdown
// for ever: the break-run scanner below the md4c callbacks stopped on a bare
// CR without advancing, and the outer loop re-read the same byte. A CR-only
// file is common enough (old Mac exports, some toolchains) that the app could
// be wedged by opening one. Each case must complete, and CR must act as the
// line ending the CommonMark spec says it is.
TEST(ParserCr, LoneCrDoesNotHang) {
    Document d;
    const bool ok = ParseMarkdown("\r", d);
    (void)ok;   // the assertion is that we get here at all
}

TEST(ParserCr, CrTerminatedLineReturns) {
    Document d;
    ParseMarkdown("a\r", d);
    EXPECT_TRUE(true);
}

TEST(ParserCr, CrFollowedByTextReturns) {
    Document d;
    ParseMarkdown("\ra", d);
    EXPECT_TRUE(true);
}

TEST(ParserCr, RepeatedCrReturns) {
    Document d;
    ParseMarkdown("\r\r\r", d);
    EXPECT_TRUE(true);
}

TEST(ParserCr, LfThenCrReturns) {
    Document d;
    ParseMarkdown("\n\r", d);
    EXPECT_TRUE(true);
}

TEST(ParserCr, CrThenCrlfReturns) {
    Document d;
    ParseMarkdown("\r\r\n", d);
    EXPECT_TRUE(true);
}

// CR must be treated as a real line ending. A single CR is a SOFT break
// inside one paragraph, exactly like a single LF or CRLF, so the three must
// produce the same shape. Two blocks need a blank line between them.
static int CountParagraphs(const Document& d) {
    int paras = 0;
    for (const auto& n : d.nodes) {
        if (n.block == BlockKind::Paragraph && !n.virtualEmptyParagraph) ++paras;
    }
    return paras;
}

TEST(ParserCr, CrIsASoftBreakLikeLf) {
    Document cr, lf, crlf;
    ParseMarkdown("alpha\rbravo\r", cr);
    ParseMarkdown("alpha\nbravo\n", lf);
    ParseMarkdown("alpha\r\nbravo\r\n", crlf);
    // One paragraph each, and the same source extent.
    EXPECT_EQ(CountParagraphs(cr), CountParagraphs(lf));
    EXPECT_EQ(CountParagraphs(cr), CountParagraphs(crlf));
    EXPECT_EQ(CountParagraphs(lf), 1);
    if (!cr.nodes.empty()) {
        EXPECT_EQ(cr.nodes[0].srcLength, lf.nodes[0].srcLength);
    }
}

TEST(ParserCr, BlankCrLineSeparatesBlocks) {
    Document d;
    ParseMarkdown("alpha\r\rbravo\r", d);
    EXPECT_GE(CountParagraphs(d), 2);
}

TEST(ParserCr, BlankLfLineSeparatesBlocks) {
    Document d;
    ParseMarkdown("alpha\n\nbravo\n", d);
    EXPECT_GE(CountParagraphs(d), 2);
}

// A real-world mixed document, the shape that triggered the fuzz finding.
TEST(ParserCr, MixedCrLfDocumentParses) {
    Document d;
    const std::string md =
        "# Title\r\n\r\nSome text.\r\n\r\n- one\r\n- two\r\n\r\n```cpp\r\n"
        "int main(){}\r\n```\r\n\r\n| a | b |\r\n|---|---|\r\n| 1 | 2 |\r\n";
    ParseMarkdown(md, d);
    EXPECT_FALSE(d.nodes.empty());
}


// --- NUL bytes in the source --------------------------------------------
// A NUL byte is reported by md4c as MD_TEXT_NULLCHAR carrying a pointer to
// md4c's own static "" with size 1, NOT a pointer into the input. Treating
// that as an input pointer made `text - ctx->input` a garbage offset (measured
// 642048361 for a 1-byte file), which was then used to index the source and
// crashed the parser. A one-byte document containing NUL was enough.
TEST(ParserNul, SingleNulByteDoesNotCrash) {
    Document d;
    ParseMarkdown(std::string(1, '\0'), d);
    EXPECT_TRUE(true);
}

TEST(ParserNul, NulInsideTextDoesNotCrash) {
    Document d;
    ParseMarkdown("before\0after", d);
    EXPECT_TRUE(true);
}

TEST(ParserNul, LeadingAndTrailingNulDoNotCrash) {
    Document d;
    ParseMarkdown(std::string("\0a\0", 3), d);
    EXPECT_TRUE(true);
}

TEST(ParserNul, NulInHeadingDoesNotCrash) {
    Document d;
    ParseMarkdown("# head\0ing\n", d);
    EXPECT_TRUE(true);
}

TEST(ParserNul, NulInListItemDoesNotCrash) {
    Document d;
    ParseMarkdown("- item\0one\n- two\n", d);
    EXPECT_TRUE(true);
}

TEST(ParserNul, NulInTableCellDoesNotCrash) {
    Document d;
    ParseMarkdown("| a\0 | b |\n|---|---|\n| 1 | 2 |\n", d);
    EXPECT_TRUE(true);
}

TEST(ParserNul, NulInCodeFenceDoesNotCrash) {
    Document d;
    ParseMarkdown("```cpp\nint x\0y;\n```\n", d);
    EXPECT_TRUE(true);
}

// The NUL becomes U+FFFD, so the node must carry a replacement character
// rather than a NUL or a garbage offset.
TEST(ParserNul, NulBecomesReplacementChar) {
    Document d;
    ParseMarkdown(std::string("a\0b", 3), d);
    // md4c reports the NUL as its own run, so U+FFFD lands in a child of its
    // own. What matters is that a NUL never reaches the render path.
    bool sawReplacement = false;
    bool sawRawNul = false;
    for (const auto& n : d.nodes) {
        for (const auto& ib : n.children) {
            for (char32_t cp : ib.text) {
                if (cp == 0xFFFD) sawReplacement = true;
                if (cp == 0) sawRawNul = true;
            }
        }
    }
    EXPECT_TRUE(sawReplacement);
    EXPECT_FALSE(sawRawNul);
}

// Every node offset must stay inside the document. This is the invariant the
// garbage offset violated.
TEST(ParserNul, NodeOffsetsStayInRange) {
    const std::string md = "para one\n\n# head\0ing\n\n| a\0 | b |\n|---|---|\n| 1 | 2 |\n";
    Document d;
    ParseMarkdown(md, d);
    for (const auto& n : d.nodes) {
        EXPECT_LE(n.srcOffset, md.size());
        EXPECT_LE(static_cast<size_t>(n.srcOffset) + n.srcLength, md.size());
        for (const auto& ib : n.children) {
            EXPECT_LE(ib.srcOffset, md.size());
        }
    }
}
