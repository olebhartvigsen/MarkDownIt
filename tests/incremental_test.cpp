#include "gtest_lite.h"
#include "parser.h"
#include "dom.h"
#include <string>

// Helper: compare two documents by serializing them.
static std::string DocString(const Document& d) {
    return DocumentToString(d);
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
