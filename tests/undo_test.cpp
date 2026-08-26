#include "gtest_lite.h"
#include "undostack.h"
#include "textbuffer.h"
#include "caret.h"

static uint64_t T(uint64_t ms) { return ms; }

TEST(UndoStack, PushAndUndo) {
    UndoStack s;
    UndoEntry e{};
    e.offset = 0;
    e.removed = "";
    e.inserted = "hello";
    e.selBefore = {};
    e.selAfter = {CaretPos{5}};
    e.timestamp = T(100);
    e.type = EditType::Insert;
    s.Push(e);

    UndoEntry out;
    ASSERT_TRUE(s.Undo(out));
    EXPECT_EQ(out.inserted, "hello");
    EXPECT_FALSE(s.CanUndo());
}

TEST(UndoStack, CoalesceConsecutiveInserts) {
    UndoStack s;
    for (int i = 0; i < 5; i++) {
        char c = 'a' + i;
        UndoEntry e{};
        e.offset = static_cast<uint32_t>(i);
        e.removed = "";
        e.inserted = std::string(1, c);
        e.selAfter = {CaretPos{static_cast<uint32_t>(i + 1)}};
        e.timestamp = T(100 + i * 10);  // 10ms apart
        e.type = EditType::Insert;
        s.Push(e);
    }
    // Should coalesce into one entry.

    UndoEntry out;
    ASSERT_TRUE(s.Undo(out));
    EXPECT_EQ(out.inserted, "abcde");
    EXPECT_FALSE(s.CanUndo());
}

TEST(UndoStack, NoCoalesceOnWhitespace) {
    UndoStack s;
    UndoEntry e1{};
    e1.offset = 0; e1.inserted = "hi"; e1.timestamp = T(100);
    e1.type = EditType::Insert; e1.selAfter = {CaretPos{2}};
    s.Push(e1);

    UndoEntry e2{};
    e2.offset = 2; e2.inserted = " "; e2.timestamp = T(110);
    e2.type = EditType::Insert; e2.selAfter = {CaretPos{3}};
    s.Push(e2);

    // The space should break coalescing, so two entries.

    UndoEntry out;
    ASSERT_TRUE(s.Undo(out));
    EXPECT_EQ(out.inserted, " ");
    ASSERT_TRUE(s.Undo(out));
    EXPECT_EQ(out.inserted, "hi");
}

TEST(UndoStack, BreakCoalesceOnCaretMove) {
    UndoStack s;
    UndoEntry e1{};
    e1.offset = 0; e1.inserted = "ab"; e1.timestamp = T(100);
    e1.type = EditType::Insert; e1.selAfter = {CaretPos{2}};
    s.Push(e1);

    s.BreakCoalesce();

    UndoEntry e2{};
    e2.offset = 2; e2.inserted = "cd"; e2.timestamp = T(110);
    e2.type = EditType::Insert; e2.selAfter = {CaretPos{4}};
    s.Push(e2);

    UndoEntry out;
    ASSERT_TRUE(s.Undo(out));
    EXPECT_EQ(out.inserted, "cd");
    ASSERT_TRUE(s.Undo(out));
    EXPECT_EQ(out.inserted, "ab");
}

TEST(UndoStack, RedoAfterUndo) {
    UndoStack s;
    UndoEntry e{};
    e.offset = 0; e.inserted = "x"; e.timestamp = T(100);
    e.type = EditType::Insert; e.selAfter = {CaretPos{1}};
    s.Push(e);

    UndoEntry out;
    ASSERT_TRUE(s.Undo(out));
    EXPECT_FALSE(s.CanUndo());
    ASSERT_TRUE(s.CanRedo());
    ASSERT_TRUE(s.Redo(out));
    EXPECT_EQ(out.inserted, "x");
    EXPECT_FALSE(s.CanRedo());
}

TEST(UndoStack, RedoClearedByFreshEdit) {
    UndoStack s;
    UndoEntry e1{};
    e1.offset = 0; e1.inserted = "a"; e1.timestamp = T(100);
    e1.type = EditType::Insert; e1.selAfter = {CaretPos{1}};
    s.Push(e1);

    UndoEntry out;
    s.Undo(out);
    ASSERT_TRUE(s.CanRedo());

    UndoEntry e2{};
    e2.offset = 0; e2.inserted = "b"; e2.timestamp = T(200);
    e2.type = EditType::Insert; e2.selAfter = {CaretPos{1}};
    s.Push(e2);

    EXPECT_FALSE(s.CanRedo());
}

TEST(UndoStack, NoCoalesceOnDeleteAfterInsert) {
    UndoStack s;
    UndoEntry e1{};
    e1.offset = 0; e1.inserted = "abc"; e1.timestamp = T(100);
    e1.type = EditType::Insert; e1.selAfter = {CaretPos{3}};
    s.Push(e1);

    UndoEntry e2{};
    e2.offset = 2; e2.removed = "c"; e2.timestamp = T(110);
    e2.type = EditType::Delete; e2.selAfter = {CaretPos{2}};
    s.Push(e2);

    UndoEntry out;
    ASSERT_TRUE(s.Undo(out));
    EXPECT_EQ(out.removed, "c");
    ASSERT_TRUE(s.Undo(out));
    EXPECT_EQ(out.inserted, "abc");
}

TEST(UndoStack, CoalesceConsecutiveBackwardDeletes) {
    UndoStack s;
    // Delete "c" at offset 2, then "b" at offset 1, then "a" at offset 0
    for (int i = 2; i >= 0; i--) {
        UndoEntry e{};
        e.offset = static_cast<uint32_t>(i);
        e.removed = std::string(1, 'a' + i);
        e.timestamp = T(100 + (2 - i) * 10);
        e.type = EditType::Delete;
        e.selAfter = {CaretPos{static_cast<uint32_t>(i)}};
        s.Push(e);
    }

    UndoEntry out;
    ASSERT_TRUE(s.Undo(out));
    EXPECT_EQ(out.removed, "abc");
}
TEST(UndoStack, CoalesceConsecutiveForwardDeletes) {
    UndoStack s;
    // Forward deletes at the same offset: delete "a", then "b", then "c".
    for (int i = 0; i < 3; i++) {
        UndoEntry e{};
        e.offset = 0;
        e.removed = std::string(1, 'a' + i);
        e.timestamp = T(100 + i * 10);
        e.type = EditType::Delete;
        e.selAfter = {CaretPos{0}};
        s.Push(e);
    }

    UndoEntry out;
    ASSERT_TRUE(s.Undo(out));
    EXPECT_EQ(out.removed, "abc");
}
