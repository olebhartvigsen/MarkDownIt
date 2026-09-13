// State diagram: parse `stateDiagram-v2` sources (flat subset).
//
// Grammar (mermaid-compatible subset):
//   stateDiagram-v2
//   direction TB|LR|RL|BT
//   state Name                    explicit state declaration
//   state Name { ... }            composite state (parsed but not laid out
//                                 yet; layout returns error for composites)
//   [*] --> Name [: label]        start transition
//   Name --> [*] [: label]        end transition
//   Name --> Name [: label]       transition
//   %% comments skipped
#ifndef MERMAID_STATE_PARSE_H
#define MERMAID_STATE_PARSE_H

#include <string>
#include <vector>

namespace mermaid {

enum class StateKind { Start, End, State, Composite };

struct StateNode {
    std::string id;
    StateKind kind = StateKind::State;
    std::string text;              // rendered label (id for plain states)
    bool has_description = false;  // `state X { desc }` not yet supported
};

struct StateTransition {
    std::string from;              // node id, or "[*]" markers handled as
    std::string to;                // implicit start/end nodes
    std::string label;
};

struct StateDiagram {
    std::vector<StateNode> nodes;      // explicit state declarations
    std::vector<StateTransition> transitions;
    std::string direction = "TB";
    std::string error;
};

// Parses `stateDiagram-v2 ...` source (without code fences). On error, error
// is set and the returned diagram is empty.
StateDiagram ParseStateDiagram(std::string_view src);

}  // namespace mermaid

#endif  // MERMAID_STATE_PARSE_H