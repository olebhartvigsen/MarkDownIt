// The shared list layout of plan Batch 3: the indent by depth, the
// marker gutter and the task checkbox, ported from the renderers. The
// marker text, the ordered counter included, is resolved here once so
// paint never has to count again.

#include "layout/document_layout.h"
#include "layout/paragraph_layout.h"
#include "layout/text_runs.h"

#include <string>
#include <vector>

namespace layout {
namespace {

// The number of ordered items before this one in its run: the state
// the renderers kept in their render loops, spelled over the flat
// node list the walk hands down. The run of an item is the stretch of
// list nodes at one depth around it, because the counter resets when
// the kind or the depth changes and empties on any other block, the
// rule the report pins.
int OrderedItemsBefore(const Document& doc, const Node& node,
                       uint32_t nodeIndex) {
    if (nodeIndex >= doc.nodes.size()) return 0;
    if (&doc.nodes[nodeIndex] != &node) return 0;
    int ordered = 0;
    for (size_t j = nodeIndex; j-- > 0;) {
        const Node& prev = doc.nodes[j];
        if (prev.block != BlockKind::List || prev.depth != node.depth) break;
        if (prev.ordered) ++ordered;
    }
    return ordered;
}

// The marker of one item: the ordered counter with its period, the
// depth cycled glyph from the shared theme table, or nothing for a
// task item, whose checkbox is its marker.
std::string MarkerTextOf(const LayoutInput& in, const Node& node,
                        uint32_t nodeIndex) {
    if (node.taskMark >= 0) return std::string();
    if (node.ordered) {
        const int before = in.doc
            ? OrderedItemsBefore(*in.doc, node, nodeIndex)
            : 0;
        return std::to_string(before + 1) + ". ";
    }
    return ListMarkerUtf8(node.depth);
}

// The placeholder of a textless item: one space of the body size, the
// run both layouts keep so the item stays addressable for the caret.
std::vector<platform::GlyphRun> PlaceholderRuns(const LayoutInput& in) {
    Node node;
    InlineBlock child;
    child.kind = InlineKind::Text;
    child.text = U" ";
    node.children.push_back(child);
    return BuildRuns(node, in.metrics->bodyFontSize, false, false,
                      ToColor(in.palette->textPrimary), false, nullptr,
                      in.metrics->codeFontSize, in.palette->link,
                      in.metrics->linkUnderline);
}

}  // namespace

LaidBlock LayoutListBlock(const LayoutCtx& ctx, const Node& node,
                          uint32_t nodeIndex, float* y) {
    const LayoutInput& in = *ctx.in;
    const LayoutMetrics& m = *in.metrics;
    LaidBlock block;
    block.kind = BlockKind::List;
    block.level = node.level;
    block.nodeIndex = nodeIndex;
    block.srcOffset = node.contentOffset;
    block.srcLength = node.contentLength;

    // The item steps in with the nesting depth, and the text wraps in
    // the width left after the marker gutter.
    const float indent = m.listIndent * (node.depth + 1);
    block.x = indent;
    block.width = in.contentWidth - indent;
    const float markerW = m.listIndent * m.markerRatio + m.markerGutter;

    block.markerText = MarkerTextOf(in, node, nodeIndex);
    block.task = node.taskMark >= 0;

    const float size = m.bodyFontSize;
    std::vector<platform::GlyphRun> runs =
        BuildRuns(node, size, false, false, ToColor(in.palette->textPrimary),
                  false, nullptr, m.codeFontSize, in.palette->link,
                  m.linkUnderline);
    bool hasImage = false;
    for (const InlineBlock& child : node.children) {
        if (child.kind == InlineKind::Image && !child.url.empty()) {
            hasImage = true;
        }
    }
    if (runs.empty() && !hasImage) {
        runs = PlaceholderRuns(in);
    }

    float textW = block.width - markerW;
    if (textW <= 0.0f) textW = block.width;
    std::vector<platform::TextLine> lines =
        in.engine->MeasureWrapped(runs, textW, 0.0f);
    ApplyLineSpacing(&lines, size, m.bodyLineHeight, in.zoom, in.engine);

    block.y = *y;
    ChargeForImages(ctx, node, &block, *y);
    const float textTop = *y + block.imagesHeight;

    float textHeight = 0.0f;
    for (const platform::TextLine& line : lines) {
        textHeight += line.height;
    }
    // The renderers draw the marker under the gutter before the
    // images; none of the three can overlap another, the images end
    // above textTop where the gutter begins, so the record keeps the
    // images first, the order the charge emits them.
    const float lineH = lines.empty() ? m.bodyFontSize
                                      : lines.front().height;
    const float lineAscent = lines.empty() ? m.bodyFontSize
                                           : lines.front().ascent;

    if (node.taskMark >= 0) {
        // The checkbox sits where the bullet of a plain item sits: an
        // outlined box in the gutter, centred on the first line. The
        // box is data now, a stroke and two check segments, the shape
        // carrying the state rather than the colour.
        float box = markerW - m.taskBoxInset;
        if (box > m.taskBoxMax) box = m.taskBoxMax;
        if (box < m.taskBoxMin) box = m.taskBoxMin;
        const float bx = block.x + 1.0f;
        const float by = textTop + (lineH - box) * 0.5f;
        const platform::Color ink = ToColor(in.palette->textPrimary);

        display::StrokeRect boxItem;
        boxItem.rect = {bx, by, box, box};
        boxItem.color = ink;
        boxItem.lineWidth = 1.2f;
        ctx.list->items.push_back(boxItem);
        if (node.taskMark > 0) {
            display::StrokeLine first;
            first.x0 = bx + box * 0.22f; first.y0 = by + box * 0.55f;
            first.x1 = bx + box * 0.42f; first.y1 = by + box * 0.78f;
            first.color = ink; first.lineWidth = 1.2f;
            display::StrokeLine second;
            second.x0 = bx + box * 0.42f; second.y0 = by + box * 0.78f;
            second.x1 = bx + box * 0.82f; second.y1 = by + box * 0.25f;
            second.color = ink; second.lineWidth = 1.2f;
            ctx.list->items.push_back(first);
            ctx.list->items.push_back(second);
        }
    }

    if (!block.markerText.empty()) {
        // The marker is clipped to the gutter, the reservation both
        // renderers give it, so a long ordered number cannot run into
        // the item text. Its line borrows the first text line's box;
        // the clip, not a measured width, bounds it.
        display::PushClip clip;
        clip.rect = {block.x, textTop, markerW,
                     textHeight > 0.0f ? textHeight : m.bodyFontSize};
        ctx.list->items.push_back(clip);

        display::Text marker;
        marker.x = block.x;
        marker.top = textTop;
        marker.line.height = lineH;
        marker.line.ascent = lineAscent;
        platform::GlyphRun run;
        run.text = block.markerText;
        run.font = fontSpecific(size, false, false, false, nullptr);
        const platform::Color ink = ToColor(in.palette->textPrimary);
        run.r = ink.r; run.g = ink.g; run.b = ink.b; run.a = ink.a;
        marker.line.runs.push_back(run);
        marker.srcOffset = node.contentOffset;
        marker.srcLength = 0;  // the marker bytes are hidden syntax
        ctx.list->items.push_back(marker);
        ctx.list->items.push_back(display::PopClip{});
    }

    float textY = textTop;
    for (const platform::TextLine& line : lines) {
        display::Text item;
        item.x = block.x + markerW;
        item.top = textY;
        item.line = line;
        item.srcOffset = node.contentOffset;
        item.srcLength = node.contentLength;
        ctx.list->items.push_back(item);
        textY += line.height;
    }
    block.lines = lines;

    block.height = block.imagesHeight + textHeight;
    *y = textY;
    return block;
}

}  // namespace layout
