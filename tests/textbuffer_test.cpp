#include "gtest_lite.h"
#include "textbuffer.h"

TEST(TextBuffer, SpliceInsertsAtOffset) {
    TextBuffer b;
    b.SetText("hello world");
    uint32_t end = b.Splice(5, 0, ",");
    EXPECT_EQ(b.Text(), "hello, world");
    EXPECT_EQ(end, 6u);
}

TEST(TextBuffer, SpliceReplacesRange) {
    TextBuffer b;
    b.SetText("hello world");
    b.Splice(0, 5, "goodbye");
    EXPECT_EQ(b.Text(), "goodbye world");
}

TEST(TextBuffer, SpliceDeleteRange) {
    TextBuffer b;
    b.SetText("hello, world");
    b.Splice(5, 1, "");
    EXPECT_EQ(b.Text(), "hello world");
}

TEST(TextBuffer, SpliceInsertAtBeginning) {
    TextBuffer b;
    b.SetText("abc");
    uint32_t end = b.Splice(0, 0, ">");
    EXPECT_EQ(b.Text(), ">abc");
    EXPECT_EQ(end, 1u);
}

TEST(TextBuffer, SpliceInsertAtEnd) {
    TextBuffer b;
    b.SetText("abc");
    uint32_t end = b.Splice(3, 0, "<");
    EXPECT_EQ(b.Text(), "abc<");
    EXPECT_EQ(end, 4u);
}

TEST(TextBuffer, SpliceReplacesEntireBuffer) {
    TextBuffer b;
    b.SetText("old text");
    b.Splice(0, 8, "new");
    EXPECT_EQ(b.Text(), "new");
}

TEST(TextBuffer, SpliceClampsOffset) {
    TextBuffer b;
    b.SetText("abc");
    b.Splice(100, 0, "!");
    EXPECT_EQ(b.Text(), "abc!");
}

TEST(TextBuffer, SpliceClampsLength) {
    TextBuffer b;
    b.SetText("abc");
    b.Splice(1, 100, "X");
    EXPECT_EQ(b.Text(), "aX");
}

TEST(TextBuffer, LineStartFindsBOL) {
    TextBuffer b;
    b.SetText("one\ntwo\nthree");
    EXPECT_EQ(b.LineStart(5), 4u);
    EXPECT_EQ(b.LineStart(6), 4u);
    EXPECT_EQ(b.LineStart(4), 4u);
}

TEST(TextBuffer, LineEndFindsEOL) {
    TextBuffer b;
    b.SetText("one\ntwo\nthree");
    EXPECT_EQ(b.LineEnd(5), 7u);
    EXPECT_EQ(b.LineEnd(4), 7u);
    EXPECT_EQ(b.LineEnd(0), 3u);
}

TEST(TextBuffer, LineStartOnFirstLine) {
    TextBuffer b;
    b.SetText("hello\nworld");
    EXPECT_EQ(b.LineStart(0), 0u);
    EXPECT_EQ(b.LineStart(3), 0u);
}

TEST(TextBuffer, LineEndOnLastLine) {
    TextBuffer b;
    b.SetText("hello\nworld");
    EXPECT_EQ(b.LineEnd(6), 11u);
}

TEST(TextBuffer, LineEndOnLastLineNoNewline) {
    TextBuffer b;
    b.SetText("hello");
    EXPECT_EQ(b.LineEnd(0), 5u);
}

TEST(TextBuffer, DirtyFlag) {
    TextBuffer b;
    b.SetText("test");
    EXPECT_TRUE(b.Dirty());
    b.ClearDirty();
    EXPECT_FALSE(b.Dirty());
    b.Splice(0, 0, "x");
    EXPECT_TRUE(b.Dirty());
}

