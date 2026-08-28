#include "gtest_lite.h"
#include "diagramcache.h"

using namespace mermaid;

// --- Opgave 14: Diagram cache ---

TEST(DiagramCache, CacheHit) {
    DiagramCache c;
    auto* a = c.Get(0, U"graph TD\n  A --> B\n", 800.0f);
    auto* b = c.Get(0, U"graph TD\n  A --> B\n", 800.0f);
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    ASSERT_TRUE(a == b);  // same pointer means it was cached
}

TEST(DiagramCache, DifferentSourceMisses) {
    DiagramCache c;
    auto* a = c.Get(0, U"graph TD\n  A --> B\n", 800.0f);
    auto* b = c.Get(0, U"graph TD\n  A --> C\n", 800.0f);
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    ASSERT_TRUE(a != b);  // different content, different layout
}

TEST(DiagramCache, DifferentWidthMisses) {
    DiagramCache c;
    auto* a = c.Get(0, U"graph TD\n  A --> B\n", 800.0f);
    auto* b = c.Get(0, U"graph TD\n  A --> B\n", 600.0f);
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    ASSERT_TRUE(a != b);  // different width, different layout
}

TEST(DiagramCache, DifferentOffsetMisses) {
    DiagramCache c;
    auto* a = c.Get(0, U"graph TD\n  A --> B\n", 800.0f);
    auto* b = c.Get(100, U"graph TD\n  A --> B\n", 800.0f);
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    ASSERT_TRUE(a != b);
}

TEST(DiagramCache, InvalidDiagramReturnsNull) {
    DiagramCache c;
    auto* r = c.Get(0, U"gantt\n  title X\n", 800.0f);
    ASSERT_TRUE(r == nullptr);
}

TEST(DiagramCache, EmptyDiagramReturnsNull) {
    DiagramCache c;
    auto* r = c.Get(0, U"graph TD\n", 800.0f);
    // A graph with no nodes/edges should return nullptr
    ASSERT_TRUE(r == nullptr);
}

TEST(DiagramCache, ClearEmpties) {
    DiagramCache c;
    c.Get(0, U"graph TD\n  A --> B\n", 800.0f);
    ASSERT_EQ(c.Size(), 1u);
    c.Clear();
    ASSERT_EQ(c.Size(), 0u);
}

TEST(DiagramCache, CacheDoesNotGrowOnRepeat) {
    DiagramCache c;
    for (int i = 0; i < 10; i++) {
        c.Get(0, U"graph TD\n  A --> B\n", 800.0f);
    }
    ASSERT_EQ(c.Size(), 1u);  // same key, one entry
}
