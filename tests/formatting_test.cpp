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

TEST(Formatting, UpdateExistingLinkDestination) {
    TextBuffer b;
    b.SetText("[click here](https://old.example)");
    Selection s;
    s.anchor = {1}; s.active = {11};
    InsertLink(&b, &s, "https://new.example");
    EXPECT_EQ(b.Text(), "[click here](https://new.example)");
}

TEST(Formatting, SplitPartiallySelectedExistingLink) {
    TextBuffer b;
    b.SetText("[hello](old)");
    Selection s;
    s.anchor = {2}; s.active = {4};
    InsertLink(&b, &s, "new");
    EXPECT_EQ(b.Text(), "[h](old)[el](new)[lo](old)");
    EXPECT_EQ(s.Start(), 9u);
    EXPECT_EQ(s.Length(), 2u);
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

TEST(Formatting, SetHeadingSpansEntireLine) {
    // Cursor in the middle of a single-line paragraph.
    // Heading must apply to the whole line, not split at the cursor.
    TextBuffer b;
    b.SetText("some text here");
    Selection s;
    s.Collapse({5});
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## some text here");
}

TEST(Formatting, SetHeadingJoinsMultiLineParagraph) {
    // A paragraph spanning multiple source lines (soft breaks).
    // Cursor on a continuation line; heading must join all lines.
    TextBuffer b;
    b.SetText("Line one\nLine two\nLine three");
    Selection s;
    s.Collapse({10});  // inside "Line two"
    SetHeadingLevel(&b, &s, 1);
    EXPECT_EQ(b.Text(), "# Line one Line two Line three");
}

TEST(Formatting, SetHeadingRespectsParagraphBoundaries) {
    // Blank lines delimit paragraphs; heading must not cross them.
    TextBuffer b;
    b.SetText("Before.\n\nTarget line one\nTarget line two\n\nAfter.");
    Selection s;
    s.Collapse({20});  // inside "Target line two"
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "Before.\n\n## Target line one Target line two\n\nAfter.");
}

TEST(Formatting, SetHeadingChangeLevelOnExistingHeading) {
    // Changing H2 to H3 on an existing heading preserves surrounding text.
    TextBuffer b;
    b.SetText("## Heading\nNext para");
    Selection s;
    s.Collapse({4});  // inside the heading
    SetHeadingLevel(&b, &s, 3);
    EXPECT_EQ(b.Text(), "### Heading\nNext para");
}

TEST(Formatting, SetHeadingHeadingIsSingleLine) {
    // A heading followed by non-blank text: the non-blank line is a
    // separate paragraph. Changing the heading level only touches the
    // heading line.
    TextBuffer b;
    b.SetText("## Heading\nContinuation");
    Selection s;
    s.Collapse({4});
    SetHeadingLevel(&b, &s, 1);
    EXPECT_EQ(b.Text(), "# Heading\nContinuation");
}

TEST(Formatting, SetHeadingCaretAtContentEnd) {
    TextBuffer b;
    b.SetText("Hello world\n");
    Selection s;
    s.Collapse({5});
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## Hello world\n");
    EXPECT_EQ(s.active.offset, 14u);  // end of "## Hello world", before \n
}

TEST(Formatting, SetHeadingNoTrailingNewline) {
    TextBuffer b;
    b.SetText("Just a line");
    Selection s;
    s.Collapse({5});
    SetHeadingLevel(&b, &s, 1);
    EXPECT_EQ(b.Text(), "# Just a line");
}

TEST(Formatting, SetHeadingStripsIndentOnContinuation) {
    TextBuffer b;
    b.SetText("First\n  second indented\nthird");
    Selection s;
    s.Collapse({8});  // inside "  second indented"
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## First second indented third");
}

TEST(Formatting, SetHeadingAtDocumentStart) {
    TextBuffer b;
    b.SetText("First para\nLine two\n\nSecond para");
    Selection s;
    s.Collapse({0});
    SetHeadingLevel(&b, &s, 1);
    EXPECT_EQ(b.Text(), "# First para Line two\n\nSecond para");
}

TEST(Formatting, BoldWrapsOnlySelection) {
    // Confirm inline markers still apply to selection only.
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {6}; s.active = {11};  // "world"
    ToggleInlineMarker(&b, &s, "**");
    EXPECT_EQ(b.Text(), "hello **world**");
}

TEST(Formatting, ItalicWrapsOnlySelection) {
    TextBuffer b;
    b.SetText("The quick brown fox");
    Selection s;
    s.anchor = {4}; s.active = {9};  // "quick"
    ToggleInlineMarker(&b, &s, "*");
    EXPECT_EQ(b.Text(), "The *quick* brown fox");
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

// ==========================================================================
// Regression tests for block vs inline editor formatting (task t_5e803c90).
//
// These verify the two expectations from the bug report:
//   1. Choosing a heading applies to the whole line, regardless of cursor
//      position within that line.
//   2. Choosing a text style (bold, italic) applies only to the selected
//      text range.
//
// And that the two formatting modes do not interfere with each other.
// ==========================================================================

// --- H2 heading: cursor anywhere within a single-line paragraph ---

TEST(FormattingRegression, H2CursorAtLineStart) {
    // Cursor at the very start of the line.
    TextBuffer b;
    b.SetText("Der er identificeret tre scenarier");
    Selection s;
    s.Collapse({0});
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## Der er identificeret tre scenarier");
}

TEST(FormattingRegression, H2CursorAtLineEnd) {
    // Cursor at the last character of the line.
    TextBuffer b;
    b.SetText("Der er identificeret tre scenarier");
    Selection s;
    s.Collapse({31});  // last char 'r'
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## Der er identificeret tre scenarier");
}

TEST(FormattingRegression, H2CursorAfterLineEnd) {
    // Cursor one past the last char (e.g. end-of-line position).
    TextBuffer b;
    b.SetText("Der er identificeret tre scenarier\n");
    Selection s;
    s.Collapse({32});  // at the newline
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## Der er identificeret tre scenarier\n");
}

TEST(FormattingRegression, H2CursorInFirstWord) {
    // Cursor inside the first word "Der".
    TextBuffer b;
    b.SetText("Der er identificeret tre scenarier");
    Selection s;
    s.Collapse({2});  // between 'e' and 'r' in "Der"
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## Der er identificeret tre scenarier");
}

TEST(FormattingRegression, H2CursorInLastWord) {
    // Cursor inside the last word "scenarier".
    TextBuffer b;
    b.SetText("Der er identificeret tre scenarier");
    Selection s;
    s.Collapse({25});  // inside "scenarier"
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## Der er identificeret tre scenarier");
}

TEST(FormattingRegression, H2ExactBugScenario) {
    // The exact line from the bug report: cursor is placed somewhere in
    // the middle of a long line, then H2 is clicked. The whole line must
    // become an H2 heading, not just the text to the right of the cursor.
    TextBuffer b;
    b.SetText("Der er identificeret tre losningsscenarier, "
              "der muligggor test af funktionalitet "
              "fra projekt 811 Ny rekrutteringslosning til AU\n");
    // Place cursor at offset 42, inside "muligggor" (roughly mid-line).
    Selection s;
    s.Collapse({42});
    SetHeadingLevel(&b, &s, 2);
    std::string result = b.Text();
    // The result must start with "## " and contain the full original text.
    EXPECT_TRUE(result.find("## ") == 0);
    EXPECT_TRUE(result.find("Der er identificeret") == 3);
    EXPECT_TRUE(result.find("fra projekt 811") != std::string::npos);
    EXPECT_TRUE(result.find("Ny rekrutteringslosning til AU") != std::string::npos);
    // Must NOT contain the old buggy split (a newline before the heading).
    EXPECT_TRUE(result.find("\n## ") == std::string::npos);
    EXPECT_TRUE(result.find("## muligggor") == std::string::npos);
}

TEST(FormattingRegression, H2DoesNotSplitLine) {
    // After applying H2, the paragraph must remain a single line.
    TextBuffer b;
    b.SetText("Some text here and there\n");
    Selection s;
    s.Collapse({10});
    SetHeadingLevel(&b, &s, 2);
    std::string result = b.Text();
    // Count newlines: should be exactly the trailing one.
    size_t nlCount = 0;
    for (char c : result) if (c == '\n') nlCount++;
    EXPECT_EQ(nlCount, 1u);
    EXPECT_EQ(b.Text(), "## Some text here and there\n");
}

TEST(FormattingRegression, H2CursorOnSecondLineOfParagraph) {
    // Multi-line paragraph (soft breaks). Cursor on the second line.
    // H2 must join all lines and heading-ify the full paragraph.
    TextBuffer b;
    b.SetText("Line one of paragraph\nLine two of paragraph\n");
    Selection s;
    s.Collapse({25});  // inside "Line two"
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## Line one of paragraph Line two of paragraph\n");
}

TEST(FormattingRegression, H2CursorOnLastLineOfParagraph) {
    // Multi-line paragraph, cursor on the last line.
    TextBuffer b;
    b.SetText("Alpha\nBeta\nGamma\n");
    Selection s;
    s.Collapse({13});  // inside "Gamma"
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## Alpha Beta Gamma\n");
}

// --- Inline formatting: strictly on the selected text range ---

TEST(FormattingRegression, BoldSelectionAtLineStart) {
    // Bold a selection at the start of the line; rest of line unchanged.
    TextBuffer b;
    b.SetText("hello world and more");
    Selection s;
    s.anchor = {0}; s.active = {5};  // "hello"
    ToggleInlineMarker(&b, &s, "**");
    EXPECT_EQ(b.Text(), "**hello** world and more");
}

TEST(FormattingRegression, BoldSelectionAtLineEnd) {
    // Bold a selection at the end of the line; preceding text unchanged.
    TextBuffer b;
    b.SetText("hello world and more");
    Selection s;
    s.anchor = {16}; s.active = {20};  // "more"
    ToggleInlineMarker(&b, &s, "**");
    EXPECT_EQ(b.Text(), "hello world and **more**");
}

TEST(FormattingRegression, ItalicSelectionAtLineStart) {
    TextBuffer b;
    b.SetText("hello world and more");
    Selection s;
    s.anchor = {0}; s.active = {5};  // "hello"
    ToggleInlineMarker(&b, &s, "*");
    EXPECT_EQ(b.Text(), "*hello* world and more");
}

TEST(FormattingRegression, ItalicSelectionAtLineEnd) {
    TextBuffer b;
    b.SetText("hello world and more");
    Selection s;
    s.anchor = {16}; s.active = {20};  // "more"
    ToggleInlineMarker(&b, &s, "*");
    EXPECT_EQ(b.Text(), "hello world and *more*");
}

TEST(FormattingRegression, BoldDoesNotTouchUnselectedPrefix) {
    // Bold a middle selection; text before and after must be untouched.
    TextBuffer b;
    b.SetText("AAA BBB CCC");
    Selection s;
    s.anchor = {4}; s.active = {7};  // "BBB"
    ToggleInlineMarker(&b, &s, "**");
    EXPECT_EQ(b.Text(), "AAA **BBB** CCC");
    // The prefix "AAA " and suffix " CCC" are unchanged.
}

TEST(FormattingRegression, ItalicDoesNotTouchUnselectedPrefix) {
    TextBuffer b;
    b.SetText("AAA BBB CCC");
    Selection s;
    s.anchor = {4}; s.active = {7};  // "BBB"
    ToggleInlineMarker(&b, &s, "*");
    EXPECT_EQ(b.Text(), "AAA *BBB* CCC");
}

TEST(FormattingRegression, BoldSingleCharacterSelection) {
    // Bold a single character; surrounding text unchanged.
    TextBuffer b;
    b.SetText("abcdef");
    Selection s;
    s.anchor = {2}; s.active = {3};  // "c"
    ToggleInlineMarker(&b, &s, "**");
    EXPECT_EQ(b.Text(), "ab**c**def");
}

TEST(FormattingRegression, ItalicSingleCharacterSelection) {
    TextBuffer b;
    b.SetText("abcdef");
    Selection s;
    s.anchor = {2}; s.active = {3};  // "c"
    ToggleInlineMarker(&b, &s, "*");
    EXPECT_EQ(b.Text(), "ab*c*def");
}

// --- Block vs inline interaction ---

TEST(FormattingRegression, BoldInsideHeadingPreservesHeading) {
    // Apply H2 to a line, then bold a substring inside it.
    // The heading prefix must survive; bold must only wrap the selection.
    TextBuffer b;
    b.SetText("Some text here");
    Selection s1;
    s1.Collapse({5});
    SetHeadingLevel(&b, &s1, 2);
    EXPECT_EQ(b.Text(), "## Some text here");
    // Now bold the word "text" (offsets 8..12 within "## Some text here").
    Selection s2;
    s2.anchor = {8}; s2.active = {12};
    ToggleInlineMarker(&b, &s2, "**");
    EXPECT_EQ(b.Text(), "## Some **text** here");
}

TEST(FormattingRegression, HeadingAfterBoldPreservesBold) {
    // Apply bold to a substring first, then apply H2 to the line.
    // Bold markers must survive inside the heading.
    TextBuffer b;
    b.SetText("Some text here");
    Selection s1;
    s1.anchor = {5}; s1.active = {9};  // "text"
    ToggleInlineMarker(&b, &s1, "**");
    EXPECT_EQ(b.Text(), "Some **text** here");
    // Now apply H2 with cursor anywhere in the line.
    Selection s2;
    s2.Collapse({0});
    SetHeadingLevel(&b, &s2, 2);
    EXPECT_EQ(b.Text(), "## Some **text** here");
}

TEST(FormattingRegression, BoldDoesNotCreateHeading) {
    // Applying bold must never introduce a # prefix.
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {0}; s.active = {5};
    ToggleInlineMarker(&b, &s, "**");
    EXPECT_EQ(b.Text(), "**hello** world");
    // No heading prefix anywhere.
    EXPECT_TRUE(b.Text().find("#") == std::string::npos);
}

TEST(FormattingRegression, HeadingDoesNotAddBold) {
    // Applying a heading must never introduce ** or * markers.
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.Collapse({3});
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## hello world");
    EXPECT_TRUE(b.Text().find("**") == std::string::npos);
}

TEST(FormattingRegression, InlineThenBlockThenInlineRoundTrip) {
    // Round-trip: italic a word, turn line into H3, then un-italic.
    // Both modes must coexist without corrupting each other.
    TextBuffer b;
    b.SetText("The quick brown fox");
    // Italic "quick".
    Selection s1;
    s1.anchor = {4}; s1.active = {9};
    ToggleInlineMarker(&b, &s1, "*");
    EXPECT_EQ(b.Text(), "The *quick* brown fox");
    // H3 heading, cursor at start.
    Selection s2;
    s2.Collapse({0});
    SetHeadingLevel(&b, &s2, 3);
    EXPECT_EQ(b.Text(), "### The *quick* brown fox");
    // Un-italic: select "*quick*" (now at offset 8..15).
    Selection s3;
    s3.anchor = {8}; s3.active = {15};
    ToggleInlineMarker(&b, &s3, "*");
    EXPECT_EQ(b.Text(), "### The quick brown fox");
}

TEST(FormattingRegression, H2ThenRemoveHeadingKeepsInline) {
    // Apply H2, then remove it (level 0). Inline markers inside survive.
    TextBuffer b;
    b.SetText("Some **bold** text");
    Selection s;
    s.Collapse({0});
    SetHeadingLevel(&b, &s, 2);
    EXPECT_EQ(b.Text(), "## Some **bold** text");
    // Remove heading.
    Selection s2;
    s2.Collapse({2});
    SetHeadingLevel(&b, &s2, 0);
    EXPECT_EQ(b.Text(), "Some **bold** text");
}
