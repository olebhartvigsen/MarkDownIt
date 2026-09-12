#include "gtest_lite.h"
#include "../src/zoommodel.h"

TEST(ZoomModel, Constants) {
    EXPECT_EQ(zoom::kDefaultZoom, 1.0f);
    EXPECT_EQ(zoom::kMinZoom, 0.25f);
    EXPECT_EQ(zoom::kMaxZoom, 4.0f);
    EXPECT_EQ(zoom::kZoomStep, 1.25f);
}

TEST(ZoomModel, ClampKeepsInRangeValues) {
    EXPECT_EQ(zoom::Clamp(1.0f), 1.0f);
    EXPECT_EQ(zoom::Clamp(0.25f), 0.25f);
    EXPECT_EQ(zoom::Clamp(4.0f), 4.0f);
    EXPECT_EQ(zoom::Clamp(2.5f), 2.5f);
}

TEST(ZoomModel, ClampToLowerBound) {
    // 25% floor: anything below clamps back into range.
    EXPECT_EQ(zoom::Clamp(0.1f), 0.25f);
    EXPECT_EQ(zoom::Clamp(0.0f), 0.25f);
    EXPECT_EQ(zoom::Clamp(-1.0f), 0.25f);
}

TEST(ZoomModel, ClampToUpperBound) {
    // 400% ceiling: anything above clamps back into range.
    EXPECT_EQ(zoom::Clamp(4.5f), 4.0f);
    EXPECT_EQ(zoom::Clamp(100.0f), 4.0f);
}

TEST(ZoomModel, DefaultIsResetTarget) {
    // ResetZoom() restores kDefaultZoom; the neutral level is 100%
    // and sits inside the clamped range.
    EXPECT_EQ(zoom::Clamp(zoom::kDefaultZoom), 1.0f);
}
