// Golden gate tests for the mermaid layout pipeline. Compares our C++
// output against dagre-d3-es JSON goldens produced by tools/mermaid-oracle.
// Tolerance is 0.5 DIP per the plan's Definition of Done.
#include "../src/mermaid/layout.h"
#include "../src/mermaid/layout_internal.h"
#include "../src/mermaid/parse.h"
#include "mermaid/golden_loader.h"
#include "gtest_lite.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string ReadFixture(const char* name) {
    std::ifstream f(std::string("tests/mermaid/fixtures/") + name + ".mmd");
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

mermaid::LaidOutFlowchart LayoutFromFixture(const char* name) {
    mermaid::Flowchart flow = mermaid::ParseFlowchart(ReadFixture(name));
    mermaid::LayoutParams p;
    p.margin = 0.0;
    return mermaid::LayoutFlowchart(flow, p);
}

void CheckNodes(const char* name, const mermaid::Golden& gold,
                const std::vector<mermaid::LayoutNode>& nodes, double tol) {
    for (const auto& gn : gold.nodes) {
        int idx = -1;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].label == gn.label || nodes[i].label == gn.id) {
                idx = static_cast<int>(i); break;
            }
        }
        ASSERT_GE(idx, 0);
        double dx = static_cast<double>(nodes[idx].x) - static_cast<double>(gn.x);
        double dy = static_cast<double>(nodes[idx].y) - static_cast<double>(gn.y);
        if (std::abs(dx) > tol || std::abs(dy) > tol) {
            std::fprintf(stderr,
                "[%s] node %s: ours=(%.3f,%.3f) golden=(%.3f,%.3f) diff=(%.3f,%.3f)\n",
                name, gn.id.c_str(),
                nodes[idx].x, nodes[idx].y, gn.x, gn.y, dx, dy);
        }
        EXPECT_NEAR(nodes[idx].x, gn.x, tol);
        EXPECT_NEAR(nodes[idx].y, gn.y, tol);
    }
}

void CheckEdges(const char* name, const mermaid::Golden& gold,
                const std::vector<mermaid::LayoutEdge>& edges,
                const std::vector<mermaid::LayoutNode>& nodes,
                double tol) {
    for (const auto& ge : gold.edges) {
        int from = -1, to = -1;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].label == ge.from) from = static_cast<int>(i);
            if (nodes[i].label == ge.to)   to   = static_cast<int>(i);
        }
        if (from < 0 || to < 0) continue;
        int ei = -1;
        for (size_t i = 0; i < edges.size(); ++i) {
            if (edges[i].from == from && edges[i].to == to) { ei = static_cast<int>(i); break; }
        }
        ASSERT_GE(ei, 0);
        const auto& route = edges[ei].route;
        if (route.size() != ge.points.size()) {
            std::fprintf(stderr, "[%s] edge %s->%s: route %zu pts, golden %zu\n",
                name, ge.from.c_str(), ge.to.c_str(), route.size(), ge.points.size());
        }
        ASSERT_EQ(route.size(), ge.points.size());
        for (size_t k = 0; k < route.size(); ++k) {
            double dx = route[k].x - static_cast<double>(ge.points[k].first);
            double dy = route[k].y - static_cast<double>(ge.points[k].second);
            if (std::abs(dx) > tol || std::abs(dy) > tol) {
                std::fprintf(stderr,
                    "[%s] edge %s->%s pt[%zu]: ours=(%.3f,%.3f) golden=(%.3f,%.3f) diff=(%.3f,%.3f)\n",
                    name, ge.from.c_str(), ge.to.c_str(), k,
                    route[k].x, route[k].y, ge.points[k].first, ge.points[k].second, dx, dy);
            }
            EXPECT_NEAR(route[k].x, ge.points[k].first,  tol);
            EXPECT_NEAR(route[k].y, ge.points[k].second, tol);
        }
    }
}

}  // namespace

TEST(MermaidGolden, AllFixtures) {
    const char* fixtures[] = {
        "01-linear",
        "02-shapes-edges",
        "03-diamond",
        "04-crossing",
        "05-long-edge",
        "06-siblings",
        "07-lr",
        "08-rl",
        "09-bt",
    };
    const double tol = 0.5;
    for (const char* name : fixtures) {
        auto gold = mermaid::LoadGolden(std::string("tests/mermaid/golden/") + name + ".json");
        auto out = LayoutFromFixture(name);
        CheckNodes(name, gold, out.nodes, tol);
        CheckEdges(name, gold, out.edges, out.nodes, tol);
    }
}
