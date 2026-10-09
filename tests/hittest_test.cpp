#include "gtest_lite.h"
#include "layoutcache.h"
#include "navigation.h"
#include "parser.h"
#include "editcontroller.h"
#include <string>

// ASCII: each byte is one UTF-8 char and one UTF-16 unit.
TEST(Utf8Utf16, AsciiRoundTrip) {
    std::string s = "Hello, World!";
    EXPECT_EQ(Utf8OffsetToUtf16(s, 0), 0u);
    EXPECT_EQ(Utf8OffsetToUtf16(s, 5), 5u);
    EXPECT_EQ(Utf8OffsetToUtf16(s, 13), 13u);
    EXPECT_EQ(Utf16OffsetToUtf8(s, 0), 0u);
    EXPECT_EQ(Utf16OffsetToUtf8(s, 5), 5u);
    EXPECT_EQ(Utf16OffsetToUtf8(s, 13), 13u);
}

// Danish characters: ae (0xC3 0xA6), o-stroke (0xC3 0xB8), a-ring (0xC3 0xA5).
// Each is 2 UTF-8 bytes -> 1 UTF-16 code unit.
TEST(Utf8Utf16, DanishChars) {
    // "ae" in UTF-8: 0xC3 0xA6 (2 bytes, 1 UTF-16 unit)
    std::string s = "\xc3\xa6";  // ae
    EXPECT_EQ(Utf8OffsetToUtf16(s, 0), 0u);
    EXPECT_EQ(Utf8OffsetToUtf16(s, 2), 1u);
    EXPECT_EQ(Utf16OffsetToUtf8(s, 0), 0u);
    EXPECT_EQ(Utf16OffsetToUtf8(s, 1), 2u);
}

// Mixed ASCII and Danish: "Hej" + a-ring = "Hej\xC3\xA5"
TEST(Utf8Utf16, MixedAsciiDanish) {
    std::string s = "Hej\xc3\xa5";  // "Hej" + a-ring, 5 bytes, 4 UTF-16 units
    EXPECT_EQ(Utf8OffsetToUtf16(s, 0), 0u);
    EXPECT_EQ(Utf8OffsetToUtf16(s, 3), 3u);   // after "Hej"
    EXPECT_EQ(Utf8OffsetToUtf16(s, 5), 4u);   // after a-ring
    EXPECT_EQ(Utf16OffsetToUtf8(s, 0), 0u);
    EXPECT_EQ(Utf16OffsetToUtf8(s, 3), 3u);   // "Hej"
    EXPECT_EQ(Utf16OffsetToUtf8(s, 4), 5u);   // full string
}

// Emoji: U+1F600 (grinning face) = 4 UTF-8 bytes -> 2 UTF-16 units (surrogate pair).
TEST(Utf8Utf16, EmojiSurrogatePair) {
    // U+1F600 in UTF-8: F0 9F 98 80 (4 bytes)
    std::string s = "A\xf0\x9f\x98\x80";  // "A" + emoji, 5 bytes, 3 UTF-16 units
    EXPECT_EQ(Utf8OffsetToUtf16(s, 0), 0u);
    EXPECT_EQ(Utf8OffsetToUtf16(s, 1), 1u);  // after "A"
    EXPECT_EQ(Utf8OffsetToUtf16(s, 5), 3u);  // after emoji (1 + 2 = 3)
    EXPECT_EQ(Utf16OffsetToUtf8(s, 0), 0u);
    EXPECT_EQ(Utf16OffsetToUtf8(s, 1), 1u);  // "A"
    EXPECT_EQ(Utf16OffsetToUtf8(s, 3), 5u);  // full string (A + surrogate pair)
}

// Round-trip: UTF-8 -> UTF-16 -> UTF-8 should be identity.
TEST(Utf8Utf16, RoundTrip) {
    std::string s = "Hello \xc3\xa6\xc3\xb8\xc3\xa5 \xf0\x9f\x98\x80";
    for (uint32_t i = 0; i <= s.size(); ) {
        uint32_t u16 = Utf8OffsetToUtf16(s, i);
        uint32_t back = Utf16OffsetToUtf8(s, u16);
        EXPECT_EQ(back, i);
        // Advance by one UTF-8 character.
        unsigned char b = static_cast<unsigned char>(s[i]);
        if (i < s.size()) {
            if (b < 0x80) i += 1;
            else if ((b & 0xE0) == 0xC0) i += 2;
            else if ((b & 0xF0) == 0xE0) i += 3;
            else i += 4;
        } else {
            break;
        }
    }
}

// Offset past the end clamps to the string length.
TEST(Utf8Utf16, ClampPastEnd) {
    std::string s = "abc";
    EXPECT_EQ(Utf8OffsetToUtf16(s, 100), 3u);
    EXPECT_EQ(Utf16OffsetToUtf8(s, 100), 3u);
}


TEST(HitTest, RightSideWhitespaceUsesOwningBlock) {
    LayoutCache cache;
    BlockLayout block;
    block.x = 20.0f;
    block.y = 10.0f;
    block.width = 40.0f;
    block.height = 20.0f;
    block.textStartOffset = 7;
    cache.Add(block);

    EXPECT_EQ(cache.PointToOffset(100.0f, 15.0f), UINT32_MAX);
    EXPECT_EQ(cache.PointToOffsetAtOrAfterBlock(100.0f, 15.0f), 7u);
    EXPECT_EQ(cache.PointToOffsetAtOrAfterBlock(10.0f, 15.0f), UINT32_MAX);
    EXPECT_EQ(cache.PointToOffsetAtOrAfterBlock(100.0f, 35.0f), UINT32_MAX);
}


TEST(HitTest, NormalizeParagraphEndPastInlineSyntax) {
    std::string source = "**bold** [go](https://example.com/a_(b))";
    LayoutCache cache;
    cache.SetSourceText(&source);
    BlockLayout block;
    block.srcOffset = 0;
    block.srcLength = static_cast<uint32_t>(source.size());
    block.u16ToSrc = {2, 3, 4, 5, 10, 11};
    block.u16ToSrcEnd = {3, 4, 5, 6, 11, 12};
    cache.Add(block);

    EXPECT_EQ(cache.NormalizeToRenderedCaret(6), 8u);
    EXPECT_EQ(cache.NormalizeToRenderedCaret(12),
              static_cast<uint32_t>(source.size()));
}

// AC-03: selecting item 3's text must not consume its structural newline.
TEST(ListSelection, DeleteThirdItemTextKeepsFiveItemsAndTheirDepths) {
    for (const std::string newline : {"\n", "\r\n"}) {
        for (const std::string body : {"three", "**three**", "[three](https://example.org)"}) {
            for (const std::string prefix : {"- ", "* ", "+ "}) {
                TextBuffer buffer;
                const std::string source = prefix + "one" + newline +
                    prefix + "two" + newline + prefix + body + newline +
                    prefix + "four" + newline + prefix + "five";
                buffer.SetText(source);
                Document doc;
                ASSERT_TRUE(ParseMarkdown(source, doc));
                ASSERT_EQ(doc.nodes.size(), 5u);
                LayoutCache cache;
                cache.SetSourceText(&source);
                const Node& node = doc.nodes[2];
                BlockLayout block;
                block.nodeIndex = 2;
                for (const auto& child : node.children) {
                    for (uint32_t i = 0; i < child.srcLength; ++i) {
                        block.u16ToSrc.push_back(child.srcOffset + i);
                        block.u16ToSrcEnd.push_back(child.srcOffset + i + 1);
                    }
                }
                cache.Add(block);
                uint32_t start = 0, end = 0;
                ASSERT_TRUE(GetBlockSelectionRange(&doc, cache, 0, &start, &end));
                const uint32_t expectedStart = node.srcOffset + static_cast<uint32_t>(prefix.size());
                EXPECT_EQ(start, expectedStart);
                EXPECT_EQ(end, expectedStart + body.size());
                if (body == "three") {
                    uint32_t oldStart = 0, oldEnd = 0;
                    ASSERT_TRUE(cache.GetRenderedBlockRange(0, &oldStart, &oldEnd));
                    EXPECT_EQ(oldEnd, end + newline.size());
                    std::string broken = source;
                    broken.erase(oldStart, oldEnd - oldStart);
                    Document oldResult;
                    ASSERT_TRUE(ParseMarkdown(broken, oldResult));
                    ASSERT_EQ(oldResult.nodes.size(), 5u);
                    EXPECT_EQ(oldResult.nodes[3].depth, 1);
                }
                Selection selection;
                selection.anchor = {start};
                selection.active = {end};
                UndoStack undo;
                EditController editor(&buffer, &selection);
                editor.SetUndoStack(&undo);
                ASSERT_TRUE(editor.DeleteBackward(&doc));
                const std::string expected = prefix + "one" + newline +
                    prefix + "two" + newline + prefix + newline +
                    prefix + "four" + newline + prefix + "five";
                EXPECT_EQ(buffer.Text(), expected);
                EXPECT_EQ(selection.active.offset, start);
                EXPECT_TRUE(selection.Empty());
                Document after;
                ASSERT_TRUE(ParseMarkdown(buffer.Text(), after));
                ASSERT_EQ(after.nodes.size(), 5u);
                for (size_t i = 0; i < 5; ++i) {
                    EXPECT_EQ(static_cast<int>(after.nodes[i].block), static_cast<int>(BlockKind::List));
                    EXPECT_EQ(after.nodes[i].depth, doc.nodes[i].depth);
                }
                EXPECT_TRUE(after.nodes[2].children.empty());
                EXPECT_EQ(after.nodes[3].children[0].text, U"four");
                EXPECT_EQ(after.nodes[4].children[0].text, U"five");
                ASSERT_TRUE(editor.Undo());
                EXPECT_EQ(buffer.Text(), source);
                EXPECT_EQ(selection.anchor.offset, start);
                EXPECT_EQ(selection.active.offset, end);
                EXPECT_FALSE(undo.CanUndo());
                ASSERT_TRUE(editor.Redo());
                EXPECT_EQ(buffer.Text(), expected);
            }
        }
    }
}

TEST(ListSelection, OrdinaryParagraphAndSourceViewKeepTheirNewlineRule) {
    const std::string source = "abc\ndef";
    Document doc;
    Node node;
    node.block = BlockKind::Paragraph;
    doc.nodes.push_back(node);
    LayoutCache cache;
    cache.SetSourceText(&source);
    BlockLayout block;
    block.u16ToSrc = {0, 1, 2};
    block.u16ToSrcEnd = {1, 2, 3};
    cache.Add(block);
    uint32_t start = 0, end = 0;
    ASSERT_TRUE(GetBlockSelectionRange(&doc, cache, 0, &start, &end));
    EXPECT_EQ(start, 0u);
    EXPECT_EQ(end, 4u);
    doc.nodes[0].block = BlockKind::List;
    ASSERT_TRUE(GetBlockSelectionRange(nullptr, cache, 0, &start, &end));
    EXPECT_EQ(end, 4u);
}
