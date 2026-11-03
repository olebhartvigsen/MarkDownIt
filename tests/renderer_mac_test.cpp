// The app layer's renderer, laid out against a fixed text engine.
//
// The shared suite covers the parser and the document model; the mac
// renderer talks to Core Text, so its layout arithmetic is checked here
// with an engine that measures on a fixed grid. What the tests pin is
// the Windows parity: the reading width and its centered column, the
// line spacing, the gaps between blocks, the equal table columns with
// per row heights, the list marker gutter, the air under an H1, the
// quote bar and the zoom.

#include "gtest_lite.h"

#include "../src/macos/renderer_mac.h"
#include "../src/parser.h"

#include <cmath>
#include <string>
#include <vector>

using mac::FontSpec;
using mac::GlyphRun;
using mac::LaidOutBlock;
using mac::MacRenderer;
using mac::Rect;
using mac::TextEngine;
using mac::TextLine;

namespace {

// A text engine with fixed metrics: a character is half an em wide, a
// line is as tall as the em, and every hard line break starts a new
// line. The renderer's own arithmetic is what the tests check, so the
// engine stays simple and deterministic.
class FakeEngine : public TextEngine {
public:
    struct Call {
        float maxWidth = 0.0f;
        float size = 0.0f;
        std::string text;
    };
    std::vector<Call> calls;

    std::vector<TextLine> MeasureWrapped(const std::vector<GlyphRun>& runs,
                                          float maxWidth,
                                          float lineSpacing) override {
        (void)lineSpacing;

        std::string text;
        for (const GlyphRun& run : runs) text += run.text;
        if (text.empty()) return {};

        Call call;
        call.maxWidth = maxWidth;
        call.size = runs[0].font.size;
        call.text = text;
        calls.push_back(call);

        const float charWidth = runs[0].font.size * 0.5f;
        const float width = maxWidth > 0.0f ? maxWidth : 1.0e9f;
        size_t perLine = static_cast<size_t>(std::floor(width / charWidth));
        if (perLine < 1) perLine = 1;

        // One line per hard break, wrapped at the width. The runs are
        // sliced at the line window the way the real engine slices
        // them, so the styles travel with the text.
        std::vector<TextLine> lines;
        size_t start = 0;
        while (start < text.size()) {
            const size_t hard = text.find('\n', start);
            const size_t stop =
                hard == std::string::npos ? text.size() : hard + 1;
            size_t pos = start;
            while (pos < stop) {
                size_t count = stop - pos;
                if (count > perLine) count = perLine;
                const size_t lineEnd = pos + count;

                TextLine line;
                line.width = static_cast<float>(count) * charWidth;
                line.height = runs[0].font.size;
                line.ascent = line.height;

                size_t offset = 0;
                for (const GlyphRun& run : runs) {
                    const size_t runEnd = offset + run.text.size();
                    if (runEnd > pos && offset < lineEnd) {
                        const size_t from = pos > offset ? pos - offset : 0;
                        const size_t to = lineEnd < runEnd ? lineEnd - offset
                                                           : run.text.size();
                        GlyphRun sliced = run;
                        sliced.text = run.text.substr(from, to - from);
                        sliced.x = static_cast<float>(offset + from - start) *
                                   charWidth;
                        line.runs.push_back(sliced);
                    }
                    offset = runEnd;
                }
                lines.push_back(line);
                pos = lineEnd;
            }
            start = stop;
        }
        return lines;
    }

    uint32_t HitTestOffset(const TextLine& line, float x,
                            bool* outTrailing) const override {
        if (outTrailing) *outTrailing = false;
        const float charWidth = CharWidth(line);
        if (charWidth <= 0.0f) return 0;
        const long index = static_cast<long>(x / charWidth);
        return index <= 0 ? 0u : static_cast<uint32_t>(index);
    }

    Rect OffsetToRect(const TextLine& line, uint32_t offset) const override {
        const float charWidth = CharWidth(line);
        Rect rect;
        rect.x = static_cast<float>(offset) * charWidth;
        rect.width = charWidth;
        rect.height = line.height;
        return rect;
    }

    float LineHeight(const FontSpec& font) const override {
        return font.size;
    }

private:
    static float CharWidth(const TextLine& line) {
        for (const GlyphRun& run : line.runs) {
            if (run.font.size > 0.0f) return run.font.size * 0.5f;
        }
        return 0.0f;
    }
};

// A target that records what was drawn, so the paint side can be
// checked without a screen.
class RecordingTarget : public mac::RenderTarget {
public:
    struct Fill {
        Rect rect;
        float radius = 0.0f;
        mac::Color color;
        bool roundLeft = true;
        bool roundRight = true;
    };
    struct Stroke {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        float width = 0.0f;
        mac::Color color;
    };
    struct Text {
        std::string text;
        float x = 0.0f;
        float baseline = 0.0f;
        mac::Color color;
    };

    std::vector<Fill> fills;
    std::vector<Stroke> strokes;
    std::vector<Text> texts;
    int clips = 0;

    void FillRect(const Rect& rect, const mac::Color& color) override {
        Fill fill;
        fill.rect = rect;
        fill.color = color;
        fills.push_back(fill);
    }
    void FillRoundedRect(const Rect& rect, float radius,
                          const mac::Color& color) override {
        Fill fill;
        fill.rect = rect;
        fill.radius = radius;
        fill.color = color;
        fills.push_back(fill);
    }
    void FillRoundedRectSides(const Rect& rect, float radius,
                              const mac::Color& color, bool roundLeft,
                              bool roundRight) override {
        Fill fill;
        fill.rect = rect;
        fill.radius = radius;
        fill.color = color;
        fill.roundLeft = roundLeft;
        fill.roundRight = roundRight;
        fills.push_back(fill);
    }
    void StrokeRect(const Rect& rect, const mac::Color& color,
                    float lineWidth) override {
        Stroke stroke;
        stroke.x0 = rect.x;
        stroke.y0 = rect.y;
        stroke.x1 = rect.x + rect.width;
        stroke.y1 = rect.y + rect.height;
        stroke.width = lineWidth;
        stroke.color = color;
        strokes.push_back(stroke);
    }
    void StrokeRoundedRect(const Rect& rect, float radius,
                            const mac::Color& color,
                            float lineWidth) override {
        (void)radius;
        StrokeRect(rect, color, lineWidth);
    }
    void StrokeLine(float x0, float y0, float x1, float y1,
                    const mac::Color& color, float lineWidth) override {
        Stroke stroke;
        stroke.x0 = x0;
        stroke.y0 = y0;
        stroke.x1 = x1;
        stroke.y1 = y1;
        stroke.width = lineWidth;
        stroke.color = color;
        strokes.push_back(stroke);
    }
    void DrawText(const std::string& utf8, const FontSpec& font, float x,
                  float baselineY, const mac::Color& color) override {
        (void)font;
        Text text;
        text.text = utf8;
        text.x = x;
        text.baseline = baselineY;
        text.color = color;
        texts.push_back(text);
    }
    void DrawBitmap(mac::BitmapHandle image, const Rect& rect) override {
        (void)image;
        (void)rect;
    }
    void FillEllipse(const Rect& rect, const mac::Color& color) override {
        (void)rect;
        (void)color;
    }
    void StrokeEllipse(const Rect& rect, const mac::Color& color,
                        float lineWidth) override {
        (void)rect;
        (void)color;
        (void)lineWidth;
    }
    void FillPolygon(const std::vector<mac::Point>& points,
                      const mac::Color& color) override {
        (void)points;
        (void)color;
    }
    void StrokePolygon(const std::vector<mac::Point>& points,
                        const mac::Color& color, float lineWidth,
                        bool closed) override {
        (void)points;
        (void)color;
        (void)lineWidth;
        (void)closed;
    }
    void FillPieSlice(const Rect& rect, float startAngle, float endAngle,
                       const mac::Color& color) override {
        (void)rect;
        (void)startAngle;
        (void)endAngle;
        (void)color;
    }
    void PushClip(const Rect& rect) override {
        (void)rect;
        ++clips;
    }
    void PopClip() override { --clips; }
};

// --- The hand-built documents -------------------------------------------
//
// The tests build their documents directly, so the layout numbers do
// not depend on the parser's block splitting.

std::u32string Utf32(const std::string& text) {
    std::u32string out;
    for (unsigned char c : text) out.push_back(static_cast<char32_t>(c));
    return out;
}

struct NodeBuilder {
    Node node;
    NodeBuilder(BlockKind kind, const std::string& text) {
        node.block = kind;
        InlineBlock child;
        child.kind = InlineKind::Text;
        child.text = Utf32(text);
        node.children.push_back(child);
        node.contentLength = static_cast<uint32_t>(text.size());
    }
};

Node Para(const std::string& text) {
    return NodeBuilder(BlockKind::Paragraph, text).node;
}

Node Heading(int level, const std::string& text) {
    Node node = Para(text);
    node.block = BlockKind::Heading;
    node.level = level;
    return node;
}

Node ListItem(bool ordered, int depth, const std::string& text) {
    Node node = Para(text);
    node.block = BlockKind::List;
    node.ordered = ordered;
    node.depth = depth;
    return node;
}

Node Code(const std::string& text) {
    Node node;
    node.block = BlockKind::CodeBlock;
    // The parser keeps a fenced block's text in raw, not in inline
    // children; build the node the same way so the tests match the
    // document the renderer actually sees.
    node.raw = Utf32(text);
    return node;
}

Node Table(const std::vector<std::vector<std::string>>& rows) {
    Node node;
    node.block = BlockKind::Table;
    for (size_t r = 0; r < rows.size(); ++r) {
        TableRow row;
        for (const std::string& text : rows[r]) {
            TableCell cell;
            cell.text = Utf32(text);
            cell.isHeader = (r == 0);
            row.cells.push_back(cell);
        }
        node.rows.push_back(row);
    }
    return node;
}

Document Doc(std::vector<Node> nodes) {
    Document doc;
    doc.nodes = std::move(nodes);
    return doc;
}

// The theme metrics the renderer starts from. The tests read the
// numbers from the theme instead of repeating them, so a metric
// change cannot leave an assertion behind.
const LayoutMetrics kBase = BaseMetrics();
const float kPadX = kBase.padX;
const float kPadTop = kBase.padTop;
// The content width of a 1400-point window once the padding is taken
// out. The mac renderer no longer caps the measure at a fixed value.
const float kWide = 1400.0f - 2.0f * kPadX;
constexpr float kPtToDip = 96.0f / 72.0f;
const float kBodyEm = kBase.bodyFontSize * kPtToDip;
const float kH1Em = kBase.headingSizes[1] * kPtToDip;
const float kCodeEm = kBase.codeFontSize * kPtToDip;

bool SameColor(const mac::Color& a, const mac::Color& b) {
    return std::fabs(a.r - b.r) < 1e-6f && std::fabs(a.g - b.g) < 1e-6f &&
           std::fabs(a.b - b.b) < 1e-6f && std::fabs(a.a - b.a) < 1e-6f;
}

bool SameColor(const mac::Color& a, const ThemeColor& b) {
    return std::fabs(a.r - b.r) < 1e-6f && std::fabs(a.g - b.g) < 1e-6f &&
           std::fabs(a.b - b.b) < 1e-6f && std::fabs(a.a - b.a) < 1e-6f;
}

}  // namespace

TEST(RendererMac, FollowsTheWindowWidth) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    const Document doc = Doc({Para("body text")});
    renderer.Measure(doc, 1400.0f);
    EXPECT_NEAR(renderer.contentWidth(), kWide, 0.01f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& para = renderer.blocks()[0];
    EXPECT_NEAR(para.x, kPadX, 0.01f);
    EXPECT_NEAR(para.width, kWide, 0.01f);
    // The first block has no gap before it.
    EXPECT_NEAR(para.y, kPadTop, 0.01f);

    // A narrow window: the padding slides down from the theme's value
    // toward its minimum, and what is left is the column.
    renderer.Measure(doc, 500.0f);
    const float narrowPad = 16.0f + (kPadX - 16.0f) * (100.0f / 240.0f);
    EXPECT_NEAR(renderer.contentWidth(), 500.0f - 2.0f * narrowPad, 0.01f);
    EXPECT_NEAR(renderer.blocks()[0].x, narrowPad, 0.01f);

    // With the wrap off the text gets one wide line, and the column
    // sits at the left padding.
    renderer.SetWrap(false);
    renderer.Measure(doc, 1400.0f);
    EXPECT_NEAR(renderer.contentWidth(), 10000.0f, 0.01f);
    EXPECT_NEAR(renderer.blocks()[0].x, kPadX, 0.01f);
}

TEST(RendererMac, KeepsTheGitHubTypeScale) {
    // The measured GitHub values, in DIP at the default zoom. The
    // body is 16px at 1.5, code is 85% of it at 1.45, and the six
    // headings follow the 2 / 1.5 / 1.25 / 1 / .875 / .85 em scale.
    const LayoutMetrics m = BaseMetrics();
    EXPECT_NEAR(m.bodyFontSize * kPtToDip, 16.0f, 0.01f);
    EXPECT_NEAR(m.codeFontSize * kPtToDip, 13.6f, 0.01f);
    EXPECT_NEAR(m.headingSizes[1] * kPtToDip, 32.0f, 0.01f);
    EXPECT_NEAR(m.headingSizes[2] * kPtToDip, 24.0f, 0.01f);
    EXPECT_NEAR(m.headingSizes[3] * kPtToDip, 20.0f, 0.01f);
    EXPECT_NEAR(m.headingSizes[4] * kPtToDip, 16.0f, 0.01f);
    EXPECT_NEAR(m.headingSizes[5] * kPtToDip, 14.0f, 0.01f);
    EXPECT_NEAR(m.headingSizes[6] * kPtToDip, 13.6f, 0.01f);
    EXPECT_NEAR(m.bodyLineHeight, 1.5f, 0.001f);
    EXPECT_NEAR(m.headingLineHeight, 1.25f, 0.001f);
    EXPECT_NEAR(m.codeLineHeight, 1.45f, 0.001f);
    EXPECT_NEAR(m.paraGap, 16.0f, 0.01f);
    EXPECT_NEAR(m.headingGapBefore, 24.0f, 0.01f);
    EXPECT_NEAR(m.headingGapAfter, 16.0f, 0.01f);
    EXPECT_NEAR(m.listGap, 16.0f, 0.01f);
    EXPECT_NEAR(m.listItemGap, 4.0f, 0.01f);
    EXPECT_NEAR(m.codeBlockGap, 16.0f, 0.01f);
    EXPECT_NEAR(m.ruleGapAbove, 24.0f, 0.01f);
    EXPECT_NEAR(m.ruleGapBelow, 24.0f, 0.01f);
    EXPECT_NEAR(m.codePad, 16.0f, 0.01f);
    EXPECT_NEAR(m.codeRadius, 6.0f, 0.01f);
    EXPECT_NEAR(m.cellPadX, 13.0f, 0.01f);
    EXPECT_NEAR(m.cellPadY, 6.0f, 0.01f);
}

TEST(RendererMac, AppliesTheLineSpacingAndTheReadingLead) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    const Document doc =
        Doc({Heading(1, "Title"), Para("body text"), Code("code")});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 3u);

    // H1: GitHub's 1.25 line box, with the glyphs centred in it, plus
    // .3em of padding and the 1px rule the heading carries.
    const LaidOutBlock& heading = renderer.blocks()[0];
    ASSERT_EQ(heading.lines.size(), 1u);
    EXPECT_NEAR(heading.lines[0].height, kH1Em * kBase.headingLineHeight, 0.02f);
    EXPECT_NEAR(heading.lines[0].ascent,
                (kH1Em * kBase.headingLineHeight - kH1Em) * 0.5f + kH1Em, 0.02f);
    EXPECT_NEAR(heading.height, kH1Em * kBase.headingLineHeight + 0.3f * kH1Em + 1.0f, 0.02f);

    // Body: GitHub's 1.5 line box, the glyphs centred in it.
    const LaidOutBlock& body = renderer.blocks()[1];
    ASSERT_EQ(body.lines.size(), 1u);
    EXPECT_NEAR(body.lines[0].height, kBodyEm * kBase.bodyLineHeight, 0.02f);
    EXPECT_NEAR(body.lines[0].ascent,
                (kBodyEm * kBase.bodyLineHeight - kBodyEm) * 0.5f + kBodyEm, 0.02f);

    // Code: 1.45 times the 13.6px code em, plus 16px of padding on
    // each side of the card.
    const LaidOutBlock& code = renderer.blocks()[2];
    ASSERT_EQ(code.lines.size(), 1u);
    EXPECT_NEAR(code.lines[0].height, kCodeEm * kBase.codeLineHeight, 0.02f);
    EXPECT_NEAR(code.height, kCodeEm * kBase.codeLineHeight +
                            2.0f * kBase.codePad, 0.02f);
}

TEST(RendererMac, SpacesBlocksLikeWindows) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    const Document doc = Doc({
        Para("one"), Para("two"), Heading(2, "Head"), Para("three"),
        ListItem(true, 0, "item"), ListItem(true, 0, "item"), Code("code"),
        Para("tail"),
    });
    renderer.Measure(doc, 1400.0f);
    const std::vector<LaidOutBlock>& blocks = renderer.blocks();
    ASSERT_EQ(blocks.size(), 8u);

    auto gap = [&](size_t i) {
        return blocks[i + 1].y - (blocks[i].y + blocks[i].height);
    };
    EXPECT_NEAR(gap(0), 16.0f, 0.02f);  // paragraph to paragraph
    EXPECT_NEAR(gap(1), 24.0f, 0.02f);  // the gap before a heading
    EXPECT_NEAR(gap(2), 16.0f, 0.02f);  // the gap after a heading
    EXPECT_NEAR(gap(3), 16.0f, 0.02f);  // the gap before a list
    EXPECT_NEAR(gap(4), 4.0f, 0.02f);   // between two list items
    EXPECT_NEAR(gap(5), 16.0f, 0.02f);  // the gap after a list
    EXPECT_NEAR(gap(6), 16.0f, 0.02f);  // the gap around a code block
}

TEST(RendererMac, ScalesEverythingWithTheZoom) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    renderer.SetZoom(2.0f);

    const Document doc = Doc({Para("body")});
    renderer.Measure(doc, 3000.0f);
    const float zoomColumn = 3000.0f - 4.0f * kPadX;
    EXPECT_NEAR(renderer.contentWidth(), zoomColumn, 0.01f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    EXPECT_NEAR(renderer.blocks()[0].x, 2.0f * kPadX, 0.01f);

    // The font the engine measured with is the point size converted
    // and zoomed: 12 * 4/3 * 2 = 32, and the line follows at 1.5.
    ASSERT_TRUE(!engine.calls.empty());
    EXPECT_NEAR(engine.calls.back().size, kBodyEm * 2.0f, 0.01f);
    ASSERT_EQ(renderer.blocks()[0].lines.size(), 1u);
    EXPECT_NEAR(renderer.blocks()[0].lines[0].height, kBodyEm * 2.0f * kBase.bodyLineHeight,
                0.02f);
}

TEST(RendererMac, MeasuresTablesTheWindowsWay) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    // A wrapping cell in the second column of the second row: the
    // columns are equal, and the rows are as tall as their tallest
    // cell.
    const Document doc = Doc({Table({
        {"a", "b"},
        {"c", std::string(120, 'x')},
    })});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& table = renderer.blocks()[0];
    ASSERT_TRUE(table.table != nullptr);

    const float colW = renderer.contentWidth() / 2.0f;
    EXPECT_NEAR(table.tableLayout.colW, colW, 0.01f);
    EXPECT_NEAR(table.width, renderer.contentWidth(), 0.01f);
    ASSERT_EQ(table.tableLayout.rowHeights.size(), 2u);

    // The cells were measured against the column width less GitHub's
    // 13px side padding.
    const float innerW = colW - 2.0f * kBase.cellPadX;
    bool sawInner = false;
    for (const FakeEngine::Call& call : engine.calls) {
        if (std::fabs(call.maxWidth - innerW) < 0.01f) sawInner = true;
    }
    EXPECT_TRUE(sawInner);

    const float lineH = kBodyEm * kBase.bodyLineHeight;
    EXPECT_NEAR(table.tableLayout.rowHeights[0],
                lineH + 2.0f * kBase.cellPadY, 0.02f);
    EXPECT_GT(table.tableLayout.rowHeights[1], table.tableLayout.rowHeights[0]);
    EXPECT_NEAR(table.height,
                table.tableLayout.rowHeights[0] + table.tableLayout.rowHeights[1],
                0.01f);
}

TEST(RendererMac, PaintsTheGitHubTableGrid) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    RecordingTarget target;
    renderer.SetRenderTarget(&target);

    const Document doc = Doc({Table({
        {"h1", "h2"},
        {"a", "b"},
    })});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    renderer.Render(doc, 0.0f);

    const LaidOutBlock& table = renderer.blocks()[0];
    ASSERT_EQ(table.tableLayout.rowHeights.size(), 2u);
    const Palette pal = BasePalette();

    int headerFills = 0, altFills = 0, borderFills = 0;
    for (const RecordingTarget::Fill& fill : target.fills) {
        if (SameColor(fill.color, pal.tableHeaderBg)) ++headerFills;
        if (SameColor(fill.color, pal.tableRowAlt)) ++altFills;
        if (SameColor(fill.color, pal.tableBorder)) ++borderFills;
    }
    // GitHub's header is the page's paper, and the stripe begins on
    // the first body row.
    EXPECT_EQ(headerFills, 0);
    EXPECT_EQ(altFills, 1);
    // Two columns: three vertical edges on each of the two rows, plus
    // the table's top edge and each row's bottom edge.
    EXPECT_EQ(borderFills, 9);

    bool sawColumnEdge = false;
    for (const RecordingTarget::Fill& fill : target.fills) {
        if (!SameColor(fill.color, pal.tableBorder)) continue;
        if (std::fabs(fill.rect.x -
                      (table.x + table.tableLayout.colW)) < 0.01f &&
            std::fabs(fill.rect.height -
                      table.tableLayout.rowHeights[0]) < 0.01f) {
            sawColumnEdge = true;
        }
    }
    EXPECT_TRUE(sawColumnEdge);
}

TEST(RendererMac, GivesListsTheMarkerGutter) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    RecordingTarget target;
    renderer.SetRenderTarget(&target);

    const Document doc = Doc({
        ListItem(true, 0, "first"),
        ListItem(true, 0, "second"),
        ListItem(true, 0, "third"),
    });
    renderer.Measure(doc, 1400.0f);
    const std::vector<LaidOutBlock>& blocks = renderer.blocks();
    ASSERT_EQ(blocks.size(), 3u);

    // The marker sits at 1.25em; the text edge is that plus the 1em
    // marker box the renderer reserves. Both numbers come from the
    // theme, not from a copy of them.
    const float markerW = kBase.listIndent * 0.6f + kBase.markerGutter;
    const float originX = kPadX;
    EXPECT_NEAR(blocks[0].x, originX + kBase.listIndent, 0.01f);
    ASSERT_TRUE(!engine.calls.empty());
    EXPECT_NEAR(engine.calls[0].maxWidth,
                renderer.contentWidth() - kBase.listIndent - markerW, 0.01f);

    renderer.Render(doc, 0.0f);
    bool sawFirst = false, sawSecond = false, sawThird = false;
    for (const RecordingTarget::Text& text : target.texts) {
        if (text.text == "1. " &&
            std::fabs(text.x - (originX + kBase.listIndent)) < 0.01f) {
            sawFirst = true;
        }
        if (text.text == "2. ") sawSecond = true;
        if (text.text == "3. ") sawThird = true;
    }
    EXPECT_TRUE(sawFirst);
    EXPECT_TRUE(sawSecond);
    EXPECT_TRUE(sawThird);

    // The count restarts when the list is interrupted.
    target.texts.clear();
    const Document split = Doc({
        ListItem(true, 0, "one"),
        Para("break"),
        ListItem(true, 0, "two"),
    });
    renderer.Measure(split, 1400.0f);
    renderer.Render(split, 0.0f);
    int ones = 0;
    for (const RecordingTarget::Text& text : target.texts) {
        if (text.text == "1. ") ++ones;
    }
    EXPECT_EQ(ones, 2);
}

TEST(RendererMac, PaintsTheHeadingRuleTheBarAndTheBreak) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    RecordingTarget target;
    renderer.SetRenderTarget(&target);

    Node quote = Para("quote");
    quote.block = BlockKind::BlockQuote;
    Node breaking;
    breaking.block = BlockKind::ThematicBreak;

    const Document doc =
        Doc({Heading(1, "Title"), quote, breaking, Para("tail")});
    renderer.Measure(doc, 1400.0f);
    const std::vector<LaidOutBlock>& blocks = renderer.blocks();
    ASSERT_EQ(blocks.size(), 4u);
    renderer.Render(doc, 0.0f);

    // The H1 wears GitHub's hairline, .3em under the text.
    const float ruleY = blocks[0].y + kH1Em * kBase.headingLineHeight + 0.3f * kH1Em;
    bool sawRule = false;
    for (const RecordingTarget::Fill& fill : target.fills) {
        if (std::fabs(fill.rect.y - ruleY) < 0.02f &&
            std::fabs(fill.rect.x - blocks[0].x) < 0.01f &&
            std::fabs(fill.rect.width - blocks[0].width) < 0.01f &&
            std::fabs(fill.rect.height - 1.0f) < 0.01f) {
            sawRule = true;
        }
    }
    EXPECT_TRUE(sawRule);

    // The quote bar: centered four points left of the indented text,
    // as tall as the text.
    const float barX = blocks[1].x - 20.0f + 4.0f - 2.0f;
    bool sawBar = false;
    for (const RecordingTarget::Fill& fill : target.fills) {
        if (std::fabs(fill.rect.x - barX) < 0.02f &&
            std::fabs(fill.rect.width - 4.0f) < 0.01f &&
            std::fabs(fill.rect.height - kBodyEm * kBase.bodyLineHeight) < 0.02f) {
            sawBar = true;
        }
    }
    EXPECT_TRUE(sawBar);

    // The thematic break: GitHub's 4px bar at the top of its block.
    EXPECT_NEAR(blocks[2].height, 4.0f, 0.01f);
    bool sawBreak = false;
    for (const RecordingTarget::Fill& fill : target.fills) {
        if (std::fabs(fill.rect.y - blocks[2].y) < 0.02f &&
            std::fabs(fill.rect.height - 4.0f) < 0.01f) {
            sawBreak = true;
        }
    }
    EXPECT_TRUE(sawBreak);
}

TEST(RendererMac, FillsTheInlineCodeBackground) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    RecordingTarget target;
    renderer.SetRenderTarget(&target);

    Node para;
    para.block = BlockKind::Paragraph;
    InlineBlock before;
    before.kind = InlineKind::Text;
    before.text = Utf32("text ");
    before.srcOffset = 0;
    before.srcLength = 5;
    InlineBlock span;
    span.kind = InlineKind::Text;
    span.text = Utf32("code");
    span.code = true;
    // The parser hands the content of a code span over, without the
    // backticks, so its source range is the four characters.
    span.srcOffset = 6;
    span.srcLength = 4;
    InlineBlock after;
    after.kind = InlineKind::Text;
    after.text = Utf32(" tail");
    after.srcOffset = 11;
    after.srcLength = 5;
    para.children = {before, span, after};

    const Document doc = Doc({para});
    renderer.Measure(doc, 1400.0f);
    renderer.Render(doc, 0.0f);

    // The fill behind the span: the four characters of the span plus
    // GitHub's .4em of code padding on either side.
    const float charW = kBodyEm * 0.5f;
    const float expectedWidth = 4.0f * charW + 2.0f * 0.4f * kCodeEm;
    bool sawBackground = false;
    for (const RecordingTarget::Fill& fill : target.fills) {
        if (std::fabs(fill.rect.width - expectedWidth) < 0.05f) {
            sawBackground = true;
        }
    }
    EXPECT_TRUE(sawBackground);
}

TEST(RendererMac, FillsTheInlineCodeBackgroundOnEveryWrappedLine) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    RecordingTarget target;
    renderer.SetRenderTarget(&target);

    const std::string codeText =
        "an_inline_code_span_that_is_long_enough_to_wrap_over_two_lines";
    Node para;
    para.block = BlockKind::Paragraph;
    InlineBlock before;
    before.kind = InlineKind::Text;
    before.text = Utf32("before ");
    before.srcOffset = 0;
    before.srcLength = 7;
    InlineBlock span;
    span.kind = InlineKind::Text;
    span.text = Utf32(codeText);
    span.code = true;
    span.srcOffset = 7;
    span.srcLength = static_cast<uint32_t>(codeText.size());
    InlineBlock after;
    after.kind = InlineKind::Text;
    after.text = Utf32(" after");
    after.srcOffset = 7 + static_cast<uint32_t>(codeText.size());
    after.srcLength = 6;
    para.children = {before, span, after};

    const Document doc = Doc({para});
    renderer.Measure(doc, 240.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    renderer.Render(doc, 0.0f);

    std::vector<const RecordingTarget::Fill*> backgrounds;
    for (const RecordingTarget::Fill& fill : target.fills) {
        if (SameColor(fill.color, BasePalette().inlineCodeBg)) {
            backgrounds.push_back(&fill);
        }
    }
    // The span is wider than the measure, so the fill has to be split
    // across the lines the engine wrapped it onto.
    EXPECT_TRUE(backgrounds.size() >= 2u);
    if (backgrounds.size() >= 2u) {
        EXPECT_TRUE(backgrounds[1]->rect.y > backgrounds[0]->rect.y);
        // The run has one outer left cap and one outer right cap; a
        // cap at every line edge was the artefact at line starts and
        // ends.
        EXPECT_TRUE(backgrounds.front()->roundLeft);
        EXPECT_FALSE(backgrounds.front()->roundRight);
        EXPECT_FALSE(backgrounds.back()->roundLeft);
        EXPECT_TRUE(backgrounds.back()->roundRight);
    }
}

TEST(RendererMac, FillsCodeBackgroundInsideATableCell) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    RecordingTarget target;
    renderer.SetRenderTarget(&target);

    Document doc;
    ParseMarkdown(std::string("| head | text |\n"
                              "| --- | --- |\n"
                              "| `code span` | plain |\n"), doc);
    renderer.Measure(doc, 1400.0f);
    renderer.Render(doc, 0.0f);

    // A code span inside a cell has the same wash as one in a
    // paragraph; the table painter used to draw only the text runs.
    bool sawBackground = false;
    for (const RecordingTarget::Fill& fill : target.fills) {
        if (SameColor(fill.color, BasePalette().inlineCodeBg)) {
            sawBackground = true;
        }
    }
    EXPECT_TRUE(sawBackground);
}

TEST(RendererMac, PaintsTheCodeCardWithoutABorder) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    RecordingTarget target;
    renderer.SetRenderTarget(&target);

    const Document doc = Doc({Code("x = 1")});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    renderer.Render(doc, 0.0f);

    // GitHub's fenced block is a borderless 6px card.
    EXPECT_TRUE(target.strokes.empty());
    bool sawCard = false;
    for (const RecordingTarget::Fill& fill : target.fills) {
        if (SameColor(fill.color, BasePalette().codeBg) &&
            std::fabs(fill.radius - 6.0f) < 0.01f &&
            std::fabs(fill.rect.height - renderer.blocks()[0].height) < 0.01f) {
            sawCard = true;
        }
    }
    EXPECT_TRUE(sawCard);
}

TEST(RendererMac, UnderlinesLinks) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    RecordingTarget target;
    renderer.SetRenderTarget(&target);

    Node para;
    para.block = BlockKind::Paragraph;
    InlineBlock before;
    before.kind = InlineKind::Text;
    before.text = Utf32("go ");
    InlineBlock link;
    link.kind = InlineKind::Link;
    link.text = Utf32("here");
    link.url = "https://example.invalid/";
    InlineBlock after;
    after.kind = InlineKind::Text;
    after.text = Utf32(" now");
    para.children = {before, link, after};

    const Document doc = Doc({para});
    renderer.Measure(doc, 1400.0f);
    renderer.Render(doc, 0.0f);

    // The underline runs under the link, from its own characters, two
    // points under the baseline.
    const float charW = kBodyEm * 0.5f;
    const float columnX = kPadX;
    const float underlineY =
        32.0f + (kBodyEm * kBase.bodyLineHeight - kBodyEm) * 0.5f + kBodyEm + 2.0f;
    bool sawUnderline = false;
    for (const RecordingTarget::Stroke& stroke : target.strokes) {
        if (std::fabs(stroke.y0 - underlineY) < 0.02f &&
            std::fabs(stroke.x0 - (columnX + 3.0f * charW)) < 0.02f &&
            std::fabs(stroke.x1 - (columnX + 7.0f * charW)) < 0.02f) {
            sawUnderline = true;
        }
    }
    EXPECT_TRUE(sawUnderline);
}

TEST(RendererMac, DrawsImagesAboveTheirParagraph) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    RecordingTarget target;
    renderer.SetRenderTarget(&target);

    Node para;
    para.block = BlockKind::Paragraph;
    InlineBlock before;
    before.kind = InlineKind::Text;
    before.text = Utf32("text ");
    InlineBlock image;
    image.kind = InlineKind::Image;
    image.url = "no-such-file.png";
    InlineBlock after;
    after.kind = InlineKind::Text;
    after.text = Utf32(" after");
    para.children = {before, image, after};

    const Document doc = Doc({para});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& block = renderer.blocks()[0];

    // The image keeps its reserved room above the text: the fallback
    // height and the paragraph gap the Windows layout charges.
    EXPECT_NEAR(block.imagesHeight, 100.0f + 16.0f, 0.01f);

    renderer.Render(doc, 0.0f);
    bool sawText = false;
    for (const RecordingTarget::Text& text : target.texts) {
        if (text.text.find("text") != std::string::npos &&
            text.baseline > block.y + block.imagesHeight) {
            sawText = true;
        }
    }
    EXPECT_TRUE(sawText);

    // The hit tester puts the caret in the text, not in the image
    // slot.
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    EXPECT_TRUE(renderer.OffsetToRect(0, &x, &y, &w, &h));
    EXPECT_GE(y, block.y + block.imagesHeight - 0.01f);
}

TEST(RendererMac, KeepsACaretLineForABlankParagraph) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    Node blank;
    blank.block = BlockKind::Paragraph;
    blank.virtualEmptyParagraph = true;

    const Document doc = Doc({blank});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);

    // The blank paragraph keeps one line, so the caret has a place to
    // sit.
    const LaidOutBlock& block = renderer.blocks()[0];
    ASSERT_EQ(block.lines.size(), 1u);
    EXPECT_NEAR(block.height, kBodyEm * kBase.bodyLineHeight, 0.02f);
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    EXPECT_TRUE(renderer.OffsetToRect(0, &x, &y, &w, &h));
    EXPECT_NEAR(h, kBodyEm * kBase.bodyLineHeight, 0.02f);
}

TEST(RendererMac, GivesAnEmptyParagraphNoHeight) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    Node blank;
    blank.block = BlockKind::Paragraph;

    const Document doc = Doc({blank});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    EXPECT_NEAR(renderer.blocks()[0].height, 0.0f, 0.01f);
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    EXPECT_TRUE(!renderer.OffsetToRect(0, &x, &y, &w, &h));
}

// --- The caret model ---------------------------------------------------
//
// The Windows app gets its caret rules from the shared LayoutCache; the
// mac renderer carries its own source-to-rendered map and the same
// rules on top of it. These tests pin the map, the marker rules, the
// hit test and the steps.

// A paragraph whose inline children carry their source offsets, the
// way the parser hands them over: "a **b** c".
Node MarkedPara() {
    Node node;
    node.block = BlockKind::Paragraph;
    node.srcOffset = 0;
    node.srcLength = 9;
    node.contentOffset = 0;
    node.contentLength = 9;

    InlineBlock first;
    first.kind = InlineKind::Text;
    first.text = Utf32("a ");
    first.srcOffset = 0;
    first.srcLength = 2;
    node.children.push_back(first);

    InlineBlock strong;
    strong.kind = InlineKind::Strong;
    strong.strong = true;
    strong.text = Utf32("b");
    strong.srcOffset = 4;
    strong.srcLength = 1;
    node.children.push_back(strong);

    InlineBlock rest;
    rest.kind = InlineKind::Text;
    rest.text = Utf32(" c");
    rest.srcOffset = 7;
    rest.srcLength = 2;
    node.children.push_back(rest);
    return node;
}

TEST(RendererMac, MapsTheRenderedTextBackToTheSource) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    const std::string source = "a **b** c";
    renderer.SetSourceText(&source);

    const Document doc = Doc({MarkedPara()});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);

    // Only the drawn bytes are rendered positions; the markers are the
    // gaps.
    EXPECT_TRUE(renderer.OffsetIsRendered(0u));
    EXPECT_TRUE(renderer.OffsetIsRendered(4u));
    EXPECT_TRUE(renderer.OffsetIsRendered(7u));
    EXPECT_TRUE(!renderer.OffsetIsRendered(2u));
    EXPECT_TRUE(!renderer.OffsetIsRendered(3u));

    // A position at the visual end of the bold text stays outside the
    // closing markers.
    EXPECT_EQ(renderer.NormalizeToRenderedCaret(2u), 4u);
    EXPECT_EQ(renderer.NormalizeToRenderedCaret(5u), 7u);
    // A position between the marker bytes snaps to the nearer rendered
    // end.
    EXPECT_EQ(renderer.NormalizeToRenderedCaret(3u), 2u);
    EXPECT_EQ(renderer.NormalizeToRenderedCaret(6u), 5u);
}

TEST(RendererMac, ClipsRangesToTheDrawnText) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    const std::string source = "a **b** c";
    renderer.SetSourceText(&source);

    const Document doc = Doc({MarkedPara()});
    renderer.Measure(doc, 1400.0f);

    // A range that covers the marked text clips down to the drawn
    // characters.
    uint32_t clipStart = 0;
    uint32_t clipEnd = 0;
    EXPECT_TRUE(renderer.ClipToRendered(2u, 5u, &clipStart, &clipEnd));
    EXPECT_EQ(clipStart, 4u);
    EXPECT_EQ(clipEnd, 5u);
    // A range that touches nothing drawn has no clip at all.
    EXPECT_TRUE(!renderer.ClipToRendered(2u, 2u, &clipStart, &clipEnd));
    // The whole paragraph, markers included, clips to the whole drawn
    // text.
    EXPECT_TRUE(renderer.ClipToRendered(0u, 9u, &clipStart, &clipEnd));
    EXPECT_EQ(clipStart, 0u);
    EXPECT_EQ(clipEnd, 9u);

    // A plain drawn word is fully rendered; the marked text is not.
    EXPECT_TRUE(renderer.RangeIsRendered(4u, 1u));
    EXPECT_TRUE(!renderer.RangeIsRendered(0u, 9u));
}

TEST(RendererMac, StepsSkipTheHiddenMarkers) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());
    const std::string source = "a **b** c";
    renderer.SetSourceText(&source);

    const Document doc = Doc({MarkedPara()});
    renderer.Measure(doc, 1400.0f);

    // One step to the right from 'b' crosses the closing markers and
    // lands on the text after them.
    EXPECT_EQ(renderer.StepGrapheme(4u, false), 7u);
    // One step to the left lands before the space.
    EXPECT_EQ(renderer.StepGrapheme(4u, true), 1u);
    // From the end of the paragraph, a step left lands before 'c'.
    EXPECT_EQ(renderer.StepGrapheme(9u, true), 8u);
    // The right at the end of the document stays put.
    EXPECT_EQ(renderer.StepGrapheme(9u, false), 9u);
    // A word step shares the boundaries of the shared navigation.
    EXPECT_EQ(renderer.StepWord(9u, true), 8u);
}

TEST(RendererMac, PointToOffsetIsExactBetweenCharacters) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    const Document doc = Doc({Para("ab")});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& block = renderer.blocks()[0];
    ASSERT_EQ(block.lines.size(), 1u);

    // The character grid of the fake engine: half an em per character.
    const float charW = kBodyEm * 0.5f;
    const float midY = block.y + block.lines[0].height * 0.5f;

    uint32_t offset = 0;
    // Between the two characters.
    EXPECT_TRUE(renderer.PointToOffset(block.x + charW * 1.2f, midY, &offset));
    EXPECT_EQ(offset, 1u);
    // Before the first character.
    EXPECT_TRUE(renderer.PointToOffset(block.x + charW * 0.1f, midY, &offset));
    EXPECT_EQ(offset, 0u);
    // Past the right end of the line: its last insertion position, the
    // paragraph end.
    EXPECT_TRUE(renderer.PointToOffset(block.x + charW * 9.0f, midY, &offset));
    EXPECT_EQ(offset, 2u);
    // The left margin of the column is not a caret; the caller keeps
    // the line selection.
    EXPECT_TRUE(!renderer.PointToOffset(block.x - 20.0f, midY, &offset));
    EXPECT_EQ(renderer.HitTestPoint(block.x - 20.0f, midY), UINT32_MAX);
}

TEST(RendererMac, WrappedLineClicksLandOnTheLine) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    // A pane narrow enough that the padding has slid to its minimum:
    // the column that is left wraps the word.
    const Document doc = Doc({Para("abcdef")});
    renderer.Measure(doc, 60.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& block = renderer.blocks()[0];
    ASSERT_EQ(block.lines.size(), 2u);
    const float mid1 = block.y + block.lines[0].height * 0.5f;
    const float mid2 =
        block.y + block.lines[0].height + block.lines[1].height * 0.5f;

    uint32_t offset = 0;
    // Far right on the first line: the end of that line, not of the
    // paragraph.
    EXPECT_TRUE(renderer.PointToOffset(block.x + 200.0f, mid1, &offset));
    EXPECT_EQ(offset, 3u);
    // The second line ends the paragraph.
    EXPECT_TRUE(renderer.PointToOffset(block.x + 200.0f, mid2, &offset));
    EXPECT_EQ(offset, 6u);
}

TEST(RendererMac, ListClicksAndCaretRectsUseTheMarkerGutter) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    Node item = ListItem(false, 0, "item");
    item.contentOffset = 2;
    item.contentLength = 4;
    item.children[0].srcOffset = 2;
    item.children[0].srcLength = 4;

    const Document doc = Doc({item});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& block = renderer.blocks()[0];
    ASSERT_EQ(block.lines.size(), 1u);

    const float markerW =
        BaseMetrics().listIndent * 0.6f + BaseMetrics().markerGutter;

    // A caret rect on the item text starts after the marker gutter.
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    EXPECT_TRUE(renderer.OffsetToRect(2, &x, &y, &w, &h));
    EXPECT_NEAR(x, block.x + markerW, 0.01f);

    // A click on the marker gutter is a caret at the start of the
    // item's text, never on the bullet.
    uint32_t offset = 0;
    EXPECT_TRUE(renderer.PointToOffset(
        block.x + 2.0f, block.y + block.lines[0].height * 0.5f, &offset));
    EXPECT_EQ(offset, 2u);
}

TEST(RendererMac, TableClicksLandInTheClickedCell) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    Node table = Table({{"A", "B"}, {"C", "D"}});
    uint32_t src = 0;
    for (TableRow& row : table.rows) {
        for (TableCell& cell : row.cells) {
            cell.srcOffset = src;
            cell.srcEnd = src + 1;
            cell.u16ToSrc = {src};
            cell.u16ToSrcEnd = {src + 1};
            ++src;
        }
    }

    const Document doc = Doc({table});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& block = renderer.blocks()[0];
    ASSERT_EQ(block.tableLayout.rows.size(), 2u);

    const float colW = block.tableLayout.colW;
    const float rowH = block.tableLayout.rowHeights[0];
    const float midY = block.y + rowH * 0.5f;

    // The second cell of the first row owns the source byte 1.
    uint32_t offset = 0;
    EXPECT_TRUE(renderer.PointToOffset(
        block.x + colW + BaseMetrics().cellPadX + 2.0f, midY, &offset));
    EXPECT_EQ(offset, 1u);

    // The fourth cell owns the source byte 3.
    const float midY2 =
        block.y + rowH + block.tableLayout.rowHeights[1] * 0.5f;
    EXPECT_TRUE(renderer.PointToOffset(
        block.x + colW + BaseMetrics().cellPadX + 2.0f, midY2, &offset));
    EXPECT_EQ(offset, 3u);
}

// Regression (empty-cell click): an empty cell renders no text, so the
// nearest drawn span lives in a different cell. A click must activate
// the clicked cell itself: the caret parks at the cell's own content
// start and has a rect to draw at (table guideline 7).
TEST(RendererMac, EmptyTableCellClickKeepsCaretInClickedCell) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    Node table = Table({{"A"}, {""}});
    table.rows[0].cells[0].srcOffset = 2;
    table.rows[0].cells[0].srcEnd = 3;
    table.rows[0].cells[0].u16ToSrc = {2};
    table.rows[0].cells[0].u16ToSrcEnd = {3};
    table.rows[1].cells[0].srcOffset = 24;
    table.rows[1].cells[0].srcEnd = 24;

    const Document doc = Doc({table});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& block = renderer.blocks()[0];
    ASSERT_EQ(block.tableLayout.rows.size(), 2u);

    const float emptyY =
        block.y + block.tableLayout.rowHeights[0] +
        block.tableLayout.rowHeights[1] * 0.5f;
    uint32_t offset = 0;
    EXPECT_TRUE(renderer.PointToOffset(
        block.x + BaseMetrics().cellPadX + 2.0f, emptyY, &offset));
    EXPECT_EQ(offset, 24u);
    EXPECT_EQ(renderer.NormalizeToRenderedCaret(24u), 24u);

    // Cursor visible: the empty cell has a rect to draw the caret at.
    float rx = 0.0f, ry = 0.0f, rw = 0.0f, rh = 0.0f;
    EXPECT_TRUE(renderer.OffsetToRect(24u, &rx, &ry, &rw, &rh));
}

// Regression (blank paragraph click): a blank line between
// paragraphs is a node of its own, and a click on its band must park
// the caret there, on the placeholder line, not in a neighbour.
TEST(RendererMac, BlankParagraphClickKeepsTheCaretOnTheLine) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    Node blank;
    blank.block = BlockKind::Paragraph;
    blank.virtualEmptyParagraph = true;
    blank.contentOffset = 7;

    const Document doc = Doc({Para("First"), blank, Para("Second")});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 3u);
    const LaidOutBlock& block = renderer.blocks()[1];
    ASSERT_TRUE(!block.lines.empty());

    uint32_t offset = 0xFFFFFFFFu;
    EXPECT_TRUE(renderer.PointToOffset(
        block.x + 30.0f, block.y + block.lines[0].height * 0.5f, &offset));
    EXPECT_EQ(offset, 7u);
    EXPECT_EQ(renderer.NormalizeToRenderedCaret(offset), 7u);
}


TEST(RendererMac, SourceModeDrawsTheRawBufferWithEveryByteAddressable) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    // The find guide's view-modes clause (4): the raw buffer, markers
    // and all, is what the source view draws.
    const std::string source = "# Title\n\nA **bold** line.\n";
    renderer.SetSourceText(&source);
    renderer.SetSourceMode(true);

    // The document is deliberately empty: the raw buffer replaces it.
    const Document empty;
    renderer.Measure(empty, 600.0f);

    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& block = renderer.blocks()[0];
    EXPECT_EQ(block.lines.size(), 3u);

    // Every byte of the buffer is drawn, so the map to the source is
    // the identity over the whole buffer, with the end sentinel the
    // mapping always carries.
    EXPECT_EQ(block.visToSrc.size(), source.size() + 1);
    EXPECT_EQ(block.visToSrc.front(), 0u);
    EXPECT_EQ(block.visToSrc.back(),
              static_cast<uint32_t>(source.size()));

    // The find scope accepts raw bytes here: the '#' and the '**'
    // markers are text on screen.
    EXPECT_TRUE(renderer.RangeIsRendered(0, 1));
    EXPECT_TRUE(renderer.RangeIsRendered(11, 2));

    // Nothing is stepped over in the source view, so the caret
    // normalization keeps every offset exactly where it is.
    const uint32_t samples[] = {0u, 7u, 11u, 17u, 25u};
    for (uint32_t at : samples) {
        EXPECT_EQ(renderer.NormalizeToRenderedCaret(at), at);
    }

    // Switching back returns to the document layout.
    renderer.SetSourceMode(false);
    const Document doc = Doc({Para("hello")});
    renderer.Measure(doc, 600.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    EXPECT_EQ(renderer.blocks()[0].visToSrc.size(), 6u);
}

// A find jump into a table: the offsets of a cell live in the cell's
// own rendered map, so the jump's y has to come from the cell's region.
// Before this the table match had no y at all and the jump went
// nowhere.
TEST(RendererMac, YForOffsetFindsTextInsideTableCells) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    Node table = Table({{"A", "B"}, {"C", "D"}});
    uint32_t src = 0;
    for (TableRow& row : table.rows) {
        for (TableCell& cell : row.cells) {
            cell.srcOffset = src;
            cell.srcEnd = src + 1;
            cell.u16ToSrc = {src};
            cell.u16ToSrcEnd = {src + 1};
            ++src;
        }
    }

    const Document doc = Doc({table});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& block = renderer.blocks()[0];
    ASSERT_EQ(block.tableLayout.rows.size(), 2u);

    // Byte 0 is the first cell of the first row.
    float y = -1.0f;
    ASSERT_TRUE(renderer.YForOffset(0, y));
    EXPECT_TRUE(std::fabs(y - (block.y + BaseMetrics().cellPadY)) < 0.5f);

    // Byte 2 opens the second row, so the jump lands one row down.
    const float rowTwo = block.y + block.tableLayout.rowHeights[0];
    y = -1.0f;
    ASSERT_TRUE(renderer.YForOffset(2, y));
    EXPECT_TRUE(std::fabs(y - (rowTwo + BaseMetrics().cellPadY)) < 0.5f);
}

// A mark or a selection in a table cell must sit on the drawn line, and
// a cell draws its line at the column alignment. The range rectangles
// read the same alignment, so the highlight is not stuck at the left
// padding (highlighting spec 8).
TEST(RendererMac, RangeRectsFollowTheCellAlignment) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    Node table = Table({{"A", "B"}});
    table.aligns = {ColumnAlign::Center, ColumnAlign::Right};
    uint32_t src = 0;
    for (TableRow& row : table.rows) {
        for (TableCell& cell : row.cells) {
            cell.srcOffset = src;
            cell.srcEnd = src + 1;
            cell.u16ToSrc = {src};
            cell.u16ToSrcEnd = {src + 1};
            ++src;
        }
    }

    const Document doc = Doc({table});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    const LaidOutBlock& block = renderer.blocks()[0];

    const float leftEdge = block.x + BaseMetrics().cellPadX;
    const float innerW = block.tableLayout.colW - 2.0f * BaseMetrics().cellPadX;

    // The centered cell: the rect is pushed toward the middle.
    const std::vector<Rect> centered = renderer.SelectionRects(0, 1);
    ASSERT_EQ(centered.size(), 1u);
    EXPECT_TRUE(centered[0].x > leftEdge + 1.0f);

    // The right-aligned cell: the rect ends at the right edge of its
    // own column's line box, inside the padding.
    const std::vector<Rect> right = renderer.SelectionRects(1, 2);
    ASSERT_EQ(right.size(), 1u);
    const float secondLeftEdge = leftEdge + block.tableLayout.colW;
    EXPECT_TRUE(std::fabs((right[0].x + right[0].width) -
                          (secondLeftEdge + innerW)) < 1.0f);
}

// The laid-out runs carry the palette's colours inside them, so a
// theme change must lay the document out again before it can draw in
// the new appearance: dark mode once kept the light theme's dark ink
// on the dark page.
TEST(RendererMac, RecoloursTheRunsWhenTheThemeChanges) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    const Document doc = Doc({Para("one")});
    renderer.Measure(doc, 1400.0f);
    ASSERT_EQ(renderer.blocks().size(), 1u);
    ASSERT_EQ(renderer.blocks()[0].lines.size(), 1u);
    const TextLine& light = renderer.blocks()[0].lines[0];
    ASSERT_EQ(light.runs.size(), 1u);
    EXPECT_TRUE(light.runs[0].r < 0.3f);  // the light theme's dark ink

    Palette dark = BasePalette();
    dark.textPrimary = ThemeColor{0.9f, 0.9f, 0.9f, 1.0f};
    renderer.SetTheme(BaseMetrics(), dark);
    renderer.Measure(doc, 1400.0f);
    const TextLine& fresh = renderer.blocks()[0].lines[0];
    ASSERT_EQ(fresh.runs.size(), 1u);
    EXPECT_TRUE(fresh.runs[0].r > 0.8f);  // the new appearance's ink
}

// A blank line next to a list is a caret paragraph in the model (the
// word-processor guidelines keep it addressable). While reading it must
// not open a full empty row, so the mode decides its height.
TEST(RendererMac, CollapsesTheBlankLineWhileReading) {
    FakeEngine engine;
    MacRenderer renderer;
    renderer.SetTextEngine(&engine);
    renderer.SetTheme(BaseMetrics(), BasePalette());

    Node heading = Para("H");
    heading.block = BlockKind::Heading;
    heading.level = 2;
    Node blank;
    blank.block = BlockKind::Paragraph;
    blank.virtualEmptyParagraph = true;
    const Document doc = Doc({heading, blank, ListItem(false, 0, "i")});

    renderer.SetEditing(true);
    const float editing = renderer.Measure(doc, 1400.0f);
    renderer.SetEditing(false);
    const float reading = renderer.Measure(doc, 1400.0f);

    // The blank line keeps its line either way, one strip shorter
    // while reading: more than the strip, less than a full line.
    EXPECT_TRUE(editing - reading > 4.0f);
    EXPECT_TRUE(editing - reading < 40.0f);
}
