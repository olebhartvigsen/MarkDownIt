// The shared layout walk for the first block types of plan Batch 3:
// the thematic break and the heading. The measuring rules are ported
// from the two renderers, which had them in parallel; the numbers
// agree with both, and from now on there is one place to change them.

#include "layout/document_layout.h"

#include "blockspacing.h"
#include "layout/text_runs.h"

#include <algorithm>
#include <string>
#include <vector>

namespace layout {

LaidBlock LayoutBreak(const LayoutCtx& ctx, const Node& node,
                      uint32_t nodeIndex, float* y) {
    const LayoutInput& in = *ctx.in;
    LaidBlock block;
    block.kind = BlockKind::ThematicBreak;
    block.nodeIndex = nodeIndex;
    block.x = 0.0f;
    block.width = in.contentWidth;
    block.srcOffset = node.contentOffset;
    block.srcLength = node.contentLength;

    // The rule is the whole block: GitHub's .25em bar, or the style's
    // own thickness.
    display::FillRect rule;
    rule.rect.x = 0.0f;
    rule.rect.y = *y;
    rule.rect.width = in.contentWidth;
    rule.rect.height = ctx.in->metrics->thematicBreakThickness * in.zoom;
    rule.color = ToColor(ctx.in->palette->rule);
    ctx.list->items.push_back(rule);
    *y += rule.rect.height;

    block.height = rule.rect.height;
    block.y = *y - block.height;
    return block;
}

LaidBlock LayoutHeading(const LayoutCtx& ctx, const Node& node,
                        uint32_t nodeIndex, float* y) {
    const LayoutMetrics& m = *ctx.in->metrics;
    const LayoutInput& in = *ctx.in;
    LaidBlock block;
    block.kind = BlockKind::Heading;
    block.level = node.level;
    block.nodeIndex = nodeIndex;
    block.x = 0.0f;
    block.width = in.contentWidth;
    block.srcOffset = node.contentOffset;
    block.srcLength = node.contentLength;

    const int level =
        node.level < 1 ? 1 : (node.level > 6 ? 6 : node.level);
    const float size = m.headingSizes[level];
    // The smallest heading reads muted, like a caption.
    const ThemeColor headingColor =
        level >= 6 ? ctx.in->palette->quoteText
                   : ctx.in->palette->heading;
    // The style names the face per level: the semibold body face
    // (nullptr), or a family such as the serif the Wikipedia style
    // sets its H1 and H2 in, with its own weight.
    const char* headingFamily = m.headingFamily[level]
                                    ? m.headingFamily[level]
                                    : kSemiboldBodyFont;
    const std::vector<platform::GlyphRun> runs =
        BuildRuns(node, size, m.headingBold[level], false,
                  ToColor(headingColor), false, headingFamily,
                  m.codeFontSize, ctx.in->palette->link, m.linkUnderline);
    in.engine->BeginLayout();
    std::vector<platform::TextLine> lines =
        in.engine->MeasureWrapped(runs, in.contentWidth, 0.0f);
    ApplyLineSpacing(&lines, size, m.headingLineHeight, in.zoom, in.engine);

    float textHeight = 0.0f;
    for (const platform::TextLine& line : lines) {
        display::Text item;
        item.x = 0.0f;
        item.top = *y;
        item.line = line;
        item.srcOffset = node.contentOffset;
        item.srcLength = node.contentLength;
        ctx.list->items.push_back(item);
        *y += line.height;
        textHeight += line.height;
    }
    block.lines = lines;

    // The hairline under an H1 or H2: .3em of padding plus a one point
    // border, inside the block so the gap after the heading stays the
    // CSS margin.
    if (level <= 2 && !lines.empty() && m.headingRuleThickness > 0.0f) {
        const float pad =
            m.headingRulePadRatio * size * kPtToDip * in.zoom;
        display::FillRect rule;
        rule.rect.x = 0.0f;
        rule.rect.y = *y + pad;
        rule.rect.width = in.contentWidth;
        rule.rect.height = m.headingRuleThickness * in.zoom;
        rule.color = ToColor(ctx.in->palette->rule);
        ctx.list->items.push_back(rule);
        *y += pad + rule.rect.height;
        textHeight += pad + rule.rect.height;
    }

    block.height = textHeight;
    block.y = *y - block.height;
    return block;
}

DocumentLayout LayoutDocument(const LayoutInput& in) {
    DocumentLayout out;
    if (!in.doc || !in.metrics || !in.palette || !in.engine) return out;

    const LayoutMetrics& m = *in.metrics;

    // The pass begins here: the engine may drop the measurements of
    // the pass before it, so its retained lines answer hit tests
    // against the layout that is about to be made.
    in.engine->BeginLayout();

    // The document column starts at the top left of the content area;
    // the outer gutter riding the painter's translate.
    float curY = 0.0f;
    bool havePrev = false;
    BlockKind prev = BlockKind::Paragraph;

    for (uint32_t i = 0; i < in.doc->nodes.size(); ++i) {
        const Node& node = in.doc->nodes[i];

        // The block kinds this module owns. Everything else still
        // belongs to the renderers, one block type at a time. A
        // skipped kind is remembered anyway, because the gap of the
        // next owned block is measured from its real predecessor,
        // the same gap the renderers give over the full node list.
        const bool owned =
            node.block == BlockKind::ThematicBreak ||
            node.block == BlockKind::Heading ||
            node.block == BlockKind::Paragraph ||
            node.block == BlockKind::List ||
            node.block == BlockKind::BlockQuote ||
            node.block == BlockKind::CodeBlock ||
            node.block == BlockKind::Table ||
            node.block == BlockKind::MermaidFlowchart ||
            node.block == BlockKind::MermaidPie ||
            node.block == BlockKind::MermaidSequence;
        if (!owned) {
            havePrev = true;
            prev = node.block;
            continue;
        }

        if (havePrev) curY += GapForTransition(prev, node.block, m);
        havePrev = true;
        prev = node.block;

        LayoutCtx ctx;
        ctx.in = &in;
        ctx.list = &out.list;

        LaidBlock block;
        switch (node.block) {
            case BlockKind::ThematicBreak:
                block = LayoutBreak(ctx, node, i, &curY);
                break;
            case BlockKind::Heading:
                block = LayoutHeading(ctx, node, i, &curY);
                break;
            case BlockKind::Paragraph:
                block = LayoutParagraph(ctx, node, i, &curY);
                break;
            case BlockKind::List:
                block = LayoutListBlock(ctx, node, i, &curY);
                break;
            case BlockKind::BlockQuote:
                block = LayoutQuote(ctx, node, i, &curY);
                break;
            case BlockKind::CodeBlock:
                // A standalone svg fence is the picture form; the code
                // card only when the size is unknown or the caller
                // holds the caret inside the fence.
                if (node.lang == "svg") {
                    block = LayoutSvgBlock(ctx, node, i, &curY);
                    break;
                }
                block = LayoutCodeBlock(ctx, node, i, &curY);
                break;
            case BlockKind::MermaidFlowchart:
            case BlockKind::MermaidPie:
            case BlockKind::MermaidSequence:
                block = LayoutMermaidBlock(ctx, node, i, &curY);
                break;
            case BlockKind::Table:
                block = LayoutTableBlock(ctx, node, i, &curY);
                break;
            default:
                break;
        }
        out.blocks.push_back(block);
    }

    out.list.height = curY;
    float widest = in.contentWidth;
    for (const LaidBlock& block : out.blocks) {
        widest = (std::max)(widest, block.x + block.width);
    }
    out.list.width = widest;
    return out;
}

}  // namespace layout
