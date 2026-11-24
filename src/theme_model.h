#pragma once

// What an appearance switch means for the code that draws the window.
//
// The platform notices that the OS changed; the shells then ask this
// header what follows. Keeping the answer here means Windows and macOS
// cannot drift on the question, and a test can answer it without a
// window.
//
// A switch is a repaint and nothing else. The palette decides colours,
// never geometry: no block moves, no line rewraps, no text is reparsed,
// and the caret and scroll position stay where they were. That is the
// "presentation layer" rule from references/night-mode-reference.md,
// written down where the shells read it.

#include "thememode.h"

namespace theme {

struct SwitchPlan {
    // The window must be repainted with the new palette.
    bool repaint = true;
    // The document must be laid out again. False: colours do not move
    // glyphs, so a relayout would only cost time and risk a caret jump.
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
    return plan;
}

// Whether the chrome palette has to be rebuilt for this appearance.
// It always does when the appearance differs: the pane, the strip and
// the separators all have a dark counterpart.
inline bool ChromeNeedsRebuild(Appearance from, Appearance to) {
    return from != to;
}

}  // namespace theme
