#include "gtest_lite.h"
#include "mermaid.h"

using namespace mermaid;

// --- Opgave 4: Detect diagram type and direction ---

TEST(Mermaid, DetectsFlowchart) {
    auto d = Parse("flowchart LR\n  A --> B\n");
    ASSERT_EQ(d.type, DiagramType::Flowchart);
    ASSERT_EQ(d.dir, Direction::LR);
}

TEST(Mermaid, DetectsGraphAlias) {
    auto d = Parse("graph TD\n  A --> B\n");
    ASSERT_EQ(d.type, DiagramType::Flowchart);
    ASSERT_EQ(d.dir, Direction::TD);
}

TEST(Mermaid, DetectsTBAlias) {
    auto d = Parse("graph TB\n  A --> B\n");
    ASSERT_EQ(d.type, DiagramType::Flowchart);
    ASSERT_EQ(d.dir, Direction::TD);
}

TEST(Mermaid, RejectsUnknownType) {
    auto d = Parse("gantt\n  title X\n");
    ASSERT_EQ(d.type, DiagramType::Unknown);
    EXPECT_TRUE(!d.error.empty());
}

TEST(Mermaid, EmptyDiagram) {
    auto d = Parse("");
    ASSERT_EQ(d.type, DiagramType::Unknown);
    EXPECT_TRUE(!d.error.empty());
}

TEST(Mermaid, CommentLineSkipped) {
    auto d = Parse("%% a comment\nflowchart TD\n  A --> B\n");
    ASSERT_EQ(d.type, DiagramType::Flowchart);
}

// --- Opgave 5: Parse flowchart nodes and shapes ---

TEST(Mermaid, NodeShapes) {
    auto d = Parse(
        "graph TD\n"
        "  A[Firkant] --> B(Rund)\n"
        "  B --> C{Valg}\n");
    ASSERT_EQ(d.nodes.size(), 3u);
    EXPECT_EQ(d.nodes[0].label, std::string("Firkant"));
    EXPECT_EQ(d.nodes[0].shape, NodeShape::Rect);
    EXPECT_EQ(d.nodes[1].shape, NodeShape::RoundRect);
    EXPECT_EQ(d.nodes[2].shape, NodeShape::Diamond);
}

TEST(Mermaid, NodeWithoutLabelUsesId) {
    auto d = Parse("graph TD\n  A --> B\n");
    ASSERT_EQ(d.nodes.size(), 2u);
    EXPECT_EQ(d.nodes[0].label, std::string("A"));
    EXPECT_EQ(d.nodes[1].label, std::string("B"));
}

TEST(Mermaid, CircleNode) {
    auto d = Parse("graph TD\n  D((Cirkel))\n");
    ASSERT_EQ(d.nodes.size(), 1u);
    EXPECT_EQ(d.nodes[0].shape, NodeShape::Circle);
    EXPECT_EQ(d.nodes[0].label, std::string("Cirkel"));
}

TEST(Mermaid, QuotedLabel) {
    auto d = Parse("graph TD\n  A[\"Tekst med ]\"]\n");
    ASSERT_EQ(d.nodes.size(), 1u);
    EXPECT_EQ(d.nodes[0].label, std::string("Tekst med ]"));
}

TEST(Mermaid, NodeRepeatedKeepsFirstLabel) {
    auto d = Parse("graph TD\n  A[First] --> B\n  A --> C\n");
    ASSERT_EQ(d.nodes.size(), 3u);
    EXPECT_EQ(d.nodes[0].label, std::string("First"));
}

// --- Opgave 6: Parse flowchart edges ---

TEST(Mermaid, EdgeLabel) {
    auto d = Parse("graph LR\n  A -->|ja| B\n");
    ASSERT_EQ(d.edges.size(), 1u);
    EXPECT_EQ(d.edges[0].label, std::string("ja"));
    EXPECT_EQ(d.edges[0].head, ArrowHead::Arrow);
}

TEST(Mermaid, DottedEdge) {
    auto d = Parse("graph LR\n  A -.-> B\n");
    EXPECT_EQ(d.edges[0].style, EdgeStyle::Dotted);
}

TEST(Mermaid, ThickEdge) {
    auto d = Parse("graph LR\n  A ==> B\n");
    EXPECT_EQ(d.edges[0].style, EdgeStyle::Thick);
}

TEST(Mermaid, ChainedEdges) {
    auto d = Parse("graph LR\n  A --> B --> C\n");
    EXPECT_EQ(d.nodes.size(), 3u);
    EXPECT_EQ(d.edges.size(), 2u);
}

TEST(Mermaid, DottedEdgeWithLabel) {
    auto d = Parse("graph LR\n  A -.->|nej| B\n");
    EXPECT_EQ(d.edges[0].style, EdgeStyle::Dotted);
    EXPECT_EQ(d.edges[0].label, std::string("nej"));
}

// --- Opgave 7: Parse sequenceDiagram ---

TEST(Mermaid, Sequence) {
    auto d = Parse(
        "sequenceDiagram\n"
        "  participant Bruger\n"
        "  participant API\n"
        "  Bruger->>API: Hent data\n"
        "  API-->>Bruger: Svar\n");
    ASSERT_EQ(d.type, DiagramType::Sequence);
    ASSERT_EQ(d.nodes.size(), 2u);
    EXPECT_EQ(d.messages.size(), 2u);
    EXPECT_EQ(d.messages[1].style, EdgeStyle::Dotted);
}

TEST(Mermaid, SequenceImplicitParticipant) {
    auto d = Parse("sequenceDiagram\n  A->>B: hej\n");
    ASSERT_EQ(d.nodes.size(), 2u);
    EXPECT_EQ(d.messages.size(), 1u);
    EXPECT_EQ(d.messages[0].label, std::string("hej"));
}

TEST(Mermaid, SequenceActorAlias) {
    auto d = Parse(
        "sequenceDiagram\n"
        "  actor X\n"
        "  X->>Y: besked\n");
    ASSERT_EQ(d.nodes.size(), 2u);
}

// --- Garbage input does not throw ---

TEST(Mermaid, GarbageDoesNotThrow) {
    auto d = Parse("graph TD\n  ]]][\n  \x01\x02\n");
    // Just reaching this point means no throw.
    // Type may be flowchart, nodes may be empty.
}
