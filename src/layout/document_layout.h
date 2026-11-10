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

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mermaid {
struct LaidOutFlowchart;
}  // namespace mermaid

namespace layout {

// The intrinsic size of an image, which the image caches learn as
// they decode; unknown sizes fall back to the placeholder height.
struct ImageSize {
    float width = 0.0f;
    float height = 0.0f;
    bool known = false;
};

// One laid out table: the column split, the row heights and the
// cells with their own lines, the data the caret layer needs for
// cell addressing. Column widths share the column equally today, the
// same rule both renderers apply.
struct LaidTableCell {
    std::vector<platform::TextLine> lines;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    uint32_t srcCellStart = 0;
    uint32_t srcCellEnd = 0;
};

struct LaidTable {
    // The equal split, kept for the fall back and the legacy
    // consumers: contentWidth / columnCount.
    float colW = 0.0f;
    size_t columnCount = 0;
    // The per column widths: a column carries its longest
    // unbreakable word instead of slicing it at the cell edge.
    // colWs[c] is the column's outer width, colXs[c] its left edge,
    // with one extra boundary at the end.
    std::vector<float> colWs;
    std::vector<float> colXs;
    std::vector<float> rowHeights;
    std::vector<std::vector<LaidTableCell>> cells;
};

// The column split of a table. minInner[c] is the widest
// unbreakable token of column c, measured by the same engine that
// lays the cells out. Columns share contentWidth equally unless a
// column's longest word needs more, in which case it takes its
// need and the rest keep sharing what is left. When the demands
// alone outweigh the table, the deficit is shared so every column
// degrades evenly instead of one column eating the frame.
std::vector<float> DistributeTableColumns(
    const std::vector<float>& minInner, float contentWidth,
    size_t columnCount, float cellPadX);

// The left edge of column index and the width after it, from a
// table's column records. A table without records falls back to
// the equal split.
float TableColumnX(const LaidTable& t, size_t column);
float TableColumnW(const LaidTable& t, size_t column);

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
    // The marker of a list item: the bullet or the ordered counter,
    // resolved to text once, in shared layout, the way paint must not
    // have to know how to count any more.
    std::string markerText;
    bool task = false;
    // A table, when this block is one.
    std::shared_ptr<LaidTable> table;
    // The images of a text block, their reserved heights stacked above
    // the text, and the total height they charge.
    std::vector<std::string> imageUrls;
    float imagesHeight = 0.0f;
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
    // Answers the intrinsic size of an image by its url; empty means
    // sizes are unknown yet and the layout charges the 100 DIP
    // placeholder, like both renderers do today.
    std::function<ImageSize(const std::string&)> imageSize;
    // The flowchart the calling renderer's painter has measured for a
    // fence, the same layout object the paint path will draw. The walk
    // charges this relaid when the caller answers one, so the reserved
    // room and the painted ink stay one story; an empty answer keeps
    // the provisional layout the parser left on the node.
    std::function<std::shared_ptr<mermaid::LaidOutFlowchart>(const Node&)>
        flowchartRelaid;
};

// The per type layout entry points. Each one receives the walk state
// and advances *y by its own height: the caller owns the gaps between
// blocks, the function owns the inside of the block. A function returns
// a LaidBlock with kind set and the display list fed.
struct LayoutCtx {
    const LayoutInput* in = nullptr;
    display::DisplayList* list = nullptr;
};

LaidBlock LayoutBreak(const LayoutCtx& ctx, const Node& node,
                      uint32_t nodeIndex, float* y);
LaidBlock LayoutHeading(const LayoutCtx& ctx, const Node& node,
                        uint32_t nodeIndex, float* y);
LaidBlock LayoutParagraph(const LayoutCtx& ctx, const Node& node,
                          uint32_t nodeIndex, float* y);
LaidBlock LayoutListBlock(const LayoutCtx& ctx, const Node& node,
                          uint32_t nodeIndex, float* y);
LaidBlock LayoutQuote(const LayoutCtx& ctx, const Node& node,
                      uint32_t nodeIndex, float* y);
LaidBlock LayoutCodeBlock(const LayoutCtx& ctx, const Node& node,
                          uint32_t nodeIndex, float* y);
LaidBlock LayoutTableBlock(const LayoutCtx& ctx, const Node& node,
                          uint32_t nodeIndex, float* y);
LaidBlock LayoutSvgBlock(const LayoutCtx& ctx, const Node& node,
                         uint32_t nodeIndex, float* y);
LaidBlock LayoutMermaidBlock(const LayoutCtx& ctx, const Node& node,
                             uint32_t nodeIndex, float* y);

// The walk: gaps, dispatch to the per type layouts, the block records.
// Today the walk owns the break and heading; the other kinds arrive
// with their modules and get their case here, one per batch step.
DocumentLayout LayoutDocument(const LayoutInput& in);

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_DOCUMENT_LAYOUT_H_
