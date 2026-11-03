#pragma once

#include <algorithm>

#include "dom.h"
#include "theme.h"

// The vertical rhythm between two blocks, shared by both renderers.
//
// GitHub's CSS margin collapse: the gap between two blocks is the
// larger of the previous block's bottom margin and this block's top
// margin. A paragraph, a quote, a table and a fenced block keep 1rem
// below and nothing above; a heading keeps 1.5rem above and 1rem
// below; a thematic break keeps 1.5rem on both sides. List items are
// the one exception: `li + li` adds .25em.
inline float BlockBottomMargin(BlockKind kind, const LayoutMetrics& m) {
    switch (kind) {
        case BlockKind::Heading: return m.headingGapAfter;
        case BlockKind::CodeBlock: return m.codeBlockGap;
        case BlockKind::List: return m.listGap;
        case BlockKind::ThematicBreak: return m.ruleGapBelow;
        default: return m.paraGap;
    }
}

inline float BlockTopMargin(BlockKind kind, const LayoutMetrics& m) {
    switch (kind) {
        case BlockKind::Heading: return m.headingGapBefore;
        case BlockKind::ThematicBreak: return m.ruleGapAbove;
        default: return 0.0f;
    }
}

inline float GapForTransition(BlockKind prev, BlockKind cur,
                              const LayoutMetrics& m) {
    if (cur == BlockKind::List && prev == BlockKind::List) {
        return m.listItemGap;
    }
    return std::max(BlockBottomMargin(prev, m), BlockTopMargin(cur, m));
}
