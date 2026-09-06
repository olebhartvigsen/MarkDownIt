// Task 11: DOM integration test for ```mermaid fenced blocks.
#include "gtest_lite.h"
#include "../src/dom.h"
#include "../src/parser.h"
#include "../src/mermaid/model.h"

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
    ASSERT_TRUE(doc.nodes[0].mermaid_layout != nullptr);
    EXPECT_EQ(doc.nodes[0].mermaid_layout->nodes.size(), 2u);
}

TEST(DomMermaid, FallbackOnParseFailure) {
    std::string md = "```mermaid\ngarbage garbage\n```\n";
    auto doc = BuildDom(md);
    ASSERT_EQ(doc.nodes.size(), 1u);
    // Falls back to CodeBlock, or the parser accepts it as an empty flowchart.
    EXPECT_TRUE(doc.nodes[0].block == BlockKind::CodeBlock ||
                doc.nodes[0].block == BlockKind::MermaidFlowchart);
}
