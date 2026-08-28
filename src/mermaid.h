#pragma once

// Mermaid subset parser. Produces a backend-independent graph model.
// No Windows dependencies: this header is portable C++17 and unit tested.

#include <string>
#include <vector>

namespace mermaid {

enum class DiagramType { Unknown, Flowchart, Sequence };
enum class Direction   { TD, LR, RL, BT };
enum class NodeShape   { Rect, RoundRect, Stadium, Diamond, Circle };
enum class EdgeStyle   { Solid, Dotted, Thick };
enum class ArrowHead   { None, Arrow, Cross, Circle };

struct GraphNode {
    std::string id;
    std::string label;
    NodeShape   shape = NodeShape::Rect;
};

struct GraphEdge {
    int       from = -1;      // index into nodes
    int       to   = -1;
    std::string label;
    EdgeStyle style = EdgeStyle::Solid;
    ArrowHead head  = ArrowHead::Arrow;
};

// One message in a sequenceDiagram.
struct SeqMessage {
    int       from = -1;      // index into nodes (participants)
    int       to   = -1;
    std::string label;
    EdgeStyle style = EdgeStyle::Solid;
};

struct Diagram {
    DiagramType             type = DiagramType::Unknown;
    Direction               dir  = Direction::TD;
    std::vector<GraphNode>  nodes;
    std::vector<GraphEdge>  edges;
    std::vector<SeqMessage> messages;
    std::string             error;   // non-empty means parse failed
};

// Parse mermaid source. On failure, result.type is Unknown and
// result.error explains why, so the caller can fall back to code rendering.
Diagram Parse(const std::string& src);

}  // namespace mermaid
