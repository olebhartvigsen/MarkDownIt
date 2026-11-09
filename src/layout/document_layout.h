// The shared block layout of plan Batch 3. Headed for both renderers:
// the walking of the document, the measuring and the placement happen
// here once, behind the text seam, and the renderers paint the list.
//
// Block types arrive one at a time, in the plan's order. Today this
// module lays out the thematic break and headings; every other kind is
// skipped on purpose, still owned by the renderers, until its turn.

#ifndef MARKDOWNIT_SRC_LAYOUT_DOCUMENT_LAYOUT_H_
#define MARKDOWNIT_SRC_LAYOUT_DOCUMENT_LAYOUT_H_

#include "displaylist.h"
#include "dom.h"
#include "platform/seams.h"
#include "theme.h"

#include <vector>

namespace layout {

// One laid out block: what the caret and selection layers need while
// plan task 3.3 still lives in the renderers. The fields mirror the
// mac LaidOutBlock, which has carried this data all along.
struct LaidBlock {
    BlockKind kind = BlockKind::ThematicBreak;
    int level = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    uint32_t nodeIndex = 0;
    // The laid out lines of a text block, the engine's own output with
    // the theme's line heights applied.
    std::vector<platform::TextLine> lines;
    // The source byte range of the drawn text: where the editable
    // content of this block starts, and how long it is.
    uint32_t srcOffset = 0;
    uint32_t srcLength = 0;
};

// The result of one layout pass.
struct DocumentLayout {
    display::DisplayList list;
    std::vector<LaidBlock> blocks;
};

// Everything the layout needs. The metrics come in zoom scaled, like
// the renderer's own ComputeMetrics product; the zoom field is the
// live zoom because font sizes convert from points at layout time.
struct LayoutInput {
    const Document* doc = nullptr;
    const LayoutMetrics* metrics = nullptr;
    const Palette* palette = nullptr;
    // The wrap width of the content column, already zoom scaled.
    float contentWidth = 0.0f;
    float zoom = 1.0f;
    bool editing = false;
    platform::TextEngine* engine = nullptr;
};

// Lays out the block kinds this module owns. See the header comment.
DocumentLayout LayoutDocument(const LayoutInput& in);

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_DOCUMENT_LAYOUT_H_
