// Task 11: verify MeasureFn seam. A stub measurer reproduces the pre-Task-11
// stubbed sizes (label chars * 8.4 wide, 19 tall) once we add padding 15
// on each side.
#include "gtest_lite.h"
#include "../src/mermaid/layout.h"
#include "../src/mermaid/model.h"

using namespace mermaid;

static LabelSize StubMeasure(const std::string& s, float /*maxWidth*/, void*) {
    return { static_cast<float>(s.size()) * 8.4f, 19.0f };
}

TEST(MermaidMeasure, StubReproducesGoldenSizes) {
    Flowchart f;
    f.dir = Dir::TB;
    FlowNode a; a.id = "A"; a.label = "Start"; a.shape = Shape::Rect;
    f.nodes.push_back(a);
    auto laid = LayoutFlowchartWith(f, StubMeasure, nullptr);
    // 5 chars * 8.4 = 42, + padding*2 (30) = 72; 19 + 30 = 49.
    EXPECT_NEAR(laid.nodes[0].width, 72.0f, 0.5f);
    EXPECT_NEAR(laid.nodes[0].height, 49.0f, 0.5f);
}

TEST(MermaidMeasure, NullMeasureUsesDefaults) {
    Flowchart f;
    f.dir = Dir::TB;
    FlowNode a; a.id = "A"; a.label = "Start"; a.shape = Shape::Rect;
    f.nodes.push_back(a);
    auto laid = LayoutFlowchartWith(f, nullptr, nullptr);
    // Falls back to FlowchartToLayoutGraph's built-in sizing.
    EXPECT_GT(laid.nodes[0].width, 0.0f);
    EXPECT_GT(laid.nodes[0].height, 0.0f);
}
