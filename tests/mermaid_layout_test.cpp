#include "gtest_lite.h"
#include "mermaid.h"
#include "mermaidlayout.h"

using namespace mermaid;

static float StubMeasure(const std::string& s, void*) {
    return static_cast<float>(s.size()) * 8.0f;
}

// --- Opgave 9: Layered flowchart layout ---

TEST(MermaidLayout, AssignsLayers) {
    auto d = Parse("graph TD\n  A --> B\n  B --> C\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.nodes.size(), 3u);
    EXPECT_TRUE(l.nodes[0].y < l.nodes[1].y);
    EXPECT_TRUE(l.nodes[1].y < l.nodes[2].y);
}

TEST(MermaidLayout, HorizontalDirection) {
    auto d = Parse("graph LR\n  A --> B\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.nodes.size(), 2u);
    EXPECT_TRUE(l.nodes[0].x < l.nodes[1].x);
}

TEST(MermaidLayout, HandlesCycle) {
    auto d = Parse("graph TD\n  A --> B\n  B --> A\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.nodes.size(), 2u);  // must terminate, not hang
}

TEST(MermaidLayout, NodesHavePositiveSize) {
    auto d = Parse("graph TD\n  A[Hello] --> B[World]\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    for (const auto& n : l.nodes) {
        EXPECT_TRUE(n.w > 0);
        EXPECT_TRUE(n.h > 0);
    }
}

TEST(MermaidLayout, EdgesHavePoints) {
    auto d = Parse("graph TD\n  A --> B\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.edges.size(), 1u);
    EXPECT_TRUE(l.edges[0].points.size() >= 2u);
}

TEST(MermaidLayout, EdgeLabelPositioned) {
    auto d = Parse("graph LR\n  A -->|yes| B\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.edges.size(), 1u);
    EXPECT_EQ(l.edges[0].label, std::string("yes"));
    // Label should be somewhere between the two nodes
    float minX = std::min(l.nodes[0].x, l.nodes[1].x);
    float maxX = std::max(l.nodes[0].x + l.nodes[0].w, l.nodes[1].x + l.nodes[1].w);
    EXPECT_TRUE(l.edges[0].labelX >= minX);
    EXPECT_TRUE(l.edges[0].labelX <= maxX);
}

TEST(MermaidLayout, RightToLeftMirrors) {
    auto d = Parse("graph RL\n  A --> B\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.nodes.size(), 2u);
    // In RL, A should be to the right of B (A's x > B's x)
    EXPECT_TRUE(l.nodes[0].x > l.nodes[1].x);
}

TEST(MermaidLayout, EmptyDiagram) {
    auto d = Parse("not a diagram\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    EXPECT_EQ(l.nodes.size(), 0u);
    EXPECT_EQ(l.edges.size(), 0u);
}

TEST(MermaidLayout, ThreeNodeFan) {
    auto d = Parse("graph TD\n  A --> B\n  A --> C\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.nodes.size(), 3u);
    // B and C should be in the same layer (same y)
    EXPECT_TRUE(std::abs(l.nodes[1].y - l.nodes[2].y) < 1.0f);
    // A should be above them
    EXPECT_TRUE(l.nodes[0].y < l.nodes[1].y);
}

// --- Opgave 10: Sequence diagram layout ---

TEST(MermaidLayout, SequenceColumns) {
    auto d = Parse(
        "sequenceDiagram\n  A->>B: en\n  B->>A: to\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.nodes.size(), 2u);
    EXPECT_TRUE(l.nodes[0].x < l.nodes[1].x);
    EXPECT_EQ(l.edges.size(), 2u);
    // Messages go downward
    EXPECT_TRUE(l.edges[0].points[0].second < l.edges[1].points[0].second);
}

TEST(MermaidLayout, SequenceParticipantOrder) {
    auto d = Parse(
        "sequenceDiagram\n"
        "  participant Charlie\n"
        "  participant Alice\n"
        "  participant Bob\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.nodes.size(), 3u);
    // Charlie first, then Alice, then Bob (declaration order)
    EXPECT_TRUE(l.nodes[0].x < l.nodes[1].x);
    EXPECT_TRUE(l.nodes[1].x < l.nodes[2].x);
    EXPECT_EQ(l.nodes[0].label, std::string("Charlie"));
}

TEST(MermaidLayout, SequenceEdgeDirection) {
    auto d = Parse("sequenceDiagram\n  Left->>Right: msg\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.edges.size(), 1u);
    // Edge goes from Left to Right
    EXPECT_TRUE(l.edges[0].points[0].first < l.edges[0].points[1].first);
}

TEST(MermaidLayout, SequenceHasPositiveHeight) {
    auto d = Parse("sequenceDiagram\n  A->>B: hello\n  B->>A: reply\n");
    auto l = ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    EXPECT_TRUE(l.height > 0);
    EXPECT_TRUE(l.width > 0);
}
