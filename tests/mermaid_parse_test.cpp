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
