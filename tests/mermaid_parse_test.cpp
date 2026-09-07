#include "gtest_lite.h"
#include "../src/mermaid/parse.h"

TEST(MermaidParse, HeaderDirection) {
    EXPECT_EQ((int)mermaid::ParseFlowchart("flowchart TD").dir, (int)mermaid::Dir::TB);
    EXPECT_EQ((int)mermaid::ParseFlowchart("flowchart TB").dir, (int)mermaid::Dir::TB);
    EXPECT_EQ((int)mermaid::ParseFlowchart("graph LR").dir,     (int)mermaid::Dir::LR);
    EXPECT_EQ((int)mermaid::ParseFlowchart("graph RL").dir,     (int)mermaid::Dir::RL);
    EXPECT_EQ((int)mermaid::ParseFlowchart("flowchart BT").dir, (int)mermaid::Dir::BT);
    EXPECT_EQ((int)mermaid::ParseFlowchart("flowchart").dir,    (int)mermaid::Dir::TB);  // default
}

TEST(MermaidParse, RejectsNonFlowchart) {
    EXPECT_TRUE(!mermaid::ParseFlowchart("sequenceDiagram").error.empty());
}

TEST(MermaidParse, NodeShapes) {
    auto f = mermaid::ParseFlowchart(
        "flowchart TD\n"
        "  A[Rect]\n  B(Round)\n  C([Stadium])\n  D{Diamond}\n  E((Circle))\n  F\n");
    ASSERT_EQ(f.nodes.size(), 6u);
    EXPECT_EQ((int)f.nodes[0].shape, (int)mermaid::Shape::Rect);
    EXPECT_EQ((int)f.nodes[1].shape, (int)mermaid::Shape::Round);
    EXPECT_EQ((int)f.nodes[2].shape, (int)mermaid::Shape::Stadium);
    EXPECT_EQ((int)f.nodes[3].shape, (int)mermaid::Shape::Diamond);
    EXPECT_EQ((int)f.nodes[4].shape, (int)mermaid::Shape::Circle);
    EXPECT_EQ((int)f.nodes[5].shape, (int)mermaid::Shape::Rect);   // bare id
    EXPECT_EQ(f.nodes[5].label, "F");                              // label defaults to id
}

TEST(MermaidParse, EdgeStyles) {
    auto f = mermaid::ParseFlowchart(
        "flowchart TD\n  A --> B\n  B -.-> C\n  C ==> D\n  D --- E\n");
    ASSERT_EQ(f.edges.size(), 4u);
    EXPECT_EQ((int)f.edges[0].style, (int)mermaid::LineStyle::Solid);
    EXPECT_EQ((int)f.edges[1].style, (int)mermaid::LineStyle::Dotted);
    EXPECT_EQ((int)f.edges[2].style, (int)mermaid::LineStyle::Thick);
    EXPECT_EQ((int)f.edges[3].head,  (int)mermaid::Head::None);   // --- is open
}
TEST(MermaidParse, EdgeLabelAndChain) {
    auto f = mermaid::ParseFlowchart("flowchart TD\n  A -->|yes| B --> C\n");
    ASSERT_EQ(f.nodes.size(), 3u);
    ASSERT_EQ(f.edges.size(), 2u);
    EXPECT_EQ(f.edges[0].label, "yes");
    EXPECT_EQ(f.edges[1].label, "");
}

// Dotted open edges with inline text: `A -. text .-> B` and `A -. text .- B`.
TEST(MermaidParse, DottedInlineLabel) {
    auto f = mermaid::ParseFlowchart(
        "flowchart TD\n  A -. maskering af uploads .-> B\n  C -. klientmaskering .- D\n");
    ASSERT_EQ(f.edges.size(), 2u);
    EXPECT_TRUE(f.edges[0].style == mermaid::LineStyle::Dotted);
    EXPECT_TRUE(f.edges[0].head == mermaid::Head::Arrow);
    EXPECT_EQ(f.edges[0].label, "maskering af uploads");
    EXPECT_TRUE(f.edges[1].style == mermaid::LineStyle::Dotted);
    EXPECT_TRUE(f.edges[1].head == mermaid::Head::None);
    EXPECT_EQ(f.edges[1].label, "klientmaskering");
    ASSERT_EQ(f.nodes.size(), 4u);
}

// Redeclared node: explicit label replaces an earlier label; a bare
// re-mention (label == id) does not erase an earlier explicit label.
TEST(MermaidParse, RedeclareLabelRules) {
    auto f = mermaid::ParseFlowchart(
        "flowchart TB\n"
        "  U --> PII\n"
        "  PII[PII-kontrolservice<br/>Presidio Analyzer]\n");
    ASSERT_EQ(f.nodes.size(), 2u);
    EXPECT_EQ(f.nodes[1].label, "PII-kontrolservice<br/>Presidio Analyzer");

    auto g = mermaid::ParseFlowchart(
        "flowchart TB\n"
        "  A[Latest text] --> B\n"
        "  A --> C\n");
    ASSERT_EQ(g.nodes.size(), 3u);
    EXPECT_EQ(g.nodes[0].label, "Latest text");
}
