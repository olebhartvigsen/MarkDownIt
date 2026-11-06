#include "gtest_lite.h"
#include "../src/tablepicker_model.h"

TEST(TablePickerModel, DefaultsToThreeByThree) {
    // Word opens the picker at 3 x 3, and so do both builds.
    TablePickerModel model;
    EXPECT_EQ(model.cols, 3);
    EXPECT_EQ(model.rows, 3);
}

TEST(TablePickerModel, LabelCountsTheHeaderRow) {
    // Three data rows are a "4 x 3 Table" because the header is part of
    // the table the user gets. The label reads rows first.
    TablePickerModel model;
    EXPECT_EQ(model.VisibleRows(), 4);
    EXPECT_EQ(model.Label(), std::string("4 x 3 Table"));
}

TEST(TablePickerModel, HoverSetsOneBasedCounts) {
    TablePickerModel model;
    model.Set(1, 1);
    EXPECT_EQ(model.cols, 1);
    EXPECT_EQ(model.rows, 1);
    EXPECT_EQ(model.VisibleRows(), 2);
    EXPECT_EQ(model.Label(), std::string("2 x 1 Table"));
}

TEST(TablePickerModel, ClampsToTheGrid) {
    TablePickerModel model;
    model.Set(0, 0);
    EXPECT_EQ(model.cols, 1);
    EXPECT_EQ(model.rows, 1);
    model.Set(11, 9);
    EXPECT_EQ(model.cols, 10);
    EXPECT_EQ(model.rows, 8);
    EXPECT_EQ(model.VisibleRows(), 9);
}

TEST(TablePickerModel, ArrowKeysMoveOneCellAndStopAtTheEdge) {
    TablePickerModel model;
    model.Move(1, 1);
    EXPECT_EQ(model.cols, 4);
    EXPECT_EQ(model.rows, 4);
    model.Move(-1, -1);
    EXPECT_EQ(model.cols, 3);
    EXPECT_EQ(model.rows, 3);

    // Nine presses to the left must stop at one column, not wrap.
    for (int i = 0; i < 9; ++i) model.Move(-1, -1);
    EXPECT_EQ(model.cols, 1);
    EXPECT_EQ(model.rows, 1);

    // And the same at the far corner.
    for (int i = 0; i < 20; ++i) model.Move(1, 1);
    EXPECT_EQ(model.cols, 10);
    EXPECT_EQ(model.rows, 8);
}
