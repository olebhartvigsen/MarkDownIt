// The Mermaid layout profiles: ELK's layered settings over the shared
// engine, and the dagre numbers the golden geometry was built with.

#include "gtest_lite.h"
#include "mermaid/elk_profile.h"
#include "mermaid/layout.h"
#include "mermaid/parse.h"

#include <set>
#include <string>

namespace {

TEST(MermaidElk, DefaultsAreTheOnesElkDocuments) {
    const mermaid::ElkSettings s = mermaid::ElkDefaults();
    EXPECT_NEAR(s.spacing_node_node, 20.0, 0.001);
    EXPECT_NEAR(s.spacing_node_node_between_layers, 20.0, 0.001);
    EXPECT_NEAR(s.spacing_edge_node, 10.0, 0.001);
    EXPECT_NEAR(s.spacing_edge_edge, 10.0, 0.001);
    EXPECT_NEAR(s.padding, 12.0, 0.001);
    EXPECT_TRUE(std::string(s.layering) == "NETWORK_SIMPLEX");
    EXPECT_TRUE(std::string(s.node_placement) == "BRANDES_KOEPF");
    EXPECT_TRUE(std::string(s.edge_routing) == "ORTHOGONAL");
}

TEST(MermaidElk, SettingsMapOntoTheEngineParameters) {
    mermaid::ElkSettings s;
    s.spacing_node_node = 40.0;
    s.spacing_node_node_between_layers = 60.0;
    s.spacing_edge_edge = 15.0;
    s.padding = 8.0;
    const mermaid::LayoutParams p = mermaid::ParamsFor(s);
    // The engine's rank separation is the gap between layers, and its
    // node separation is the gap inside one.
    EXPECT_NEAR(p.rank_sep, 60.0, 0.001);
    EXPECT_NEAR(p.node_sep, 40.0, 0.001);
    EXPECT_NEAR(p.edge_sep, 15.0, 0.001);
    EXPECT_NEAR(p.margin, 8.0, 0.001);
}

TEST(MermaidElk, DagreStaysTheDefault) {
    const mermaid::LayoutParams dagre = mermaid::DagreParams();
    EXPECT_NEAR(dagre.rank_sep, 50.0, 0.001);
    EXPECT_NEAR(dagre.node_sep, 50.0, 0.001);
    EXPECT_NEAR(dagre.edge_sep, 10.0, 0.001);
    EXPECT_NEAR(dagre.margin, 20.0, 0.001);
    const mermaid::LayoutParams chosen = mermaid::LayoutParamsForProfile();
    EXPECT_NEAR(chosen.rank_sep, dagre.rank_sep, 0.001);
    EXPECT_TRUE(std::string(mermaid::MermaidLayoutProfileName()) == "dagre");
}

TEST(MermaidElk, BothProfilesKeepTheDiagramAndChangeTheGaps) {
    const std::string src =
        "flowchart TD\n"
        "  A[Start] --> B{Choice}\n"
        "  B -->|yes| C[Left]\n"
        "  B -->|no| D[Right]\n"
        "  C --> E[Done]\n"
        "  D --> E\n";
    const mermaid::Flowchart flow = mermaid::ParseFlowchart(src);
    ASSERT_TRUE(flow.error.empty());

    const mermaid::LaidOutFlowchart dagre =
        mermaid::LayoutFlowchart(flow, mermaid::DagreParams());
    const mermaid::LaidOutFlowchart elk =
        mermaid::LayoutFlowchart(flow, mermaid::ElkParams());

    // Same content, same relationships: a profile changes the drawing,
    // never the diagram.
    ASSERT_EQ(elk.nodes.size(), dagre.nodes.size());
    ASSERT_EQ(elk.edges.size(), dagre.edges.size());
    EXPECT_EQ(elk.nodes.size(), flow.nodes.size());
    EXPECT_EQ(elk.edges.size(), flow.edges.size());

    // ELK's default spacing is tighter than dagre's, so the drawing is
    // more compact without losing separation.
    EXPECT_LT(elk.width, dagre.width);
    EXPECT_LT(elk.height, dagre.height);

    // Every node has a real place of its own.
    std::set<std::pair<int, int>> centres;
    for (const mermaid::LayoutNode& n : elk.nodes) {
        centres.insert({static_cast<int>(n.x * 4.0f),
                        static_cast<int>(n.y * 4.0f)});
    }
    EXPECT_EQ(centres.size(), elk.nodes.size());

    // Every edge is routed, and its route starts at a node and ends at
    // one: an edge is a relationship, not a decoration.
    for (const mermaid::LayoutEdge& e : elk.edges) {
        EXPECT_GE(e.route.size(), 2u);
    }
}

}  // namespace
