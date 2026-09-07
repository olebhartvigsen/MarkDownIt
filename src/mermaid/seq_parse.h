// Sequence diagram: parse `sequenceDiagram` sources.
//
// Grammar (mermaid-compatible subset):
//   sequenceDiagram [autonumber] [title <text>]
//   participant|actor <id> [as <display>]
//   <from> <arrow> <to> : <text>   arrows: ->> -->> -> --> ->x --x ->o --o -) --)
//   Note [left of|right of|over] <id>[, <id>] : <text>
//   activate <id>  /  deactivate <id>   (plus +/- arrow suffixes)
//   loop|alt|opt|par|critical|break <text> ... end / and|else|option
//   rect rgb(r,g,b)|rgba(r,g,b,a) ... end
//   %% comments skipped
#ifndef MERMAID_SEQ_PARSE_H
#define MERMAID_SEQ_PARSE_H

#include <string>
#include <vector>

namespace mermaid {

enum class MsgType {
    Solid,          // ->>  arrowhead, dashed=no
    Dotted,         // -->>
    SolidOpen,      // ->    no head
    DottedOpen,     // -->
    SolidCross,     // ->x   cross head
    DottedCross,    // --x
    SolidPoint,     // ->o   filled/lollipop head
    DottedPoint,    // --o
    SolidAsync,     // -)    thin-line async
    DottedAsync,    // --)
    Note,
    ActiveStart,
    ActiveEnd,
    LoopStart, LoopEnd,
    AltStart, AltEnd, AltElse,
    OptStart, OptEnd,
    ParStart, ParEnd, ParAnd,
    CriticalStart, CriticalEnd, CriticalOption,
    BreakStart, BreakEnd,
    RectStart, RectEnd,
};

enum class NotePlacement { Over, LeftOf, RightOf };

enum class ActorShape { Participant, ActorStickman };

bool MsgIsMessage(MsgType t);   // true for the 10 arrow types
bool MsgIsDotted(MsgType t);    // true for dotted variants (dashed lines)
bool MsgOpenHead(MsgType t);    // no arrowhead
bool MsgCrossHead(MsgType t);   // cross head
bool MsgPointHead(MsgType t);   // point head

struct SeqMessage {
    std::string from;       // actor id (source)
    std::string to;         // actor id (target)
    std::string text;       // message text (after ':')
    MsgType type = MsgType::Solid;
    int activation_delta = 0;   // +/- suffixes: -1, 0, +1, +2
    bool cr = false;            // `-` overload (unused, kept for parity)
    // Note fields (type == Note):
    NotePlacement placement = NotePlacement::Over;
    bool note_wrap = false;
};

struct SeqParticipant {
    std::string id;         // identifier used in messages
    std::string display;    // `as` display name or id
    ActorShape shape = ActorShape::Participant;
    bool declared = false;
};

// One loop/alt/opt/par/critical/break/rect item: the parser pairs Start/End
// by struct nesting, so the layout can walk items linearly and keep its own
// stack (matching mermaid's sequenceItems stack).
struct SeqLoop {
    std::string kind;       // loop|alt|opt|par|critical|break|rect
    std::string label;      // text after keyword on the Start line
    std::string fill;       // rect fill (rect rgb/rgba only)
    // section titles: alt `else X`, par `and X`, critical `option X`
    std::vector<std::string> section_titles;
};

struct SequenceDiagram {
    struct Item {
        MsgType type = MsgType::Solid;
        int msg_index = -1;      // for messages / notes
        int loop_index = -1;     // for Loop*/Alt*/Opt*/Par*/Critical*/Break*/Rect*
        int actor_index = -1;    // for activate/deactivate
        int loop_id = -1;        // section markers reference the open loop
    };
    std::vector<SeqParticipant> participants;  // declared then discovered
    std::vector<SeqMessage> messages;
    std::vector<SeqLoop> loops;
    std::vector<Item> items;
    bool autonumber = false;
    std::string title;    // `title X` (mermaid: title goes to diagram title)
    std::string error;
};

SequenceDiagram ParseSequence(std::string_view src);

}  // namespace mermaid

#endif  // MERMAID_SEQ_PARSE_H
