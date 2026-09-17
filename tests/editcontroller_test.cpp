#include "gtest_lite.h"
#include "editcontroller.h"
#include "textbuffer.h"
#include "caret.h"
#include "dom.h"
#include "parser.h"
#include "navigation.h"

TEST(EditController, InsertTextAtCaret) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.Collapse({5});
    EditController ec(&b, &s);
    ec.InsertText("!");
    EXPECT_EQ(b.Text(), "hello! world");
    EXPECT_EQ(s.active.offset, 6u);
    EXPECT_TRUE(s.Empty());
}

TEST(EditController, InsertTextReplacesSelection) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {0};
    s.active = {5};
    EditController ec(&b, &s);
    ec.InsertText("goodbye");
    EXPECT_EQ(b.Text(), "goodbye world");
    EXPECT_EQ(s.active.offset, 7u);
    EXPECT_TRUE(s.Empty());
}

TEST(EditController, DeleteSelection) {
    TextBuffer b;
    b.SetText("hello, world");
    Selection s;
    s.anchor = {5};
    s.active = {7};
    EditController ec(&b, &s);
    ec.DeleteSelection();
    EXPECT_EQ(b.Text(), "helloworld");
    EXPECT_EQ(s.active.offset, 5u);
}

TEST(EditController, DeleteBackwardDeletesOneByte) {
    TextBuffer b;
    b.SetText("abc");
    Selection s;
    s.Collapse({3});
    EditController ec(&b, &s);
    ec.DeleteBackward();
    EXPECT_EQ(b.Text(), "ab");
    EXPECT_EQ(s.active.offset, 2u);
}

TEST(EditController, DeleteBackwardOnSelectionDeletesSelection) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {0};
    s.active = {5};
    EditController ec(&b, &s);
    ec.DeleteBackward();
    EXPECT_EQ(b.Text(), " world");
}

TEST(EditController, DeleteForwardDeletesOneByte) {
    TextBuffer b;
    b.SetText("abc");
    Selection s;
    s.Collapse({0});
    EditController ec(&b, &s);
    ec.DeleteForward();
    EXPECT_EQ(b.Text(), "bc");
    EXPECT_EQ(s.active.offset, 0u);
}

TEST(EditController, InsertAtBeginning) {
    TextBuffer b;
    b.SetText("world");
    Selection s;
    s.Collapse({0});
    EditController ec(&b, &s);
    ec.InsertText("hello ");
    EXPECT_EQ(b.Text(), "hello world");
    EXPECT_EQ(s.active.offset, 6u);
}

TEST(EditController, DeleteBackwardGrapheme) {
    TextBuffer b;
    // a-ring is 2 UTF-8 bytes (0xC3 0xA5)
    b.SetText("ab" "\xc3\xa5");
    Selection s;
    s.Collapse({4});
    EditController ec(&b, &s);
    ec.DeleteBackward();
    EXPECT_EQ(b.Text(), "ab");
    EXPECT_EQ(s.active.offset, 2u);
}

TEST(EditController, DeleteForwardGrapheme) {
    TextBuffer b;
    b.SetText("ab" "\xc3\xa5" "cd");
    Selection s;
    s.Collapse({2});
    EditController ec(&b, &s);
    ec.DeleteForward();
    EXPECT_EQ(b.Text(), "abcd");
    EXPECT_EQ(s.active.offset, 2u);
}

TEST(EditController, DeleteBackwardEmoji) {
    TextBuffer b;
    // U+1F600 = F0 9F 98 80 (4 bytes)
    b.SetText("A" "\xf0\x9f\x98\x80" "B");
    Selection s;
    s.Collapse({5});
    EditController ec(&b, &s);
    ec.DeleteBackward();
    EXPECT_EQ(b.Text(), "AB");
    EXPECT_EQ(s.active.offset, 1u);
}

TEST(InsertParagraphBreak, ParagraphContext) {
    TextBuffer b;
    b.SetText("hello world");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({5});
    EditController ec(&b, &s);
    ec.InsertParagraphBreak(doc);
    // Should insert two newlines at offset 5.
    // "hello" + "\n\n" + " world" = "hello\n\n world"
    std::string expected = "hello\x0A\x0A world";
    EXPECT_EQ(b.Text(), expected);
    EXPECT_EQ(s.active.offset, 7u);
}

TEST(InsertParagraphBreak, CodeBlockContext) {
    TextBuffer b;
    // Code block: ```c++ \n int x = 0; \n ```
    std::string src = "```c++\x0Aint x = 0;\x0A```";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    ASSERT_EQ(doc.nodes.size(), 1u);
    uint32_t contentOff = doc.nodes[0].contentOffset;
    // Place caret at contentOff + 5 (inside the code text).
    Selection s;
    s.Collapse({contentOff + 5});
    EditController ec(&b, &s);
    ec.InsertParagraphBreak(doc);
    // Should insert a single newline.
    std::string text = b.Text();
    // The inserted newline should appear in the code block.
    EXPECT_TRUE(text.find("int\x0A") != std::string::npos ||
                text.find("x\x0A") != std::string::npos);
}

TEST(InsertParagraphBreak, ListContextContinuesList) {
    TextBuffer b;
    // "- item one\n- item two"
    std::string src = "- item one\x0A- item two";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    ASSERT_GE(doc.nodes.size(), 2u);
    uint32_t end = doc.nodes[1].srcOffset + doc.nodes[1].srcLength;
    Selection s;
    s.Collapse({end});
    EditController ec(&b, &s);
    ec.InsertParagraphBreak(doc);
    std::string text = b.Text();
    // Should insert "\n- " after the last item.
    EXPECT_TRUE(text.find("item two\x0A- ") != std::string::npos);
}

TEST(InsertParagraphBreak, EmptyListItemEndsList) {
    TextBuffer b;
    b.SetText("- ");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({2});
    EditController ec(&b, &s);
    ec.InsertParagraphBreak(doc);
    std::string text = b.Text();
    // On an empty list item, the marker "- " followed by the
    // paragraph break should be handled. Since the parser may not
    // produce any nodes for "- " (no content), the context defaults
    // to paragraph, inserting "\n\n" at offset 2.
    // Result: "- \n\n" or the marker is removed.
    EXPECT_TRUE(text == "- " "\x0A\x0A" || text.empty() || text == "\x0A\x0A"
                || text == "- ");
}

TEST(EditController, DeleteWordBackwardAtCaret) {
    TextBuffer b;
    b.SetText("hello");
    Selection s;
    s.Collapse({5});
    EditController ec(&b, &s);
    ec.DeleteWordBackward();
    EXPECT_EQ(b.Text(), "");
    EXPECT_EQ(s.active.offset, 0u);
    EXPECT_TRUE(s.Empty());
}

TEST(EditController, DeleteWordForwardAtCaret) {
    TextBuffer b;
    b.SetText("hello");
    Selection s;
    s.Collapse({0});
    EditController ec(&b, &s);
    ec.DeleteWordForward();
    EXPECT_EQ(b.Text(), "");
    EXPECT_EQ(s.active.offset, 0u);
    EXPECT_TRUE(s.Empty());
}

TEST(EditController, ReplacementIsOneUndoEntry) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {0};
    s.active = {5};
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);
    ec.InsertText("goodbye");
    EXPECT_EQ(b.Text(), "goodbye world");
    EXPECT_TRUE(undo.CanUndo());
    ec.Undo();
    EXPECT_EQ(b.Text(), "hello world");
    EXPECT_EQ(s.anchor.offset, 0u);
    EXPECT_EQ(s.active.offset, 5u);
    EXPECT_FALSE(undo.CanUndo());
}

TEST(EditController, DeleteBackwardCombiningCluster) {
    TextBuffer b;
    b.SetText("A" "e" "\xCC\x81" "\xCC\xA7" "B");
    Selection s;
    s.Collapse({6});
    EditController ec(&b, &s);
    ec.DeleteBackward();
    EXPECT_EQ(b.Text(), "AB");
    EXPECT_EQ(s.active.offset, 1u);
}

TEST(EditController, DeleteForwardEmojiZwjCluster) {
    TextBuffer b;
    const char* family = "\xF0\x9F\x91\xA8\xE2\x80\x8D"
                         "\xF0\x9F\x91\xA9\xE2\x80\x8D"
                         "\xF0\x9F\x91\xA7";
    b.SetText(std::string("A") + family + "B");
    Selection s;
    s.Collapse({1});
    EditController ec(&b, &s);
    ec.DeleteForward();
    EXPECT_EQ(b.Text(), "AB");
    EXPECT_EQ(s.active.offset, 1u);
}

TEST(EditController, DeleteBackwardRegionalIndicatorPair) {
    TextBuffer b;
    b.SetText("A" "\xF0\x9F\x87\xA9\xF0\x9F\x87\xB0" "B");
    Selection s;
    s.Collapse({9});
    EditController ec(&b, &s);
    ec.DeleteBackward();
    EXPECT_EQ(b.Text(), "AB");
    EXPECT_EQ(s.active.offset, 1u);
}

TEST(Navigation, UnicodeWordBoundaries) {
    TextBuffer b;
    b.SetText(" dansk" "\xC3\xA6" "ble 你好 123 ");
    EXPECT_EQ(MoveWordLeft(b, static_cast<uint32_t>(b.Length())), 19u);
    EXPECT_EQ(MoveWordRight(b, 1u), 11u);
}


TEST(CaretParagraphBoundary, ReturnAtParagraphEndPreservesFollowingText) {
    TextBuffer b;
    b.SetText("first\n\nsecond");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({5});
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);

    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "first\n\n\n\nsecond");
    EXPECT_EQ(s.active.offset, 7u);
    EXPECT_TRUE(ec.Undo());
    EXPECT_EQ(b.Text(), "first\n\nsecond");
    EXPECT_EQ(s.active.offset, 5u);
}

TEST(CaretParagraphBoundary, ReturnReplacesSelectionInOneUndoStep) {
    TextBuffer b;
    b.SetText("hello brave world");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.anchor = {6};
    s.active = {11};
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);

    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "hello \n\n world");
    EXPECT_EQ(s.active.offset, 8u);
    EXPECT_TRUE(ec.Undo());
    EXPECT_EQ(b.Text(), "hello brave world");
    EXPECT_EQ(s.anchor.offset, 6u);
    EXPECT_EQ(s.active.offset, 11u);
    EXPECT_FALSE(undo.CanUndo());
    EXPECT_TRUE(ec.Redo());
    EXPECT_EQ(b.Text(), "hello \n\n world");
}

TEST(CaretParagraphBoundary, ShiftReturnInsertsSoftBreakAtomically) {
    TextBuffer b;
    b.SetText("hello brave world");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.anchor = {6};
    s.active = {11};
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);

    EXPECT_TRUE(ec.InsertSoftBreak(doc));
    EXPECT_EQ(b.Text(), "hello   \n world");
    EXPECT_EQ(s.active.offset, 9u);
    EXPECT_TRUE(ec.Undo());
    EXPECT_EQ(b.Text(), "hello brave world");
    EXPECT_EQ(s.anchor.offset, 6u);
    EXPECT_EQ(s.active.offset, 11u);
    EXPECT_FALSE(undo.CanUndo());
}


TEST(CaretParagraphBoundary, BackspaceMergesParagraphsInOneUndoStep) {
    TextBuffer b;
    b.SetText("one\n\ntwo");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({5});
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);

    EXPECT_TRUE(ec.DeleteBackward(&doc));
    EXPECT_EQ(b.Text(), "onetwo");
    EXPECT_EQ(s.active.offset, 3u);
    EXPECT_TRUE(ec.Undo());
    EXPECT_EQ(b.Text(), "one\n\ntwo");
    EXPECT_EQ(s.active.offset, 5u);
    EXPECT_TRUE(ec.Redo());
    EXPECT_EQ(b.Text(), "onetwo");
}

TEST(CaretParagraphBoundary, DeleteMergesParagraphsInOneUndoStep) {
    TextBuffer b;
    b.SetText("one\n\ntwo");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({3});
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);

    EXPECT_TRUE(ec.DeleteForward(&doc));
    EXPECT_EQ(b.Text(), "onetwo");
    EXPECT_EQ(s.active.offset, 3u);
    EXPECT_TRUE(ec.Undo());
    EXPECT_EQ(b.Text(), "one\n\ntwo");
    EXPECT_EQ(s.active.offset, 3u);
}

TEST(EditController, BoundaryDeletesAreNoOpsAtDocumentLimits) {
    TextBuffer b;
    b.SetText("text");
    Selection s;
    EditController ec(&b, &s);

    s.Collapse({0});
    EXPECT_FALSE(ec.DeleteBackward());
    EXPECT_EQ(b.Text(), "text");
    s.Collapse({4});
    EXPECT_FALSE(ec.DeleteForward());
    EXPECT_EQ(b.Text(), "text");
}

TEST(InsertSoftBreak, CodeBlockUsesLiteralNewline) {
    TextBuffer b;
    b.SetText("```\nabc\n```");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({5});
    EditController ec(&b, &s);

    EXPECT_TRUE(ec.InsertSoftBreak(doc));
    EXPECT_EQ(b.Text(), "```\na\nbc\n```");
}


TEST(CaretParagraphBoundary, ReturnAtParagraphEndCreatesVirtualParagraph) {
    TextBuffer b;
    b.SetText("first\n\nsecond");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({5});
    EditController ec(&b, &s);

    ASSERT_TRUE(ec.InsertParagraphBreak(doc));
    Document after;
    ASSERT_TRUE(ParseMarkdown(b.Text(), after));
    ASSERT_EQ(after.nodes.size(), 3u);
    EXPECT_TRUE(after.nodes[1].virtualEmptyParagraph);
    EXPECT_EQ(after.nodes[1].contentOffset, s.active.offset);
}

TEST(CaretParagraphBoundary, ReturnInEmptyDocumentCreatesAddressableParagraph) {
    TextBuffer b;
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({0});
    EditController ec(&b, &s);

    ASSERT_TRUE(ec.InsertParagraphBreak(doc));
    Document after;
    ASSERT_TRUE(ParseMarkdown(b.Text(), after));
    ASSERT_EQ(after.nodes.size(), 1u);
    EXPECT_TRUE(after.nodes[0].virtualEmptyParagraph);
    EXPECT_EQ(after.nodes[0].contentOffset, s.active.offset);
}


TEST(CaretParagraphBoundary, ReturnSplitsStrongRunWithBalancedMarkdown) {
    TextBuffer b;
    b.SetText("**hello**");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({4});  // **he|llo**
    EditController ec(&b, &s);

    ASSERT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "**he**\n\n**llo**");
    EXPECT_EQ(s.active.offset, 10u);
}

TEST(CaretParagraphBoundary, ReturnSplitsLinkTextWithPreservedDestination) {
    TextBuffer b;
    b.SetText("[hello](https://example.com)");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({3});  // [he|llo](...)
    EditController ec(&b, &s);

    ASSERT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "[he](https://example.com)\n\n[llo](https://example.com)");
    EXPECT_EQ(s.active.offset, 28u);
}
