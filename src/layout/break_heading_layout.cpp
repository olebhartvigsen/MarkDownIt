// The shared layout walk for the first block types of plan Batch 3:
// the thematic break and the heading. The measuring rules are ported
// from the two renderers, which had them in parallel; the numbers
// agree with both, and from now on there is one place to change them.

#include "layout/document_layout.h"

#include "blockspacing.h"

#include <algorithm>
#include <string>
#include <vector>

namespace layout {
namespace {

// The same factor both renderers apply to convert the theme's point
// sizes to device independent pixels.
constexpr float kPtToDip = 96.0f / 72.0f;

// The theme names fonts by role; both engines resolve these families,
// the way the theme macros spell them for each platform.
constexpr const char* kBodyFont = "Segoe UI";
constexpr const char* kSemiboldBodyFont = "Segoe UI Semibold";
constexpr const char* kCodeFont = "Cascadia Mono";

platform::Color ToColor(const ThemeColor& c) {
    platform::Color out;
    out.r = c.r;
    out.g = c.g;
    out.b = c.b;
    out.a = c.a;
    return out;
}

std::string Utf32ToUtf8(const std::u32string& text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        if (c < 0x80) {
            out.push_back((char)c);
        } else if (c < 0x800) {
            out.push_back((char)(0xC0 | (c >> 6)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back((char)(0xE0 | (c >> 12)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (c >> 18)));
            out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// The font of one run, the shared spelling of what both renderers do:
// the theme's point size becomes device pixels at the live zoom.
platform::FontSpec FontFor(float baseSize, bool bold, bool italic,
                           bool monospace, const char* family) {
    platform::FontSpec font;
    font.family = family ? family : kBodyFont;
    font.size = baseSize * kPtToDip;
    font.bold = bold;
    font.italic = italic;
    font.monospace = monospace;
    return font;
}

// The CSS line box: the line is lineHeightEm times the font size, the
// glyphs centred in it with half leading, the ascent kept from the
// engine. Ported from the renderers; the one place line heights are
// decided from now on.
void ApplyLineSpacing(std::vector<platform::TextLine>* lines,
                      float ptSize, float lineHeightEm, float zoom,
                      platform::TextEngine* engine) {
    const float em = ptSize * kPtToDip * zoom;
    float natural = em;
    float fontAscent = em;
    if (engine && !lines->empty() && !lines->front().runs.empty()) {
        const float measured =
            engine->LineHeight(lines->front().runs.front().font);
        if (measured > 0.0f) natural = measured;
        if (lines->front().ascent > 0.0f) fontAscent =
            lines->front().ascent;
    }
    const float height = lineHeightEm * em;
    const float ascent = (height - natural) * 0.5f + fontAscent;
    for (platform::TextLine& line : *lines) {
        line.height = height;
        line.ascent = ascent;
    }
}

// One run per inline span, ported from the renderers: a link is a
// colour change and an underline, an inline code span takes the code
// face at the code size, a strong span takes the semibold face.
std::vector<platform::GlyphRun> BuildRuns(const Node& node,
                                          float baseSize, bool bold,
                                          bool italic,
                                          const platform::Color& color,
                                          bool monospace,
                                          const char* family,
                                          float codeFontSize,
                                          const ThemeColor& linkColor) {
    std::vector<platform::GlyphRun> runs;
    for (const InlineBlock& child : node.children) {
        const std::string runText = Utf32ToUtf8(child.text);
        if (runText.empty()) continue;

        platform::GlyphRun run;
        run.text = runText;
        const bool childCode = child.code;
        const float runSize = childCode ? codeFontSize : baseSize;
        const char* runFamily = family;
        bool runBold = bold;
        if (child.strong && runFamily == nullptr) {
            runFamily = kSemiboldBodyFont;
            runBold = false;
        }
        run.font = FontFor(runSize, runBold, italic || child.em,
                           monospace || childCode, runFamily);
        const bool link = child.kind == InlineKind::Link;
        const platform::Color runColor =
            link ? ToColor(linkColor) : color;
        run.r = runColor.r;
        run.g = runColor.g;
        run.b = runColor.b;
        run.a = runColor.a;
        run.underline = link;
        run.strike = child.strike;
        runs.push_back(run);
    }
    return runs;
}

}  // namespace

DocumentLayout LayoutDocument(const LayoutInput& in) {
    DocumentLayout out;
    if (!in.doc || !in.metrics || !in.palette || !in.engine) return out;

    const LayoutMetrics& m = *in.metrics;
    const Palette& pal = *in.palette;

    // The document column starts at the top left of the content area;
    // the outer gutter riding the painter's translate.
    float curY = 0.0f;
    bool havePrev = false;
    BlockKind prev = BlockKind::Paragraph;

    for (uint32_t i = 0; i < in.doc->nodes.size(); ++i) {
        const Node& node = in.doc->nodes[i];

        // The block kinds this module owns. Everything else still
        // belongs to the renderers, one block type at a time.
        if (node.block != BlockKind::ThematicBreak &&
            node.block != BlockKind::Heading) {
            continue;
        }

        if (havePrev) curY += GapForTransition(prev, node.block, m);
        havePrev = true;
        prev = node.block;

        LaidBlock block;
        block.kind = node.block;
        block.nodeIndex = i;
        block.x = 0.0f;
        block.width = in.contentWidth;
        block.srcOffset = node.contentOffset;
        block.srcLength = node.contentLength;

        if (node.block == BlockKind::ThematicBreak) {
            // GitHub's .25em bar: the rule is the whole block.
            display::FillRect rule;
            rule.rect.x = 0.0f;
            rule.rect.y = curY;
            rule.rect.width = in.contentWidth;
            rule.rect.height = 4.0f * in.zoom;
            rule.color = ToColor(pal.rule);
            out.list.items.push_back(rule);
            block.height = rule.rect.height;
        } else {
            const int level =
                node.level < 1 ? 1 : (node.level > 6 ? 6 : node.level);
            const float size = m.headingSizes[level];
            // The smallest heading reads muted, like a caption.
            const ThemeColor headingColor =
                level >= 6 ? pal.quoteText : pal.heading;
            const std::vector<platform::GlyphRun> runs =
                BuildRuns(node, size, false, false,
                          ToColor(headingColor), false,
                          kSemiboldBodyFont, m.codeFontSize, pal.link);
            in.engine->BeginLayout();
            std::vector<platform::TextLine> lines =
                in.engine->MeasureWrapped(runs, in.contentWidth, 0.0f);
            ApplyLineSpacing(&lines, size, m.headingLineHeight, in.zoom,
                             in.engine);

            float textHeight = 0.0f;
            for (const platform::TextLine& line : lines) {
                display::Text item;
                item.x = 0.0f;
                item.top = curY;
                item.line = line;
                item.srcOffset = node.contentOffset;
                item.srcLength = node.contentLength;
                out.list.items.push_back(item);
                curY += line.height;
                textHeight += line.height;
            }
            block.lines = lines;

            // The hairline under an H1 or H2: .3em of padding plus a
            // one point border, inside the block so the gap after the
            // heading stays the CSS margin.
            if (level <= 2 && !lines.empty()) {
                const float pad = m.headingRulePadRatio * size *
                                  kPtToDip * in.zoom;
                display::FillRect rule;
                rule.rect.x = 0.0f;
                rule.rect.y = curY + 0.0f + pad;
                rule.rect.width = in.contentWidth;
                rule.rect.height = m.headingRuleThickness * in.zoom;
                rule.color = ToColor(pal.rule);
                out.list.items.push_back(rule);
                curY += pad + rule.rect.height;
                textHeight += pad + rule.rect.height;
            }
            block.height = textHeight;
        }

        block.y = curY - block.height;
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
