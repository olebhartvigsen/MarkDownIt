// Golden gate tests for the mermaid layout pipeline. Compares our C++
// output against dagre-d3-es JSON goldens produced by tools/mermaid-oracle.
#include "../src/mermaid/layout.h"
#include "../src/mermaid/layout_internal.h"
#include "../src/mermaid/parse.h"
#include "mermaid/golden_loader.h"
#include "gtest_lite.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// Run the internal pipeline against a golden with sizes injected from the
// oracle output, so font-metric drift cannot skew the comparison.
mermaid::LayoutGraph LayoutOursFromGolden(const mermaid::Golden& gold) {
    mermaid::LayoutGraph g = mermaid::LayoutFromGolden(gold);
    mermaid::LayoutParams p;
    mermaid::MakeAcyclic(g);
    mermaid::AssignRanks(g);
    mermaid::Normalize(g);
    mermaid::Order(g);
    mermaid::AssignCoordinates(g, p);
    mermaid::RouteEdges(g, p);
    mermaid::Denormalize(g);
    return g;
}

}  // namespace

TEST(MermaidGolden, NodeCentersMatchDagre) {
    const char* names[] = {
        "01-linear", "02-shapes-edges", "03-diamond",
        "04-crossing", "05-long-edge", "06-siblings",
    };
    for (const char* name : names) {
        std::string path = std::string("tests/mermaid/golden/") + name + ".json";
        auto gold = mermaid::LoadGolden(path);
        auto ours = LayoutOursFromGolden(gold);
        for (const auto& gn : gold.nodes) {
            int idx = mermaid::FindByLabel(ours, gn.id);
            ASSERT_GE(idx, 0);
            if (std::abs(ours.nodes[idx].x - gn.x) > 0.5 ||
                std::abs(ours.nodes[idx].y - gn.y) > 0.5) {
                std::fprintf(stderr,
                             "[%s] node %s: ours=(%.3f,%.3f) golden=(%.3f,%.3f)\n",
                             name, gn.id.c_str(),
                             ours.nodes[idx].x, ours.nodes[idx].y, gn.x, gn.y);
            }
            EXPECT_NEAR(ours.nodes[idx].x, gn.x, 0.5);
            EXPECT_NEAR(ours.nodes[idx].y, gn.y, 0.5);
        }
    }
}

TEST(MermaidGolden, EdgePointsMatchDagre_05LongEdge) {
    auto gold = mermaid::LoadGolden("tests/mermaid/golden/05-long-edge.json");
    auto ours = LayoutOursFromGolden(gold);
    for (const auto& ge : gold.edges) {
        int fi = mermaid::FindByLabel(ours, ge.from);
        int ti = mermaid::FindByLabel(ours, ge.to);
        ASSERT_GE(fi, 0);
        ASSERT_GE(ti, 0);
        int ei = mermaid::FindEdge(ours, fi, ti);
        ASSERT_GE(ei, 0);
        const auto& route = ours.edges[ei].route;
        ASSERT_EQ(route.size(), ge.points.size());
        for (size_t k = 0; k < route.size(); ++k) {
            if (std::abs(route[k].x - ge.points[k].first) > 0.5 ||
                std::abs(route[k].y - ge.points[k].second) > 0.5) {
                std::fprintf(stderr,
                             "edge %s->%s pt[%zu]: ours=(%.3f,%.3f) golden=(%.3f,%.3f)\n",
                             ge.from.c_str(), ge.to.c_str(), k,
                             route[k].x, route[k].y,
                             ge.points[k].first, ge.points[k].second);
            }
            EXPECT_NEAR(route[k].x, ge.points[k].first,  0.5);
            EXPECT_NEAR(route[k].y, ge.points[k].second, 0.5);
        }
    }
}

TEST(MermaidGolden, RankdirLR_07) {
    auto gold = mermaid::LoadGolden("tests/mermaid/golden/07-lr.json");
    std::string src = ReadFile("tests/mermaid/fixtures/07-lr.mmd");
    mermaid::Flowchart flow = mermaid::ParseFlowchart(src);
    mermaid::LayoutParams p;
    p.margin = 0.0;
    auto out = mermaid::LayoutFlowchart(flow, p);
    for (const auto& gn : gold.nodes) {
        int found = -1;
        for (size_t i = 0; i < out.nodes.size(); ++i) {
            if (out.nodes[i].label == gn.label || out.nodes[i].label == gn.id) {
                found = static_cast<int>(i); break;
            }
        }
        ASSERT_GE(found, 0);
        if (std::abs(out.nodes[found].x - gn.x) > 0.5 ||
            std::abs(out.nodes[found].y - gn.y) > 0.5) {
            std::fprintf(stderr,
                         "[07-lr] node %s: ours=(%.3f,%.3f) golden=(%.3f,%.3f)\n",
                         gn.id.c_str(),
                         out.nodes[found].x, out.nodes[found].y, gn.x, gn.y);
        }
        EXPECT_NEAR(out.nodes[found].x, gn.x, 0.5);
        EXPECT_NEAR(out.nodes[found].y, gn.y, 0.5);
    }
}
