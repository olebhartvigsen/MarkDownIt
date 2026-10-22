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

#include "mermaid/model.h"
#include "mermaid/pie_layout.h"
#include "mermaid/seq_layout.h"

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

// The code point count of a UTF-8 string, which the code block needs to
// know how many lines it has.
size_t CountUtf8Chars(const std::string& text) {
    size_t count = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++count;
    }
    return count;
}

}  // namespace

MacRenderer::MacRenderer() = default;
MacRenderer::~MacRenderer() = default;

FontSpec MacRenderer::FontFor(const Node& node, float baseSize, bool bold,
                               bool italic, bool monospace) const {
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

    // The content column. The padding comes from the theme, and the
    // column is capped so a line never runs longer than is comfortable to
    // read.
    const float available = std::max(1.0f, widthDip - metrics_.padX * 2.0f);
    contentWidth_ = std::min(available, metrics_.maxContentWidth);

    float y = metrics_.padTop;
    for (const Node& node : doc.nodes) {
        LayoutBlock(node, metrics_.padX, contentWidth_, &y);
    }
    totalHeight_ = y + metrics_.padTop;
    return totalHeight_;
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
            // parser. The height comes from the laid out model.
            float diagramHeight = 0.0f;
            if (node.mermaid_layout) {
                diagramHeight = static_cast<float>(
                    node.mermaid_layout->height);
            } else if (node.mermaid_pie) {
                diagramHeight = static_cast<float>(node.mermaid_pie->height);
            } else if (node.mermaid_seq) {
                diagramHeight = static_cast<float>(node.mermaid_seq->height);
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

void MacRenderer::Render(const Document& doc, float scrollOffset) {
    (void)doc;
    if (!target_) return;
    for (const LaidOutBlock& block : blocks_) {
        PaintBlock(block, scrollOffset);
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
            // The mermaid painter is a separate piece, added with the
            // rest of the renderer parity work.
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
            break;
        }
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
                // The offset is into the block's text, and the block knows
                // where its text starts in the source.
                return block.node->srcOffset + offset;
            }
            lineY += line.height;
        }
    }
    return 0;
}

bool MacRenderer::OffsetToRect(uint32_t offset, float* outX, float* outY,
                               float* outWidth, float* outHeight) const {
    for (const LaidOutBlock& block : blocks_) {
        if (!block.node) continue;
        const uint32_t start = block.node->contentOffset;
        const uint32_t end = start + block.node->contentLength;
        if (offset < start || offset > end) continue;

        float lineY = block.y;
        for (const TextLine& line : block.lines) {
            const Rect rect = engine_->OffsetToRect(line, offset - start);
            if (outX) *outX = block.x + rect.x;
            if (outY) *outY = lineY;
            if (outWidth) *outWidth = rect.width;
            if (outHeight) *outHeight = line.height;
            return true;
            lineY += line.height;
        }
    }
    return false;
}

}  // namespace mac
