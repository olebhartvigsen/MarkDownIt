// The mode value, the resolution rule and what a switch means.
//
// None of this needs a window, which is the point: the same answers the
// two shells read are the answers this suite checks.
//
// The local gate builds this file together with the shared test main:
//   g++ -std=c++17 -Wall -Wextra -I src -I tests
//       tests/thememode_test.cpp tests/test_main.cpp -o /tmp/thememode_test

#include "gtest_lite.h"
#include "theme_model.h"
#include "thememode.h"

#include <string>

namespace {

TEST(ThemeMode, ParsesEveryName) {
    theme::ThemeMode mode = theme::ThemeMode::Dark;
    EXPECT_TRUE(theme::ParseThemeMode("light", &mode));
    EXPECT_TRUE(mode == theme::ThemeMode::Light);
    EXPECT_TRUE(theme::ParseThemeMode("SYSTEM", &mode));
    EXPECT_TRUE(mode == theme::ThemeMode::System);
    EXPECT_TRUE(theme::ParseThemeMode("  dark\t", &mode));
    EXPECT_TRUE(mode == theme::ThemeMode::Dark);
    // "auto" is what a person types when they mean System.
    EXPECT_TRUE(theme::ParseThemeMode("auto", &mode));
    EXPECT_TRUE(mode == theme::ThemeMode::System);
}

TEST(ThemeMode, RejectsUnknownText) {
    theme::ThemeMode mode = theme::ThemeMode::Light;
    EXPECT_FALSE(theme::ParseThemeMode("dusk", &mode));
    EXPECT_FALSE(theme::ParseThemeMode("", &mode));
    // A rejected parse leaves the value alone.
    EXPECT_TRUE(mode == theme::ThemeMode::Light);
}

TEST(ThemeMode, NamesRoundTrip) {
    const theme::ThemeMode modes[] = {theme::ThemeMode::System,
                                      theme::ThemeMode::Light,
                                      theme::ThemeMode::Dark};
    for (theme::ThemeMode mode : modes) {
        theme::ThemeMode parsed = theme::ThemeMode::System;
        EXPECT_TRUE(theme::ParseThemeMode(theme::ThemeModeName(mode), &parsed));
        EXPECT_TRUE(parsed == mode);
    }
}

TEST(ThemeMode, StoredIntIsClamped) {
    EXPECT_TRUE(theme::ThemeModeFromInt(0) == theme::ThemeMode::System);
    EXPECT_TRUE(theme::ThemeModeFromInt(1) == theme::ThemeMode::Light);
    EXPECT_TRUE(theme::ThemeModeFromInt(2) == theme::ThemeMode::Dark);
    // A corrupt registry value falls back to the safe default rather
    // than to an arbitrary appearance.
    EXPECT_TRUE(theme::ThemeModeFromInt(7) == theme::ThemeMode::System);
    EXPECT_TRUE(theme::ThemeModeFromInt(-1) == theme::ThemeMode::System);
}

TEST(ThemeMode, ExplicitChoiceBeatsTheOs) {
    // The truth table the shells rely on: System follows the OS, and an
    // explicit choice ignores it in both directions.
    EXPECT_TRUE(theme::ResolveAppearance(theme::ThemeMode::System, true) ==
                theme::Appearance::Dark);
    EXPECT_TRUE(theme::ResolveAppearance(theme::ThemeMode::System, false) ==
                theme::Appearance::Light);
    EXPECT_TRUE(theme::ResolveAppearance(theme::ThemeMode::Light, true) ==
                theme::Appearance::Light);
    EXPECT_TRUE(theme::ResolveAppearance(theme::ThemeMode::Light, false) ==
                theme::Appearance::Light);
    EXPECT_TRUE(theme::ResolveAppearance(theme::ThemeMode::Dark, true) ==
                theme::Appearance::Dark);
    EXPECT_TRUE(theme::ResolveAppearance(theme::ThemeMode::Dark, false) ==
                theme::Appearance::Dark);
}

TEST(ThemeMode, DefaultIsSystem) {
    // Adopting the OS scheme is the default, not a setting a user has to
    // find.
    EXPECT_TRUE(theme::kDefaultThemeMode == theme::ThemeMode::System);
}

TEST(ThemeMode, SwitchNeverTouchesTheDocument) {
    const theme::Appearance both[] = {theme::Appearance::Light,
                                      theme::Appearance::Dark};
    for (theme::Appearance from : both) {
        for (theme::Appearance to : both) {
            const theme::SwitchPlan plan = theme::PlanForSwitch(from, to);
            // The display list bakes colours in, so a real switch lays
            // the document out again. It still never reparses and never
            // touches the document itself.
            EXPECT_EQ(plan.relayout, from != to);
            EXPECT_FALSE(plan.reparse);
            EXPECT_FALSE(plan.touchesDocument);
            EXPECT_EQ(plan.repaint, from != to);
        }
    }
}

TEST(ThemeMode, ChromeIsRebuiltOnEveryRealSwitch) {
    EXPECT_TRUE(theme::ChromeNeedsRebuild(theme::Appearance::Light,
                                          theme::Appearance::Dark));
    EXPECT_TRUE(theme::ChromeNeedsRebuild(theme::Appearance::Dark,
                                          theme::Appearance::Light));
    EXPECT_FALSE(theme::ChromeNeedsRebuild(theme::Appearance::Dark,
                                           theme::Appearance::Dark));
}

}  // namespace
