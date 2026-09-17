#include "gtest_lite.h"
#include "layoutcache.h"
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
