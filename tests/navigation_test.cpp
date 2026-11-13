#include "gtest_lite.h"
#include "layoutcache.h"
#include "navigation.h"
#include "textbuffer.h"

TEST(Navigation, MoveLeftAscii) {
    TextBuffer b;
    b.SetText("hello");
    EXPECT_EQ(MoveLeft(b, 3), 2u);
    EXPECT_EQ(MoveLeft(b, 1), 0u);
    EXPECT_EQ(MoveLeft(b, 0), 0u);
}

TEST(Navigation, MoveRightAscii) {
    TextBuffer b;
    b.SetText("hello");
    EXPECT_EQ(MoveRight(b, 0), 1u);
    EXPECT_EQ(MoveRight(b, 4), 5u);
    EXPECT_EQ(MoveRight(b, 5), 5u);
}

TEST(Navigation, MoveLeftGrapheme) {
    TextBuffer b;
    b.SetText("ab" "\xc3\xa5");  // a-ring
    EXPECT_EQ(MoveLeft(b, 4), 2u);
    EXPECT_EQ(MoveLeft(b, 2), 1u);
}

TEST(Navigation, MoveRightGrapheme) {
    TextBuffer b;
    b.SetText("ab" "\xc3\xa5");  // a-ring
    EXPECT_EQ(MoveRight(b, 2), 4u);
    EXPECT_EQ(MoveRight(b, 0), 1u);
}

// Regression: Right from just before the last character of a paragraph
// must stop at the paragraph's own end, not jump into the next
// paragraph's first character (word-processor-guidelines.md section 3/E,
// acceptance criterion B/E). The two paragraphs are separated by a blank
// line, which is never "rendered" and used to be walked straight through.
TEST(Navigation, MoveRightStopsAtParagraphEndBeforeNextParagraph) {
    const std::string source =
        "dette er en linjeslutning.\n\ndette er naeste afsnit";
    TextBuffer b;
    b.SetText(source);

    LayoutCache cache;
    cache.SetSourceText(&source);

    BlockLayout p1;
    p1.srcOffset = 0;
    p1.srcLength = 27;  // "dette er en linjeslutning."
    for (uint32_t i = 0; i < p1.srcLength; ++i) {
        p1.u16ToSrc.push_back(i);
        p1.u16ToSrcEnd.push_back(i + 1);
    }
    cache.Add(p1);

    BlockLayout p2;
    p2.srcOffset = 29;  // past "\n\n"
    p2.srcLength = static_cast<uint32_t>(source.size()) - 29;
    for (uint32_t i = 0; i < p2.srcLength; ++i) {
        p2.u16ToSrc.push_back(29 + i);
        p2.u16ToSrcEnd.push_back(29 + i + 1);
    }
    cache.Add(p2);

    // Caret just before the final '.', at offset 26.
    EXPECT_EQ(MoveRight(b, 26, &cache), 27u);   // paragraph-end, not block 2
    EXPECT_EQ(MoveRight(b, 27, &cache), 29u);   // a further Right enters p2
}

TEST(Navigation, MoveWordLeft) {
    TextBuffer b;
    b.SetText("hello world");
    EXPECT_EQ(MoveWordLeft(b, 11), 6u);
    EXPECT_EQ(MoveWordLeft(b, 6), 0u);
}

TEST(Navigation, MoveWordRight) {
    TextBuffer b;
    b.SetText("hello world");
    EXPECT_EQ(MoveWordRight(b, 0), 5u);
    EXPECT_EQ(MoveWordRight(b, 5), 11u);
}

TEST(Navigation, MoveWordLeftPunctuation) {
    TextBuffer b;
    b.SetText("foo, bar");
    EXPECT_EQ(MoveWordLeft(b, 4), 3u);  // skip comma
    EXPECT_EQ(MoveWordLeft(b, 3), 0u);  // skip foo
}

TEST(Navigation, MoveWordRightPastSpaces) {
    TextBuffer b;
    b.SetText("hello   world");
    EXPECT_EQ(MoveWordRight(b, 5), 13u);  // skip spaces + word to end
}


TEST(Navigation, SlugifyAnchorFoldsHeadingsTheWindowsWay) {
    // Internal #links resolve through this fold, so it must match the
    // Windows slug byte for byte (link activation 4).
    EXPECT_EQ(SlugifyAnchor("Hello World"), std::string("hello-world"));
    EXPECT_EQ(SlugifyAnchor("Installation og brug!"),
              std::string("installation-og-brug"));
    EXPECT_EQ(SlugifyAnchor("  API Reference  "),
              std::string("api-reference"));
    EXPECT_EQ(SlugifyAnchor("My_Heading"), std::string("my_heading"));
    EXPECT_EQ(SlugifyAnchor("M\xc3\xa5nedens oversigt"),
              std::string("m\xc3\xa5nedens-oversigt"));
    EXPECT_EQ(SlugifyAnchor("..."), std::string(""));
}
