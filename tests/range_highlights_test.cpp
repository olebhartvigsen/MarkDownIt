// The shared search-match and marker highlight pass, tested without a
// window, a GPU or a platform text stack: a fake source answers the geometry
// and a fake target records what was filled.
//
//   g++ -std=c++17 -Wall -Wextra -I tests -I src
//       tests/range_highlights_test.cpp tests/test_main.cpp
//       src/layout/range_highlights.cpp src/searchreplace.cpp -o /tmp/rh_test

#include "gtest_lite.h"
#include "layout/range_highlights.h"

#include <cstdint>
#include <vector>

namespace {

using platform::Color;
using platform::Rect;
using platform::TextRangeSource;

const Color kMatch{1.0f, 0.0f, 0.0f, 1.0f};
const Color kCurrent{0.0f, 1.0f, 0.0f, 1.0f};
const Color kMarker{0.0f, 0.0f, 1.0f, 1.0f};
const Color kMarkerLine{0.0f, 0.0f, 0.0f, 1.0f};

// Records the fills, and the batches: one FillRects call per colour.
class FakeTarget : public platform::RenderTarget {
public:
    std::vector<Rect> rects;
    std::vector<Color> colors;

    void FillRect(const Rect& rect, const Color& color) override {
        rects.push_back(rect);
        colors.push_back(color);
    }
    void FillRects(const std::vector<Rect>& batch,
                   const Color& color) override {
        for (const Rect& rect : batch) {
            rects.push_back(rect);
            colors.push_back(color);
        }
    }
    void FillRoundedRect(const Rect&, float, const Color&) override {}
    void StrokeRect(const Rect&, const Color&, float) override {}
    void StrokeRoundedRect(const Rect&, float, const Color&, float) override {}
    void StrokeLine(float, float, float, float, const Color&,
                    float) override {}
    void DrawText(const std::string&, const platform::FontSpec&, float, float,
                  const Color&) override {}
    void DrawBitmap(platform::BitmapHandle, const Rect&) override {}
    void FillEllipse(const Rect&, const Color&) override {}
    void StrokeEllipse(const Rect&, const Color&, float) override {}
    void FillPolygon(const std::vector<platform::Point>&,
                     const Color&) override {}
    void StrokePolygon(const std::vector<platform::Point>&, const Color&,
                       float, bool) override {}
    void FillPieSlice(const Rect&, float, float, const Color&) override {}
    void PushClip(const Rect&) override {}
    void PopClip() override {}
};

// One ten point rectangle per UTF-16 unit, on one line at y 100, so a range
// answers a rectangle per character and the assertions can name them.
class FakeSource : public TextRangeSource {
public:
    struct Ask {
        uint32_t start = 0;
        uint32_t end = 0;
        float cullTop = 0.0f;
        float cullBottom = 0.0f;
    };
    std::vector<Ask> asks;

    void AppendRangeRects(uint32_t start, uint32_t end, float cullTop,
                          float cullBottom,
                          std::vector<Rect>* out) override {
        asks.push_back(Ask{start, end, cullTop, cullBottom});
        for (uint32_t i = start; i < end; ++i) {
            out->push_back(Rect{10.0f * static_cast<float>(i), 100.0f, 10.0f,
                                20.0f});
        }
    }
};

// A block of `len` UTF-16 units starting at byte 0, mapping identity.
layout::BlockTextMap IdentityMap(uint32_t len) {
    static std::vector<uint32_t> mapping;
    mapping.clear();
    for (uint32_t i = 0; i < len; ++i) mapping.push_back(i);
    layout::BlockTextMap map;
    map.u16ToSrc = &mapping;
    map.u16Len = len;
    map.blockStart = 0;
    map.blockEnd = len;
    return map;
}

const layout::PaintBand kBand{0.0f, 800.0f};

}  // namespace

TEST(RangeHighlights, MatchFillsEveryCharacterOfTheRange) {
    const layout::BlockTextMap map = IdentityMap(20);
    FakeSource source;
    FakeTarget target;
    const std::vector<TextMatch> matches = {{5, 3}};

    layout::PaintMatchHighlights(target, source, map, kBand, matches, -1,
                                 kMatch, kCurrent);

    EXPECT_EQ(source.asks.size(), static_cast<size_t>(1));
    EXPECT_EQ(source.asks[0].start, 5u);
    EXPECT_EQ(source.asks[0].end, 8u);
    EXPECT_EQ(target.rects.size(), static_cast<size_t>(3));
    EXPECT_EQ(target.rects[0].x, 50.0f);
    EXPECT_EQ(target.rects[2].x, 70.0f);
}

TEST(RangeHighlights, CurrentMatchGetsItsOwnColourAndTheRestDoNot) {
    const layout::BlockTextMap map = IdentityMap(20);
    FakeSource source;
    FakeTarget target;
    const std::vector<TextMatch> matches = {{1, 1}, {10, 1}, {15, 1}};

    layout::PaintMatchHighlights(target, source, map, kBand, matches, 1,
                                 kMatch, kCurrent);

    EXPECT_EQ(target.rects.size(), static_cast<size_t>(3));
    // The weaker fills first, the current one last, one batch per colour.
    EXPECT_TRUE(target.colors[0].r == kMatch.r && target.colors[0].g == kMatch.g);
    EXPECT_TRUE(target.colors[1].r == kMatch.r && target.colors[1].g == kMatch.g);
    EXPECT_TRUE(target.colors[2].r == kCurrent.r &&
                target.colors[2].g == kCurrent.g);
    EXPECT_EQ(target.rects[2].x, 100.0f);
}

TEST(RangeHighlights, BlockNoMatchTouchesAsksTheSourceNothing) {
    const layout::BlockTextMap map = IdentityMap(10);
    FakeSource source;
    FakeTarget target;
    const std::vector<TextMatch> matches = {{40, 4}};

    layout::PaintMatchHighlights(target, source, map, kBand, matches, 0,
                                 kMatch, kCurrent);

    EXPECT_EQ(source.asks.size(), static_cast<size_t>(0));
    EXPECT_EQ(target.rects.size(), static_cast<size_t>(0));
}

TEST(RangeHighlights, MatchPastTheBlockEndClampsToTheBlock) {
    const layout::BlockTextMap map = IdentityMap(10);
    FakeSource source;
    FakeTarget target;
    const std::vector<TextMatch> matches = {{7, 100}};

    layout::PaintMatchHighlights(target, source, map, kBand, matches, -1,
                                 kMatch, kCurrent);

    EXPECT_EQ(source.asks.size(), static_cast<size_t>(1));
    EXPECT_EQ(source.asks[0].start, 7u);
    // The match runs past the block, so the block's last unit is the end.
    EXPECT_EQ(source.asks[0].end, 10u);
}

TEST(RangeHighlights, UnsortedMappingFallsBackToTheLinearScan) {
    // A table cell carries gaps where md4c split the text, so the mapping is
    // not sorted. Index 3 reads 5 after index 2 read 5: the first index whose
    // value reaches 5 is 2, which a binary search over the unsorted vector
    // would answer as 1.
    std::vector<uint32_t> mapping = {0, 1, 5, 3, 6};
    layout::BlockTextMap map;
    map.u16ToSrc = &mapping;
    map.u16Len = static_cast<uint32_t>(mapping.size());
    map.blockStart = 0;
    map.blockEnd = 7;

    FakeSource source;
    FakeTarget target;
    const std::vector<TextMatch> matches = {{5, 1}};

    layout::PaintMatchHighlights(target, source, map, kBand, matches, -1,
                                 kMatch, kCurrent);

    EXPECT_EQ(source.asks.size(), static_cast<size_t>(1));
    EXPECT_EQ(source.asks[0].start, 2u);
}

TEST(RangeHighlights, ZeroLengthMatchFillsNothing) {
    const layout::BlockTextMap map = IdentityMap(10);
    FakeSource source;
    FakeTarget target;
    const std::vector<TextMatch> matches = {{3, 0}};

    layout::PaintMatchHighlights(target, source, map, kBand, matches, -1,
                                 kMatch, kCurrent);

    EXPECT_EQ(source.asks.size(), static_cast<size_t>(0));
    EXPECT_EQ(target.rects.size(), static_cast<size_t>(0));
}

TEST(RangeHighlights, TheBandIsWhatTheSourceCullsAgainst) {
    const layout::BlockTextMap map = IdentityMap(10);
    FakeSource source;
    FakeTarget target;
    const std::vector<TextMatch> matches = {{1, 1}};
    const layout::PaintBand band{240.0f, 640.0f};

    layout::PaintMatchHighlights(target, source, map, band, matches, -1,
                                 kMatch, kCurrent);

    EXPECT_EQ(source.asks.size(), static_cast<size_t>(1));
    EXPECT_EQ(source.asks[0].cullTop, 240.0f);
    EXPECT_EQ(source.asks[0].cullBottom, 640.0f);
}

TEST(RangeHighlights, MarkerAddsALineAlongTheBottomOfEveryRect) {
    const layout::BlockTextMap map = IdentityMap(20);
    FakeSource source;
    FakeTarget target;
    const std::vector<TextMatch> markers = {{2, 2}};

    layout::PaintMarkerHighlights(target, source, map, kBand, markers, kMarker,
                                  kMarkerLine);

    // Two fills, then the two lines that keep a mark off colour alone.
    EXPECT_EQ(target.rects.size(), static_cast<size_t>(4));
    EXPECT_EQ(target.colors[0].b, kMarker.b);
    EXPECT_EQ(target.colors[1].b, kMarker.b);
    EXPECT_TRUE(target.colors[2].r == kMarkerLine.r &&
                target.colors[2].a == kMarkerLine.a);
    EXPECT_EQ(target.rects[2].height, 2.0f);
    EXPECT_EQ(target.rects[2].y, 118.0f);
    EXPECT_EQ(target.rects[2].width, 10.0f);
}

TEST(RangeHighlights, MarkerLineNeverRunsPastAShortRect) {
    // A band under two points tall keeps the whole band as its line and not
    // a line that hangs below it.
    std::vector<uint32_t> mapping = {0, 1, 2};
    layout::BlockTextMap map;
    map.u16ToSrc = &mapping;
    map.u16Len = 3;
    map.blockStart = 0;
    map.blockEnd = 3;

    class ShortSource : public TextRangeSource {
    public:
        void AppendRangeRects(uint32_t, uint32_t, float, float,
                              std::vector<Rect>* out) override {
            out->push_back(Rect{0.0f, 10.0f, 8.0f, 1.0f});
        }
    } source;
    FakeTarget target;
    const std::vector<TextMatch> markers = {{0, 1}};

    layout::PaintMarkerHighlights(target, source, map, kBand, markers, kMarker,
                                  kMarkerLine);

    EXPECT_EQ(target.rects.size(), static_cast<size_t>(2));
    EXPECT_EQ(target.rects[1].y, 10.0f);
    EXPECT_EQ(target.rects[1].height, 1.0f);
}

TEST(RangeHighlights, NoMappingPaintsNothing) {
    layout::BlockTextMap map;  // no u16ToSrc, no block
    FakeSource source;
    FakeTarget target;
    const std::vector<TextMatch> matches = {{0, 1}};

    layout::PaintMatchHighlights(target, source, map, kBand, matches, -1,
                                 kMatch, kCurrent);

    EXPECT_EQ(source.asks.size(), static_cast<size_t>(0));
    EXPECT_EQ(target.rects.size(), static_cast<size_t>(0));
}
