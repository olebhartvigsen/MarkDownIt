#include "gtest_lite.h"
#include "searchreplace.h"
#include "textdrag.h"
#include "parser.h"
#include "navigation.h"
#include "editcontroller.h"

TEST(SearchReplace, FindsCaseInsensitiveAndWholeWord) {
    const auto all = FindTextMatches("Cat scatter CAT", "cat");
    EXPECT_EQ(all.size(), 3u);
    const auto words = FindTextMatches("Cat scatter CAT", "cat", {false, true});
    EXPECT_EQ(words.size(), 2u);
    EXPECT_EQ(words[0].start, 0u);
    EXPECT_EQ(words[1].start, 12u);
}

TEST(SearchReplace, UnicodeCaseAndWordBoundariesUseCodePoints) {
    const std::string text = "\xC3\x86" "ble " "\xC3\xA6" "ble caf\xC3\xA9 caf\xC3\xA9ine";
    const auto matches = FindTextMatches(text, "\xC3\xA6" "BLE", {false, true});
    EXPECT_EQ(matches.size(), 2u);
    EXPECT_EQ(matches[0].length, 5u);
}

TEST(SearchReplace, InvalidUtf8DoesNotSplitAValidMatch) {
    const std::string text("x\xC3\x28x", 4);
    const auto matches = FindTextMatches(text, "(", {true, false});
    EXPECT_EQ(matches.size(), 1u);
}
TEST(SearchReplace, ReplacesMatchesWithoutChangingOffsets) {
    const std::string text = "one two one";
    const auto matches = FindTextMatches(text, "one", {true, true});
    EXPECT_EQ(ReplaceTextMatches(text, matches, "three"), "three two three");
}

TEST(SearchReplace, EmptyQueryAndOverlappingMatchesAreSafe) {
    EXPECT_TRUE(FindTextMatches("abc", "").empty());
    const auto matches = FindTextMatches("aaa", "aa", {true, false});
    EXPECT_EQ(matches.size(), 1u);
}

TEST(SearchReplace, DoesNotMatchPartOfCombiningGrapheme) {
    const std::string text = "A" "e" "\xCC\x81" "B";
    const auto whole = FindTextMatches(text, "e" "\xCC\x81", {true, false});
    EXPECT_EQ(whole.size(), 1u);
    EXPECT_EQ(whole[0].start, 1u);
    EXPECT_EQ(whole[0].length, 3u);

    const auto combiningOnly = FindTextMatches(text, "\xCC\x81", {true, false});
    EXPECT_TRUE(combiningOnly.empty());
}

TEST(SearchReplace, DoesNotMatchInsideEmojiZwjGrapheme) {
    const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D"
                               "\xF0\x9F\x91\xA9\xE2\x80\x8D"
                               "\xF0\x9F\x91\xA7";
    const std::string text = "A" + family + "B";
    const auto whole = FindTextMatches(text, family, {true, false});
    EXPECT_EQ(whole.size(), 1u);
    EXPECT_EQ(whole[0].start, 1u);
    EXPECT_EQ(whole[0].length, family.size());

    const std::string man = "\xF0\x9F\x91\xA8";
    EXPECT_TRUE(FindTextMatches(text, man, {true, false}).empty());
}

TEST(SearchReplace, SupplementaryWordIsWordCharacter) {
    const std::string text = "A\xF0\x90\x90\x80";
    const auto matches = FindTextMatches(text, "A", {true, true});
    EXPECT_TRUE(matches.empty());
}

TEST(TextDrag, MovesRangeForwardAndReportsNewStart) {
    std::string moved;
    uint32_t newStart = 0;
    EXPECT_TRUE(MoveTextRange("one two three", 0, 3, 13, &moved, &newStart));
    EXPECT_EQ(moved, " two threeone");
    EXPECT_EQ(newStart, 10u);
}

TEST(TextDrag, MovesRangeBackwardAndReportsNewStart) {
    std::string moved;
    uint32_t newStart = 0;
    EXPECT_TRUE(MoveTextRange("one two three", 8, 5, 0,
                              &moved, &newStart));
    EXPECT_EQ(moved, "threeone two ");
    EXPECT_EQ(newStart, 0u);
}

TEST(TextDrag, RejectsDropInsideSelection) {
    std::string moved;
    uint32_t newStart = 0;
    EXPECT_FALSE(MoveTextRange("abcdef", 1, 3, 2, &moved, &newStart));
}

TEST(TextDrag, MovesCombiningGraphemeAsOneRange) {
    const std::string text = "A" "e" "\xCC\x81" "B";
    std::string moved;
    uint32_t newStart = 0;
    EXPECT_TRUE(MoveTextRange(text, 1, 3,
                              static_cast<uint32_t>(text.size()),
                              &moved, &newStart));
    EXPECT_EQ(moved, "AB" "e" "\xCC\x81");
    EXPECT_EQ(newStart, 2u);

    EXPECT_FALSE(MoveTextRange(text, 2, 1,
                               static_cast<uint32_t>(text.size()),
                               &moved, &newStart));
}

TEST(TextDrag, EnforcesEmojiZwjGraphemeBoundaries) {
    const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D"
                               "\xF0\x9F\x91\xA9\xE2\x80\x8D"
                               "\xF0\x9F\x91\xA7";
    const std::string text = "A" + family + "B";
    std::string moved;
    uint32_t newStart = 0;
    EXPECT_FALSE(MoveTextRange(text, 1, 4,
                               static_cast<uint32_t>(text.size()),
                               &moved, &newStart));
    EXPECT_FALSE(MoveTextRange(text, 0, 1, 1 + 4,
                               &moved, &newStart));
    EXPECT_TRUE(MoveTextRange(text, 1,
                              static_cast<uint32_t>(family.size()),
                              static_cast<uint32_t>(text.size()),
                              &moved, &newStart));
    EXPECT_EQ(moved, "AB" + family);
    EXPECT_EQ(newStart, 2u);
}

TEST(TextDrag, MovesRegionalIndicatorFlagAsOneGrapheme) {
    const std::string flag = "\xF0\x9F\x87\xBA" "\xF0\x9F\x87\xB8";
    const std::string text = "A" + flag + "B";
    std::string moved;
    uint32_t newStart = 0;
    EXPECT_FALSE(MoveTextRange(text, 1, 4,
                               static_cast<uint32_t>(text.size()),
                               &moved, &newStart));
    EXPECT_FALSE(MoveTextRange(text, 0, 1, 1 + 4,
                               &moved, &newStart));
    EXPECT_TRUE(MoveTextRange(text, 1,
                              static_cast<uint32_t>(flag.size()),
                              static_cast<uint32_t>(text.size()),
                              &moved, &newStart));
    EXPECT_EQ(moved, "AB" + flag);
    EXPECT_EQ(newStart, 2u);
}

TEST(Navigation, TableTabMovesToAdjacentCell) {
    TextBuffer b;
    b.SetText("| a | b |\n|---|---|\n| c | d |");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    uint32_t first = 0;
    bool found = false;
    for (const auto& node : doc.nodes) {
        if (node.block == BlockKind::Table && !node.rows.empty() &&
            !node.rows[0].cells.empty()) {
            first = node.rows[0].cells[0].srcOffset;
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
    uint32_t next = 0;
    EXPECT_TRUE(MoveTableCell(doc, first, false, &next));
    EXPECT_TRUE(next > first);
}

TEST(Navigation, TableTabStopsAtTableBoundary) {
    TextBuffer b;
    b.SetText("| a | b |\n|---|---|\n\n| c | d |");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    uint32_t last = 0;
    bool found = false;
    for (const auto& node : doc.nodes) {
        if (node.block == BlockKind::Table && !node.rows.empty() &&
            node.rows[0].cells.size() >= 2) {
            const auto& cell = node.rows[0].cells.back();
            if (!cell.text.empty()) {
                last = cell.srcOffset;
                for (char32_t cp : cell.text)
                    last += cp <= 0x7F ? 1 : cp <= 0x7FF ? 2 :
                            cp <= 0xFFFF ? 3 : 4;
                found = true;
                break;
            }
        }
    }
    ASSERT_TRUE(found);
    uint32_t destination = 0;
    EXPECT_FALSE(MoveTableCell(doc, last, false, &destination));
}
TEST(Navigation, TableShiftTabStopsAtFirstCell) {
    TextBuffer b;
    b.SetText("| a | b |\n|---|---|");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    uint32_t first = 0;
    bool found = false;
    for (const auto& node : doc.nodes) {
        if (node.block == BlockKind::Table && !node.rows.empty() &&
            !node.rows[0].cells.empty()) {
            first = node.rows[0].cells[0].srcOffset;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);
    uint32_t destination = 0;
    EXPECT_FALSE(MoveTableCell(doc, first, true, &destination));
}

TEST(Navigation, EmptyTableCellIsNotSkipped) {
    TextBuffer b;
    b.SetText("| a | b | c |\n|---|---|---|\n| d || f |");
    Document doc;
    ParseMarkdown(b.Text(), doc);

    const Node* table = nullptr;
    for (const auto& node : doc.nodes) {
        if (node.block == BlockKind::Table) {
            table = &node;
            break;
        }
    }
    ASSERT_TRUE(table != nullptr);
    ASSERT_GE(table->rows.size(), 2u);
    ASSERT_EQ(table->rows[1].cells.size(), 3u);
    ASSERT_TRUE(table->rows[1].cells[1].text.empty());

    const auto& beforeEmpty = table->rows[1].cells[0];
    const auto& empty = table->rows[1].cells[1];
    const auto& afterEmpty = table->rows[1].cells[2];
    uint32_t destination = 0;
    EXPECT_TRUE(MoveTableCell(doc, beforeEmpty.srcOffset, false,
                              &destination));
    EXPECT_EQ(destination, table->rows[1].cells[1].srcOffset);

    EXPECT_TRUE(MoveTableCell(doc, afterEmpty.srcOffset, true,
                              &destination));
    EXPECT_EQ(destination, empty.srcOffset);

    EXPECT_TRUE(MoveTableCell(doc, empty.srcOffset, false, &destination));
    EXPECT_EQ(destination, afterEmpty.srcOffset);

    EXPECT_TRUE(MoveTableCell(doc, empty.srcOffset, true, &destination));
    EXPECT_EQ(destination, beforeEmpty.srcOffset + 1u);
}

TEST(EditController, SelectedEnterInTableIsNoOp) {
    TextBuffer b;
    b.SetText("| a | b |\n|---|---|\n| c | d |");
    const std::string original = b.Text();
    Document doc;
    ParseMarkdown(b.Text(), doc);

    uint32_t cellStart = 0;
    bool found = false;
    for (const auto& node : doc.nodes) {
        if (node.block == BlockKind::Table && node.rows.size() >= 2 &&
            !node.rows[1].cells.empty()) {
            cellStart = node.rows[1].cells[0].srcOffset;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    Selection s;
    s.anchor = {cellStart};
    s.active = {cellStart + 1};
    const Selection before = s;
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);
    EXPECT_FALSE(ec.InsertParagraphBreak(doc));

    EXPECT_EQ(b.Text(), original);
    EXPECT_EQ(s.anchor.offset, before.anchor.offset);
    EXPECT_EQ(s.active.offset, before.active.offset);
    EXPECT_FALSE(undo.CanUndo());
}

TEST(EditController, CodeBlockIndentationUsesFourSpaces) {
    TextBuffer b;
    b.SetText("```\ncode\n```");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    ASSERT_EQ(doc.nodes.size(), 1u);
    ASSERT_EQ(static_cast<int>(doc.nodes[0].block),
              static_cast<int>(BlockKind::CodeBlock));

    Selection s;
    s.Collapse({doc.nodes[0].contentOffset});
    EditController ec(&b, &s);
    ec.InsertText("    ");

    EXPECT_EQ(b.Text(), "```\n    code\n```");
    EXPECT_EQ(s.active.offset, doc.nodes[0].contentOffset + 4u);
}

TEST(EditController, EnterContinuesUnorderedListExactly) {
    TextBuffer b;
    b.SetText("- first");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({static_cast<uint32_t>(b.Length())});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));

    EXPECT_EQ(b.Text(), "- first\n- ");
    EXPECT_EQ(s.active.offset, 10u);
}

TEST(EditController, EnterContinuesOrderedListExactly) {
    TextBuffer b;
    b.SetText("1. first");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({static_cast<uint32_t>(b.Length())});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));

    EXPECT_EQ(b.Text(), "1. first\n1. ");
    EXPECT_EQ(s.active.offset, 12u);
}

TEST(EditController, EnterOnEmptyListItemEndsTheListItem) {
    TextBuffer b;
    b.SetText("- ");
    Document doc;
    ParseMarkdown(b.Text(), doc);
    Selection s;
    s.Collapse({2});
    EditController ec(&b, &s);
    EXPECT_TRUE(ec.InsertParagraphBreak(doc));

    EXPECT_EQ(b.Text(), "");
    EXPECT_EQ(s.active.offset, 0u);
}

// AC-03 and caret criterion F: an empty bullet is removed as one
// structural edit, not a byte deletion or a merge with the preceding item.
TEST(EmptyBulletBackspace, RemovesMarkerAndIndentationInOneUndoStep) {
    struct Case {
        const char* source;
        uint32_t caret;
        const char* expected;
        uint32_t expectedCaret;
    };
    const Case cases[] = {
        {"- ", 2, "", 0},
        {"* ", 2, "", 0},
        {"+ ", 2, "", 0},
        {"-", 1, "", 0},
        {"- \t ", 4, "", 0},
        {"- first\n- ", 10, "- first\n", 8},
        {"- first\n- \n- third", 10, "- first\n\n- third", 8},
        {"- first\r\n- \r\n- third", 11, "- first\r\n\r\n- third", 9},
        {"- \n- next", 2, "\n- next", 0},
        {"- parent\n  - ", 13, "- parent\n", 9},
        {"- parent\n  - \n- next", 13, "- parent\n\n- next", 9},
    };
    for (const auto& c : cases) {
        TextBuffer b;
        b.SetText(c.source);
        Document doc;
        ASSERT_TRUE(ParseMarkdown(b.Text(), doc));
        Selection s;
        s.Collapse({c.caret});
        UndoStack undo;
        EditController ec(&b, &s);
        ec.SetUndoStack(&undo);

        ASSERT_TRUE(ec.DeleteBackward(&doc));
        EXPECT_EQ(b.Text(), std::string(c.expected));
        EXPECT_EQ(s.anchor.offset, c.expectedCaret);
        EXPECT_EQ(s.active.offset, c.expectedCaret);
        EXPECT_TRUE(s.Empty());

        Document after;
        ASSERT_TRUE(ParseMarkdown(b.Text(), after));
        bool foundParagraph = false;
        for (const auto& node : after.nodes) {
            if (node.block == BlockKind::Paragraph &&
                node.virtualEmptyParagraph &&
                node.contentOffset == c.expectedCaret) foundParagraph = true;
        }
        EXPECT_TRUE(foundParagraph);

        ASSERT_TRUE(ec.Undo());
        EXPECT_EQ(b.Text(), std::string(c.source));
        EXPECT_EQ(s.anchor.offset, c.caret);
        EXPECT_EQ(s.active.offset, c.caret);
        EXPECT_FALSE(undo.CanUndo());
        ASSERT_TRUE(ec.Redo());
        EXPECT_EQ(b.Text(), std::string(c.expected));
        EXPECT_EQ(s.anchor.offset, c.expectedCaret);
        EXPECT_EQ(s.active.offset, c.expectedCaret);
        EXPECT_FALSE(undo.CanRedo());
    }
}

TEST(EmptyBulletBackspace, DoesNotCoalesceWithPreviousTextDeletion) {
    TextBuffer b;
    b.SetText("- x");
    Selection s;
    s.Collapse({3});
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);
    Document doc;
    ASSERT_TRUE(ParseMarkdown(b.Text(), doc));
    ASSERT_TRUE(ec.DeleteBackward(&doc));
    EXPECT_EQ(b.Text(), "- ");
    doc = Document{};
    ASSERT_TRUE(ParseMarkdown(b.Text(), doc));
    ASSERT_TRUE(ec.DeleteBackward(&doc));
    EXPECT_EQ(b.Text(), "");
    ASSERT_TRUE(ec.Undo());
    EXPECT_EQ(b.Text(), "- ");
    EXPECT_EQ(s.active.offset, 2u);
    ASSERT_TRUE(ec.Undo());
    EXPECT_EQ(b.Text(), "- x");
    EXPECT_EQ(s.active.offset, 3u);
    EXPECT_FALSE(undo.CanUndo());
}

TEST(EmptyBulletBackspace, OrdinaryDeletionInCodeAndLiteralText) {
    struct Case { const char* source; uint32_t caret; const char* expected; };
    const Case cases[] = {
        {"```\n- \n```", 6, "```\n-\n```"},
        {"    - ", 6, "    -"},
        {"\\- ", 3, "\\-"},
        {"- text", 6, "- tex"},
        {"1. ", 3, "1."},
        {"plain - ", 8, "plain -"},
    };
    for (const auto& c : cases) {
        TextBuffer b;
        b.SetText(c.source);
        Document doc;
        ASSERT_TRUE(ParseMarkdown(b.Text(), doc));
        Selection s;
        s.Collapse({c.caret});
        EditController ec(&b, &s);
        ASSERT_TRUE(ec.DeleteBackward(&doc));
        EXPECT_EQ(b.Text(), std::string(c.expected));
        EXPECT_EQ(s.active.offset, c.caret - 1);
    }
}

TEST(EmptyBulletBackspace, SourceViewUsesOrdinaryDeletion) {
    TextBuffer b;
    b.SetText("- ");
    Selection s;
    s.Collapse({2});
    EditController ec(&b, &s);
    ASSERT_TRUE(ec.DeleteBackward());
    EXPECT_EQ(b.Text(), "-");
    EXPECT_EQ(s.active.offset, 1u);
}

TEST(EmptyBulletBackspace, RefreshHintIsLimitedToEmptyBulletLines) {
    TextBuffer b;
    Selection s;
    EditController ec(&b, &s);
    for (const std::string text : {"- ", "* ", "+ ", "-", "  - \t"}) {
        b.SetText(text);
        s.Collapse({static_cast<uint32_t>(b.Length())});
        EXPECT_TRUE(ec.MayRemoveEmptyBullet());
    }
    for (const std::string text : {"text", "- text", "1. ", "\\- ", ""}) {
        b.SetText(text);
        s.Collapse({static_cast<uint32_t>(b.Length())});
        EXPECT_FALSE(ec.MayRemoveEmptyBullet());
    }
    b.SetText("- ");
    s.anchor = {1};
    s.active = {2};
    EXPECT_FALSE(ec.MayRemoveEmptyBullet());
}

TEST(EmptyBulletBackspace, SelectionUsesOrdinaryDeletion) {
    TextBuffer b;
    b.SetText("- first\n- ");
    Document doc;
    ASSERT_TRUE(ParseMarkdown(b.Text(), doc));
    Selection s;
    s.anchor = {10};
    s.active = {9};
    UndoStack undo;
    EditController ec(&b, &s);
    ec.SetUndoStack(&undo);
    ASSERT_TRUE(ec.DeleteBackward(&doc));
    EXPECT_EQ(b.Text(), "- first\n-");
    EXPECT_EQ(s.active.offset, 9u);
    ASSERT_TRUE(ec.Undo());
    EXPECT_EQ(b.Text(), "- first\n- ");
    EXPECT_EQ(s.anchor.offset, 10u);
    EXPECT_EQ(s.active.offset, 9u);
    EXPECT_FALSE(undo.CanUndo());
}
