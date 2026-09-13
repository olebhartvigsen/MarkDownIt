// Golden gate tests for the class diagram layout, mirroring the seq/flowchart
// pattern: parse each cls*.mmd fixture, run LayoutClassDiagram, and compare
// nodes (centers + sizes), edge polylines, edge labels/terminals, and the
// canvas against the committed oracle goldens. Tolerance = 0.5 DIP.
#include "../src/mermaid/class_layout.h"
#include "../src/mermaid/class_parse.h"
#include "mermaid/mermaid_class_golden_loader.h"
#include "gtest_lite.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string ReadFixture(const char* name) {
    std::string path = std::string("tests/mermaid/fixtures/") + name + ".mmd";
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

mermaid::LaidOutClass LayoutFromFixture(const char* name) {
    mermaid::ClassDiagram d = mermaid::ParseClassDiagram(ReadFixture(name));
    return mermaid::LayoutClassDiagram(d);
}

void CheckNodes(const char* name, const mermaid::ClassGolden& gold,
                const mermaid::LaidOutClass& out, double tol) {
    for (const auto& gn : gold.nodes) {
        int idx = -1;
        for (size_t i = 0; i < out.nodes.size(); ++i) {
            if (out.nodes[i].id == gn.id) { idx = static_cast<int>(i); break; }
        }
        ASSERT_GE(idx, 0);
        double dx = std::abs(out.nodes[idx].x - gn.x);
        double dy = std::abs(out.nodes[idx].y - gn.y);
        double dw = std::abs(out.nodes[idx].w - gn.w);
        double dh = std::abs(out.nodes[idx].h - gn.h);
        if (dx > tol || dy > tol || dw > tol || dh > tol) {
            std::fprintf(stderr,
                "[%s] node %s: ours=(%.3f,%.3f,%.3fx%.3f) golden=(%.3f,%.3f,%.3fx%.3f)\n",
                name, gn.id.c_str(), out.nodes[idx].x, out.nodes[idx].y,
                out.nodes[idx].w, out.nodes[idx].h, gn.x, gn.y, gn.w, gn.h);
        }
        EXPECT_NEAR(out.nodes[idx].x, gn.x, tol);
        EXPECT_NEAR(out.nodes[idx].y, gn.y, tol);
        EXPECT_NEAR(out.nodes[idx].w, gn.w, tol);
        EXPECT_NEAR(out.nodes[idx].h, gn.h, tol);
    }
}

void CheckEdges(const char* name, const mermaid::ClassGolden& gold,
                const mermaid::LaidOutClass& out, double tol) {
    for (const auto& ge : gold.edges) {
        int ei = -1;
        for (size_t i = 0; i < out.edges.size(); ++i) {
            if (out.edges[i].from == ge.from && out.edges[i].to == ge.to) {
                ei = static_cast<int>(i); break;
            }
        }
        ASSERT_GE(ei, 0);
        const auto& e = out.edges[ei];
        EXPECT_EQ(e.start_marker, ge.start_marker);
        EXPECT_EQ(e.end_marker, ge.end_marker);
        EXPECT_EQ(e.pattern, ge.pattern);
        if (e.points.size() != ge.points.size()) {
            std::fprintf(stderr, "[%s] edge %s->%s: %zu pts, golden %zu\n",
                name, ge.from.c_str(), ge.to.c_str(), e.points.size(), ge.points.size());
        }
        ASSERT_EQ(e.points.size(), ge.points.size());
        for (size_t k = 0; k < e.points.size(); ++k) {
            double dx = std::abs(e.points[k].x - ge.points[k].first);
            double dy = std::abs(e.points[k].y - ge.points[k].second);
            if (dx > tol || dy > tol) {
                std::fprintf(stderr,
                    "[%s] edge %s->%s pt[%zu]: ours=(%.3f,%.3f) golden=(%.3f,%.3f)\n",
                    name, ge.from.c_str(), ge.to.c_str(), k,
                    e.points[k].x, e.points[k].y, ge.points[k].first, ge.points[k].second);
            }
            EXPECT_NEAR(e.points[k].x, ge.points[k].first, tol);
            EXPECT_NEAR(e.points[k].y, ge.points[k].second, tol);
        }
    }
}

void CheckLabels(const char* name, const mermaid::ClassGolden& gold,
                 const mermaid::LaidOutClass& out, double tol) {
    for (const auto& gl : gold.labels) {
        int li = -1;
        for (size_t i = 0; i < out.edge_labels.size(); ++i) {
            if (out.edge_labels[i].text == gl.text && out.edge_labels[i].kind == gl.kind) {
                li = static_cast<int>(i); break;
            }
        }
        ASSERT_GE(li, 0);
        EXPECT_NEAR(out.edge_labels[li].x, gl.x, tol);
        EXPECT_NEAR(out.edge_labels[li].y, gl.y, tol);
    }
}

void CheckCanvas(const char* name, const mermaid::ClassGolden& gold,
                 const mermaid::LaidOutClass& out, double tol) {
    EXPECT_NEAR(out.vbwidth, gold.canvas.vbwidth, tol);
    EXPECT_NEAR(out.vbheight, gold.canvas.vbheight, tol);
    EXPECT_NEAR(out.startx, gold.canvas.startx, tol);
    EXPECT_NEAR(out.starty, gold.canvas.starty, tol);
}

}  // namespace

TEST(MermaidClassGolden, NodeCentersAllFixtures) {
    const char* fixtures[] = {"cls1", "cls2", "cls3", "cls4", "cls5"};
    const double tol = 0.5;
    for (const char* name : fixtures) {
        auto gold = mermaid::LoadClassGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        auto out = LayoutFromFixture(name);
        CheckNodes(name, gold, out, tol);
    }
}

TEST(MermaidClassGolden, EdgeMarkersAndPolylines) {
    const char* fixtures[] = {"cls1", "cls2", "cls3", "cls4", "cls5"};
    const double tol = 0.5;
    for (const char* name : fixtures) {
        auto gold = mermaid::LoadClassGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        auto out = LayoutFromFixture(name);
        CheckEdges(name, gold, out, tol);
    }
}

TEST(MermaidClassGolden, EdgeLabelsAndTerminals) {
    const char* fixtures[] = {"cls1", "cls2", "cls3", "cls4", "cls5"};
    const double tol = 0.5;
    for (const char* name : fixtures) {
        auto gold = mermaid::LoadClassGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        auto out = LayoutFromFixture(name);
        CheckLabels(name, gold, out, tol);
    }
}

TEST(MermaidClassGolden, CanvasMatches) {
    const char* fixtures[] = {"cls1", "cls2", "cls3", "cls4", "cls5"};
    const double tol = 0.5;
    for (const char* name : fixtures) {
        auto gold = mermaid::LoadClassGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        auto out = LayoutFromFixture(name);
        CheckCanvas(name, gold, out, tol);
    }
}