#include "gtest_lite.h"
#include "editcontroller.h"
#include "textbuffer.h"
#include "caret.h"

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
