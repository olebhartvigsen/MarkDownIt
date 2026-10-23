#include "gtest_lite.h"
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
