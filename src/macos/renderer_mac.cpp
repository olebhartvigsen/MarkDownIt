// The macOS renderer.
//
// The Windows renderer draws with Direct2D. This one draws with Core
// Graphics through the RenderTarget seam, and measures with Core Text
// through the TextEngine seam.
//
// The layout is the shared one: the theme metrics, the DOM structure and
// the mermaid engine are all reused, so the two platforms lay the same
// document out the same way. Only the drawing calls differ.

#include "renderer_mac.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "mermaid/layout_cache.h"
#include "mermaid/model.h"
#include "mermaid/pie_layout.h"
#include "mermaid/seq_layout.h"
#include "mermaid_painter_mac.h"
#include "svg_mac.h"

namespace mac {
namespace {

// A colour from the palette.
Color ToColor(const ThemeColor& c) {
    Color out;
    out.r = c.r;
    out.g = c.g;
    out.b = c.b;
    out.a = c.a;
    return out;
}

// The DOM holds UTF-32, the encoder is UTF-8, so each code point
// becomes one to four bytes.
std::string Utf32ToUtf8(const std::u32string& text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// The text of a block's inline children, in UTF-8.
std::string InlineTextUtf8(const Node& node) {
    std::string text;
    for (const InlineBlock& child : node.children) {
        text += Utf32ToUtf8(child.text);
    }
    return text;
}

// A node that holds one run of text, so the table code can reuse the run
// builder without building a whole DOM node.
Node TextNode(const std::u32string& text) {
    Node node;
    InlineBlock child;
    child.kind = InlineKind::Text;
    child.text = text;
    node.children.push_back(child);
    return node;
}

// The height an image takes when its size is not known yet.
//
// The Windows renderer charges the same height, so the two agree on how
// much room an image takes before it has been measured.
constexpr float kImageFallbackHeight = 100.0f;

// The largest height the images of one block may take.
constexpr float kImageMaxHeight = 400.0f;

// The width of the text column when the word wrap is off. The text is
// laid out on one very wide line and the user scrolls to its end.
constexpr float kNoWrapContentWidthDip = 10000.0f;

// The theme sizes are points, and the Windows renderer converts them to
// DIPs with this factor (kPtToDip there) when it creates its text
// formats. The mac renderer applies the same factor, so a document lays
// out at the same em on both platforms and the line breaks land in the
// same places.
constexpr float kPtToDip = 96.0f / 72.0f;

// The baseline offset inside a body line, as a multiple of the font
// size. The Windows body format sets the same value (SetLineSpacing
// with 1.24).
constexpr float kBodyBaselineOffset = 1.24f;

// The gap before a block, from the block that came before it: the rules
// and the numbers of the Windows renderer (Renderer::GapForTransition),
// so the two platforms leave the same space between the same pair of
// blocks.
float GapForTransition(BlockKind prev, BlockKind cur, BlockKind next,
                       int curDepth, int prevDepth, const LayoutMetrics& m) {
    (void)next;
    (void)curDepth;
    (void)prevDepth;
    if (cur == BlockKind::Heading) {
        if (prev == BlockKind::Heading) return m.headingGapAfter;
        return m.headingGapBefore;
    }
    if (prev == BlockKind::Heading) return m.headingGapAfter;
    if (cur == BlockKind::List && prev == BlockKind::List) {
        return m.listItemGap;
    }
    if (cur == BlockKind::List && prev != BlockKind::List) return m.listGap;
    if (prev == BlockKind::List && cur != BlockKind::List) return m.listGap;
    if (cur == BlockKind::CodeBlock || prev == BlockKind::CodeBlock) {
        return m.codeBlockGap;
    }
    return m.paraGap;
}

// A font, in the layout unit.
FontSpec MakeFont(const LayoutMetrics& metrics, float size, bool bold,
                  bool italic, bool monospace) {
    FontSpec font;
    font.family = monospace ? metrics.codeFont : metrics.bodyFont;
    font.size = size;
    font.bold = bold;
    font.italic = italic;
    font.monospace = monospace;
    return font;
}

// The mermaid layout of a block, in the form the painter takes.
mermaid::MermaidRender MermaidRenderFor(const Node& node) {
    mermaid::MermaidRender diagram;
    switch (node.block) {
        case BlockKind::MermaidPie:
            diagram.kind = mermaid::MermaidKind::Pie;
            diagram.pie = node.mermaid_pie;
            break;
        case BlockKind::MermaidSequence:
            diagram.kind = mermaid::MermaidKind::Sequence;
            diagram.seq = node.mermaid_seq;
            break;
        case BlockKind::MermaidFlowchart:
        default:
            diagram.kind = mermaid::MermaidKind::Flowchart;
            diagram.flow = node.mermaid_layout;
            break;
    }
    return diagram;
}

// True when the diagram has a layout to draw.
bool HasMermaidLayout(const mermaid::MermaidRender& diagram) {
    switch (diagram.kind) {
        case mermaid::MermaidKind::Pie: return diagram.pie != nullptr;
        case mermaid::MermaidKind::Sequence: return diagram.seq != nullptr;
        case mermaid::MermaidKind::Flowchart:
        default: return diagram.flow != nullptr;
    }
}

// The width a mermaid block is measured against.
//
// The Windows build measures a flowchart against the width inside the
// block padding, and a pie and a sequence against the full column. The
// painters fit the diagram into the padded width, so the measurement
// follows the same split.
float MermaidMeasureWidth(const mermaid::MermaidRender& diagram,
                          float width) {
    const float pad = mermaid::kMermaidBlockPad;
    if (diagram.kind != mermaid::MermaidKind::Flowchart) return width;
    return width > 2.0f * pad ? width - 2.0f * pad : 0.0f;
}

}  // namespace

MacRenderer::MacRenderer() {
    // The theme, until the caller sets its own: the same numbers the
    // Windows renderer starts from.
    baseMetrics_ = BaseMetrics();
    metrics_ = ComputeMetrics();
}
MacRenderer::~MacRenderer() {
    // The engine of the diagrams belongs to the renderer.
    delete diagramEngine_;
    diagramEngine_ = nullptr;
}

FontSpec MacRenderer::FontFor(const Node& node, float baseSize, bool bold,
                               bool italic, bool monospace) const {
    (void)node;
    return MakeFont(metrics_, baseSize * kPtToDip * zoom_, bold, italic,
                    monospace);
}

FontSpec MacRenderer::DiagramFontFor(const Node& node, float baseSize,
                                      bool bold, bool italic,
                                      bool monospace) const {
    // The mermaid painters scale their text by the zoom themselves, the
    // same way the Windows measure format stays unzoomed.
    (void)node;
    return MakeFont(metrics_, baseSize * kPtToDip, bold, italic, monospace);
}

void MacRenderer::ApplyLineSpacing(std::vector<TextLine>* lines, float ptSize,
                                    float heightMult,
                                    float baselineMult) const {
    const float em = ptSize * kPtToDip * zoom_;
    for (TextLine& line : *lines) {
        line.height = heightMult * em;
        line.ascent = baselineMult * em;
    }
}

std::vector<GlyphRun> MacRenderer::BuildCellRuns(const TableCell& cell,
                                                  bool bold,
                                                  const Color& color) const {
    // The cell text, cut into runs at the boundaries of the inline
    // spans. The spans index the text in UTF-16 units, so the walk
    // counts them as it goes.
    std::vector<GlyphRun> runs;
    const std::u32string& text = cell.text;
    const size_t count = text.size();

    auto styleAt = [&](uint32_t index, bool* strong, bool* em, bool* code,
                       bool* strike) {
        *strong = bold;
        *em = false;
        *code = false;
        *strike = false;
        for (const CellInlineSpan& span : cell.inlineSpans) {
            if (index < span.u16Start || index >= span.u16End) continue;
            *strong = *strong || span.bold;
            *em = *em || span.italic;
            *code = *code || span.code;
            *strike = *strike || span.strike;
        }
    };
    auto isLink = [&](uint32_t index) {
        if (index >= cell.u16ToSrc.size()) return false;
        const uint32_t source = cell.u16ToSrc[index];
        for (const TableLink& link : cell.links) {
            if (source >= link.srcOffset &&
                source < link.srcOffset + link.srcLength) {
                return true;
            }
        }
        return false;
    };

    uint32_t u16 = 0;
    size_t i = 0;
    while (i < count) {
        bool strong = false, em = false, code = false, strike = false;
        styleAt(u16, &strong, &em, &code, &strike);
        const bool link = isLink(u16);

        // The run takes every code point with the same style.
        std::string run;
        while (i < count) {
            bool next = false, nextEm = false, nextCode = false;
            bool nextStrike = false;
            styleAt(u16, &next, &nextEm, &nextCode, &nextStrike);
            if (next != strong || nextEm != em || nextCode != code ||
                nextStrike != strike || isLink(u16) != link) {
                break;
            }
            run += Utf32ToUtf8(std::u32string(1, text[i]));
            u16 += text[i] > 0xFFFF ? 2u : 1u;
            ++i;
        }
        if (run.empty()) continue;

        GlyphRun glyph;
        glyph.text = std::move(run);
        // The windows renderer gives a cell code span the code family
        // at the body size, and the header its weight.
        glyph.font = FontFor(Node{}, metrics_.bodyFontSize, strong, em, code);
        glyph.r = color.r;
        glyph.g = color.g;
        glyph.b = color.b;
        glyph.a = color.a;
        glyph.underline = link;
        glyph.strike = strike;
        runs.push_back(std::move(glyph));
    }
    return runs;
}

std::vector<GlyphRun> MacRenderer::BuildRuns(const Node& node, float baseSize,
                                              bool bold, bool italic,
                                              const Color& color,
                                              bool monospace) const {
    std::vector<GlyphRun> runs;

    // The children are walked one by one, so a run carries the style of
    // the span it came from. A link is a colour change and an
    // underline, an inline code span switches to the smaller code face,
    // and everything else is a font change.
    for (const InlineBlock& child : node.children) {
        const std::string runText = Utf32ToUtf8(child.text);
        if (runText.empty()) continue;

        GlyphRun run;
        run.text = runText;
        // An inline code span uses the code size, the way the Windows
        // renderer sets codeFontSize on the span.
        const bool childCode = child.code;
        const float runSize = childCode ? metrics_.codeFontSize : baseSize;
        run.font = FontFor(node, runSize, bold || child.strong,
                           italic || child.em, monospace || childCode);
        // A link is drawn in the link colour, everything else in the text
        // colour of the block.
        const bool link = child.kind == InlineKind::Link;
        const Color runColor = link ? ToColor(palette_.link) : color;
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

float MacRenderer::Measure(const Document& doc, float widthDip) {
    blocks_.clear();
    needsLayout_ = false;

    // The engine drops the measurements of the pass before this one, so
    // the hit tester works against the layout that is about to be made.
    if (engine_) {
        engine_->BeginLayout();
    }

    if (widthDip <= 0.0f) {
        contentWidth_ = 1.0f;
        originX_ = metrics_.padX;
        totalHeight_ = metrics_.padTop;
        return totalHeight_;
    }

    // The content column: the view width less the theme padding, capped
    // at the reading width, and centered when it is narrower than the
    // space. The same arithmetic the Windows renderer uses, so a
    // document sits the same way on both platforms. With the wrap off
    // the text gets one very wide line, and the user scrolls to its
    // end.
    const float available = widthDip - metrics_.padX * 2.0f;
    contentWidth_ = wrap_ ? std::min(available, metrics_.maxContentWidth)
                          : kNoWrapContentWidthDip;
    if (contentWidth_ <= 0.0f) {
        contentWidth_ = 1.0f;
    }
    originX_ = wrap_ ? (widthDip - contentWidth_) * 0.5f : metrics_.padX;

    float y = metrics_.padTop;
    BlockKind prevBlock = BlockKind::Paragraph;
    int prevDepth = -1;
    bool first = true;

    for (const Node& node : doc.nodes) {
        // The gap belongs to the block that follows it, and the first
        // block gets none, the same model the Windows renderer uses.
        if (!first) {
            y += GapForTransition(prevBlock, node.block, BlockKind::Paragraph,
                                  node.depth, prevDepth, metrics_);
        }
        first = false;
        LayoutBlock(node, originX_, contentWidth_, &y);
        prevBlock = node.block;
        prevDepth = node.depth;
    }

    totalHeight_ = y + metrics_.padTop * 2.0f;
    return totalHeight_;
}

void MacRenderer::SetContentWidthMode(int mode) {
    if (mode < 0 || mode > 3) {
        mode = 0;
    }
    contentWidthMode_ = mode;
    metrics_ = ComputeMetrics();
}

LayoutMetrics MacRenderer::ComputeMetrics() const {
    LayoutMetrics m = ScaleMetrics(baseMetrics_, zoom_);
    // Override the content width cap based on the user's width setting.
    // Mode 3 (Full) means no cap, so we use a very large value. The
    // same numbers the Windows renderer resolves, zoom included.
    switch (contentWidthMode_) {
        case 1: m.maxContentWidth = 960.0f * zoom_; break;
        case 2: m.maxContentWidth = 1600.0f * zoom_; break;
        case 3: m.maxContentWidth = 100000.0f; break;
        case 0:
        default: m.maxContentWidth = 800.0f * zoom_; break;
    }
    return m;
}

const SvgDoc* MacRenderer::GetSvgDoc(const Node& node) {
    // The document is read once, and it is kept until the document
    // changes.
    for (const auto& entry : svgDocs_) {
        if (entry.first == node.srcOffset) return &entry.second;
    }

    SvgDoc doc;
    if (!doc.Load(InlineTextUtf8(node))) return nullptr;
    svgDocs_.emplace_back(node.srcOffset, std::move(doc));
    return &svgDocs_.back().second;
}

float MacRenderer::ChargeForImages(LaidOutBlock* block) {
    // The images are drawn above the text of the block, so the block
    // has to reserve the room for them, the same height the Windows
    // layout charges.
    //
    // A remote image is fetched in the background. Until it has arrived
    // the block gets the fallback height, and the document is laid out
    // again when the image shows up.
    float imageHeight = 0.0f;
    for (const InlineBlock& child : block->node->children) {
        if (child.kind != InlineKind::Image || child.url.empty()) continue;

        images_.Prefetch(child.url);

        float height = kImageFallbackHeight;
        if (imageHeight + height > kImageMaxHeight) {
            height = kImageMaxHeight;
        }
        imageHeight += height + metrics_.paraGap;
        block->images.push_back(child.url);

        if (!images_.GetSize(child.url, nullptr, nullptr)) {
            needsLayout_ = true;
        }
    }
    block->imagesHeight = imageHeight;
    return imageHeight;
}

void MacRenderer::LayoutBlock(const Node& node, float x, float width,
                              float* y) {
    LaidOutBlock block;
    block.node = &node;
    block.kind = node.block;
    block.level = node.level;

    // A quote and a list step their content inward, and the text
    // narrows with it, the same way the Windows renderer measures them.
    float drawX = x;
    float drawW = width;
    if (node.block == BlockKind::BlockQuote) {
        const float indent = metrics_.quoteIndent * (node.depth + 1);
        drawX += indent;
        drawW -= indent;
    } else if (node.block == BlockKind::List) {
        const float indent = metrics_.listIndent * (node.depth + 1);
        drawX += indent;
        drawW -= indent;
    }
    block.x = drawX;
    block.width = drawW;

    switch (node.block) {
        case BlockKind::Heading: {
            const int level =
                node.level < 1 ? 1 : (node.level > 6 ? 6 : node.level);
            const float size = metrics_.headingSizes[level];
            const std::vector<GlyphRun> runs = BuildRuns(
                node, size, true, false, ToColor(palette_.heading), false);
            block.lines = engine_->MeasureWrapped(runs, drawW, 0.0f);
            ApplyLineSpacing(&block.lines, size, metrics_.headingLineHeight,
                             1.0f);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            // The rule under an H1 and an H2 lives inside the block, so
            // the next block keeps its distance from the rule.
            if (level <= 2 && !block.lines.empty()) {
                block.height += metrics_.ruleGapAbove + metrics_.ruleGapBelow;
            }
            block.height += ChargeForImages(&block);
            break;
        }

        case BlockKind::Paragraph: {
            const float size = metrics_.bodyFontSize;
            std::vector<GlyphRun> runs = BuildRuns(
                node, size, false, false, ToColor(palette_.textPrimary),
                false);
            if (runs.empty() && node.virtualEmptyParagraph) {
                // A blank paragraph keeps a line, so the caret has a
                // place to sit: the same placeholder line the Windows
                // layout keeps for it.
                runs = BuildRuns(TextNode(U" "), size, false, false,
                                 ToColor(palette_.textPrimary), false);
            }
            block.lines = engine_->MeasureWrapped(runs, drawW, 0.0f);
            ApplyLineSpacing(&block.lines, size, metrics_.bodyLineHeight,
                             kBodyBaselineOffset);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            block.height += ChargeForImages(&block);
            break;
        }

        case BlockKind::CodeBlock: {
            // A standalone SVG file is a picture, not a code specimen,
            // so it is measured as one.
            if (node.lang == "svg" && standaloneSvg_) {
                const SvgDoc* doc = GetSvgDoc(node);
                if (doc && doc->Width() > 0.0f && doc->Height() > 0.0f) {
                    const float available = drawW - metrics_.codePad * 2.0f;
                    const float scale = available / doc->Width();
                    block.height = doc->Height() * scale;
                    break;
                }
            }

            // The raw text, with the trailing newlines stripped the way
            // the Windows measure pass strips them.
            std::string code = InlineTextUtf8(node);
            while (!code.empty() &&
                   (code.back() == '\n' || code.back() == '\r')) {
                code.pop_back();
            }

            // One run, with the line breaks kept in the text: Core Text
            // breaks a line there, so a blank line in the source stays a
            // blank line on screen.
            std::vector<GlyphRun> runs;
            if (!code.empty()) {
                GlyphRun run;
                run.text = code;
                run.font = FontFor(node, metrics_.codeFontSize, false, false,
                                   true);
                const Color color = ToColor(palette_.textPrimary);
                run.r = color.r;
                run.g = color.g;
                run.b = color.b;
                run.a = color.a;
                runs.push_back(std::move(run));
            }
            block.lines = engine_->MeasureWrapped(
                runs, drawW - metrics_.codePad * 2.0f, 0.0f);
            ApplyLineSpacing(&block.lines, metrics_.codeFontSize,
                             metrics_.codeLineHeight, 1.0f);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            block.height += metrics_.codePad * 2.0f;
            break;
        }

        case BlockKind::BlockQuote: {
            const float size = metrics_.bodyFontSize;
            const std::vector<GlyphRun> runs = BuildRuns(
                node, size, false, false, ToColor(palette_.quoteText), false);
            block.lines = engine_->MeasureWrapped(runs, drawW, 0.0f);
            ApplyLineSpacing(&block.lines, size, metrics_.bodyLineHeight,
                             kBodyBaselineOffset);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            block.height += ChargeForImages(&block);
            break;
        }

        case BlockKind::ThematicBreak: {
            // The band the rule sits in the middle of, the same 12
            // points the Windows layout reserves.
            block.height = 12.0f;
            break;
        }

        case BlockKind::List: {
            const float size = metrics_.bodyFontSize;
            std::vector<GlyphRun> runs = BuildRuns(
                node, size, false, false, ToColor(palette_.textPrimary),
                false);
            bool hasImage = false;
            for (const InlineBlock& child : node.children) {
                if (child.kind == InlineKind::Image && !child.url.empty()) {
                    hasImage = true;
                }
            }
            if (runs.empty() && !hasImage) {
                // The marker keeps the item addressable even without
                // text, the way the Windows layout keeps its line.
                runs = BuildRuns(TextNode(U" "), size, false, false,
                                 ToColor(palette_.textPrimary), false);
            }
            // The text wraps in the space that is left after the marker
            // gutter: the same reservation the Windows layout makes.
            const float markerW =
                metrics_.listIndent * 0.6f + metrics_.markerGutter;
            block.lines = engine_->MeasureWrapped(runs, drawW - markerW,
                                                  0.0f);
            ApplyLineSpacing(&block.lines, size, metrics_.bodyLineHeight,
                             kBodyBaselineOffset);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            block.height += ChargeForImages(&block);
            break;
        }

        case BlockKind::Table: {
            block.table = &node;
            size_t columnCount = 0;
            for (const TableRow& row : node.rows) {
                columnCount = std::max(columnCount, row.cells.size());
            }
            if (columnCount == 0) {
                break;
            }

            // The columns are equal, the width the Windows renderer
            // gives them (colW = width / cols).
            block.tableLayout.colW = drawW / static_cast<float>(columnCount);
            const float innerW =
                block.tableLayout.colW - 2.0f * metrics_.cellPadX;

            float total = 0.0f;
            for (const TableRow& row : node.rows) {
                std::vector<LaidOutBlock::TableCellLayout> cells;
                float tallest = 0.0f;
                for (size_t c = 0; c < row.cells.size() && c < columnCount;
                     ++c) {
                    const TableCell& cell = row.cells[c];
                    LaidOutBlock::TableCellLayout laid;
                    if (c < node.aligns.size()) {
                        laid.align = node.aligns[c];
                    }
                    const std::vector<GlyphRun> runs = BuildCellRuns(
                        cell, cell.isHeader, ToColor(palette_.textPrimary));
                    laid.lines = engine_->MeasureWrapped(
                        runs, innerW > 0.0f ? innerW : 1.0f, 0.0f);
                    ApplyLineSpacing(&laid.lines, metrics_.bodyFontSize,
                                     metrics_.bodyLineHeight,
                                     kBodyBaselineOffset);
                    float height = 0.0f;
                    for (const TextLine& line : laid.lines) {
                        height += line.height;
                    }
                    if (height > tallest) tallest = height;
                    cells.push_back(std::move(laid));
                }
                // A row with no text at all still has the Windows
                // minimum, and every row carries the cell padding.
                if (tallest == 0.0f) tallest = 20.0f;
                const float rowH = tallest + 2.0f * metrics_.cellPadY;
                block.tableLayout.rowHeights.push_back(rowH);
                block.tableLayout.rows.push_back(std::move(cells));
                total += rowH;
            }
            block.height = total;
            break;
        }

        case BlockKind::MermaidFlowchart:
        case BlockKind::MermaidPie:
        case BlockKind::MermaidSequence: {
            // The mermaid layout is shared and already computed by the
            // parser. The height is the one the Windows build reserves,
            // so the two platforms lay the document out the same way.
            const mermaid::MermaidRender diagram = MermaidRenderFor(node);
            float diagramHeight = 0.0f;
            if (HasMermaidLayout(diagram)) {
                diagramHeight = mermaid::MeasureMermaidHeight(
                    diagram, zoom_, MermaidMeasureWidth(diagram, drawW));
            }
            block.height = diagramHeight > 0.0f ? diagramHeight : 100.0f;
            break;
        }
    }

    block.y = *y;
    *y += block.height;
    blocks_.push_back(std::move(block));
}

void MacRenderer::SetMarkers(const std::vector<TextMatch>* markers) {
    markers_.clear();
    if (!markers) return;
    markers_ = *markers;
}

void MacRenderer::SetSearchMatches(const std::vector<TextMatch>* matches,
                                  int currentIndex) {
    searchMatches_.clear();
    searchCurrent_ = -1;
    if (!matches) {
        return;
    }
    searchMatches_ = *matches;
    searchCurrent_ = currentIndex;
}

void MacRenderer::Render(const Document& doc, float scrollOffset) {
    (void)doc;
    if (!target_) return;

    // The marks go under the text, so they are painted first.
    PaintMarkerHighlights(scrollOffset);
    PaintSearchHighlights(scrollOffset);

    // The ordered list markers count across a run of items, and the
    // count restarts when the list ends or the depth changes, the same
    // way the Windows renderer counts them.
    int listCounter = 0;
    BlockKind prevKind = BlockKind::Paragraph;
    int prevDepth = -1;
    for (const LaidOutBlock& block : blocks_) {
        std::string marker;
        const int depth = block.node ? block.node->depth : 0;
        if (block.kind == BlockKind::List) {
            if (prevKind != BlockKind::List || depth != prevDepth) {
                listCounter = 0;
            }
            if (block.node && block.node->ordered) {
                ++listCounter;
                marker = std::to_string(listCounter) + ". ";
            } else {
                marker = "\xE2\x80\xA2 ";
            }
        } else {
            listCounter = 0;
        }
        PaintBlock(block, scrollOffset, marker);
        prevKind = block.kind;
        prevDepth = depth;
    }
}

void MacRenderer::PaintMarkerHighlights(float scrollOffset) {
    if (!target_ || markers_.empty()) return;

    for (const TextMatch& marker : markers_) {
        for (const Rect& rect : RectsForRange(marker.start,
                                              marker.start + marker.length)) {
            Rect drawn = rect;
            drawn.y -= scrollOffset;
            target_->FillRect(drawn, ToColor(palette_.markerBg));

            // The line along the bottom, so a mark never relies on
            // colour alone.
            Rect line;
            line.x = drawn.x;
            line.y = drawn.y + drawn.height - 1.0f;
            line.width = drawn.width;
            line.height = 1.0f;
            target_->FillRect(line, ToColor(palette_.markerLine));
        }
    }
}

void MacRenderer::PaintSearchHighlights(float scrollOffset) {
    if (!target_ || searchMatches_.empty()) return;

    for (size_t i = 0; i < searchMatches_.size(); ++i) {
        const TextMatch& match = searchMatches_[i];
        const bool current = static_cast<int>(i) == searchCurrent_;
        for (const Rect& rect : RectsForRange(match.start,
                                              match.start + match.length)) {
            Rect drawn = rect;
            drawn.y -= scrollOffset;
            target_->FillRect(drawn, current ? ToColor(palette_.findCurrentMatchBg)
                                              : ToColor(palette_.findMatchBg));
        }
    }
}

void MacRenderer::PaintBlock(const LaidOutBlock& block, float scrollOffset,
                              const std::string& marker) {
    if (!target_) return;
    const float top = block.y - scrollOffset;

    switch (block.kind) {
        case BlockKind::ThematicBreak: {
            // The rule sits in the middle of the band the layout
            // reserves for it, the same place the Windows renderer puts
            // it.
            Rect rect;
            rect.x = block.x;
            rect.y = top + 6.0f;
            rect.width = block.width;
            rect.height = 1.0f;
            target_->FillRect(rect, ToColor(palette_.rule));
            break;
        }

        case BlockKind::CodeBlock: {
            // A standalone SVG file is drawn as a picture, without the
            // card behind it.
            if (block.node->lang == "svg" && standaloneSvg_) {
                const SvgDoc* doc = GetSvgDoc(*block.node);
                if (doc && doc->Width() > 0.0f && doc->Height() > 0.0f) {
                    const float available =
                        block.width - metrics_.codePad * 2.0f;
                    const float scale = available / doc->Width();
                    doc->Draw(target_, block.x + metrics_.codePad, top,
                              available, doc->Height() * scale);
                    break;
                }
            }

            // The background and the border, then the code itself. The
            // border follows the same rounded outline as the fill, the
            // way the Windows renderer strokes it.
            Rect bg;
            bg.x = block.x;
            bg.y = top;
            bg.width = block.width;
            bg.height = block.height;
            target_->FillRoundedRect(bg, metrics_.codeRadius,
                                     ToColor(palette_.codeBg));
            target_->StrokeRoundedRect(bg, metrics_.codeRadius,
                                       ToColor(palette_.codeBorder), 1.0f);

            PaintLines(block.lines, block.x + metrics_.codePad,
                       top + metrics_.codePad);
            break;
        }

        case BlockKind::BlockQuote: {
            // The bar steps inward with the nesting depth, and it is as
            // tall as the text. The geometry is the Windows one: the
            // bar is centered four points left of the text edge.
            float textHeight = 0.0f;
            for (const TextLine& line : block.lines) {
                textHeight += line.height;
            }
            const float barX = block.x - metrics_.quoteIndent + 4.0f;
            Rect bar;
            bar.x = barX - metrics_.quoteBarWidth * 0.5f;
            bar.y = top + block.imagesHeight;
            bar.width = metrics_.quoteBarWidth;
            bar.height = textHeight;
            target_->FillRect(bar, ToColor(palette_.quoteBar));

            PaintImages(block, top, block.width);
            PaintInlineCodeBg(block, scrollOffset);
            PaintLines(block.lines, block.x, top + block.imagesHeight);
            break;
        }

        case BlockKind::List: {
            // The text sits after the marker gutter, and the marker sits
            // in the gutter, clipped to it the way the Windows renderer
            // clips its marker layout.
            const float markerW =
                metrics_.listIndent * 0.6f + metrics_.markerGutter;
            float textHeight = 0.0f;
            for (const TextLine& line : block.lines) {
                textHeight += line.height;
            }
            const float textTop = top + block.imagesHeight;
            if (!marker.empty()) {
                const FontSpec markerFont = FontFor(
                    *block.node, metrics_.bodyFontSize, false, false, false);
                Rect clip;
                clip.x = block.x;
                clip.y = textTop;
                clip.width = markerW;
                clip.height = textHeight > 0.0f ? textHeight
                                                : metrics_.bodyFontSize;
                const float baseline =
                    textTop + (block.lines.empty()
                                   ? metrics_.bodyFontSize
                                   : block.lines.front().ascent);
                target_->PushClip(clip);
                target_->DrawText(marker, markerFont, block.x, baseline,
                                  ToColor(palette_.textPrimary));
                target_->PopClip();
            }
            PaintImages(block, top, block.width);
            PaintInlineCodeBg(block, scrollOffset);
            PaintLines(block.lines, block.x + markerW, textTop);
            break;
        }

        case BlockKind::Table: {
            PaintTable(block, scrollOffset);
            break;
        }

        case BlockKind::MermaidFlowchart:
        case BlockKind::MermaidPie:
        case BlockKind::MermaidSequence: {
            // The diagram is painted by the mermaid painter, which draws
            // the shared layout through the seam. The painter follows
            // the zoom itself, so it gets the unzoomed font.
            const mermaid::MermaidRender diagram =
                MermaidRenderFor(*block.node);
            if (!HasMermaidLayout(diagram)) break;
            if (diagramEngine_) diagramEngine_->BeginLayout();
            const float pad = mermaid::kMermaidBlockPad;
            const float availWidth =
                block.width > 2.0f * pad ? block.width - 2.0f * pad : 0.0f;
            PaintMermaidDiagram(diagramEngine_ ? diagramEngine_ : engine_,
                                target_, diagram, zoom_, availWidth,
                                block.x, top, pad, palette_,
                                DiagramFontFor(*block.node,
                                               metrics_.bodyFontSize, false,
                                               false, false));
            break;
        }

        case BlockKind::Heading:
        case BlockKind::Paragraph: {
            const float textTop = top + block.imagesHeight;
            PaintImages(block, top, block.width);
            PaintInlineCodeBg(block, scrollOffset);
            PaintLines(block.lines, block.x, textTop);

            // The rule under an H1 and an H2, inside the room the layout
            // reserved: under the text, the gap of the rule above it.
            if (block.kind == BlockKind::Heading && block.level <= 2) {
                float textHeight = 0.0f;
                for (const TextLine& line : block.lines) {
                    textHeight += line.height;
                }
                const float ruleY =
                    textTop + textHeight + metrics_.ruleGapAbove;
                target_->StrokeLine(block.x, ruleY, block.x + block.width,
                                    ruleY, ToColor(palette_.rule), 1.0f);
            }
            break;
        }
    }
}

void MacRenderer::PaintLines(const std::vector<TextLine>& lines, float x,
                              float top) const {
    float y = top;
    for (const TextLine& line : lines) {
        PaintLineRuns(line, x, y);
        y += line.height;
    }
}

void MacRenderer::PaintLineRuns(const TextLine& line, float x,
                                 float y) const {
    if (!target_) return;

    for (size_t i = 0; i < line.runs.size(); ++i) {
        const GlyphRun& run = line.runs[i];
        Color color;
        color.r = run.r;
        color.g = run.g;
        color.b = run.b;
        color.a = run.a;
        target_->DrawText(run.text, run.font, x + run.x,
                          y + line.ascent, color);

        // The underline of a link and the strike-through of a struck
        // span. The seam has no draw call for the two decorations, so
        // they are stroked where DirectWrite puts them.
        if (!run.underline && !run.strike) continue;
        float runWidth = line.width - run.x;
        if (i + 1 < line.runs.size()) {
            runWidth = line.runs[i + 1].x - run.x;
        }
        if (runWidth <= 0.0f) continue;
        if (run.underline) {
            target_->StrokeLine(x + run.x, y + line.ascent + 2.0f,
                                x + run.x + runWidth,
                                y + line.ascent + 2.0f, color, 1.0f);
        }
        if (run.strike) {
            const float strikeY = y + line.ascent - 0.30f * run.font.size;
            target_->StrokeLine(x + run.x, strikeY, x + run.x + runWidth,
                                strikeY, color, 1.0f);
        }
    }
}

void MacRenderer::PaintInlineCodeBg(const LaidOutBlock& block,
                                     float scrollOffset) const {
    if (!target_ || !block.node) return;

    // The background of an inline code span, behind the text and two
    // points wider than the glyphs, the same fill the Windows renderer
    // draws.
    uint32_t childStart = block.node->contentOffset;
    for (const InlineBlock& child : block.node->children) {
        const uint32_t length =
            static_cast<uint32_t>(Utf32ToUtf8(child.text).size());
        if (child.code && length > 0) {
            for (const Rect& rect : RectsForRangeInBlock(
                     block, childStart, childStart + length)) {
                Rect drawn = rect;
                drawn.x -= 2.0f;
                drawn.width += 4.0f;
                drawn.y -= scrollOffset;
                target_->FillRect(drawn, ToColor(palette_.inlineCodeBg));
            }
        }
        childStart += length;
    }
}

void MacRenderer::PaintImages(const LaidOutBlock& block, float top,
                              float width) {
    if (!target_) return;

    // The images are drawn in the room the layout reserved for them, one
    // under the other.
    float y = top;
    for (const std::string& url : block.images) {
        float height = kImageFallbackHeight;
        float imageWidth = 0.0f;
        float imageHeight = 0.0f;
        if (images_.GetSize(url, &imageWidth, &imageHeight) &&
            imageWidth > 0.0f && imageHeight > 0.0f) {
            // The image keeps its aspect ratio and fits the width of
            // the column.
            height = width * (imageHeight / imageWidth);
            if (height > kImageMaxHeight) {
                height = kImageMaxHeight;
            }
        }

        Rect rect;
        rect.x = block.x;
        rect.y = y;
        rect.width = width;
        rect.height = height;

        BitmapHandle image = images_.Get(url);
        if (image) {
            target_->DrawBitmap(image, rect);
        }
        y += height + metrics_.paraGap;
    }
}

void MacRenderer::PaintTable(const LaidOutBlock& block, float scrollOffset) {
    if (!target_ || !block.table) return;
    const Node& node = *block.table;
    const LaidOutBlock::TableLayout& layout = block.tableLayout;
    if (layout.rowHeights.empty()) return;

    float y = block.y - scrollOffset;
    for (size_t r = 0; r < node.rows.size() && r < layout.rows.size(); ++r) {
        const TableRow& row = node.rows[r];
        const std::vector<LaidOutBlock::TableCellLayout>& cells =
            layout.rows[r];
        const bool isHeaderRow = !row.cells.empty() && row.cells[0].isHeader;
        const float rowH = layout.rowHeights[r];

        // The row background: the header has its own, and the body rows
        // alternate, the same stripes the Windows renderer paints.
        Rect rowBg;
        rowBg.x = block.x;
        rowBg.y = y;
        rowBg.width = block.width;
        rowBg.height = rowH;
        if (isHeaderRow) {
            target_->FillRect(rowBg, ToColor(palette_.tableHeaderBg));
        } else if (r % 2 == 0) {
            target_->FillRect(rowBg, ToColor(palette_.tableRowAlt));
        }

        const float innerW = layout.colW - 2.0f * metrics_.cellPadX;
        for (size_t c = 0; c < cells.size(); ++c) {
            const float cellX = block.x + static_cast<float>(c) * layout.colW;

            // The cell border, one rectangle per cell.
            Rect border;
            border.x = cellX;
            border.y = y;
            border.width = layout.colW;
            border.height = rowH;
            target_->StrokeRect(border, ToColor(palette_.tableBorder), 1.0f);

            // The lines of the cell, placed at the column alignment of
            // the delimiter row, inside the cell padding, clipped to
            // the cell the way the Windows renderer clips them.
            Rect inner;
            inner.x = cellX + metrics_.cellPadX;
            inner.y = y + metrics_.cellPadY;
            inner.width = innerW > 0.0f ? innerW : 0.0f;
            inner.height = rowH - 2.0f * metrics_.cellPadY;
            if (inner.width > 0.0f && inner.height > 0.0f) {
                target_->PushClip(inner);
            }
            float lineY = y + metrics_.cellPadY;
            for (const TextLine& line : cells[c].lines) {
                float lineX = cellX + metrics_.cellPadX;
                if (cells[c].align == ColumnAlign::Center) {
                    lineX += (innerW - line.width) * 0.5f;
                } else if (cells[c].align == ColumnAlign::Right) {
                    lineX += innerW - line.width;
                }
                PaintLineRuns(line, lineX, lineY);
                lineY += line.height;
            }
            if (inner.width > 0.0f && inner.height > 0.0f) {
                target_->PopClip();
            }
        }
        y += rowH;
    }
}

bool MacRenderer::YForOffset(uint32_t offset, float& y) const {
    for (const LaidOutBlock& block : blocks_) {
        if (!block.node) continue;

        // The block holds the offset when the offset is inside its
        // editable text.
        const uint32_t contentStart = block.node->contentOffset;
        const uint32_t contentEnd = contentStart + block.node->contentLength;
        if (offset < contentStart || offset > contentEnd) continue;

        y = block.y + block.imagesHeight;
        return true;
    }
    return false;
}

uint32_t MacRenderer::HitTestPoint(float x, float y) const {
    for (const LaidOutBlock& block : blocks_) {
        if (y < block.y || y > block.y + block.height) continue;
        if (x < block.x || x > block.x + block.width) continue;

        float lineY = block.y + block.imagesHeight;
        for (const TextLine& line : block.lines) {
            if (y >= lineY && y < lineY + line.height) {
                bool trailing = false;
                const uint32_t offset = engine_->HitTestOffset(
                    line, x - block.x, &trailing);
                // The offset is into the block's text, and the block
                // knows where its editable text starts in the source.
                return block.node->contentOffset + offset;
            }
            lineY += line.height;
        }
    }
    return 0;
}

bool MacRenderer::LineRangeAtPoint(float x, float y, uint32_t* outStart,
                                   uint32_t* outEnd) const {
    // The x is ignored on purpose. A click in the margin is level with
    // a line, and the line is what the user wants to select.
    (void)x;

    for (const LaidOutBlock& block : blocks_) {
        if (y < block.y || y > block.y + block.height) continue;

        uint32_t lineStart = block.node->srcOffset;
        float lineY = block.y + block.imagesHeight;
        for (const TextLine& line : block.lines) {
            if (y >= lineY && y < lineY + line.height) {
                size_t length = 0;
                for (const GlyphRun& run : line.runs) {
                    length += run.text.size();
                }
                if (outStart) *outStart = lineStart;
                if (outEnd) {
                    *outEnd = lineStart + static_cast<uint32_t>(length);
                }
                return true;
            }
            for (const GlyphRun& run : line.runs) {
                lineStart += static_cast<uint32_t>(run.text.size());
            }
            lineY += line.height;
        }
        return false;
    }
    return false;
}

bool MacRenderer::OffsetToRect(uint32_t offset, float* outX, float* outY,
                               float* outWidth, float* outHeight) const {
    for (const LaidOutBlock& block : blocks_) {
        if (!block.node) continue;
        const uint32_t start = block.node->contentOffset;
        const uint32_t end = start + block.node->contentLength;
        if (offset < start || offset > end) continue;

        // The lines are walked with the range each one covers, so the
        // line the offset is on is the one that is found.
        float lineY = block.y + block.imagesHeight;
        uint32_t lineStart = start;
        for (const TextLine& line : block.lines) {
            size_t length = 0;
            for (const GlyphRun& run : line.runs) {
                length += run.text.size();
            }
            const uint32_t lineEnd = lineStart + static_cast<uint32_t>(length);
            if (offset >= lineStart && offset <= lineEnd) {
                const Rect rect = engine_->OffsetToRect(line, offset - lineStart);
                if (outX) *outX = block.x + rect.x;
                if (outY) *outY = lineY;
                if (outWidth) *outWidth = rect.width;
                if (outHeight) *outHeight = line.height;
                return true;
            }
            lineStart = lineEnd;
            lineY += line.height;
        }
    }
    return false;
}

std::vector<Rect> MacRenderer::SelectionRects(uint32_t start,
                                               uint32_t end) const {
    return RectsForRange(start, end);
}

std::vector<Rect> MacRenderer::RectsForRange(uint32_t start,
                                             uint32_t end) const {
    std::vector<Rect> rects;
    for (const LaidOutBlock& block : blocks_) {
        const std::vector<Rect> blockRects =
            RectsForRangeInBlock(block, start, end);
        rects.insert(rects.end(), blockRects.begin(), blockRects.end());
    }
    return rects;
}

std::vector<Rect> MacRenderer::RectsForRangeInBlock(const LaidOutBlock& block,
                                                     uint32_t start,
                                                     uint32_t end) const {
    std::vector<Rect> rects;
    if (!block.node) return rects;
    if (end < start) {
        const uint32_t swap = start;
        start = end;
        end = swap;
    }

    // The lines are walked with the range each one covers, so the
    // rectangles land on the lines that hold the range. The text of a
    // block starts below its images.
    float lineY = block.y + block.imagesHeight;
    uint32_t lineStart = block.node->contentOffset;
    for (const TextLine& line : block.lines) {
        size_t length = 0;
        for (const GlyphRun& run : line.runs) {
            length += run.text.size();
        }
        const uint32_t lineEnd = lineStart + static_cast<uint32_t>(length);

        // The part of the line the range covers.
        const uint32_t from = start > lineStart ? start : lineStart;
        const uint32_t to = end < lineEnd ? end : lineEnd;
        if (from < to) {
            const Rect first = engine_->OffsetToRect(line, from - lineStart);
            // The right edge of the range is the left edge of the
            // character after it, which is the rect OffsetToRect gives
            // for that position.
            const Rect after = engine_->OffsetToRect(line, to - lineStart);
            Rect rect;
            rect.x = block.x + first.x;
            rect.y = lineY;
            rect.width = (block.x + after.x) - rect.x;
            rect.height = line.height;
            if (rect.width < 0.0f) {
                rect.width = 0.0f;
            }
            rects.push_back(rect);
        }
        lineStart = lineEnd;
        lineY += line.height;
    }
    return rects;
}

}  // namespace mac
