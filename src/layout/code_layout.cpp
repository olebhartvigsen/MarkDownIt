// The shared layout of a fenced code block, plan Batch 3.2 step 5.
// Ported from the two renderers, which held the same rules twice: the
// raw text loses its trailing CR/LF run, every kept character goes out
// as ONE monospace run (breaks and wrap live in the engine), the theme
// adds the code pad on every side and the rounded card behind it all.

#include "layout/document_layout.h"
#include "layout/text_runs.h"

#include <string>
#include <vector>

namespace layout {

LaidBlock LayoutCodeBlock(const LayoutCtx& ctx, const Node& node,
                          uint32_t nodeIndex, float* y) {
    const LayoutInput& in = *ctx.in;
    const LayoutMetrics& m = *in.metrics;
    LaidBlock block;
    block.kind = BlockKind::CodeBlock;
    block.nodeIndex = nodeIndex;
    block.x = 0.0f;
    block.width = in.contentWidth;
    block.srcOffset = node.contentOffset;

    // The trailing CR/LF run is trimmed, the trim both renderers apply
    // before measuring, on the UTF-32 raw itself.
    std::u32string raw = node.raw;
    while (!raw.empty() && (raw.back() == U'\n' || raw.back() == U'\r')) {
        raw.pop_back();
    }
    const std::string code = Utf32ToUtf8(raw);
    // The drawn bytes decide the block's source length, so the trailing
    // trim shows up in the caret range instead of node.contentLength.
    block.srcLength = static_cast<uint32_t>(code.size());

    const float wrap = in.contentWidth - 2.0f * m.codePad;
    if (wrap <= 0.0f) {
        // No inner width at all: the Windows measure pass guards the
        // same way and charges nothing for the block.
        block.y = *y;
        return block;
    }

    // One run of the full trimmed text. The line breaks stay inside it
    // and the engine owns both the breaks at them and the wrap at the
    // card's inner width; a run per source line is not what either
    // renderer does.
    std::vector<platform::GlyphRun> runs;
    if (!code.empty()) {
        platform::GlyphRun run;
        run.text = code;
        run.font = fontSpecific(m.codeFontSize, false, false, true,
                                kCodeFont);
        const platform::Color ink = ToColor(in.palette->textPrimary);
        run.r = ink.r;
        run.g = ink.g;
        run.b = ink.b;
        run.a = ink.a;
        runs.push_back(std::move(run));
    }

    std::vector<platform::TextLine> lines =
        in.engine->MeasureWrapped(runs, wrap, 0.0f);
    ApplyLineSpacing(&lines, m.codeFontSize, m.codeLineHeight, in.zoom,
                     in.engine);

    float blockH = 2.0f * m.codePad;
    for (const platform::TextLine& line : lines) blockH += line.height;

    // The card, first in paint order: the theme's muted surface over
    // the full column width, the corner radius the theme names.
    // A style with a card border paints it first, as a slightly larger
    // card in the border colour, so no port needs a stroke primitive.
    if (m.codeBorderWidth > 0.0f) {
        display::FillRoundedRect edge;
        edge.rect.x = 0.0f;
        edge.rect.y = *y;
        edge.rect.width = in.contentWidth;
        edge.rect.height = blockH;
        edge.radius = m.codeRadius;
        edge.color = ToColor(in.palette->codeBorder);
        ctx.list->items.push_back(edge);
    }
    const float edgeW = m.codeBorderWidth;
    display::FillRoundedRect card;
    card.rect.x = edgeW;
    card.rect.y = *y + edgeW;
    card.rect.width = in.contentWidth - 2.0f * edgeW;
    card.rect.height = blockH - 2.0f * edgeW;
    card.radius = m.codeRadius > edgeW ? m.codeRadius - edgeW : 0.0f;
    card.color = ToColor(in.palette->codeBg);
    ctx.list->items.push_back(card);

    // One code pad in from the card's edge, the lines stacked by the
    // line-box heights. The line's source bytes walk contiguously from
    // the content offset, the code mapping's own rule.
    float lineY = *y + m.codePad;
    uint32_t src = node.contentOffset;
    for (const platform::TextLine& line : lines) {
        display::Text item;
        item.x = m.codePad;
        item.top = lineY;
        item.line = line;
        item.srcOffset = src;
        for (const platform::GlyphRun& run : line.runs) {
            item.srcLength += static_cast<uint32_t>(run.text.size());
        }
        src += item.srcLength;
        ctx.list->items.push_back(item);
        lineY += line.height;
    }

    block.lines = std::move(lines);
    block.height = blockH;
    *y += blockH;
    block.y = *y - blockH;
    return block;
}

}  // namespace layout
