#include "gtest_lite.h"
#include "parser.h"
#include "dom.h"
#include "textbuffer.h"
#include <cstdio>
#include <random>
#include <string>
#include <vector>

// Helper: compare two documents by serializing them.
static std::string DocString(const Document& d) {
    return DocumentToString(d);
}

// Apply one splice to a text and report the range a text buffer would report.
static void ApplySplice(std::string& text, uint32_t offset, uint32_t length,
                        const std::string& replacement, ReparseRange& range) {
    if (offset > text.size()) offset = static_cast<uint32_t>(text.size());
    if (offset + length > text.size())
        length = static_cast<uint32_t>(text.size()) - offset;
    const uint32_t oldEnd = offset + length;
    const uint32_t newEnd = offset + static_cast<uint32_t>(replacement.size());
    if (range.valid) {
        if (offset < range.start) range.start = offset;
        if (oldEnd > range.oldEnd) range.oldEnd = oldEnd;
        if (newEnd > range.newEnd) range.newEnd = newEnd;
    } else {
        range.start = offset;
        range.oldEnd = oldEnd;
        range.newEnd = newEnd;
        range.valid = true;
    }
    text.replace(offset, length, replacement);
}

TEST(Incremental, InsertCharMatchesFullReparse) {
    std::string text = "# Hello\n\nWorld paragraph.\n\n- item 1\n- item 2\n";
    Document oldDoc;
    ParseMarkdown(text, oldDoc);

    // Simulate inserting 'x' at offset 10 (in the paragraph).
    std::string newText = text;
    newText.insert(10, "x");

    Document incDoc, fullDoc;
    ParseMarkdownIncremental(newText, oldDoc, 10, 0, 1, incDoc);
    ParseMarkdown(newText, fullDoc);

    EXPECT_EQ(DocString(incDoc), DocString(fullDoc));
}

TEST(Incremental, DeleteCharMatchesFullReparse) {
    std::string text = "# Hello\n\nWorld paragraph.\n\n- item 1\n- item 2\n";
    Document oldDoc;
    ParseMarkdown(text, oldDoc);

    // Simulate deleting at offset 10.
    std::string newText = text;
    newText.erase(10, 1);

    Document incDoc, fullDoc;
    ParseMarkdownIncremental(newText, oldDoc, 10, 1, 0, incDoc);
    ParseMarkdown(newText, fullDoc);

    EXPECT_EQ(DocString(incDoc), DocString(fullDoc));
}

TEST(Incremental, InsertParagraphMatchesFullReparse) {
    std::string text = "# Hello\n\nWorld paragraph.\n";
    Document oldDoc;
    ParseMarkdown(text, oldDoc);

    // Simulate inserting a new paragraph.
    std::string newText = text + "\n\nNew paragraph here.\n";

    Document incDoc, fullDoc;
    ParseMarkdownIncremental(newText, oldDoc,
        static_cast<uint32_t>(text.size()), 0,
        static_cast<uint32_t>(newText.size() - text.size()), incDoc);
    ParseMarkdown(newText, fullDoc);

    EXPECT_EQ(DocString(incDoc), DocString(fullDoc));
}

TEST(Incremental, ReplaceWordMatchesFullReparse) {
    std::string text = "# Hello\n\nThe quick brown fox.\n";
    Document oldDoc;
    ParseMarkdown(text, oldDoc);

    // Replace "quick" with "slow".
    size_t pos = text.find("quick");
    std::string newText = text;
    newText.replace(pos, 5, "slow");

    Document incDoc, fullDoc;
    ParseMarkdownIncremental(newText, oldDoc,
        static_cast<uint32_t>(pos), 5, 4, incDoc);
    ParseMarkdown(newText, fullDoc);

    EXPECT_EQ(DocString(incDoc), DocString(fullDoc));
}

TEST(Incremental, LargeDocumentMatchesFullReparse) {
    // Build a 10KB document.
    std::string text;
    for (int i = 0; i < 50; i++) {
        text += "# Heading " + std::to_string(i) + "\n\n";
        text += "Paragraph " + std::to_string(i) + " with **bold** text.\n\n";
        text += "- Item A\n- Item B\n- Item C\n\n";
    }
    Document oldDoc;
    ParseMarkdown(text, oldDoc);

    // Insert at a position near the middle.
    size_t mid = text.size() / 2;
    std::string newText = text;
    newText.insert(mid, "INSERTED");

    Document incDoc, fullDoc;
    ParseMarkdownIncremental(newText, oldDoc,
        static_cast<uint32_t>(mid), 0, 8, incDoc);
    ParseMarkdown(newText, fullDoc);

    EXPECT_EQ(DocString(incDoc), DocString(fullDoc));
}

// A keystroke inside a paragraph is the case the whole task exists for: the
// fast path must actually run, not stand down and leave a full parse to do the
// work. The plan's "no visible lag" claim rests on this.
TEST(Incremental, TypingInParagraphTakesTheFastPath) {
    std::string text;
    for (int i = 0; i < 40; i++) {
        text += "# Heading " + std::to_string(i) + "\n\n";
        text += "Paragraph " + std::to_string(i) + " with **bold** text.\n\n";
        text += "- Item A\n- Item B\n- Item C\n\n";
    }
    Document doc;
    ASSERT_TRUE(ParseMarkdown(text, doc));

    // Type a character in the middle of the first paragraph.
    const size_t at = text.find("Paragraph 0 with");
    ASSERT_TRUE(at != std::string::npos);
    std::string typed = text;
    typed.insert(at + 5, "Q");

    bool incremental = false;
    Document out;
    ASSERT_TRUE(ParseMarkdownIncremental(typed, doc, static_cast<uint32_t>(at + 5),
                                         0, 1, out, &incremental));
    EXPECT_TRUE(incremental);

    Document full;
    ASSERT_TRUE(ParseMarkdown(typed, full));
    EXPECT_EQ(DocString(out), DocString(full));
}

// The correctness guard the plan asks for: over many documents and many
// edits, the incremental result must be byte-identical to a full parse. The
// generator is seeded so a failure is reproducible.
TEST(Incremental, RandomEditsMatchFullReparse) {
    const std::vector<std::string> docs = {
        "Hello world.\n\nSecond paragraph here.\n",
        "# Title\n\nBody text.\n\n## Sub\n\nMore.\n",
        "# H\n\npara **bold** *it* `code` [l](u)\n\n- x\n- y\n\n> q\n\n"
        "```\nfence\n```\n\n| a | b |\n|---|---|\n| 1 | 2 |\n",
        "- one\n- two\n  - nested\n- three\n\nAfter.\n",
        "> quote a\n> quote b\n\n- one\n- two\n\n# H1\n\npara\n",
        "# H\r\n\r\npara\r\n\r\n- a\r\n- b\r\n",
        "a\n\n---\n\nb\n\n```c\ncode\n```\n",
        "# Title\n\nBody text.\n\n## Sub\n\nMore.\n",
        "| h1 | h2 |\n|----|----|\n| a  | b  |\n\nAfter table.\n",
        "Text with [ref][1] link.\n\n[1]: https://example.com\n",
    };
    const std::vector<std::string> inserts = {"x", "zz", "\n", "\n\n", "- item\n",
                                              "`", "**", "| a |\n", " ", "\r\n"};

    std::mt19937 rng(20260812u);
    long long total = 0, fast = 0, mismatch = 0;
    for (const std::string& base : docs) {
        for (int round = 0; round < 40; ++round) {
            std::string text = base;
            Document doc;
            if (!ParseMarkdown(text, doc)) continue;
            for (int step = 0; step < 6; ++step) {
                ReparseRange range;
                const uint32_t pos = static_cast<uint32_t>(
                    text.empty() ? 0 : rng() % (text.size() + 1));
                const uint32_t len = static_cast<uint32_t>(
                    text.empty() ? 0 : rng() % 3);
                const uint32_t bounded = pos + len > text.size()
                    ? static_cast<uint32_t>(text.size() - pos) : len;
                const std::string& ins = inserts[rng() % inserts.size()];
                ApplySplice(text, pos, bounded, ins, range);

                Document inc = doc;
                const bool ok = ParseMarkdownIncremental(text, inc, range);
                Document full;
                const bool fullOk = ParseMarkdown(text, full);
                ++total;
                if (ok) ++fast;
                // Only a document the incremental path actually produced can
                // be compared: when it stands down it leaves the old document
                // untouched by design, and the caller reparses instead. The
                // first version of this test compared those copies too and
                // counted every stand-down as a divergence.
                if (ok && fullOk && DocString(inc) != DocString(full)) {
                    ++mismatch;
                    if (mismatch <= 3) {
                        std::printf("  MISMATCH pos=%u len=%u ins=[%s]\n"
                                    "  text=[%s]\n", pos, bounded, ins.c_str(),
                                    text.c_str());
                    }
                }
                doc = full;
            }
        }
    }
    std::printf("  incremental: %lld edits, %lld fast, %lld mismatch\n",
                total, fast, mismatch);
    EXPECT_EQ(mismatch, 0);
    // The generator inserts line breaks, fences and table rows as well as
    // characters, and those are block-level edits that stand down by design. A
    // floor here only guards against a regression that stands down on
    // everything; the keystroke case has its own test below, because that is
    // the one the 5 ms budget is written for.
    EXPECT_TRUE(fast * 5 >= total);
}

// Typing a character is the case the whole task exists for, so it must be both
// correct and fast. Structural edits (a line break, a fence, a table row) are
// expected to stand down; this test says nothing about them.
TEST(Incremental, TypingCharactersTakesTheFastPath) {
    const std::vector<std::string> docs = {
        "Hello world.\n\nSecond paragraph here.\n",
        "# Title\n\nBody text.\n\n## Sub\n\nMore.\n",
        "- one\n- two\n  - nested\n- three\n\nAfter.\n",
        "> quote a\n> quote b\n\n- one\n- two\n\n# H1\n\npara\n",
        "| h1 | h2 |\n|----|----|\n| a  | b  |\n\nAfter table.\n",
    };
    std::mt19937 rng(20261126u);
    long long total = 0, fast = 0, mismatch = 0;
    for (const std::string& base : docs) {
        for (int round = 0; round < 8; ++round) {
            std::string text = base;
            Document doc;
            if (!ParseMarkdown(text, doc)) continue;
            for (int step = 0; step < 5; ++step) {
                ReparseRange range;
                const uint32_t pos = static_cast<uint32_t>(
                    text.empty() ? 0 : rng() % (text.size() + 1));
                ApplySplice(text, pos, 0, "x", range);
                Document inc = doc;
                const bool ok = ParseMarkdownIncremental(text, inc, range);
                Document full;
                ParseMarkdown(text, full);
                ++total;
                if (ok) {
                    ++fast;
                    if (DocString(inc) != DocString(full)) ++mismatch;
                }
                doc = full;
            }
        }
    }
    std::printf("  characters: %lld edits, %lld fast, %lld mismatch\n", total,
                fast, mismatch);
    EXPECT_EQ(mismatch, 0);
    // Most of these land inside prose, and a position at a block edge, in a
    // table or inside a fence stands down. Measured at 65% on this generator;
    // the floor is 60%, so a real regression shows up without the test
    // tripping over which random position it drew.
    EXPECT_TRUE(fast * 5 >= total * 3);
}

// The text buffer is what feeds the parser in the app: it accumulates the
// union of the splices since the last reparse and forgets it on a reload.
TEST(Incremental, TextBufferReportsSpliceRange) {
    TextBuffer buf;
    buf.SetText("hello world");
    EXPECT_TRUE(!buf.reparseRange().valid);

    buf.Splice(5, 0, ",");        // "hello, world"
    EXPECT_TRUE(buf.reparseRange().valid);
    EXPECT_EQ(buf.reparseRange().start, 5u);
    EXPECT_EQ(buf.reparseRange().oldEnd, 5u);
    EXPECT_EQ(buf.reparseRange().newEnd, 6u);

    buf.Splice(0, 1, "H");        // "Hello, world"
    // The range is expressed in the coordinates of the text as it was at the
    // last parse, so the comma inserted at 5 earlier does not move the old
    // end: old[0,5) became new[0,6), which is "Hello," either way.
    EXPECT_EQ(buf.reparseRange().start, 0u);
    EXPECT_EQ(buf.reparseRange().oldEnd, 5u);
    EXPECT_EQ(buf.reparseRange().newEnd, 6u);
    EXPECT_EQ(buf.Text(), std::string("Hello, world"));

    buf.ClearReparseRange();
    EXPECT_TRUE(!buf.reparseRange().valid);

    // A reload invalidates the range: the whole text changed.
    buf.Splice(0, 2, "he");
    EXPECT_TRUE(buf.reparseRange().valid);
    buf.SetText("fresh");
    EXPECT_TRUE(!buf.reparseRange().valid);
}

// A change that reaches the title's H1 changes the window title and the
// outline, so the incremental path stands down and the caller reparses.
TEST(Incremental, TitleEditStandsDown) {
    std::string text = "# Title\n\nBody.\n";
    Document doc;
    ASSERT_TRUE(ParseMarkdown(text, doc));
    std::string edited = "# Titel\n\nBody.\n";
    Document out = doc;
    EXPECT_TRUE(!ParseMarkdownIncremental(edited, out, 3, 1, 1));
}

// Link reference definitions are global: a definition anywhere resolves
// references everywhere, so a single slice cannot reproduce the parse.
TEST(Incremental, LinkReferenceDocumentStandsDown) {
    std::string text = "# T\n\nSee [ref].\n\n[ref]: https://example.com\n";
    Document doc;
    ASSERT_TRUE(ParseMarkdown(text, doc));
    std::string edited = text;
    edited.insert(edited.find("See"), "X");
    Document out = doc;
    EXPECT_TRUE(!ParseMarkdownIncremental(edited, out, 8, 0, 1));
}

// A document whose size no longer matches the range's old text cannot be
// edited incrementally: the caller passed a stale document. Two bytes were
// inserted while the range claims one, so the old size the range implies is
// ten and the document holds nine.
TEST(Incremental, StaleDocumentStandsDown) {
    Document doc;
    ASSERT_TRUE(ParseMarkdown("abc\n\ndef\n", doc));
    Document out = doc;
    EXPECT_TRUE(!ParseMarkdownIncremental("abc\n\ndefgh\n", out, 8, 0, 1));
}
