#pragma once

#include "dom.h"
#include "theme.h"

// Where a block's own text starts inside the block's box, shared by
// both renderers.
//
// The walk places the block; this rule places the text within it. Two
// branches cover every kind: an indented list opens its text after the
// marker gutter, and a code card one pad inside its surface, on both
// axes. Everything else starts at the block's left edge, below any
// images the block stacked above its text.
//
// The numbers are the walk's own placement, the same placement the
// replay paints the glyphs at, so the washes and the caret rectangles
// that are built from this rule sit under the drawn text by
// construction. The caller adds its frame origin and the block's x and
// y; this function is arithmetic only, with no platform fact behind
// it.
struct BlockTextInset {
    float x = 0.0f;
    float top = 0.0f;
};

inline BlockTextInset BlockTextInsetFor(BlockKind kind, float markerWidth,
                                        float imagesHeight,
                                        const LayoutMetrics& m) {
    BlockTextInset inset;
    inset.top = imagesHeight;
    if (kind == BlockKind::List) {
        // markerWidth is the measured gutter, widened past the fixed
        // formula for a wide ordinal like "10."; a block with no
        // marker (an empty placeholder row) still falls back to the
        // fixed reservation.
        inset.x = markerWidth > 0.0f
                      ? markerWidth
                      : m.listIndent * m.markerRatio + m.markerGutter;
    } else if (kind == BlockKind::CodeBlock) {
        inset.x = m.codePad;
        inset.top = m.codePad;
    }
    return inset;
}
