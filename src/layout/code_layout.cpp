// The shared layout of a fenced code block, plan Batch 3.2 step 5.
// Ported from the two renderers, which held the same rules twice: the
// raw text loses its trailing CR/LF run, every kept character goes out
// as ONE monospace run (breaks and wrap live in the engine), the theme
// adds the code pad on every side and the rounded card behind it all.

#include "layout/document_layout.h"
#include "layout/text_runs.h"
#include "json.h"

#include <string>
#include <vector>

namespace layout {

std::vector<platform::GlyphRun> CodeBlockRuns(const std::string& code,
                                              const std::string& lang,
                                              const LayoutMetrics& m,
                                              const Palette& pal) {
    // One run of the full trimmed text, normally: the line breaks stay
    // inside it and the engine owns both the breaks at them and the
    // wrap at the card's inner width. A ```json fence splits that one
    // run into several runs instead, one per json::Token, each
    // carrying its own ink colour; every byte is still covered and
    // still in order, so this changes nothing about wrapping or line
    // breaking, only which colour rides each run.
    std::vector<platform::GlyphRun> runs;
    if (code.empty()) return runs;
    const platform::Color ink = ToColor(pal.textPrimary);
    if (lang == "json") {
        const platform::Color keyColor = ToColor(pal.jsonKey);
        const platform::Color stringColor = ToColor(pal.jsonString);
        const platform::Color numberColor = ToColor(pal.jsonNumber);
        const platform::Color literalColor = ToColor(pal.jsonLiteral);
        for (const json::Token& tok : json::Tokenize(code)) {
            platform::GlyphRun run;
            run.text = code.substr(tok.offset, tok.length);
            run.font = fontSpecific(m.codeFontSize, false, false, true,
                                    kCodeFont);
            platform::Color c = ink;
            switch (tok.kind) {
                case json::TokenKind::Key: c = keyColor; break;
                case json::TokenKind::String: c = stringColor; break;
                case json::TokenKind::Number: c = numberColor; break;
                case json::TokenKind::Literal: c = literalColor; break;
                case json::TokenKind::Punct: c = ink; break;
            }
            run.r = c.r;
            run.g = c.g;
            run.b = c.b;
            run.a = c.a;
            runs.push_back(std::move(run));
        }
    } else {
        platform::GlyphRun run;
        run.text = code;
        run.font = fontSpecific(m.codeFontSize, false, false, true,
                                kCodeFont);
        run.r = ink.r;
        run.g = ink.g;
        run.b = ink.b;
        run.a = ink.a;
        runs.push_back(std::move(run));
    }
    return runs;
}

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

    // One run of the full trimmed text, normally: the line breaks stay
    // inside it and the engine owns both the breaks at them and the
    // wrap at the card's inner width. A ```json fence splits that one
    // run into several runs instead, one per json::Token, each
    // carrying its own ink colour; every byte is still covered and
    // still in order, so this changes nothing about wrapping or line
    // breaking, only which colour rides each run.
    const std::vector<platform::GlyphRun> runs =
        CodeBlockRuns(code, node.lang, m, *in.palette);

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
