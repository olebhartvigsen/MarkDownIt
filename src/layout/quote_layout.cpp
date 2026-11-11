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
    // An information box (a style with calloutBoxes) is a tinted
    // rounded surface instead: the text sits inside its padding.
    const CalloutMatch callout =
        m.calloutBoxes ? DetectCallout(node) : CalloutMatch{};
    const float depthIndent = m.quoteIndent * node.depth;
    const float indent = callout.found
                             ? depthIndent + m.calloutPadX
                             : m.quoteIndent * (node.depth + 1);
    block.x = indent;
    block.width = in.contentWidth - indent -
                  (callout.found ? m.calloutPadX : 0.0f);

    const float size = m.bodyFontSize;
    const platform::Color ink = ToColor(in.palette->quoteText);
    std::vector<platform::GlyphRun> runs =
        BuildRuns(node, size, false, false,
                  callout.found ? ToColor(in.palette->textPrimary) : ink,
                  false, nullptr, m.codeFontSize, in.palette->link,
                  m.linkUnderline);
    if (callout.found && !runs.empty() &&
        runs[0].text.size() >= callout.labelLength) {
        // The label word: bold, in the kind's own colour, split off
        // the first run so the text itself is unchanged.
        const platform::Color labelInk = ToColor(
            in.palette->calloutLabel[static_cast<int>(callout.kind)]);
        platform::GlyphRun label = runs[0];
        label.text = runs[0].text.substr(0, callout.labelLength);
        label.font.bold = true;
        label.r = labelInk.r; label.g = labelInk.g;
        label.b = labelInk.b; label.a = labelInk.a;
        runs[0].text.erase(0, callout.labelLength);
        if (runs[0].text.empty()) runs.erase(runs.begin());
        runs.insert(runs.begin(), label);
    }
    std::vector<platform::TextLine> lines =
        in.engine->MeasureWrapped(runs, block.width, 0.0f);
    ApplyLineSpacing(&lines, size, m.bodyLineHeight, in.zoom, in.engine);

    // The box pads the text on every side. The padding is charged to
    // the gap around the block, not to the block, so the block's own
    // geometry stays the text's and every hit test and wash keeps
    // working from it.
    if (callout.found) *y += m.calloutPadY;
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
    if (callout.found) {
        display::FillRoundedRect box;
        box.rect.x = depthIndent;
        box.rect.y = textTop - m.calloutPadY;
        box.rect.width = in.contentWidth - depthIndent;
        box.rect.height = textHeight + 2.0f * m.calloutPadY;
        box.radius = m.calloutRadius;
        box.color = ToColor(
            in.palette->calloutBg[static_cast<int>(callout.kind)]);
        ctx.list->items.push_back(box);
    } else {
        const float barX = block.x - m.quoteIndent + 4.0f;
        display::FillRect bar;
        bar.rect.x = barX - m.quoteBarWidth * 0.5f;
        bar.rect.y = textTop;
        bar.rect.width = m.quoteBarWidth;
        bar.rect.height = textHeight;
        bar.color = ToColor(in.palette->quoteBar);
        ctx.list->items.push_back(bar);
    }

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
    *y = textY + (callout.found ? m.calloutPadY : 0.0f);
    return block;
}

}  // namespace layout
