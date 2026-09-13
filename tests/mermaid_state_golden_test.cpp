// Golden gate tests for the state diagram layout, mirroring the class/seq
// pattern: parse each stt*.mmd fixture, run LayoutStateDiagram, and compare
// nodes (centers + sizes), edge d-strings, edge labels, and the canvas
// against the committed oracle goldens. Tolerance = 0.5 DIP.
#include "../src/mermaid/state_layout.h"
#include "../src/mermaid/state_parse.h"
#include "mermaid/mermaid_state_golden_loader.h"
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

mermaid::LaidOutState LayoutFromFixture(const char* name) {
    mermaid::StateDiagram d = mermaid::ParseStateDiagram(ReadFixture(name));
    return mermaid::LayoutStateDiagram(d);
}

void CheckNodes(const char* name, const mermaid::StateGolden& gold,
                const mermaid::LaidOutState& out, double tol) {
    for (const auto& gn : gold.nodes) {
        int idx = -1;
        for (size_t i = 0; i < out.nodes.size(); ++i) {
            if (out.nodes[i].id == gn.id) { idx = static_cast<int>(i); break; }
        }
        ASSERT_GE(idx, 0);
        double dx = std::abs(out.nodes[idx].cx - gn.cx);
        double dy = std::abs(out.nodes[idx].cy - gn.cy);
        double dw = std::abs(out.nodes[idx].w - gn.w);
        double dh = std::abs(out.nodes[idx].h - gn.h);
        if (dx > tol || dy > tol || dw > tol || dh > tol) {
            std::fprintf(stderr,
                "[%s] node %s: ours=(%.3f,%.3f,%.3fx%.3f) golden=(%.3f,%.3f,%.3fx%.3f)\n",
                name, gn.id.c_str(), out.nodes[idx].cx, out.nodes[idx].cy,
                out.nodes[idx].w, out.nodes[idx].h, gn.cx, gn.cy, gn.w, gn.h);
        }
        EXPECT_NEAR(out.nodes[idx].cx, gn.cx, tol);
        EXPECT_NEAR(out.nodes[idx].cy, gn.cy, tol);
        EXPECT_NEAR(out.nodes[idx].w, gn.w, tol);
        EXPECT_NEAR(out.nodes[idx].h, gn.h, tol);
    }
}

void CheckEdges(const char* name, const mermaid::StateGolden& gold,
                const mermaid::LaidOutState& out, double tol) {
    ASSERT_EQ(gold.edges.size(), out.edges.size());
    for (size_t i = 0; i < gold.edges.size(); ++i) {
        const auto& ge = gold.edges[i];
        const auto& e = out.edges[i];
        // The oracle JSON does not carry from/to (empty strings); the d
        // string comparison below is the authoritative check.
        // Compare the numeric tokens of the d strings.
        auto nums = [](const std::string& d, std::vector<double>& v) {
            v.clear();
            std::string cur;
            for (char ch : d) {
                if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.') { cur += ch; }
                else if (!cur.empty()) { v.push_back(std::strtod(cur.c_str(), nullptr)); cur.clear(); }
            }
            if (!cur.empty()) v.push_back(std::strtod(cur.c_str(), nullptr));
        };
        std::vector<double> a, b;
        nums(e.d, a); nums(ge.d, b);
        if (a.size() != b.size()) {
            std::fprintf(stderr, "[%s] edge%zu token count: ours=%zu golden=%zu\n",
                         name, i, a.size(), b.size());
        }
        ASSERT_EQ(a.size(), b.size());
        for (size_t k = 0; k < a.size(); ++k) {
            if (std::abs(a[k] - b[k]) > tol) {
                std::fprintf(stderr,
                    "[%s] edge%zu token[%zu]: ours=%.3f golden=%.3f (ours d=%s)\n",
                    name, i, k, a[k], b[k], e.d.c_str());
            }
            EXPECT_NEAR(a[k], b[k], tol);
        }
    }
}

void CheckLabels(const char* name, const mermaid::StateGolden& gold,
                 const mermaid::LaidOutState& out, double tol) {
    ASSERT_EQ(gold.labels.size(), out.labels.size());
    for (size_t i = 0; i < gold.labels.size(); ++i) {
        const auto& gl = gold.labels[i];
        const auto& l = out.labels[i];
        EXPECT_EQ(gl.text, l.text);
        // golden stores the label CENTER; ours stores the top-left corner.
        double cx = l.x + l.w * 0.5;
        double cy = l.y + l.h * 0.5;
        if (std::abs(cx - gl.x) > tol || std::abs(cy - gl.y) > tol) {
            std::fprintf(stderr,
                "[%s] label %s: ours-center=(%.3f,%.3f) golden=(%.3f,%.3f)\n",
                name, gl.text.c_str(), cx, cy, gl.x, gl.y);
        }
        EXPECT_NEAR(cx, gl.x, tol);
        EXPECT_NEAR(cy, gl.y, tol);
    }
}

void CheckCanvas(const char* name, const mermaid::StateGolden& gold,
                 const mermaid::LaidOutState& out, double tol) {
    EXPECT_NEAR(out.vbwidth, gold.canvas.vbwidth, tol);
    EXPECT_NEAR(out.vbheight, gold.canvas.vbheight, tol);
    EXPECT_NEAR(out.startx, gold.canvas.startx, tol);
    EXPECT_NEAR(out.starty, gold.canvas.starty, tol);
}

}  // namespace

TEST(MermaidStateGolden, NodeCentersAllFixtures) {
    const char* fixtures[] = {"stt1", "stt3", "stt4", "stt5"};
    const double tol = 0.5;
    for (const char* name : fixtures) {
        auto gold = mermaid::LoadStateGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        auto out = LayoutFromFixture(name);
        ASSERT_TRUE(out.error.empty());
        CheckNodes(name, gold, out, tol);
    }
}

TEST(MermaidStateGolden, EdgePaths) {
    const char* fixtures[] = {"stt1", "stt3", "stt4", "stt5"};
    const double tol = 0.5;
    for (const char* name : fixtures) {
        auto gold = mermaid::LoadStateGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        auto out = LayoutFromFixture(name);
        ASSERT_TRUE(out.error.empty());
        CheckEdges(name, gold, out, tol);
    }
}

TEST(MermaidStateGolden, EdgeLabels) {
    const char* fixtures[] = {"stt1", "stt3", "stt4", "stt5"};
    const double tol = 0.5;
    for (const char* name : fixtures) {
        auto gold = mermaid::LoadStateGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        auto out = LayoutFromFixture(name);
        ASSERT_TRUE(out.error.empty());
        CheckLabels(name, gold, out, tol);
    }
}

TEST(MermaidStateGolden, CanvasMatches) {
    const char* fixtures[] = {"stt1", "stt3", "stt4", "stt5"};
    const double tol = 0.5;
    for (const char* name : fixtures) {
        auto gold = mermaid::LoadStateGolden(
            std::string("tests/mermaid/golden/") + name + ".json");
        auto out = LayoutFromFixture(name);
        ASSERT_TRUE(out.error.empty());
        CheckCanvas(name, gold, out, tol);
    }
}