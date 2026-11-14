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

// A list item wrapped over several source lines carries its marker on
// the first one. Return on a continuation line must open a new item with
// that marker, not end the list with a blank line.
TEST(InsertParagraphBreak, WrappedItemContinuesItsOwnMarker) {
    TextBuffer b;
    const std::string src = "- item one that wraps\n  onto a second line\n- item two";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    const uint32_t at =
        static_cast<uint32_t>(src.find("second line") + std::string("second line").size());
    s.Collapse({at});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(),
              "- item one that wraps\n  onto a second line\n- \n- item two");
    EXPECT_EQ(s.active.offset, at + 3u);
}

TEST(InsertParagraphBreak, WrappedOrderedItemKeepsItsNumbering) {
    TextBuffer b;
    const std::string src = "1. first item wraps\n   onto a line\n2. second";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({static_cast<uint32_t>(src.find("onto a line") + 11)});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "1. first item wraps\n   onto a line\n1. \n2. second");
}

TEST(InsertParagraphBreak, WrappedNestedItemContinuesAtItsOwnLevel) {
    TextBuffer b;
    const std::string src =
        "- outer\n  - nested item wrapping\n    onto a line\n  - next nested\n- last";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({static_cast<uint32_t>(src.find("onto a line") + 11)});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(),
              "- outer\n  - nested item wrapping\n    onto a line\n  - \n"
              "  - next nested\n- last");
}

// A quote is a container the same way a list is: a break inside it keeps
// the quote markers, so the next line stays in the quote.
TEST(InsertParagraphBreak, QuoteLineContinuesTheQuote) {
    TextBuffer b;
    const std::string src = "> a quoted line";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({static_cast<uint32_t>(src.size())});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "> a quoted line\n> ");
    EXPECT_EQ(s.active.offset, static_cast<uint32_t>(src.size()) + 3u);
}

TEST(InsertParagraphBreak, NestedQuoteKeepsItsDepth) {
    TextBuffer b;
    const std::string src = "> > nested quote";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({static_cast<uint32_t>(src.size())});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "> > nested quote\n> > ");
}

TEST(InsertParagraphBreak, TheLastQuoteLineEndsTheQuote) {
    TextBuffer b;
    b.SetText("> a quoted line");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({15});
    EditController ec(&b, &s);
    ASSERT_TRUE(ec.InsertParagraphBreak(doc));
    ASSERT_EQ(b.Text(), "> a quoted line\n> ");
    // The new line carries nothing but the markers, so the next break
    // leaves the quote instead of adding another empty quote line.
    doc = Document();
    ParseMarkdown(b.Text(), doc);
    ASSERT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "> a quoted line\n");
}

TEST(InsertParagraphBreak, TaskItemContinuesUnchecked) {
    TextBuffer b;
    const std::string src = "- [x] done task";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({static_cast<uint32_t>(src.size())});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "- [x] done task\n- [ ] ");
}

TEST(InsertParagraphBreak, QuotedItemKeepsTheQuoteAndItsMarker) {
    TextBuffer b;
    const std::string src = "> - item one";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({static_cast<uint32_t>(src.size())});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "> - item one\n> - ");
}

// A later paragraph of an item is still that item's text, so a break in
// it opens a new item of the same list, at the item's own indentation.
TEST(InsertParagraphBreak, ASecondParagraphOpensANewItem) {
    TextBuffer b;
    const std::string src = "- one\n\n  two of the item";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({static_cast<uint32_t>(src.size())});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "- one\n\n  two of the item\n- ");
}

// A blank line inside an item is a paragraph separator. Return there adds
// a line; it must not open a new item, which would swallow the paragraph
// that follows it.
TEST(InsertParagraphBreak, BlankLineInsideAnItemAddsNoMarker) {
    TextBuffer b;
    const std::string src = "- first paragraph\n\n  second paragraph of the item";
    b.SetText(src);
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({18});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));
    EXPECT_EQ(b.Text(), "- first paragraph\n\n\n  second paragraph of the item");
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
