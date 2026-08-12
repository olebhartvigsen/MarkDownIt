#include "gtest_lite.h"
#include "inputfilter.h"
#include "textbuffer.h"

TEST(InputFilter, EscapeAsterisk) {
    TextBuffer b;
    b.SetText("hello ");
    std::string r = EscapeForInsert(b, 6, "*");
    EXPECT_EQ(r, "\\*");
}

TEST(InputFilter, EscapeUnderscore) {
    TextBuffer b;
    b.SetText("hello ");
    std::string r = EscapeForInsert(b, 6, "_");
    EXPECT_EQ(r, "\\_");
}

TEST(InputFilter, EscapeBacktick) {
    TextBuffer b;
    b.SetText("hello ");
    std::string r = EscapeForInsert(b, 6, "`");
    EXPECT_EQ(r, "\\`");
}

TEST(InputFilter, EscapeHashAtLineStart) {
    TextBuffer b;
    b.SetText("");
    std::string r = EscapeForInsert(b, 0, "#");
    EXPECT_EQ(r, "\\#");
}

TEST(InputFilter, HashNotEscapedMidLine) {
    TextBuffer b;
    b.SetText("hello ");
    std::string r = EscapeForInsert(b, 6, "#");
    EXPECT_EQ(r, "#");
}

TEST(InputFilter, EscapeGreaterThanAtLineStart) {
    TextBuffer b;
    b.SetText("");
    std::string r = EscapeForInsert(b, 0, ">");
    EXPECT_EQ(r, "\\>");
}

TEST(InputFilter, GreaterThanNotEscapedMidLine) {
    TextBuffer b;
    b.SetText("x ");
    std::string r = EscapeForInsert(b, 2, ">");
    EXPECT_EQ(r, ">");
}

TEST(InputFilter, EscapeDashAtLineStart) {
    TextBuffer b;
    b.SetText("");
    std::string r = EscapeForInsert(b, 0, "-");
    EXPECT_EQ(r, "\\-");
}

TEST(InputFilter, DashNotEscapedMidLine) {
    TextBuffer b;
    b.SetText("hello ");
    std::string r = EscapeForInsert(b, 6, "-");
    EXPECT_EQ(r, "-");
}

TEST(InputFilter, EscapeBracket) {
    TextBuffer b;
    b.SetText("hello ");
    std::string r = EscapeForInsert(b, 6, "[");
    EXPECT_EQ(r, "\\[");
}

TEST(InputFilter, EscapeBackslash) {
    TextBuffer b;
    b.SetText("hello ");
    std::string r = EscapeForInsert(b, 6, "\\");
    EXPECT_EQ(r, "\\\\");
}

TEST(InputFilter, EscapePipe) {
    TextBuffer b;
    b.SetText("hello ");
    std::string r = EscapeForInsert(b, 6, "|");
    EXPECT_EQ(r, "\\|");
}

TEST(InputFilter, NormalTextNotEscaped) {
    TextBuffer b;
    b.SetText("hello ");
    std::string r = EscapeForInsert(b, 6, "world");
    EXPECT_EQ(r, "world");
}

TEST(InputFilter, EscapeForPasteMultiChar) {
    TextBuffer b;
    b.SetText("");
    std::string r = EscapeForPaste(b, 0, "# Hello *world*");
    EXPECT_EQ(r, "\\# Hello \\*world\\*");
}

TEST(InputFilter, HashEscapedAfterNewline) {
    TextBuffer b;
    b.SetText("line1\n");
    std::string r = EscapeForInsert(b, 6, "#");
    EXPECT_EQ(r, "\\#");
}

TEST(InputFilter, HashEscapedAfterSpaces) {
    TextBuffer b;
    b.SetText("    ");
    std::string r = EscapeForInsert(b, 4, "#");
    EXPECT_EQ(r, "\\#");
}
