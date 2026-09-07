// Sequence diagram layout: parity with sequenceRenderer (mermaid.js).
//
// Constants from mermaid's DEFAULT sequence config (dumped via seq_config.mjs);
// text metrics = the oracle shim model (width = UTF-16 units * 4,
// height = 12 per line) — identical to the pie legend sizing convention.
//
// The layout reproduces mermaid's algorithm transcript-style (verified
// line-by-line against the dist chunk and the 5 goldens):
//   getMaxMessageWidthPerActor → calculateActorMargins →
//   addActorRenderingData (each actor inserted into bounds at vP=0; bump 65)
//   → per-statement walk (boundMessage / drawNote / activations / loops)
//   → mirrorActors footer → canvas.
// The bounds bookkeeping: `insert()` merges into data min/max and inflates
// EVERY open sequenceItem by n*boxMargin (innermost n=1). The vertical
// account: vP_after_message = lineStartY (= starty + 34 for a normal
// message; self messages end at starty + 64 = lineStartY + 30). Activations
// start at vP (drawn y), deactivate clamps min-height 18→(vP-6, +12).
// Loop header: bump(10) → starty → bump(15 + max(12,20)); sections: bump(15)
// → divider y → bump(10 + max(12,20)). Footer: bump(20) → bottom actor y;
// bump(12 + 10); height = boxH + 2·MY − 10 + 1 (mirrorActors).
#ifndef MERMAID_SEQ_LAYOUT_H
#define MERMAID_SEQ_LAYOUT_H

#include "seq_parse.h"

#include <string>
#include <vector>

namespace mermaid {

// One actor box: positions at draw time; lifeline runs center.x from
// y=65 (top box bottom) to y=stopy (footer box top).
struct SeqActorBox {
    std::string name;         // display name (rect label)
    std::string id;           // actor id (mermaid actor.name; golden key)
    double x = 0, w = 0;      // box left edge / width (top), height 65 / 12
    double lifeline_x = 0;    // center x
    double stopy = 0;         // footer box y (mirrorActors)
    ActorShape shape = ActorShape::Participant;
};

struct SeqMessageGeo {
    bool self = false;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // line endpoints (y1==y2)
    // self path: M x,y1 C x+60,y1-10 x+60,y1+30 x,y1+20 (renderer derives)
    double tx = 0, ty = 0;    // message text position (centered)
    double starty = 0;
    MsgType type = MsgType::Solid;
    std::string text;
    int autonumber_n = 0;     // 0 when autonumber off
};

struct SeqNoteGeo {
    double x = 0, y = 0, w = 0, h = 0;
    double tx = 0, ty = 0;
    std::string text;
};

struct SeqActivationGeo {
    double x = 0, y = 0, w = 0, h = 0;
};

struct SeqLoopGeo {
    std::string kind;         // loop|alt|opt|par|critical|break|rect
    double startx = 0, starty = 0, stopx = 0, stopy = 0;
    std::string label;        // section keyword drawn in labelBox (loop/alt)
    std::string title;        // first branch title (unwrapped), may be empty
    std::vector<double> section_y;            // divider y's (else/and/option)
    std::vector<std::string> section_titles;  // divider titles (raw)
};

struct SeqBackgroundGeo {
    double x = 0, y = 0, w = 0, h = 0;
    std::string fill;
};

struct SeqNumberGeo {
    int n = 0;
    double x = 0, y = 0;      // text placed at (startx, lineStartY + 4)
};

struct LaidOutSequence {
    double width = 0, height = 0;   // svg width/height (title does NOT add)
    double startx = 0, starty = 0;  // viewBox origin (title shifts starty -40)
    double vbheight = 0;            // height + 40 when titled, else height
    std::vector<SeqActorBox> actors;
    std::vector<SeqMessageGeo> messages;
    std::vector<SeqNoteGeo> notes;
    std::vector<SeqActivationGeo> activations;
    std::vector<SeqLoopGeo> loops;
    std::vector<SeqBackgroundGeo> backgrounds;
    std::vector<SeqNumberGeo> numbers;
    bool autonumber = false;
    std::string title;
    double title_x = 0;   // drawn at y=-25 when title set
    std::string error;
};

// Text metrics matching the oracle shim (width = UTF-16 units * 4, height
// 12/line). Shared by tests and the renderer.
double SeqTextWidth(const std::string& utf8);
double SeqTextHeight(const std::string& utf8);

LaidOutSequence LayoutSequence(const SequenceDiagram& seq);

}  // namespace mermaid

#endif  // MERMAID_SEQ_LAYOUT_H
