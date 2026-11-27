// Pie label placement and colour adjacency.
//
// The painter used to put every percent label on the slice bisector at a
// fixed 0.66 r, which ignores the width of the text: on a narrow slice
// the label's corners crossed a boundary ray. PlacePieLabel holds the
// whole box inside the wedge with a margin, and this suite pins the
// cases the diagrams actually have: wide slices at mermaid's 0.75 r,
// narrow slices pushed out to the rim or given smaller type, and a slice
// too narrow for any placement reported as not inside rather than
// silently drawn across a boundary.
//
// The colour check is the other half of "which slice is this": no two
// neighbouring slices, the seam between the last and the first included,
// may share a fill index.

#include "mermaid/pie_layout.h"
#include "mermaid/pie_parse.h"
#include "gtest_lite.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

mermaid::PieArc ArcAt(double start, double end) {
    mermaid::PieArc arc;
    arc.start_angle = start;
    arc.end_angle = end;
    return arc;
}

// True when the corners of a horizontal box centred on the placement lie
// inside the wedge, a comfortable distance from both rays.
bool CornersInside(const mermaid::PieArc& arc,
                   const mermaid::PieLabelPlacement& place, double half_w,
                   double half_h, double radius) {
    const double mid = (arc.start_angle + arc.end_angle) * 0.5;
    const double half = (arc.end_angle - arc.start_angle) * 0.5;
    const double hw = half_w * place.font_scale;
    const double hh = half_h * place.font_scale;
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            const double px = place.x + sx * hw;
            const double py = place.y + sy * hh;
            const double d = std::hypot(px, py);
            if (d >= radius - 1.0) return false;
            double off = std::atan2(px, -py) - mid;
            while (off > 3.141592653589793) off -= 2 * 3.141592653589793;
            while (off <= -3.141592653589793) off += 2 * 3.141592653589793;
            if (std::fabs(off) >= half) return false;
        }
    }
    return true;
}

mermaid::PieDiagram EqualSlices(int count) {
    mermaid::PieDiagram pie;
    for (int i = 0; i < count; ++i) {
        pie.slices.push_back({"s" + std::to_string(i), 1.0});
    }
    return pie;
}

}  // namespace

TEST(PieLabel, WideSliceSitsAtMermaidsPosition) {
    // A quarter of the circle, a 24x12 box: mermaid's own 0.75 r spot is
    // inside with room to spare, so no push to the rim is needed.
    const auto arc = ArcAt(0.0, 1.5707963267948966);
    const auto place = mermaid::PlacePieLabel(arc, 12.0, 6.0, 185.0);
    EXPECT_TRUE(place.inside);
    EXPECT_NEAR(place.font_scale, 1.0, 1e-9);
    const double r = std::hypot(place.x, place.y);
    EXPECT_NEAR(r, 185.0 * 0.75, 0.01);
    EXPECT_TRUE(CornersInside(arc, place, 12.0, 6.0, 185.0));
}

TEST(PieLabel, NarrowSliceShrinksOrMovesOutButStaysInside) {
    // A 7.2 degree slice (2 percent of twelve): the width of "2%" is
    // wider than the chord at 0.75 r, so the placement has to adapt.
    const auto arc = ArcAt(0.0, 0.12566370614359172);
    const auto place = mermaid::PlacePieLabel(arc, 11.0, 8.5, 185.0);
    EXPECT_TRUE(place.inside);
    EXPECT_TRUE(place.font_scale < 1.0 || std::hypot(place.x, place.y) > 185.0 * 0.75);
    EXPECT_TRUE(CornersInside(arc, place, 11.0, 8.5, 185.0));
}

TEST(PieLabel, ImpossibleSliceIsReportedNotHidden) {
    // 1 percent of a circle is 3.6 degrees; "1%" at 17 DIP cannot fit even
    // shrunk to half, so the placement says so instead of pretending.
    const auto arc = ArcAt(0.0, 0.06283185307179587);
    const auto place = mermaid::PlacePieLabel(arc, 11.0, 8.5, 185.0);
    EXPECT_FALSE(place.inside);
    // The least bad spot is still on the bisector, so the label points at
    // the one wedge it can belong to.
    const double mid = (arc.start_angle + arc.end_angle) * 0.5;
    EXPECT_NEAR(std::atan2(place.x, -place.y), mid, 1e-6);
    EXPECT_GT(std::hypot(place.x, place.y), 185.0 * 0.7);
}

TEST(PieLabel, EveryFixtureLabelLandsInsideItsOwnSlice) {
    // The three oracle fixtures, at the type size the painter uses: the
    // fake engine's glyph is 8 DIP, so a 17 DIP line of "NN%" is three
    // glyphs by 8 wide and 1.5 lines tall.
    const char* names[] = {"pie1", "pie2", "pie3"};
    for (const char* name : names) {
        std::ifstream f(std::string("tests/mermaid/fixtures/") + name + ".mmd");
        std::stringstream ss;
        ss << f.rdbuf();
        auto pie = mermaid::ParsePie(ss.str());
        auto laid = mermaid::LayoutPie(pie);
        ASSERT_TRUE(laid.error.empty());
        for (const auto& arc : laid.arcs) {
            const double half_w = arc.pct.size() * 8.0 * 0.5;
            const auto place =
                mermaid::PlacePieLabel(arc, half_w, 17.0 * 0.75, laid.radius);
            EXPECT_TRUE(place.inside);
            EXPECT_TRUE(CornersInside(arc, place, half_w, 17.0 * 0.75,
                                      laid.radius));
        }
    }
}

TEST(PieLabel, NeighboursNeverShareAFillIncludingTheSeam) {
    // 13 slices is one past the palette: the cycle would put the last arc
    // back on the first colour, and the seam between them is a real
    // boundary. 14 repeats a colour but never on a neighbour.
    for (int count : {2, 5, 12, 13, 14, 24, 25}) {
        auto laid = mermaid::LayoutPie(EqualSlices(count));
        ASSERT_EQ(laid.arcs.size(), static_cast<size_t>(count));
        for (size_t i = 0; i < laid.arcs.size(); ++i) {
            const size_t next = (i + 1) % laid.arcs.size();
            EXPECT_NE(laid.arcs[i].color_index, laid.arcs[next].color_index);
            EXPECT_GE(laid.arcs[i].color_index, 0);
            EXPECT_LT(laid.arcs[i].color_index, mermaid::kPieColorCount);
        }
        // The legend shows the colour its own arc was given, in arc order.
        for (size_t i = 0; i < laid.legend.size(); ++i) {
            EXPECT_EQ(laid.legend[i].color_index, laid.arcs[i].color_index);
        }
    }
}
