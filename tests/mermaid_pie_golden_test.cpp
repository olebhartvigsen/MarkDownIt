// Pie golden tests: C++ LayoutPie must match the live mermaid oracle for
// parse values, arc angles, label centroids, legend rows and canvas size.
// Tolerance 0.5 DIP (plan's Definition of Done) for positions, 1e-6 for
// angles.
#include "mermaid/pie_parse.h"
#include "mermaid/pie_layout.h"
#include "mermaid/pie_golden_loader.h"
#include "gtest_lite.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

}  // namespace

TEST(PieGolden, ParsesAndSortsLikeMermaid) {
    // Mermaid sorts slices desc by value (stable). Fixture order:
    // Arbejde 30, Søvn 33, Træning 17, Andet 20 → arc order S,A,An,T.
    auto pie = mermaid::ParsePie(ReadFile("tests/mermaid/fixtures/pie1.mmd"));
    ASSERT_TRUE(pie.error.empty());
    EXPECT_EQ(pie.title, "Ugeoversigt");
    EXPECT_EQ(pie.slices.size(), 4u);
    EXPECT_NEAR(pie.slices[0].value, 30.0, 1e-9);
    EXPECT_EQ(pie.slices[0].label, "Arbejde");
    EXPECT_EQ(pie.slices[1].label, "Søvn");     // UTF-8 label intact
    EXPECT_NEAR(pie.slices[1].value, 33.0, 1e-9);

    auto lo = mermaid::LayoutPie(pie);
    ASSERT_EQ(lo.arcs.size(), 4u);
    // arc order: desc by value → Søvn(33) first
    EXPECT_EQ(lo.arcs[0].label, "Søvn");
    EXPECT_EQ(lo.arcs[1].label, "Arbejde");
    EXPECT_EQ(lo.arcs[2].label, "Andet");
    EXPECT_EQ(lo.arcs[3].label, "Træning");
}

TEST(PieGolden, ArcAnglesMatchOracle) {
    const char* fixtures[] = {"pie1", "pie2", "pie3"};
    // Angle: the oracle derives angles from SVG path coords rounded to
    // 3 decimals; at r=185 the rounding is ≈0.0005/185 ≈ 2.7e-6 rad. Allow
    // 1e-5 rad (still < 0.002 DIP at r=185).
    const double ang_tol = 1e-5;
    const double pos_tol = 0.5;
    for (const char* name : fixtures) {
        auto gold = mermaid::LoadPieGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        auto src = mermaid::ParsePie(ReadFile(
            std::string("tests/mermaid/fixtures/") + name + ".mmd"));
        auto lo = mermaid::LayoutPie(src);
        EXPECT_EQ(lo.error, "");
        ASSERT_EQ(lo.arcs.size(), gold.arcs.size());
        for (size_t i = 0; i < lo.arcs.size(); ++i) {
            const auto& a = lo.arcs[i];
            const auto& ga = gold.arcs[i];
            if (std::abs(a.start_angle - ga.start) > ang_tol ||
                std::abs(a.end_angle - ga.end) > ang_tol) {
                std::fprintf(stderr,
                    "[%s] arc %zu: ours=(%.6f,%.6f) gold=(%.6f,%.6f)\n",
                    name, i, a.start_angle, a.end_angle, ga.start, ga.end);
            }
            EXPECT_NEAR(a.start_angle, ga.start, ang_tol);
            EXPECT_NEAR(a.end_angle, ga.end, ang_tol);
            EXPECT_EQ(a.pct, ga.pct);
            EXPECT_NEAR(a.label_x, ga.label_x, pos_tol);
            EXPECT_NEAR(a.label_y, ga.label_y, pos_tol);
        }
        // Legend
        ASSERT_EQ(lo.legend.size(), gold.legend.size());
        for (size_t i = 0; i < lo.legend.size(); ++i) {
            const auto& l = lo.legend[i];
            const auto& gl = gold.legend[i];
            if (l.label != gl.label) {
                std::fprintf(stderr, "[%s] legend %zu: ours=%s gold=%s\n",
                    name, i, l.label.c_str(), gl.label.c_str());
            }
            EXPECT_EQ(l.label, gl.label);
            EXPECT_NEAR(l.x, gl.x, pos_tol);
            EXPECT_NEAR(l.y, gl.y, pos_tol);
        }
        // Canvas
        const double dim_tol = 3.0;   // legend text width uses web metrics
        EXPECT_NEAR(lo.width, gold.width, dim_tol);
        EXPECT_NEAR(lo.height, gold.height, dim_tol);
        EXPECT_NEAR(lo.cx, gold.cx, pos_tol);
        EXPECT_NEAR(lo.cy, gold.cy, pos_tol);
        EXPECT_NEAR(lo.radius, gold.radius, pos_tol);
    }
}
