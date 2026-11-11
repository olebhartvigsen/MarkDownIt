// The shared blockquote layout of plan Batch 3: the indent chain, the
// muted body text and the bar, the rules both renderers spell today.
// The text goes through the paragraph machinery at the quote colour,
// and the bar geometry is recorded so the painters no longer derive
// it.

#include "layout/document_layout.h"
#include "layout/paragraph_layout.h"
#include "layout/text_runs.h"

#include <vector>

namespace layout {

LaidBlock LayoutQuote(const LayoutCtx& ctx, const Node& node,
                      uint32_t nodeIndex, float* y) {
    const LayoutInput& in = *ctx.in;
    const LayoutMetrics& m = *in.metrics;
    LaidBlock block;
    block.kind = BlockKind::BlockQuote;
    block.level = node.level;
    block.nodeIndex = nodeIndex;
    block.srcOffset = node.contentOffset;
    block.srcLength = node.contentLength;

    // A quote steps its content inward with the nesting depth, and
    // the text narrows with it, the way both renderers measure them.
    const float indent = m.quoteIndent * (node.depth + 1);
    block.x = indent;
    block.width = in.contentWidth - indent;

    const float size = m.bodyFontSize;
    std::vector<platform::GlyphRun> runs =
        BuildRuns(node, size, false, false, ToColor(in.palette->quoteText),
                  false, nullptr, m.codeFontSize, in.palette->link,
                  m.linkUnderline);
    std::vector<platform::TextLine> lines =
        in.engine->MeasureWrapped(runs, block.width, 0.0f);
    ApplyLineSpacing(&lines, size, m.bodyLineHeight, in.zoom, in.engine);

    block.y = *y;
    const float imageHeight = ChargeForImages(ctx, node, &block, *y);
    const float textTop = *y + block.imagesHeight;
    (void)imageHeight;

    float textHeight = 0.0f;
    for (const platform::TextLine& line : lines) {
        textHeight += line.height;
    }

    // The renderers paint the bar before the images, but the two never
    // overlap: the bar ends left of the text edge and the images start
    // at it, so the record keeps the images first, the order the
    // charge emits them, and a replay is the same picture.
    const float barX = block.x - m.quoteIndent + 4.0f;
    display::FillRect bar;
    bar.rect.x = barX - m.quoteBarWidth * 0.5f;
    bar.rect.y = textTop;
    bar.rect.width = m.quoteBarWidth;
    bar.rect.height = textHeight;
    bar.color = ToColor(in.palette->quoteBar);
    ctx.list->items.push_back(bar);

    float textY = textTop;
    for (const platform::TextLine& line : lines) {
        display::Text item;
        item.x = block.x;
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
