#include "gtest_lite.h"
#include "formatting.h"
#include "textbuffer.h"
#include "caret.h"

TEST(Formatting, ToggleBoldOnSelection) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {0}; s.active = {5};
    ToggleInlineMarker(&b, &s, "**");
    EXPECT_EQ(b.Text(), "**hello** world");
    // Selection should cover the whole bolded text including markers.
    EXPECT_EQ(s.Start(), 0u);
    EXPECT_EQ(s.Length(), 9u);  // "**hello**" = 9 chars
}

TEST(Formatting, ToggleBoldRemovesExisting) {
    TextBuffer b;
    b.SetText("**hello** world");
    Selection s;
    s.anchor = {0}; s.active = {9};
    ToggleInlineMarker(&b, &s, "**");
    EXPECT_EQ(b.Text(), "hello world");
    EXPECT_EQ(s.Start(), 0u);
    EXPECT_EQ(s.Length(), 5u);
}

TEST(Formatting, ToggleItalicOnSelection) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {0}; s.active = {5};
    ToggleInlineMarker(&b, &s, "*");
    EXPECT_EQ(b.Text(), "*hello* world");
}

TEST(Formatting, ToggleCodeOnSelection) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {0}; s.active = {5};
    ToggleInlineMarker(&b, &s, "`");
    EXPECT_EQ(b.Text(), "`hello` world");
}

TEST(Formatting, ToggleStrikethroughOnSelection) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {0}; s.active = {5};
    ToggleInlineMarker(&b, &s, "~~");
    EXPECT_EQ(b.Text(), "~~hello~~ world");
}

TEST(Formatting, ToggleBoldEmptySelection) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.Collapse({5});
    ToggleInlineMarker(&b, &s, "**");
    EXPECT_EQ(b.Text(), "hello**** world");
    EXPECT_EQ(s.active.offset, 7u);  // Between the two ** pairs.
}

TEST(Formatting, ToggleBoldPartialSelection) {
    // Selecting half of an existing bold run.
    TextBuffer b;
    b.SetText("**hello world**");
    Selection s;
    // Select just "hello" (inside the bold markers, positions 2-7).
    s.anchor = {2}; s.active = {7};
    ToggleInlineMarker(&b, &s, "**");
    // This should wrap "hello" in its own bold, splitting the run.
    // Result: **[**hello** world]** -> not exactly this, but the text
    // should have the markers added correctly.
    // The behavior: we add ** around the selection, which is inside
    // the existing bold. This is the naive case that splits runs.
    std::string text = b.Text();
    EXPECT_TRUE(text.find("**hello**") != std::string::npos);
}

TEST(Formatting, IsWrappedIn) {
    EXPECT_TRUE(IsWrappedIn("**hello**", 0, 9, "**"));
    EXPECT_FALSE(IsWrappedIn("*hello*", 0, 7, "**"));
    EXPECT_FALSE(IsWrappedIn("hello", 0, 5, "**"));
    EXPECT_TRUE(IsWrappedIn("`code`", 0, 6, "`"));
}

TEST(Formatting, InsertLinkOnSelection) {
    TextBuffer b;
    b.SetText("click here");
    Selection s;
    s.anchor = {0}; s.active = {10};
    InsertLink(&b, &s, "https://example.com");
    EXPECT_EQ(b.Text(), "[click here](https://example.com)");
}

TEST(Formatting, InsertLinkEmptySelection) {
    TextBuffer b;
    b.SetText("hello");
    Selection s;
    s.Collapse({5});
    InsertLink(&b, &s, "https://example.com");
    EXPECT_EQ(b.Text(), "hello[](https://example.com)");
    EXPECT_EQ(s.active.offset, 6u);  // Between [ and ].
}


TEST(Formatting, SetHeadingLevel2) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.Collapse({3});
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## hello world");
}

TEST(Formatting, SetHeadingLevel0RemovesHeading) {
    TextBuffer b;
    b.SetText("## hello world");
    Selection s;
    s.Collapse({5});
    SetHeadingLevel(&b, &s, 0);
    EXPECT_EQ(b.Text(), "hello world");
}

TEST(Formatting, ToggleUnorderedListAdds) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.Collapse({3});
    ToggleUnorderedList(&b, &s);
    EXPECT_EQ(b.Text(), "- hello world");
}

TEST(Formatting, ToggleUnorderedListRemoves) {
    TextBuffer b;
    b.SetText("- hello world");
    Selection s;
    s.Collapse({5});
    ToggleUnorderedList(&b, &s);
    EXPECT_EQ(b.Text(), "hello world");
}

TEST(Formatting, ToggleOrderedListAdds) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.Collapse({3});
    ToggleOrderedList(&b, &s);
    EXPECT_EQ(b.Text(), "1. hello world");
}

TEST(Formatting, ToggleOrderedListRemoves) {
    TextBuffer b;
    b.SetText("1. hello world");
    Selection s;
    s.Collapse({5});
    ToggleOrderedList(&b, &s);
    EXPECT_EQ(b.Text(), "hello world");
}

TEST(Formatting, ToggleBlockquoteAdds) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.Collapse({3});
    ToggleBlockquote(&b, &s);
    EXPECT_EQ(b.Text(), "> hello world");
}

TEST(Formatting, ToggleBlockquoteRemoves) {
    TextBuffer b;
    b.SetText("> hello world");
    Selection s;
    s.Collapse({5});
    ToggleBlockquote(&b, &s);
    EXPECT_EQ(b.Text(), "hello world");
}

TEST(Formatting, IndentLine) {
    TextBuffer b;
    b.SetText("hello\nworld");
    Selection s;
    s.Collapse({7});  // in "world"
    IndentLine(&b, &s);
    EXPECT_EQ(b.Text(), "hello\n  world");
    EXPECT_EQ(s.active.offset, 9u);
}

TEST(Formatting, OutdentLine) {
    TextBuffer b;
    b.SetText("  hello world");
    Selection s;
    s.Collapse({5});
    OutdentLine(&b, &s);
    EXPECT_EQ(b.Text(), "hello world");
    EXPECT_EQ(s.active.offset, 3u);
}
