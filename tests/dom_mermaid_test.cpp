// Task 11: DOM integration test for ```mermaid fenced blocks.
#include "gtest_lite.h"
#include "../src/dom.h"
#include "../src/parser.h"
#include "../src/mermaid/model.h"
#include "../src/mermaid/gantt_layout.h"
#include "../src/mermaid/state_layout.h"

namespace {
Document BuildDom(const std::string& md) {
    Document doc;
    ParseMarkdown(md, doc);
    return doc;
}
}

TEST(DomMermaid, RecognizesFlowchartFence) {
    std::string md = "```mermaid\nflowchart TD\nA --> B\n```\n";
    auto doc = BuildDom(md);
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].block, BlockKind::MermaidFlowchart);
    ASSERT_TRUE(doc.nodes[0].mermaid_flowchart != nullptr);
    ASSERT_TRUE(doc.nodes[0].mermaid_layout != nullptr);
    EXPECT_EQ(doc.nodes[0].mermaid_source, "flowchart TD\nA --> B\n");
    EXPECT_EQ(doc.nodes[0].mermaid_flowchart->nodes.size(), 2u);
    EXPECT_EQ(doc.nodes[0].mermaid_layout->nodes.size(), 2u);
}

TEST(DomMermaid, FallbackOnParseFailure) {
    std::string md = "```mermaid\ngarbage garbage\n```\n";
    auto doc = BuildDom(md);
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].block, BlockKind::CodeBlock);
    EXPECT_TRUE(doc.nodes[0].mermaid_flowchart == nullptr);
    EXPECT_TRUE(doc.nodes[0].mermaid_layout == nullptr);
}

TEST(DomMermaid, RecognizesGanttFence) {
    std::string md =
        "```mermaid\n"
        "gantt\n"
        "    dateFormat DD/MM/YYYY\n"
        "    section S\n"
        "    Task A :a1, 01/03/2026, 5d\n"
        "```\n";
    auto doc = BuildDom(md);
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].block, BlockKind::MermaidGantt);
    ASSERT_TRUE(doc.nodes[0].mermaid_gantt != nullptr);
    EXPECT_EQ(doc.nodes[0].mermaid_gantt->bars.size(), 1u);
}

// A state fence is a diagram, not a code card: the parser promotes it
// and the layout is already there for the painter.
TEST(DomMermaid, RecognizesStateDiagramFence) {
    const std::string md =
        "```mermaid\n"
        "stateDiagram-v2\n"
        "    [*] --> Idle\n"
        "    Idle --> Running : start\n"
        "    Running --> [*]\n"
        "```\n";
    const Document doc = BuildDom(md);
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].block, BlockKind::MermaidState);
    ASSERT_TRUE(doc.nodes[0].mermaid_state != nullptr);
    EXPECT_TRUE(doc.nodes[0].mermaid_state->error.empty());
    EXPECT_GT(doc.nodes[0].mermaid_state->nodes.size(), 0u);
    EXPECT_GT(doc.nodes[0].mermaid_state->width, 0.0);
    EXPECT_GT(doc.nodes[0].mermaid_state->height, 0.0);
}

// A fence that opens as a state diagram but holds something the layout
// refuses stays a fence, the same rule the other kinds follow.
TEST(DomMermaid, StateDiagramWithCompositeStaysACodeBlock) {
    const std::string md =
        "```mermaid\n"
        "stateDiagram-v2\n"
        "    state Active {\n"
        "        [*] --> Busy\n"
        "    }\n"
        "```\n";
    const Document doc = BuildDom(md);
    ASSERT_EQ(doc.nodes.size(), 1u);
    EXPECT_EQ(doc.nodes[0].block, BlockKind::CodeBlock);
}
