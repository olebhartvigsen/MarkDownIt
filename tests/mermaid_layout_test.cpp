#include "../src/mermaid/layout_internal.h"
#include "gtest_lite.h"

using mermaid::MakeGraph;

TEST(MermaidAcyclic, BreaksSimpleCycle) {
    // A -> B -> C -> A
    auto g = MakeGraph(3, {{0,1},{1,2},{2,0}});
    auto reversed = mermaid::MakeAcyclic(g);
    EXPECT_EQ(reversed.size(), 1u);
    EXPECT_TRUE(mermaid::IsAcyclic(g));
}
TEST(MermaidAcyclic, LeavesDagUntouched) {
    auto g = MakeGraph(3, {{0,1},{1,2}});
    EXPECT_EQ(mermaid::MakeAcyclic(g).size(), 0u);
}
TEST(MermaidAcyclic, HandlesSelfLoop) {
    auto g = MakeGraph(1, {{0,0}});
    mermaid::MakeAcyclic(g);            // self-loop removed from ranking, drawn separately
    EXPECT_TRUE(mermaid::IsAcyclic(g));
}
