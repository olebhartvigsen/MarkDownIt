// Layout cache / measure-render agreement tests (Task 12 followup).
#include "gtest_lite.h"
#include "../src/mermaid/layout_cache.h"
#include "../src/mermaid/model.h"
#include "../src/mermaid/parse.h"
#include "../src/mermaid/layout.h"

using namespace mermaid;

TEST(MermaidCache, HashEqualForSameSource) {
    std::string src = "flowchart TD\nA --> B\n";
    EXPECT_EQ(HashFenceSource(src, 1.0f), HashFenceSource(src, 1.0f));
}

TEST(MermaidCache, HashDiffersForDifferentSource) {
    std::string a = "flowchart TD\nA --> B\n";
    std::string b = "flowchart TD\nA --> C\n";
    EXPECT_FALSE(HashFenceSource(a, 1.0f) == HashFenceSource(b, 1.0f));
}

TEST(MermaidCache, HashDiffersForDifferentZoom) {
    std::string src = "flowchart TD\nA --> B\n";
    EXPECT_FALSE(HashFenceSource(src, 1.0f) == HashFenceSource(src, 1.5f));
}

TEST(MermaidCache, MeasureHeightIsIdempotent) {
    Flowchart fc = ParseFlowchart("flowchart TD\nA --> B\nB --> C\n");
    LayoutParams p;
    LaidOutFlowchart lo = LayoutFlowchart(fc, p);
    float h1 = MeasureLayoutHeight(lo, 1.0f);
    float h2 = MeasureLayoutHeight(lo, 1.0f);
    EXPECT_EQ(h1, h2);
    EXPECT_GT(h1, 2.0f * kMermaidBlockPad);
}

TEST(MermaidCache, MeasureHeightScalesWithZoom) {
    Flowchart fc = ParseFlowchart("flowchart TD\nA --> B\n");
    LayoutParams p;
    LaidOutFlowchart lo = LayoutFlowchart(fc, p);
    float h1 = MeasureLayoutHeight(lo, 1.0f);
    float h2 = MeasureLayoutHeight(lo, 2.0f);
    // Padding is constant, so h2 - 2*pad == 2 * (h1 - 2*pad).
    float body1 = h1 - 2.0f * kMermaidBlockPad;
    float body2 = h2 - 2.0f * kMermaidBlockPad;
    EXPECT_NEAR(body2, 2.0f * body1, 1e-3f);
}
