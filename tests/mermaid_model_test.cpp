#include "gtest_lite.h"
#include "../src/mermaid/model.h"

using namespace mermaid;

TEST(MermaidModel, Defaults) {
    Flowchart fc;
    EXPECT_EQ(static_cast<int>(fc.dir), static_cast<int>(Dir::TB));
    EXPECT_TRUE(fc.nodes.empty());
    EXPECT_TRUE(fc.edges.empty());
    EXPECT_TRUE(fc.error.empty());
}

TEST(MermaidModel, TwoNodesOneEdge) {
    Flowchart fc;
    FlowNode a; a.id = "A"; a.label = "Alpha"; a.shape = Shape::Rect;
    FlowNode b; b.id = "B"; b.label = "Beta";  b.shape = Shape::Rect;
    fc.nodes.push_back(a);
    fc.nodes.push_back(b);

    FlowEdge e;
    e.from = 0;
    e.to = 1;
    fc.edges.push_back(e);

    ASSERT_EQ(fc.nodes.size(), 2u);
    ASSERT_EQ(fc.edges.size(), 1u);
    EXPECT_EQ(static_cast<int>(fc.edges[0].head), static_cast<int>(Head::Arrow));
    EXPECT_EQ(static_cast<int>(fc.edges[0].style), static_cast<int>(LineStyle::Solid));
    EXPECT_EQ(fc.edges[0].minlen, 1);
    EXPECT_EQ(fc.edges[0].weight, 1);
    EXPECT_TRUE(fc.edges[0].label.empty());
}
