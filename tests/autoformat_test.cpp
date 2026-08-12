#include "gtest_lite.h"
#include "autoformat.h"
#include "textbuffer.h"
#include "caret.h"

TEST(Autoformat, HeadingOnSpace) {
    TextBuffer b;
    b.SetText("## ");
    Selection s;
    s.Collapse({3});
    // Space already typed; check if autoformat detects heading.
    EXPECT_TRUE(AutoformatHeading(&b, &s));
}

TEST(Autoformat, HeadingNoMatchOnText) {
    TextBuffer b;
    b.SetText("hello # ");
    Selection s;
    s.Collapse({7});
    // "#" is not at line start, so no heading.
    EXPECT_FALSE(AutoformatHeading(&b, &s));
}

TEST(Autoformat, HeadingLevel1) {
    TextBuffer b;
    b.SetText("# ");
    Selection s;
    s.Collapse({2});
    EXPECT_TRUE(AutoformatHeading(&b, &s));
}

TEST(Autoformat, BulletListOnSpace) {
    TextBuffer b;
    b.SetText("- ");
    Selection s;
    s.Collapse({2});
    EXPECT_TRUE(AutoformatBulletList(&b, &s));
}

TEST(Autoformat, BulletListAsterisk) {
    TextBuffer b;
    b.SetText("* ");
    Selection s;
    s.Collapse({2});
    EXPECT_TRUE(AutoformatBulletList(&b, &s));
}

TEST(Autoformat, BulletListNoMatchInWord) {
    TextBuffer b;
    b.SetText("hello- ");
    Selection s;
    s.Collapse({7});
    EXPECT_FALSE(AutoformatBulletList(&b, &s));
}

TEST(Autoformat, OrderedListOnSpace) {
    TextBuffer b;
    b.SetText("1. ");
    Selection s;
    s.Collapse({3});
    EXPECT_TRUE(AutoformatOrderedList(&b, &s));
}

TEST(Autoformat, OrderedListMultiDigit) {
    TextBuffer b;
    b.SetText("12. ");
    Selection s;
    s.Collapse({4});
    EXPECT_TRUE(AutoformatOrderedList(&b, &s));
}

TEST(Autoformat, BlockquoteOnSpace) {
    TextBuffer b;
    b.SetText("> ");
    Selection s;
    s.Collapse({2});
    EXPECT_TRUE(AutoformatBlockquote(&b, &s));
}

TEST(Autoformat, BoldAutoClose) {
    TextBuffer b;
    b.SetText("**hello**");
    Selection s;
    s.Collapse({9});
    EXPECT_TRUE(AutoformatBold(&b, &s));
}

TEST(Autoformat, BoldNoMatchSingle) {
    TextBuffer b;
    b.SetText("*hello*");
    Selection s;
    s.Collapse({7});
    // Single * not **.
    EXPECT_FALSE(AutoformatBold(&b, &s));
}

TEST(Autoformat, InlineCodeAutoClose) {
    TextBuffer b;
    b.SetText("`code`");
    Selection s;
    s.Collapse({6});
    EXPECT_TRUE(AutoformatInlineCode(&b, &s));
}

TEST(Autoformat, InlineCodeNoMatch) {
    TextBuffer b;
    b.SetText("hello`");
    Selection s;
    s.Collapse({6});
    // No opening backtick earlier.
    EXPECT_FALSE(AutoformatInlineCode(&b, &s));
}

TEST(Autoformat, CheckAutoformatHeading) {
    TextBuffer b;
    b.SetText("# ");
    Selection s;
    s.Collapse({2});
    EXPECT_TRUE(CheckAutoformat(&b, &s, ' '));
}

TEST(Autoformat, CheckAutoformatNegative) {
    TextBuffer b;
    b.SetText("#");
    Selection s;
    s.Collapse({1});
    // No space typed, should not trigger.
    EXPECT_FALSE(CheckAutoformat(&b, &s, 'x'));
}
