// Flowchart layout-cache tests.
#include "gtest_lite.h"
#include "../src/mermaid/layout_cache.h"
#include "../src/mermaid/model.h"
#include "../src/mermaid/parse.h"
#include "../src/mermaid/layout.h"

using namespace mermaid;

static std::shared_ptr<LaidOutFlowchart> Build(const std::string& source) {
    auto out = std::make_shared<LaidOutFlowchart>(
        LayoutFlowchart(ParseFlowchart(source), LayoutParams{}));
    return out;
}

TEST(MermaidCache, KeyUsesSameSourceAndZoom) {
    const std::string src = "flowchart TD\nA --> B\n";
    EXPECT_EQ(HashFenceSource(src, 1.0f), HashFenceSource(src, 1.0f));
    EXPECT_FALSE(HashFenceSource(src, 1.0f) == HashFenceSource(src, 1.5f));
}

TEST(MermaidCache, ReusesExactSourceAndZoom) {
    MermaidLayoutCache cache;
    const std::string src = "flowchart TD\nA --> B\n";
    auto first = Build(src);
    cache.Put(src, 1.0f, first);
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_EQ(cache.Find(src, 1.0f).get(), first.get());
    EXPECT_TRUE(cache.Find(src, 1.5f) == nullptr);
    EXPECT_TRUE(cache.Find("flowchart TD\nA --> C\n", 1.0f) == nullptr);
}

TEST(MermaidCache, ReplacesMatchingKey) {
    MermaidLayoutCache cache;
    const std::string src = "flowchart TD\nA --> B\n";
    auto first = Build(src);
    auto second = Build(src);
    cache.Put(src, 1.0f, first);
    cache.Put(src, 1.0f, second);
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_EQ(cache.Find(src, 1.0f).get(), second.get());
}

TEST(MermaidCache, EvictsOldestAtCapacity) {
    MermaidLayoutCache cache;
    for (size_t i = 0; i < MermaidLayoutCache::kCapacity + 1; ++i) {
        const std::string src = "flowchart TD\nA" + std::to_string(i) + " --> B\n";
        cache.Put(src, 1.0f, Build(src));
    }
    EXPECT_EQ(cache.Size(), MermaidLayoutCache::kCapacity);
    EXPECT_TRUE(cache.Find("flowchart TD\nA0 --> B\n", 1.0f) == nullptr);
    EXPECT_TRUE(cache.Find("flowchart TD\nA128 --> B\n", 1.0f) != nullptr);
}

TEST(MermaidCache, MeasureHeightScalesWithZoom) {
    const auto lo = *Build("flowchart TD\nA --> B\n");
    const float h1 = MeasureLayoutHeight(lo, 1.0f, 1.0e9f);
    const float h2 = MeasureLayoutHeight(lo, 2.0f, 1.0e9f);
    const float body1 = h1 - 2.0f * kMermaidBlockPad;
    const float body2 = h2 - 2.0f * kMermaidBlockPad;
    EXPECT_NEAR(body2, 2.0f * body1, 1e-3f);
}