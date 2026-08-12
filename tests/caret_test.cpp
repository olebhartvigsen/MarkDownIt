#include "gtest_lite.h"
#include "caret.h"

TEST(Selection, EmptyWhenCollapsed) {
    Selection s;
    s.Collapse({5});
    EXPECT_TRUE(s.Empty());
}

TEST(Selection, NotEmptyWhenAnchorAndActiveDiffer) {
    Selection s;
    s.anchor = {3};
    s.active = {7};
    EXPECT_FALSE(s.Empty());
}

TEST(Selection, StartEndNormalOrder) {
    Selection s;
    s.anchor = {3};
    s.active = {8};
    EXPECT_EQ(s.Start(), 3u);
    EXPECT_EQ(s.End(), 8u);
    EXPECT_EQ(s.Length(), 5u);
}

TEST(Selection, StartEndReversedOrder) {
    Selection s;
    s.anchor = {10};
    s.active = {2};
    EXPECT_EQ(s.Start(), 2u);
    EXPECT_EQ(s.End(), 10u);
    EXPECT_EQ(s.Length(), 8u);
}

TEST(Selection, CollapseSetsBoth) {
    Selection s;
    s.anchor = {5};
    s.active = {15};
    s.Collapse({7});
    EXPECT_EQ(s.anchor.offset, 7u);
    EXPECT_EQ(s.active.offset, 7u);
    EXPECT_TRUE(s.Empty());
}

TEST(CaretPos, Equality) {
    CaretPos a{5};
    CaretPos b{5};
    CaretPos c{6};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(CaretPos, Ordering) {
    CaretPos a{3};
    CaretPos b{7};
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}
