#include "gtest_lite.h"
#include "../src/zoomhud.h"
#include <limits>

TEST(ZoomHud, ViewportMustFitTheWholePillAndStroke) {
    EXPECT_TRUE(zoomhud::FitsViewport(800.0f, 600.0f, 68.0f, 32.0f));
    EXPECT_TRUE(zoomhud::FitsViewport(70.0f, 51.0f, 68.0f, 32.0f));
    EXPECT_FALSE(zoomhud::FitsViewport(69.0f, 51.0f, 68.0f, 32.0f));
    EXPECT_FALSE(zoomhud::FitsViewport(70.0f, 50.0f, 68.0f, 32.0f));
    EXPECT_FALSE(zoomhud::FitsViewport(0.0f, 0.0f, 68.0f, 32.0f));
}

TEST(ZoomHud, StartsHidden) {
    zoomhud::State hud;
    EXPECT_FALSE(hud.Active());
    EXPECT_EQ(hud.Opacity(0), 0.0f);
    EXPECT_FALSE(hud.Tick(5000));
}

TEST(ZoomHud, ShowRoundsPercentAndFadesIn) {
    zoomhud::State hud;
    EXPECT_TRUE(hud.ShowZoomChange(1.0f, 1.1f, 100));
    EXPECT_TRUE(hud.Active());
    EXPECT_EQ(hud.Percent(), 110);
    EXPECT_EQ(hud.Opacity(100), 0.0f);
    EXPECT_NEAR(hud.Opacity(145), 0.5f, 0.0001f);
    EXPECT_EQ(hud.Opacity(190), 1.0f);
    EXPECT_TRUE(hud.Tick(190));
}

TEST(ZoomHud, UpdateDuringFadeInDoesNotBlink) {
    zoomhud::State hud;
    hud.ShowZoomChange(1.0f, 1.1f, 100);
    EXPECT_NEAR(hud.Opacity(145), 0.5f, 0.0001f);
    EXPECT_TRUE(hud.ShowZoomChange(1.1f, 1.2f, 145));
    EXPECT_EQ(hud.Opacity(145), 1.0f);
    EXPECT_EQ(hud.Percent(), 120);
    EXPECT_TRUE(hud.Tick(1200));
    EXPECT_FALSE(hud.Tick(1245));
}

TEST(ZoomHud, HoldAndFadeDeadlinesMatchMac) {
    zoomhud::State hud;
    hud.ShowZoomChange(1.0f, 1.2f, 100);
    EXPECT_EQ(hud.Opacity(949), 1.0f);
    EXPECT_EQ(hud.Opacity(950), 1.0f);
    EXPECT_NEAR(hud.Opacity(1075), 0.5f, 0.0001f);
    EXPECT_GT(hud.Opacity(1199), 0.0f);
    EXPECT_TRUE(hud.Tick(1199));
    EXPECT_EQ(hud.Opacity(1200), 0.0f);
    EXPECT_FALSE(hud.Tick(1200));
    EXPECT_FALSE(hud.Active());
}

TEST(ZoomHud, VisibleUpdateChangesDigitsAndRenewsHold) {
    zoomhud::State hud;
    hud.ShowZoomChange(1.0f, 1.1f, 0);
    EXPECT_TRUE(hud.ShowZoomChange(1.1f, 1.2f, 600));
    EXPECT_EQ(hud.Percent(), 120);
    EXPECT_EQ(hud.Opacity(600), 1.0f);
    EXPECT_TRUE(hud.Tick(1100));  // old expiry cannot hide the new value
    EXPECT_EQ(hud.Opacity(1400), 1.0f);
    EXPECT_FALSE(hud.Tick(1700));
}

TEST(ZoomHud, RetriggerDuringFadeOutStaysVisible) {
    zoomhud::State hud;
    hud.ShowZoomChange(1.0f, 1.1f, 0);
    EXPECT_NEAR(hud.Opacity(975), 0.5f, 0.0001f);
    EXPECT_TRUE(hud.ShowZoomChange(1.1f, 0.9f, 975));
    EXPECT_EQ(hud.Percent(), 90);
    EXPECT_EQ(hud.Opacity(975), 1.0f);
    EXPECT_TRUE(hud.Tick(1100));
    EXPECT_EQ(hud.Opacity(1825), 1.0f);
    EXPECT_FALSE(hud.Tick(2075));
}

TEST(ZoomHud, RetriggerAfterExpiryFadesInEvenBeforeTimerRetiresState) {
    zoomhud::State hud;
    hud.ShowZoomChange(1.0f, 1.1f, 0);
    EXPECT_TRUE(hud.ShowZoomChange(1.1f, 1.2f, 2000));
    EXPECT_EQ(hud.Opacity(2000), 0.0f);
    EXPECT_NEAR(hud.Opacity(2045), 0.5f, 0.0001f);
    EXPECT_EQ(hud.Percent(), 120);
}

TEST(ZoomHud, HideCancelsAndNextShowStartsFresh) {
    zoomhud::State hud;
    hud.ShowZoomChange(1.0f, 1.5f, 0);
    hud.Hide();
    hud.Hide();
    EXPECT_FALSE(hud.Active());
    EXPECT_EQ(hud.Opacity(100), 0.0f);
    EXPECT_FALSE(hud.Tick(200));
    EXPECT_TRUE(hud.ShowZoomChange(1.5f, 1.0f, 300));
    EXPECT_EQ(hud.Percent(), 100);
    EXPECT_EQ(hud.Opacity(300), 0.0f);
    EXPECT_EQ(hud.Opacity(390), 1.0f);
}

TEST(ZoomHud, ZoomLimitsAndResetNoOpStayHidden) {
    zoomhud::State hud;
    EXPECT_FALSE(hud.ShowZoomChange(1.0f, 1.0f, 0));
    EXPECT_FALSE(hud.ShowZoomChange(4.0f, zoom::Step(4.0f, true), 1));
    EXPECT_FALSE(hud.ShowZoomChange(0.25f, zoom::Step(0.25f, false), 2));
    EXPECT_FALSE(hud.ShowZoomChange(4.0f, 9.0f, 3));
    EXPECT_FALSE(hud.Active());
    EXPECT_TRUE(hud.ShowZoomChange(1.0f, 0.0f, 10));
    EXPECT_EQ(hud.Percent(), 25);
    EXPECT_TRUE(hud.ShowZoomChange(0.25f, 9.0f, 100));
    EXPECT_EQ(hud.Percent(), 400);
    EXPECT_TRUE(hud.ShowZoomChange(4.0f, 1.0f, 200));
    EXPECT_EQ(hud.Percent(), 100);
}

TEST(ZoomHud, NoOpDoesNotRefreshExistingHoldOrDigits) {
    zoomhud::State hud;
    hud.ShowZoomChange(1.0f, 4.0f, 0);
    EXPECT_FALSE(hud.ShowZoomChange(4.0f, 4.5f, 800));
    EXPECT_EQ(hud.Percent(), 400);
    EXPECT_NEAR(hud.Opacity(975), 0.5f, 0.0001f);
    EXPECT_FALSE(hud.Tick(1100));
}

TEST(ZoomHud, InvalidFactorsCannotCorruptVisibleState) {
    zoomhud::State hud;
    hud.ShowZoomChange(1.0f, 1.2f, 0);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(hud.ShowZoomChange(1.2f, nan, 800));
    EXPECT_FALSE(hud.ShowZoomChange(1.2f, inf, 800));
    EXPECT_FALSE(hud.ShowZoomChange(nan, 1.0f, 800));
    EXPECT_EQ(hud.Percent(), 120);
    EXPECT_FALSE(hud.Tick(1100));
}

TEST(ZoomHud, DelayedTimerExpiresAndLongUptimeDoesNotWrap) {
    zoomhud::State hud;
    const std::uint64_t start = (std::uint64_t{1} << 32) + 100;
    hud.ShowZoomChange(1.0f, 1.1f, start);
    EXPECT_NEAR(hud.Opacity(start + 45), 0.5f, 0.0001f);
    EXPECT_EQ(hud.Opacity(start + 90), 1.0f);
    EXPECT_FALSE(hud.Tick(start + 100000));
    EXPECT_EQ(hud.Opacity(start + 100000), 0.0f);
}

TEST(ZoomHud, OpacityAlwaysStaysInRange) {
    zoomhud::State hud;
    hud.ShowZoomChange(1.0f, 1.1f, 100);
    for (std::uint64_t now = 0; now < 1500; ++now) {
        EXPECT_GE(hud.Opacity(now), 0.0f);
        EXPECT_LE(hud.Opacity(now), 1.0f);
    }
}
