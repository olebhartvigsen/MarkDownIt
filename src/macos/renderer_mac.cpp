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

#include "mermaid/layout_cache.h"
#include "mermaid/model.h"
#include "mermaid/pie_layout.h"
#include "mermaid/seq_layout.h"
#include "mermaid_painter_mac.h"

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

MacRenderer::MacRenderer() = default;
MacRenderer::~MacRenderer() {
    // The engine of the diagrams belongs to the renderer.
    delete diagramEngine_;
    diagramEngine_ = nullptr;
}

FontSpec MacRenderer::FontFor(const Node& node, float baseSize, bool bold,
                               bool italic, bool monospace) const {
    (void)node;
    FontSpec font;
    font.family = monospace ? metrics_.codeFont : metrics_.bodyFont;
    font.size = baseSize;
    font.bold = bold;
    font.italic = italic;
    font.monospace = monospace;
    return font;
}

std::vector<GlyphRun> MacRenderer::BuildRuns(const Node& node, float baseSize,
                                              bool bold, bool italic,
                                              const Color& color,
                                              bool monospace) const {
    std::vector<GlyphRun> runs;

    // The children are walked one by one, so a run carries the style of
    // the span it came from. A link is a colour change, everything else
    // is a font change.
    for (const InlineBlock& child : node.children) {
        const std::string runText = Utf32ToUtf8(child.text);
        if (runText.empty()) continue;

        GlyphRun run;
        run.text = runText;
        const bool childCode = child.code;
        run.font = FontFor(node, baseSize, bold || child.strong,
                           italic || child.em, monospace || childCode);
        // A link is drawn in the link colour, everything else in the text
        // colour of the block.
        const Color runColor = child.kind == InlineKind::Link
                                   ? ToColor(palette_.link)
                                   : color;
        run.r = runColor.r;
        run.g = runColor.g;
        run.b = runColor.b;
        run.a = runColor.a;
        runs.push_back(run);
    }
    return runs;
}

float MacRenderer::Measure(const Document& doc, float widthDip) {
    blocks_.clear();
    needsLayout_ = false;

    // The content column. The padding comes from the theme, and the
    // column is capped so a line never runs longer than is comfortable to
    // read.
    //
    // With the wrap off the text gets one very wide line, and the user
    // scrolls to its end.
    const float available = std::max(1.0f, widthDip - metrics_.padX * 2.0f);
    if (!wrap_) {
        contentWidth_ = kNoWrapContentWidthDip;
    } else {
        contentWidth_ = std::min(available, ContentWidthLimit());
    }

    float y = metrics_.padTop;
    for (const Node& node : doc.nodes) {
        LayoutBlock(node, metrics_.padX, contentWidth_, &y);
    }
    totalHeight_ = y + metrics_.padTop;
    return totalHeight_;
}

void MacRenderer::SetContentWidthMode(int mode) {
    if (mode < 0 || mode > 3) {
        mode = 0;
    }
    contentWidthMode_ = mode;
}

float MacRenderer::ContentWidthLimit() const {
    // The modes are the ones the settings keep. The standard mode uses
    // the width from the theme, the wider ones use their own, and the
    // widest one has no cap at all.
    switch (contentWidthMode_) {
        case 1: return 960.0f;
        case 2: return 1600.0f;
        case 3: return 100000.0f;
        case 0:
        default: return metrics_.maxContentWidth;
    }
}

void MacRenderer::ChargeForImages(LaidOutBlock* block) {
    // The images are drawn under the text of the block, so the block
    // has to reserve the room for them.
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
    block->height += imageHeight;
}

void MacRenderer::LayoutBlock(const Node& node, float x, float width,
                              float* y) {
    LaidOutBlock block;
    block.node = &node;
    block.x = x;
    block.width = width;
    block.kind = node.block;
    block.level = node.level;

    switch (node.block) {
        case BlockKind::Heading: {
            // The gap before a heading depends on its level: the big
            // headings get more room, like the Windows renderer.
            *y += node.level <= 2 ? metrics_.headingGapBefore
                                  : metrics_.headingGapBeforeMinor;
            const float size = metrics_.headingSizes[node.level <= 6
                                                        ? node.level
                                                        : 6];
            const std::vector<GlyphRun> runs = BuildRuns(
                node, size, true, false, ToColor(palette_.heading), false);
            block.lines = engine_->MeasureWrapped(runs, width, 0.0f);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            block.height *= metrics_.headingLineHeight /
                            metrics_.bodyLineHeight;
            *y += block.height;
            *y += metrics_.headingGapAfter;
            break;
        }

        case BlockKind::Paragraph: {
            const std::vector<GlyphRun> runs = BuildRuns(
                node, metrics_.bodyFontSize, false, false,
                ToColor(palette_.textPrimary), false);
            block.lines = engine_->MeasureWrapped(runs, width, 0.0f);
            for (const TextLine& line : block.lines) {
                block.height += line.height * metrics_.bodyLineHeight /
                                metrics_.bodyLineHeight;
            }
            ChargeForImages(&block);
            *y += block.height;
            *y += metrics_.paraGap;
            break;
        }

        case BlockKind::CodeBlock: {
            *y += metrics_.codeBlockGap;
            const std::string code = InlineTextUtf8(node);
            // The code is one run per line, so a line break in the source
            // stays a line break on screen.
            std::vector<GlyphRun> runs;
            size_t start = 0;
            while (start <= code.size()) {
                const size_t end = code.find('\n', start);
                const size_t length =
                    (end == std::string::npos ? code.size() : end) - start;
                GlyphRun run;
                run.text = code.substr(start, length);
                run.font = FontFor(node, metrics_.codeFontSize, false, false,
                                   true);
                const Color color = ToColor(palette_.textPrimary);
                run.r = color.r;
                run.g = color.g;
                run.b = color.b;
                run.a = color.a;
                runs.push_back(run);
                if (end == std::string::npos) break;
                start = end + 1;
            }
            const float lineHeight =
                engine_->LineHeight(FontFor(node, metrics_.codeFontSize, false,
                                           false, true));
            block.lines = engine_->MeasureWrapped(runs, width - metrics_.codePad * 2.0f,
                                                 0.0f);
            block.height = static_cast<float>(block.lines.size()) * lineHeight *
                           metrics_.codeLineHeight / metrics_.bodyLineHeight;
            block.height += metrics_.codePad * 2.0f;
            *y += block.height;
            *y += metrics_.codeBlockGap;
            break;
        }

        case BlockKind::BlockQuote: {
            const float indent = metrics_.quoteIndent * (node.depth + 1);
            const std::vector<GlyphRun> runs = BuildRuns(
                node, metrics_.bodyFontSize, false, false,
                ToColor(palette_.quoteText), false);
            block.lines = engine_->MeasureWrapped(runs, width - indent, 0.0f);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            *y += block.height;
            *y += metrics_.paraGap;
            break;
        }

        case BlockKind::ThematicBreak: {
            block.height = 1.0f;
            *y += metrics_.ruleGapAbove;
            *y += block.height;
            *y += metrics_.ruleGapBelow;
            break;
        }

        case BlockKind::List: {
            const float indent = metrics_.listIndent * (node.depth + 1);
            const std::vector<GlyphRun> runs = BuildRuns(
                node, metrics_.bodyFontSize, false, false,
                ToColor(palette_.textPrimary), false);
            block.lines = engine_->MeasureWrapped(runs, width - indent, 0.0f);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            *y += block.height;
            *y += metrics_.listItemGap;
            break;
        }

        case BlockKind::Table: {
            // The table is measured column by column, the same way the
            // Windows renderer does it: the widest cell in a column sets
            // the column width.
            size_t columnCount = 0;
            for (const TableRow& row : node.rows) {
                columnCount = std::max(columnCount, row.cells.size());
            }
            std::vector<float> columnWidths(columnCount, 0.0f);
            for (const TableRow& row : node.rows) {
                for (size_t c = 0; c < row.cells.size(); ++c) {
                    const std::string cellText =
                        Utf32ToUtf8(row.cells[c].text);
                    GlyphRun run;
                    run.text = cellText;
                    run.font = FontFor(node, metrics_.bodyFontSize, false,
                                       false, false);
                    const std::vector<TextLine> lines =
                        engine_->MeasureWrapped({run}, 10000.0f, 0.0f);
                    float cellWidth = 0.0f;
                    for (const TextLine& line : lines) {
                        cellWidth = std::max(cellWidth, line.width);
                    }
                    columnWidths[c] = std::max(columnWidths[c], cellWidth);
                }
            }
            // The table is capped at the content width, and the columns
            // share what is left over when they do not fit.
            float total = 0.0f;
            for (float w : columnWidths) total += w;
            const float available = width - columnCount * metrics_.cellPadX * 2.0f;
            if (total > available && total > 0.0f) {
                const float scale = available / total;
                for (float& w : columnWidths) w *= scale;
            }
            // The height is the sum of the row heights, and every row is
            // at least one line tall.
            float rowHeight = 0.0f;
            for (const TableRow& row : node.rows) {
                float tallest = 0.0f;
                for (size_t c = 0; c < row.cells.size(); ++c) {
                    const Node tmp = TextNode(row.cells[c].text);
                    const std::vector<GlyphRun> cellRuns = BuildRuns(
                        tmp, metrics_.bodyFontSize, false, false,
                        ToColor(palette_.textPrimary), false);
                    const std::vector<TextLine> cellLines =
                        engine_->MeasureWrapped(cellRuns,
                                                columnWidths[c] > 0.0f
                                                    ? columnWidths[c]
                                                    : 1.0f,
                                                0.0f);
                    float cellHeight = 0.0f;
                    for (const TextLine& line : cellLines) {
                        cellHeight += line.height;
                    }
                    tallest = std::max(tallest, cellHeight);
                }
                rowHeight = std::max(rowHeight, tallest);
            }
            block.height = static_cast<float>(node.rows.size()) *
                           (rowHeight + metrics_.cellPadY * 2.0f);
            block.table = &node;
            *y += block.height;
            *y += metrics_.paraGap;
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
                    diagram, zoom_, MermaidMeasureWidth(diagram, width));
            }
            block.height = diagramHeight > 0.0f ? diagramHeight : 100.0f;
            *y += block.height;
            *y += metrics_.paraGap;
            break;
        }
    }

    block.y = *y - block.height;
    blocks_.push_back(block);
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
    PaintSearchHighlights(scrollOffset);

    for (const LaidOutBlock& block : blocks_) {
        PaintBlock(block, scrollOffset);
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

void MacRenderer::PaintBlock(const LaidOutBlock& block, float scrollOffset) {
    if (!target_) return;
    const float top = block.y - scrollOffset;

    switch (block.kind) {
        case BlockKind::ThematicBreak: {
            Rect rect;
            rect.x = block.x;
            rect.y = top;
            rect.width = block.width;
            rect.height = 1.0f;
            target_->FillRect(rect, ToColor(palette_.rule));
            break;
        }

        case BlockKind::CodeBlock: {
            // The background and the border, then the code itself.
            Rect bg;
            bg.x = block.x;
            bg.y = top;
            bg.width = block.width;
            bg.height = block.height;
            target_->FillRoundedRect(bg, metrics_.codeRadius,
                                      ToColor(palette_.codeBg));
            Rect border = bg;
            target_->StrokeRect(border, ToColor(palette_.codeBorder), 1.0f);

            const float lineHeight =
                engine_->LineHeight(FontFor(*block.node,
                                            metrics_.codeFontSize, false,
                                            false, true));
            float y = top + metrics_.codePad;
            for (const TextLine& line : block.lines) {
                for (const GlyphRun& run : line.runs) {
                    Color color;
                    color.r = run.r;
                    color.g = run.g;
                    color.b = run.b;
                    color.a = run.a;
                    target_->DrawText(run.text, run.font,
                                      block.x + metrics_.codePad + run.x,
                                      y + line.ascent, color);
                }
                y += lineHeight;
            }
            break;
        }

        case BlockKind::BlockQuote: {
            // The bar on the left, then the text.
            const float indent = metrics_.quoteIndent * (block.node->depth + 1);
            Rect bar;
            bar.x = block.x;
            bar.y = top;
            bar.width = metrics_.quoteBarWidth;
            bar.height = block.height;
            target_->FillRect(bar, ToColor(palette_.quoteBar));

            float y = top;
            for (const TextLine& line : block.lines) {
                for (const GlyphRun& run : line.runs) {
                    Color color;
                    color.r = run.r;
                    color.g = run.g;
                    color.b = run.b;
                    color.a = run.a;
                    target_->DrawText(run.text, run.font,
                                      block.x + indent + run.x,
                                      y + line.ascent, color);
                }
                y += line.height;
            }
            break;
        }

        case BlockKind::List: {
            // The marker sits in the gutter, and the text is indented.
            const float indent = metrics_.listIndent * (block.node->depth + 1);
            const std::string marker = block.node->ordered ? "1." : "\xE2\x80\xA2";
            FontSpec markerFont = FontFor(*block.node,
                                          metrics_.bodyFontSize, false,
                                          false, false);
            target_->DrawText(marker, markerFont, block.x,
                              top + (block.lines.empty()
                                         ? metrics_.bodyFontSize
                                         : block.lines.front().ascent),
                              ToColor(palette_.textMuted));
            float y = top;
            for (const TextLine& line : block.lines) {
                for (const GlyphRun& run : line.runs) {
                    Color color;
                    color.r = run.r;
                    color.g = run.g;
                    color.b = run.b;
                    color.a = run.a;
                    target_->DrawText(run.text, run.font,
                                      block.x + indent + run.x,
                                      y + line.ascent, color);
                }
                y += line.height;
            }
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
            // the shared layout through the seam.
            const mermaid::MermaidRender diagram = MermaidRenderFor(*block.node);
            if (!HasMermaidLayout(diagram)) break;
            const float pad = mermaid::kMermaidBlockPad;
            const float availWidth =
                block.width > 2.0f * pad ? block.width - 2.0f * pad : 0.0f;
            PaintMermaidDiagram(diagramEngine_ ? diagramEngine_ : engine_,
                                target_, diagram, zoom_, availWidth, block.x,
                                top, pad, palette_,
                                FontFor(*block.node, metrics_.bodyFontSize,
                                        false, false, false));
            break;
        }

        case BlockKind::Heading:
        case BlockKind::Paragraph: {
            float y = top;
            for (const TextLine& line : block.lines) {
                for (const GlyphRun& run : line.runs) {
                    Color color;
                    color.r = run.r;
                    color.g = run.g;
                    color.b = run.b;
                    color.a = run.a;
                    target_->DrawText(run.text, run.font, block.x + run.x,
                                      y + line.ascent, color);
                }
                y += line.height;
            }
            PaintImages(block, y, block.width);
            break;
        }
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

    size_t columnCount = 0;
    for (const TableRow& row : node.rows) {
        columnCount = std::max(columnCount, row.cells.size());
    }
    if (columnCount == 0) return;

    // The column widths, measured the same way as in the layout, so the
    // painting matches the measurement.
    std::vector<float> columnWidths(columnCount, 0.0f);
    for (const TableRow& row : node.rows) {
        for (size_t c = 0; c < row.cells.size(); ++c) {
            const Node tmp = TextNode(row.cells[c].text);
            const std::vector<GlyphRun> cellRuns = BuildRuns(
                tmp, metrics_.bodyFontSize, false, false,
                ToColor(palette_.textPrimary), false);
            const std::vector<TextLine> lines =
                engine_->MeasureWrapped(cellRuns, 10000.0f, 0.0f);
            float cellWidth = 0.0f;
            for (const TextLine& line : lines) {
                cellWidth = std::max(cellWidth, line.width);
            }
            columnWidths[c] = std::max(columnWidths[c], cellWidth);
        }
    }
    float total = 0.0f;
    for (float w : columnWidths) total += w;
    const float available = block.width - columnCount * metrics_.cellPadX * 2.0f;
    if (total > available && total > 0.0f) {
        const float scale = available / total;
        for (float& w : columnWidths) w *= scale;
    }

    // The row height, the tallest cell of the tallest row.
    float rowHeight = 0.0f;
    for (const TableRow& row : node.rows) {
        float tallest = 0.0f;
        for (size_t c = 0; c < row.cells.size(); ++c) {
            Node tmp;
            tmp.children.push_back(InlineBlock{});
            tmp.children.back().text = row.cells[c].text;
            const std::vector<GlyphRun> cellRuns = BuildRuns(
                tmp, metrics_.bodyFontSize, false, false,
                ToColor(palette_.textPrimary), false);
            const std::vector<TextLine> cellLines =
                engine_->MeasureWrapped(cellRuns,
                                        columnWidths[c] > 0.0f
                                            ? columnWidths[c]
                                            : 1.0f,
                                        0.0f);
            float cellHeight = 0.0f;
            for (const TextLine& line : cellLines) {
                cellHeight += line.height;
            }
            tallest = std::max(tallest, cellHeight);
        }
        rowHeight = std::max(rowHeight, tallest);
    }

    // The rows.
    float y = block.y - scrollOffset;
    for (size_t r = 0; r < node.rows.size(); ++r) {
        const TableRow& row = node.rows[r];
        const bool isHeader = r == 0;

        // The row background: the header has its own, and the body rows
        // alternate.
        Rect rowBg;
        rowBg.x = block.x;
        rowBg.y = y;
        rowBg.width = block.width;
        rowBg.height = rowHeight + metrics_.cellPadY * 2.0f;
        if (isHeader) {
            target_->FillRect(rowBg, ToColor(palette_.tableHeaderBg));
        } else if (r % 2 == 0) {
            target_->FillRect(rowBg, ToColor(palette_.tableRowAlt));
        }

        // The cells.
        float x = block.x;
        for (size_t c = 0; c < row.cells.size(); ++c) {
            const float cellWidth = columnWidths[c];
            const Node tmp = TextNode(row.cells[c].text);
            const std::vector<GlyphRun> cellRuns = BuildRuns(
                tmp, metrics_.bodyFontSize, isHeader, false,
                ToColor(isHeader ? palette_.heading : palette_.textPrimary),
                false);
            const std::vector<TextLine> cellLines =
                engine_->MeasureWrapped(cellRuns, cellWidth > 0.0f
                                                  ? cellWidth
                                                  : 1.0f,
                                        0.0f);
            float cellY = y + metrics_.cellPadY;
            for (const TextLine& line : cellLines) {
                for (const GlyphRun& run : line.runs) {
                    Color color;
                    color.r = run.r;
                    color.g = run.g;
                    color.b = run.b;
                    color.a = run.a;
                    target_->DrawText(run.text, run.font,
                                      x + metrics_.cellPadX + run.x,
                                      cellY + line.ascent, color);
                }
                cellY += line.height;
            }

            // The cell border.
            Rect border;
            border.x = x;
            border.y = y;
            border.width = cellWidth + metrics_.cellPadX * 2.0f;
            border.height = rowHeight + metrics_.cellPadY * 2.0f;
            target_->StrokeRect(border, ToColor(palette_.tableBorder), 1.0f);

            x += border.width;
        }
        y += rowHeight + metrics_.cellPadY * 2.0f;
    }
}

uint32_t MacRenderer::HitTestPoint(float x, float y) const {
    for (const LaidOutBlock& block : blocks_) {
        if (y < block.y || y > block.y + block.height) continue;
        if (x < block.x || x > block.x + block.width) continue;

        float lineY = block.y;
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
        float lineY = block.y;
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
        float lineY = block.y;
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
    if (end < start) {
        const uint32_t swap = start;
        start = end;
        end = swap;
    }

    for (const LaidOutBlock& block : blocks_) {
        if (!block.node) continue;

        // The text of the block, in the same order the lines have it.
        float lineY = block.y;
        uint32_t lineStart = block.node->contentOffset;
        for (const TextLine& line : block.lines) {
            size_t length = 0;
            for (const GlyphRun& run : line.runs) {
                length += run.text.size();
            }
            const uint32_t lineEnd = lineStart + static_cast<uint32_t>(length);

            // The part of the line the selection covers.
            const uint32_t from = start > lineStart ? start : lineStart;
            const uint32_t to = end < lineEnd ? end : lineEnd;
            if (from < to) {
                const Rect first =
                    engine_->OffsetToRect(line, from - lineStart);
                const Rect last = engine_->OffsetToRect(line, to - lineStart);
                Rect rect;
                rect.x = block.x + first.x;
                rect.y = lineY;
                rect.width = (block.x + last.x + last.width) - rect.x;
                rect.height = line.height;
                if (rect.width < 0.0f) {
                    rect.width = 0.0f;
                }
                rects.push_back(rect);
            }
            lineStart = lineEnd;
            lineY += line.height;
        }
    }
    return rects;
}

}  // namespace mac
