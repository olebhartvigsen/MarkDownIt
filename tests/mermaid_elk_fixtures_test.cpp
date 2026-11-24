// The shipped Mermaid layout, held to the fixtures.
//
// tests/mermaid/golden/ holds dagre-d3-es geometry, and tests/mermaid_golden_test.cpp
// compares the engine against it with dagre's parameters. That stays the
// external reference, and it can only be compared on the profile the
// goldens came from.
//
// The app lays diagrams out with the ELK profile, so that is what these
// tests exercise: the same fixtures, the layout the user actually sees,
// and properties that must hold whatever the coordinates are. They are
// not goldens, because there is no ELK reference to record: ELK is a Java
// library and the goldens here are produced by dagre-d3-es.

#include "../src/mermaid/elk_profile.h"
#include "../src/mermaid/layout.h"
#include "../src/mermaid/parse.h"
#include "mermaid/golden_loader.h"
#include "gtest_lite.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* kFixtures[] = {
    "01-linear", "02-shapes-edges", "03-diamond", "04-crossing",
    "05-long-edge", "06-siblings", "07-lr", "08-rl", "09-bt",
    "12-flowchart",
};

std::string ReadFixture(const char* name) {
    const std::string path = std::string(name) == "12-flowchart"
        ? std::string("tests/mermaid/flowchart.txt")
        : std::string("tests/mermaid/fixtures/") + name + ".mmd";
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

mermaid::LaidOutFlowchart ElkLayout(const char* name) {
    mermaid::LayoutParams p = mermaid::ElkParams();
    p.margin = 0.0;
    const mermaid::Flowchart flow = mermaid::ParseFlowchart(ReadFixture(name));
    return mermaid::LayoutFlowchart(flow, p);
}

bool BoxesOverlap(const mermaid::LayoutNode& a, const mermaid::LayoutNode& b) {
    // A false positive is worse than a miss here: half a point of slack
    // keeps touching boxes legal, which is what a shared edge looks like.
    const float slack = 0.5f;
    const bool apart_x = std::fabs(a.x - b.x) >= (a.width + b.width) * 0.5f - slack;
    const bool apart_y =
        std::fabs(a.y - b.y) >= (a.height + b.height) * 0.5f - slack;
    return !(apart_x || apart_y);
}

bool InsideOrAt(const mermaid::LayoutNode& n, const mermaid::Point& p) {
    const float slack = 2.0f;
    return p.x >= n.x - n.width * 0.5f - slack &&
           p.x <= n.x + n.width * 0.5f + slack &&
           p.y >= n.y - n.height * 0.5f - slack &&
           p.y <= n.y + n.height * 0.5f + slack;
}

}  // namespace

// The ELK profile draws the same diagram: no node and no relationship is
// lost or invented, whatever the spacing says.
TEST(MermaidElkFixtures, KeepsEveryNodeAndEdge) {
    for (const char* name : kFixtures) {
        const mermaid::Golden gold = mermaid::LoadGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        const mermaid::LaidOutFlowchart out = ElkLayout(name);
        EXPECT_EQ(out.nodes.size(), gold.nodes.size());
        EXPECT_EQ(out.edges.size(), gold.edges.size());
    }
}

// Two nodes never sit on top of each other. This is the property the
// spacing tokens exist for, and it holds for every fixture or the layout
// is not readable at all.
TEST(MermaidElkFixtures, NodesDoNotOverlap) {
    for (const char* name : kFixtures) {
        const mermaid::LaidOutFlowchart out = ElkLayout(name);
        for (size_t i = 0; i < out.nodes.size(); ++i) {
            for (size_t j = i + 1; j < out.nodes.size(); ++j) {
                if (BoxesOverlap(out.nodes[i], out.nodes[j])) {
                    std::fprintf(stderr, "[%s] nodes %zu and %zu overlap\n",
                                 name, i, j);
                }
                EXPECT_FALSE(BoxesOverlap(out.nodes[i], out.nodes[j]));
            }
        }
    }
}

// Every edge is routed from its source to its target, so the drawing
// still says what the document says.
TEST(MermaidElkFixtures, EveryEdgeConnectsItsEnds) {
    for (const char* name : kFixtures) {
        const mermaid::LaidOutFlowchart out = ElkLayout(name);
        for (const mermaid::LayoutEdge& e : out.edges) {
            ASSERT_GE(e.route.size(), 2u);
            ASSERT_TRUE(e.from >= 0 && e.to >= 0);
            ASSERT_TRUE(e.from < static_cast<int>(out.nodes.size()));
            ASSERT_TRUE(e.to < static_cast<int>(out.nodes.size()));
            EXPECT_TRUE(InsideOrAt(out.nodes[e.from], e.route.front()));
            EXPECT_TRUE(InsideOrAt(out.nodes[e.to], e.route.back()));
        }
    }
}

// Ranking is the direction the author asked for: in a top-down diagram a
// target never sits above its source, and left-to-right is the same rule
// on the other axis.
TEST(MermaidElkFixtures, RankFollowsTheDirection) {
    for (const char* name : kFixtures) {
        const mermaid::LaidOutFlowchart out = ElkLayout(name);
        const mermaid::Golden gold = mermaid::LoadGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        const bool sideways = gold.rankdir == "LR" || gold.rankdir == "RL";
        const bool reversed = gold.rankdir == "BT" || gold.rankdir == "RL";
        for (const mermaid::LayoutEdge& e : out.edges) {
            const mermaid::LayoutNode& from = out.nodes[e.from];
            const mermaid::LayoutNode& to = out.nodes[e.to];
            const float delta = sideways ? to.x - from.x : to.y - from.y;
            // A long edge routed through a dummy may step sideways, and a
            // lane or a self loop is exempt; the axis must still move the
            // way the direction says, or never backwards.
            if (e.reversed) continue;
            if (from.rank == to.rank) continue;
            if (reversed) {
                EXPECT_LE(delta, 0.5f);
            } else {
                EXPECT_GE(delta, -0.5f);
            }
        }
    }
}

// The drawing is bounded and the numbers are real: a NaN or an infinity
// reaching the renderer is a crash or a blank page, not a diagram.
TEST(MermaidElkFixtures, GeometryIsFinite) {
    for (const char* name : kFixtures) {
        const mermaid::LaidOutFlowchart out = ElkLayout(name);
        EXPECT_TRUE(std::isfinite(out.width));
        EXPECT_TRUE(std::isfinite(out.height));
        EXPECT_GT(out.width, 0.0);
        EXPECT_GT(out.height, 0.0);
        for (const mermaid::LayoutNode& n : out.nodes) {
            EXPECT_TRUE(std::isfinite(n.x));
            EXPECT_TRUE(std::isfinite(n.y));
            EXPECT_GT(n.width, 0.0f);
            EXPECT_GT(n.height, 0.0f);
        }
    }
}
