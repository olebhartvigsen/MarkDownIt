#include "gtest_lite.h"
#include "../src/zoommodel.h"

TEST(ZoomModel, Constants) {
    EXPECT_EQ(zoom::kDefaultZoom, 1.0f);
    EXPECT_EQ(zoom::kMinZoom, 0.25f);
    EXPECT_EQ(zoom::kMaxZoom, 4.0f);
    // Ten percentage points per press, per the zoom guide.
    EXPECT_EQ(zoom::kZoomStepPct, 10.0f);
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

TEST(ZoomModel, StepMovesTenPercentagePoints) {
    // The guide asks for ten percentage points per press, so a press is
    // additive and lands on round values.
    EXPECT_NEAR(zoom::Step(1.0f, true), 1.10f, 1e-6f);
    EXPECT_NEAR(zoom::Step(1.10f, true), 1.20f, 1e-6f);
    EXPECT_NEAR(zoom::Step(1.0f, false), 0.90f, 1e-6f);
    EXPECT_NEAR(zoom::Step(3.0f, true), 3.10f, 1e-6f);
}

TEST(ZoomModel, StepIsUniformAcrossTheRange) {
    // A multiplier step would give a different number of points at every
    // level; an additive one gives the same. This is the property that
    // makes repeated presses predictable.
    const float fromLow  = zoom::Step(1.0f, true) - 1.0f;
    const float fromHigh = zoom::Step(3.0f, true) - 3.0f;
    EXPECT_NEAR(fromLow, 0.10f, 1e-6f);
    EXPECT_NEAR(fromHigh, 0.10f, 1e-6f);
}

TEST(ZoomModel, StepStopsExactlyAtTheLimits) {
    // The final press lands on the limit, not short of it.
    EXPECT_EQ(zoom::Step(zoom::kMaxZoom, true), zoom::kMaxZoom);
    EXPECT_EQ(zoom::Step(zoom::kMinZoom, false), zoom::kMinZoom);
    // And repeated presses at the limit stay there (idempotent).
    EXPECT_EQ(zoom::Step(zoom::Step(zoom::kMaxZoom, true), true), zoom::kMaxZoom);
}

TEST(ZoomModel, PercentRoundsToWholePoints) {
    EXPECT_EQ(zoom::Percent(1.0f), 100);
    EXPECT_EQ(zoom::Percent(zoom::kMinZoom), 25);
    EXPECT_EQ(zoom::Percent(zoom::kMaxZoom), 400);
    // Stepping from 100% must be able to reach both neighbours.
    EXPECT_EQ(zoom::Percent(zoom::Step(1.0f, true)), 110);
    EXPECT_EQ(zoom::Percent(zoom::Step(1.0f, false)), 90);
}

TEST(ZoomModel, PercentClampsOutOfRangeInput) {
    // A display must never show a value outside the declared range even
    // if something upstream hands it an out-of-range factor.
    EXPECT_EQ(zoom::Percent(0.0f), 25);
    EXPECT_EQ(zoom::Percent(100.0f), 400);
    EXPECT_EQ(zoom::Percent(-3.0f), 25);
}

TEST(ZoomModel, FitWidthScalesContentToTheViewport) {
    // 800 DIPs of content in a 1200 DIP viewport needs 150%.
    EXPECT_NEAR(zoom::FitWidth(1200.0f, 800.0f), 1.5f, 1e-5f);
    // Narrower viewport zooms out.
    EXPECT_NEAR(zoom::FitWidth(400.0f, 800.0f), 0.5f, 1e-5f);
}

TEST(ZoomModel, FitWidthRespectsTheRange) {
    // Fit width is an entry point like any other, so it cannot escape
    // the declared minimum or maximum.
    EXPECT_EQ(zoom::FitWidth(100000.0f, 800.0f), zoom::kMaxZoom);
    EXPECT_EQ(zoom::FitWidth(1.0f, 800.0f), zoom::kMinZoom);
}

TEST(ZoomModel, FitWidthRejectsDegenerateInput) {
    // A zero or negative width must not yield a zero or negative scale.
    EXPECT_EQ(zoom::FitWidth(800.0f, 0.0f), zoom::kDefaultZoom);
    EXPECT_EQ(zoom::FitWidth(800.0f, -800.0f), zoom::kDefaultZoom);
    EXPECT_EQ(zoom::FitWidth(0.0f, 800.0f), zoom::kDefaultZoom);
    EXPECT_EQ(zoom::FitWidth(-1.0f, 800.0f), zoom::kDefaultZoom);
}
