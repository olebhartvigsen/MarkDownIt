#pragma once

// What an appearance switch means for the code that draws the window.
//
// The platform notices that the OS changed; the shells then ask this
// header what follows. Keeping the answer here means Windows and macOS
// cannot drift on the question, and a test can answer it without a
// window.
//
// A switch repaints and lays the same document out again. The palette
// decides colours, never geometry: no block moves, no line rewraps and no
// text is reparsed, so the caret and the scroll position stay where they
// were. The relayout is not optional, because the display list bakes the
// palette's colours into its items at layout time; replaying the previous
// list paints the previous appearance. That is still the "presentation
// layer" rule from references/night-mode-reference.md: the document is
// read again, not changed.

#include "thememode.h"

namespace theme {

struct SwitchPlan {
    // The window must be repainted with the new palette.
    bool repaint = true;
    // The document must be laid out again, because the display list
    // carries the palette's colours. Geometry is identical, so the caret
    // does not move.
    bool relayout = false;
    // The buffer must be parsed again. False for the same reason.
    bool reparse = false;
    // The switch touches the document's saved state. False: the
    // appearance is not part of the document, so it never sets the dirty
    // flag and never adds an undo entry.
    bool touchesDocument = false;
};

inline SwitchPlan PlanForSwitch(Appearance from, Appearance to) {
    SwitchPlan plan;
    plan.repaint = (from != to);
    plan.relayout = (from != to);
    return plan;
}

// Whether the chrome palette has to be rebuilt for this appearance.
// It always does when the appearance differs: the pane, the strip and
// the separators all have a dark counterpart.
inline bool ChromeNeedsRebuild(Appearance from, Appearance to) {
    return from != to;
}

}  // namespace theme
