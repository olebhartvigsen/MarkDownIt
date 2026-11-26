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
#include <array>
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
    // The hit area of each column's filter button, in document coordinates
    // (the header row's whole right end, so the target is bigger than the
    // mark). Empty when the table asks for no filter buttons.
    std::vector<platform::Rect> filterButtons;
    // The centre of each column's funnel mark, the point the six points of
    // the glyph are built around, so a painter that redraws the band (the pin
    // above the scroll) draws the same mark in the same place.
    std::vector<platform::Point> filterCenters;
    // The words of every cell, in document coordinates and clipped to the
    // cell's own box, in one list. The window's overlay painters draw these
    // lines from the same record (selection and find marks sit between them),
    // so the list is not part of the page; it is the copy a page replay reads,
    // because the display list carries the table's washes and hairlines but
    // not its words, and a printed table without them is a grid of empty
    // boxes.
    display::DisplayList cellsText;
    // The header row's height, and the items that draw it (the row's fill,
    // its hairlines, the funnels) in document coordinates. A scrolling view
    // replays this copy at its own top edge, so the band stays while the
    // body goes under it. Zero and empty for a table without a header row.
    float headerHeight = 0.0f;
    // Where the band's own top sits in the walk's coordinates, which is the
    // table block's y; a port that replays the copy at its viewport edge
    // translates by this instead of re-reading the block.
    float headerTop = 0.0f;
    display::DisplayList header;
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

// The runs of one table cell, in the faces, sizes, colours and
// decorations the table's own layout measured it with. A painter that
// builds a text layout for the cell instead of reading the walk record
// asks here, so the words it draws are the words the record measured:
// one builder, two readers.
std::vector<platform::GlyphRun> TableCellRuns(const TableCell& cell,
                                              bool bold,
                                              const platform::Color& color,
                                              const LayoutMetrics& metrics,
                                              bool monospace);

// The runs of a fenced code block, in the face and the inks its own
// layout measures it with: one monospace run of the whole trimmed text,
// or one run per json::Token so a ```json fence keeps its colours. A
// painter that draws the card itself asks here instead of building its
// own paragraph, so the words it draws are the words the walk measured:
// one builder, two readers.
std::vector<platform::GlyphRun> CodeBlockRuns(const std::string& code,
                                              const std::string& lang,
                                              const LayoutMetrics& metrics,
                                              const Palette& palette);

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
    // The gutter width the marker needs: a fixed bullet/single-digit
    // box widened when the ordinal itself ("10.", "11.") does not
    // fit it, so every consumer reserves the same space the marker
    // was actually measured at.
    float markerWidth = 0.0f;
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
// The filter buttons of the document's tables, flattened: one rect per
// column, in document coordinates, in the order the tables and their columns
// appear. Empty when no table asks for buttons.
std::vector<platform::Rect> FilterButtons(const DocumentLayout& laid);

// The column whose filter button holds this document point. False when the
// point is not on one.
bool FilterButtonAt(const std::vector<platform::Rect>& buttons, float docX,
                    float docY, size_t* column);

// The six points of the funnel mark, built around its centre. One geometry
// for the replay item and for a painter that redraws the band above the
// scroll.
std::array<platform::Point, 6> FilterGlyph(float cx, float cy, float mark);

// The fill a pinned header band takes. The band is its own surface, because
// the body scrolls under it, so it has to be opaque even when the style paints
// the header row in the page's own paper and zebras the body instead (GitHub).
// It takes the header's own fill when the style has one and the muted row fill
// otherwise, so a pinned header still reads as a header.
platform::Color PinnedHeaderFill(const LayoutMetrics& metrics,
                                 const Palette& palette, bool dataTable);

// The header a scrolling table pins to the viewport's top edge. A table whose
// header row has scrolled above the viewport, while the table still covers it,
// keeps that row on the edge, the way a spreadsheet does; when the table is
// entirely below or entirely above the viewport, nothing is pinned. viewportTop
// is the viewport's top edge in the same coordinates as the block (document
// ones on the mac, the walk's own on Windows); *top receives that edge.
bool PinnedHeader(float blockTop, float blockHeight, float headerHeight,
                  float viewportTop, float* top);

DocumentLayout LayoutDocument(const LayoutInput& in);

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_DOCUMENT_LAYOUT_H_
