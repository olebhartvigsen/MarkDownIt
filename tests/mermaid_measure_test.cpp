// Measurement-path tests. The callback is portable; Windows supplies DirectWrite.
#include "gtest_lite.h"
#include "../src/mermaid/layout.h"
#include "../src/mermaid/model.h"

using namespace mermaid;

static LabelSize StubMeasure(const std::string& label, float /*max_width*/, void*) {
    return {static_cast<float>(label.size()) * 8.4f, 19.0f};
}
static LabelSize FailingMeasure(const std::string&, float, void*) { return {0.0f, 0.0f}; }

static Flowchart TwoNodeFlow(Dir dir) {
    Flowchart flow;
    flow.dir = dir;
    flow.nodes.push_back({"A", "Start", Shape::Rect});
    flow.nodes.push_back({"B", "End", Shape::Round});
    flow.edges.push_back(FlowEdge{0, 1, "", LineStyle::Solid, Head::Arrow, 1, 1});
    return flow;
}

TEST(MermaidMeasure, StubReproducesGoldenSizes) {
    auto laid = LayoutFlowchartWith(TwoNodeFlow(Dir::TB), StubMeasure, nullptr);
    EXPECT_NEAR(laid.nodes[0].width, 72.0f, 0.5f);
    EXPECT_NEAR(laid.nodes[0].height, 49.0f, 0.5f);
}

TEST(MermaidMeasure, HandlesAllRankDirections) {
    const Dir dirs[] = {Dir::TB, Dir::BT, Dir::LR, Dir::RL};
    for (const Dir dir : dirs) {
        const auto laid = LayoutFlowchartWith(TwoNodeFlow(dir), StubMeasure, nullptr);
        ASSERT_EQ(laid.nodes.size(), 2u);
        EXPECT_NEAR(laid.nodes[0].width, 72.0f, 0.5f);
        EXPECT_NEAR(laid.nodes[0].height, 49.0f, 0.5f);
        ASSERT_TRUE(!laid.edges.empty());
        ASSERT_GE(laid.edges[0].route.size(), 2u);
        if (dir == Dir::TB) EXPECT_LT(laid.nodes[0].y, laid.nodes[1].y);
        if (dir == Dir::BT) EXPECT_GT(laid.nodes[0].y, laid.nodes[1].y);
        if (dir == Dir::LR) EXPECT_LT(laid.nodes[0].x, laid.nodes[1].x);
        if (dir == Dir::RL) EXPECT_GT(laid.nodes[0].x, laid.nodes[1].x);
    }
}

TEST(MermaidMeasure, FailedMeasureFallsBackToStubLayout) {
    const auto flow = TwoNodeFlow(Dir::LR);
    const auto expected = LayoutFlowchart(flow, LayoutParams{});
    const auto actual = LayoutFlowchartWith(flow, FailingMeasure, nullptr);
    ASSERT_EQ(actual.nodes.size(), expected.nodes.size());
    EXPECT_NEAR(actual.nodes[0].width, expected.nodes[0].width, 0.001f);
    EXPECT_NEAR(actual.nodes[0].height, expected.nodes[0].height, 0.001f);
    EXPECT_NEAR(actual.nodes[0].x, expected.nodes[0].x, 0.001f);
    EXPECT_NEAR(actual.nodes[1].x, expected.nodes[1].x, 0.001f);
}