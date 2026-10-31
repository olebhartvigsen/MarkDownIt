#include "gtest_lite.h"

#include "editcontroller.h"
#include "textbuffer.h"
#include "undostack.h"

// The IME rules of the text editing spec ("Almindeligt tekstinput"): a
// temporary composition is text input, it creates no undo entry before
// it is confirmed, the confirmation is exactly one entry, and a
// cancelled composition puts the text and the selection back.

TEST(Composition, MarkedTextIsNotAnUndoStepUntilItIsConfirmed) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.Collapse({5});
    UndoStack u;
    EditController ec(&b, &s);
    ec.SetUndoStack(&u);

    ec.BeginComposition();
    ec.SetCompositionText("x");
    EXPECT_EQ(b.Text(), "hellox world");
    ec.SetCompositionText("xy");
    EXPECT_EQ(b.Text(), "helloxy world");
    EXPECT_TRUE(ec.HasComposition());
    EXPECT_TRUE(!u.CanUndo());

    ec.CommitComposition();
    EXPECT_TRUE(!ec.HasComposition());
    EXPECT_TRUE(u.CanUndo());
    UndoEntry entry;
    EXPECT_TRUE(u.Undo(entry));
    EXPECT_EQ(entry.removed, std::string(""));
    EXPECT_EQ(entry.inserted, std::string("xy"));
    EXPECT_TRUE(!u.CanUndo());
}

TEST(Composition, CancelPutsTextAndSelectionBack) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {0};
    s.active = {5};
    UndoStack u;
    EditController ec(&b, &s);
    ec.SetUndoStack(&u);

    ec.BeginComposition();
    ec.SetCompositionText("hi");
    EXPECT_EQ(b.Text(), "hi world");
    ec.CancelComposition();
    EXPECT_EQ(b.Text(), "hello world");
    EXPECT_EQ(s.Start(), 0u);
    EXPECT_EQ(s.Length(), 5u);
    EXPECT_TRUE(!u.CanUndo());
}

TEST(Composition, ConfirmingOverASelectionIsOneUndoEntry) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {6};
    s.active = {11};
    UndoStack u;
    EditController ec(&b, &s);
    ec.SetUndoStack(&u);

    ec.BeginComposition();
    ec.SetCompositionText("there");
    ec.CommitComposition();
    EXPECT_EQ(b.Text(), "hello there");
    EXPECT_TRUE(u.CanUndo());
    UndoEntry entry;
    EXPECT_TRUE(u.Undo(entry));
    EXPECT_EQ(entry.removed, std::string("world"));
    EXPECT_EQ(entry.inserted, std::string("there"));
    EXPECT_TRUE(!u.CanUndo());
}

TEST(Composition, TypingDuringACompositionClosesItFirst) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.Collapse({5});
    UndoStack u;
    EditController ec(&b, &s);
    ec.SetUndoStack(&u);

    ec.BeginComposition();
    ec.SetCompositionText("x");
    ec.InsertText("!");
    EXPECT_EQ(b.Text(), "hellox! world");
    EXPECT_TRUE(!ec.HasComposition());
    // Two entries: the confirmed composition, then the typed character.
    UndoEntry last;
    EXPECT_TRUE(u.Undo(last));
    EXPECT_EQ(last.inserted, std::string("!"));
    UndoEntry first;
    EXPECT_TRUE(u.Undo(first));
    EXPECT_EQ(first.inserted, std::string("x"));
    EXPECT_TRUE(!u.CanUndo());
}

TEST(Composition, AnEmptyCommitPutsTheReplacedTextBack) {
    TextBuffer b;
    b.SetText("hello world");
    Selection s;
    s.anchor = {0};
    s.active = {5};
    UndoStack u;
    EditController ec(&b, &s);
    ec.SetUndoStack(&u);

    ec.BeginComposition();
    ec.SetCompositionText("hi");
    ec.SetCompositionText("");
    ec.CommitComposition();
    EXPECT_EQ(b.Text(), "hello world");
    EXPECT_EQ(s.Start(), 0u);
    EXPECT_EQ(s.Length(), 5u);
    EXPECT_TRUE(!u.CanUndo());
}
