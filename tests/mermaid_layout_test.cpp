#include "../src/mermaid/layout_internal.h"
#include "mermaid/golden_loader.h"
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

TEST(MermaidRank, LongestPathChain) {
    auto g = MakeGraph(3, {{0,1},{1,2}});
    mermaid::AssignRanks(g);
    EXPECT_EQ(g.nodes[0].rank, 0);
    EXPECT_EQ(g.nodes[1].rank, 1);
    EXPECT_EQ(g.nodes[2].rank, 2);
}
TEST(MermaidRank, NetworkSimplexTightensLongEdge) {
    // A->B, A->C, B->D, C->D : D must be rank 2, not 3
    auto g = MakeGraph(4, {{0,1},{0,2},{1,3},{2,3}});
    mermaid::AssignRanks(g);
    EXPECT_EQ(g.nodes[3].rank, 2);
}

TEST(MermaidGolden, RanksMatchDagre_01Linear) {
    auto gold = mermaid::LoadGolden("tests/mermaid/golden/01-linear.json");
    auto ours = mermaid::LayoutFromGolden(gold);
    mermaid::MakeAcyclic(ours);
    mermaid::AssignRanks(ours);
    for (const auto& gn : gold.nodes)
        EXPECT_EQ(mermaid::RankOf(ours, gn.id), gn.rank);
}

TEST(MermaidNormalize, SplitsLongEdge) {
    auto g = MakeGraph(3, {{0,1},{1,2},{0,2}});   // 0->2 spans two ranks
    mermaid::AssignRanks(g);
    size_t before = g.nodes.size();
    mermaid::Normalize(g);
    // Normalize doubles ranks (dagre makeSpaceForEdgeLabels) then inserts a
    // mid-rank dummy on every unit segment. Two unit edges each get 1 dummy,
    // the span-2 edge gets 3, total 5 new dummies.
    EXPECT_GT(g.nodes.size(), before);
    EXPECT_TRUE(mermaid::AllEdgesUnitLength(g));
}
TEST(MermaidNormalize, DenormalizeRestoresEdgeCount) {
    auto g = MakeGraph(3, {{0,1},{1,2},{0,2}});
    mermaid::AssignRanks(g); mermaid::Normalize(g); mermaid::Denormalize(g);
    EXPECT_EQ(g.edges.size(), 3u);
}

TEST(MermaidOrder, ResolvesObviousCrossing) {
    // A->D, B->C with A,B on rank 0 and C,D on rank 1 : one crossing if
    // order is (A,B),(C,D); zero if ordering swaps.
    auto g = MakeGraph(4, {{0,3},{1,2}});
    mermaid::AssignRanks(g); mermaid::Normalize(g); mermaid::Order(g);
    EXPECT_EQ(mermaid::CountCrossings(g), 0);
}
TEST(MermaidOrder, CrossingCountIsMonotone) {
    // Same shape as 03-diamond: A->B, A->C, B->D, C->D.
    auto g = MakeGraph(4, {{0,1},{0,2},{1,3},{2,3}});
    mermaid::AssignRanks(g); mermaid::Normalize(g);
    int before = mermaid::CountCrossings(g);
    mermaid::Order(g);
    EXPECT_TRUE(mermaid::CountCrossings(g) <= before);
}

TEST(MermaidPosition, YComesFromRank) {
    auto g = MakeGraph(3, {{0,1},{1,2}});
    for (auto& n : g.nodes) { n.width = 80; n.height = 40; }
    mermaid::AssignRanks(g); mermaid::Normalize(g); mermaid::Order(g);
    mermaid::AssignCoordinates(g, mermaid::LayoutParams{});
    EXPECT_LT(g.nodes[0].y, g.nodes[1].y);
    EXPECT_LT(g.nodes[1].y, g.nodes[2].y);
}

TEST(MermaidPosition, GoldenAgreesWith_01Linear) {
    auto g = mermaid::BuildFixtureGraph("01-linear");
    mermaid::AssignRanks(g); mermaid::Normalize(g); mermaid::Order(g);
    mermaid::AssignCoordinates(g, mermaid::LayoutParams{});
    auto golden = mermaid::LoadGolden("tests/mermaid/golden/01-linear.json");
    for (const auto& gn : golden.nodes) {
        int idx = mermaid::FindByLabel(g, gn.label);
        ASSERT_GE(idx, 0);
        EXPECT_NEAR(g.nodes[idx].x, gn.x, 2.0);
        EXPECT_NEAR(g.nodes[idx].y, gn.y, 2.0);
    }
}

TEST(MermaidEdgeRoute, EndpointsOnNodeBorders) {
    auto g = MakeGraph(2, {{0,1}});
    RunLayoutPipeline(g, mermaid::LayoutParams{});
    ASSERT_EQ(g.edges.size(), 1u);
    const auto& e = g.edges[0];
    EXPECT_NEAR(e.route.front().y, g.nodes[0].y + g.nodes[0].height/2.0, 1.0);
    EXPECT_NEAR(e.route.back().y,  g.nodes[1].y - g.nodes[1].height/2.0, 1.0);
}

TEST(MermaidEdgeRoute, LongEdgeHasBends) {
    auto g = MakeGraph(3, {{0,1},{1,2},{0,2}});
    RunLayoutPipeline(g, mermaid::LayoutParams{});
    int idx = mermaid::FindEdge(g, 0, 2);
    ASSERT_GE(idx, 0);
    EXPECT_GE(g.edges[idx].route.size(), 3u);
}
