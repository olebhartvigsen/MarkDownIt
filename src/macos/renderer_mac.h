#pragma once

// The macOS renderer.
//
// The Windows renderer draws with Direct2D. This one draws with Core
// Graphics through the RenderTarget seam, and measures with Core Text
// through the TextEngine seam.
//
// The layout is the shared one: the theme metrics, the DOM structure and
// the mermaid engine are all reused, so the two platforms lay the same
// document out the same way. Only the drawing calls differ.

#include <cstdint>
#include <string>
#include <vector>

#include "dom.h"
#include "seams.h"
#include "svg_mac.h"
#include "image_mac.h"
#include "searchreplace.h"
#include "theme.h"

namespace mac {

// A laid out piece of the document. The renderer produces a list of these
// when it lays the document out, and draws them when it paints.
struct LaidOutBlock {
    // The block this came from.
    const Node* node = nullptr;
    // The y of the block's top, relative to the document.
    float y = 0.0f;
    // The height of the block.
    float height = 0.0f;
    // The x of the text column, relative to the document.
    float x = 0.0f;
    // The width of the text column.
    float width = 0.0f;
    // The lines of the block, for a text block.
    std::vector<TextLine> lines;
    // The kind of block, so the painter knows what to draw.
    BlockKind kind = BlockKind::Paragraph;
    // The heading level, for a heading.
    int level = 0;
    // The table rows, for a table.
    const Node* table = nullptr;
    // The images of the block, drawn above its text.
    std::vector<std::string> images;
    // The height the images take above the text.
    float imagesHeight = 0.0f;
    // The map from the rendered text of the block to the source.
    //
    // One entry per byte of the drawn text: the source byte the
    // character at that position starts at, in visToSrc, and the source
    // byte just past it, in visToSrcEnd. A trailing entry holds the end
    // of the last character. The Markdown markers between the inline
    // children are the gaps the map does not cover, which is what keeps
    // the caret out of them.
    std::vector<uint32_t> visToSrc;
    std::vector<uint32_t> visToSrcEnd;
    // The table cells, laid out: the lines and the row heights, so the
    // painter draws exactly what the layout measured.
    struct TableCellLayout {
        std::vector<TextLine> lines;
        ColumnAlign align = ColumnAlign::Default;
        // The cell's own rendered-to-source map, the same shape the
        // block carries, and the byte range the cell owns. A click in a
        // cell never normalizes into a different cell.
        std::vector<uint32_t> visToSrc;
        std::vector<uint32_t> visToSrcEnd;
        uint32_t srcCellStart = 0;
        uint32_t srcCellEnd = 0;
    };
    struct TableLayout {
        float colW = 0.0f;
        std::vector<float> rowHeights;
        std::vector<std::vector<TableCellLayout>> rows;
    };
    TableLayout tableLayout;
};

// The mac renderer.
class MacRenderer {
public:
    // The place of a source offset in the layout, in points. The return
    // value is false when the offset is not in the layout.
    bool YForOffset(uint32_t offset, float& y) const;

    MacRenderer();
    ~MacRenderer();

    // Sets the engine and the target the renderer draws with.
    void SetTextEngine(TextEngine* engine) { engine_ = engine; }
    void SetRenderTarget(RenderTarget* target) { target_ = target; }

    // Sets the text engine the mermaid diagrams are measured with.
    //
    // The renderer takes the engine over. The text engine keeps only
    // its last measurement, and the hit tester needs the one the layout
    // pass made, so the diagrams cannot use the document engine.
    void SetDiagramTextEngine(TextEngine* engine) { diagramEngine_ = engine; }

    // Sets the zoom of the document.
    //
    // The metrics are resolved again, so the text, the spacing and the
    // diagrams all follow the zoom, the way they do on Windows.
    void SetZoom(float zoom) {
        zoom_ = zoom;
        metrics_ = ComputeMetrics();
    }

    // Turns the word wrap on or off.
    //
    // With the wrap off the text is laid out on one very wide line and
    // the user scrolls to its end, the same way the Windows renderer
    // does it.
    void SetWrap(bool wrap) { wrap_ = wrap; }

    // Whether the text is wrapped.
    bool Wrap() const { return wrap_; }

    // Sets the width of the text column.
    //
    // The modes are the ones the settings keep: 0 is the standard
    // width, 3 is the full window width.
    void SetContentWidthMode(int mode);

    // Says whether the document is a standalone SVG file, which is
    // drawn as a picture.
    void SetStandaloneSvg(bool value) { standaloneSvg_ = value; }

    // The source text the caret normalization reads. The renderer keeps
    // the pointer, the way the shared LayoutCache does, and the App
    // owns the string.
    void SetSourceText(const std::string* text) { sourceText_ = text; }

    // The theme. The mac theme variant supplies macOS font names, the
    // shared one supplies the spacing.
    void SetTheme(const LayoutMetrics& metrics, const Palette& palette) {
        baseMetrics_ = metrics;
        metrics_ = ComputeMetrics();
        palette_ = palette;
    }

    // Lays the document out at the given width. Returns the total height.
    float Measure(const Document& doc, float widthDip);

    // Draws the laid out document.
    void Render(const Document& doc, float scrollOffset);

    // The laid out blocks, which the content view uses for hit testing.
    const std::vector<LaidOutBlock>& blocks() const { return blocks_; }

    // The document offset at a point, in document coordinates.
    //
    // The point must be on drawn text: a click in the left margin of
    // the text column has no caret and the return value is UINT32_MAX,
    // so the caller keeps the Word-like line selection.
    uint32_t HitTestPoint(float x, float y) const;

    // Where a point lands, as a source offset, under the caret rules
    // the two platforms share.
    //
    // The x decides the character, a point past the right end of a line
    // is that line's last insertion position (the paragraph-end rule),
    // a point between two blocks is the nearer line, and a point in a
    // table cell is a caret in that cell. The offset that comes back is
    // always a rendered position, never a Markdown marker. The return
    // value is false when the point is in the left margin of the text.
    bool PointToOffset(float x, float y, uint32_t* outOffset) const;

    // The nearest rendered position to a source offset: a caret never
    // sits inside a Markdown marker, and a cell owns its whole range.
    uint32_t NormalizeToRenderedCaret(uint32_t offset) const;

    // True when the source byte at the offset is drawn as text.
    bool OffsetIsRendered(uint32_t offset) const;

    // True when every byte of the range is drawn text.
    bool RangeIsRendered(uint32_t start, uint32_t length) const;

    // Tightens a range to the part of it that is drawn text. Returns
    // false when nothing in the range is rendered.
    bool ClipToRendered(uint32_t start, uint32_t length, uint32_t* outStart,
                        uint32_t* outEnd) const;

    // The source offset one grapheme cluster to the left or right. The
    // step goes through the rendered text, so a hidden marker is never
    // a caret position and the boundary of a block is a step of its
    // own.
    uint32_t StepGrapheme(uint32_t offset, bool left) const;

    // The source offset one word to the left or right, on the shared
    // word boundaries.
    uint32_t StepWord(uint32_t offset, bool left) const;

    // The first or the last source offset of the visual line the
    // offset is on.
    uint32_t LineBoundary(uint32_t offset, bool end) const;

    // The bottom of the last drawn line, in document coordinates.
    float TextBottom() const;

    // The rectangle a document offset occupies, in document coordinates.
    // Returns false when the offset is not in a text block.
    bool OffsetToRect(uint32_t offset, float* outX, float* outY,
                      float* outWidth, float* outHeight) const;

    // Marks the ranges the find bar matched.
    //
    // The matches are document byte offsets, the same unit the document
    // uses, so a zoom or a scroll cannot change which text is marked.
    // A null pointer clears the marks.
    // Paints the marked ranges behind the text.
    void PaintMarkerHighlights(float scrollOffset);

    // The text markers, the highlights the user marked. They are drawn
    // behind the text, like the search matches.
    void SetMarkers(const std::vector<TextMatch>* markers);

    void SetSearchMatches(const std::vector<TextMatch>* matches,
                          int currentIndex);

    // The rectangles a range of offsets covers, in document coordinates.
    //
    // A range that spans several lines gets one rectangle per line, the
    // way a text selection is drawn. The rectangles are empty when the
    // range is not in a text block.
    std::vector<Rect> SelectionRects(uint32_t start, uint32_t end) const;

    // The source range of the line at the point.
    //
    // The x is ignored, so a click in the left margin finds the line it
    // is level with. Returns false when the point is not on a line.
    bool LineRangeAtPoint(float x, float y, uint32_t* outStart,
                          uint32_t* outEnd) const;

    // The content width, the width of the text column.
    float contentWidth() const { return contentWidth_; }

    // True when an image of the document is still on its way.
    //
    // A remote image is fetched in the background, so the first layout
    // cannot know its size. The content view lays the document out
    // again when this turns true, so the image gets the room it needs.
    bool NeedsLayout() const { return needsLayout_; }

private:
    // Lays one block out and appends it.
    void LayoutBlock(const Node& node, float x, float width, float* y);

    // Charges the block for the room its images take and remembers them.
    // Returns the height the images take above the text.
    float ChargeForImages(LaidOutBlock* block);

    // The metrics of the current zoom and width mode, resolved the way
    // the Windows renderer resolves them.
    LayoutMetrics ComputeMetrics() const;

    // One addressable piece of drawn text: a block's own lines or one
    // of its table cells, with the geometry and the map of its text.
    struct TextRegion {
        const std::vector<TextLine>* lines = nullptr;
        const std::vector<uint32_t>* visToSrc = nullptr;
        const std::vector<uint32_t>* visToSrcEnd = nullptr;
        float x = 0.0f;      // the x of the first glyph
        float top = 0.0f;    // the y of the top of the first line
        float width = 0.0f;  // the width the text wraps at
        // The x where the region's own track begins: the margin of the
        // document column for a block, the left edge of the cell for a
        // cell. A click left of it is a Word-like line selection, and a
        // click between it and the text is still a caret in the region
        // (on a list bullet, or in cell padding).
        float trackX = 0.0f;
        // The height of the band when the region draws no lines, an
        // empty table cell that keeps its box addressable.
        float emptyBandHeight = 0.0f;
        uint32_t srcStart = 0;
        uint32_t srcEnd = 0;
        // Set when the region is a table cell, which owns its box.
        bool cell = false;
    };
    std::vector<TextRegion> TextRegions() const;

    // A rendered span of the document, in source bytes.
    struct RenderedSpan {
        uint32_t start = 0;
        uint32_t end = 0;
    };
    void CollectRenderedSpans(std::vector<RenderedSpan>* out) const;

    // The x of the first glyph of a block's lines.
    float TextX(const LaidOutBlock& block) const;

    // The y of the top of a block's text, relative to the document.
    float TextTop(const LaidOutBlock& block) const;

    // The width the text of a block wraps at.
    float TextWidth(const LaidOutBlock& block) const;

    // The rectangles a source range covers inside one region.
    void AppendRangeRects(const std::vector<TextLine>& lines,
                          const std::vector<uint32_t>& visToSrc, float x,
                          float top, uint32_t start, uint32_t end,
                          std::vector<Rect>* rects) const;

    // Draws the images of a block under its text.
    void PaintImages(const LaidOutBlock& block, float top, float width);

    // Paints the marks of the find bar under the text.
    void PaintSearchHighlights(float scrollOffset);

    // The rectangles a range of offsets covers.
    std::vector<Rect> RectsForRange(uint32_t start, uint32_t end) const;

    // The rectangles of a range inside one block.
    std::vector<Rect> RectsForRangeInBlock(const LaidOutBlock& block,
                                            uint32_t start,
                                            uint32_t end) const;

    // The runs of a block's inline children, with the styles applied.
    std::vector<GlyphRun> BuildRuns(const Node& node, float baseSize,
                                     bool bold, bool italic,
                                     const Color& color,
                                     bool monospace) const;

    // The font for a run of the document text. The theme sizes are
    // points; the font is converted to the layout unit and scaled by
    // the zoom, the same numbers the Windows renderer creates its text
    // formats with.
    FontSpec FontFor(const Node& node, float baseSize, bool bold,
                      bool italic, bool monospace) const;

    // The font for a run that lives outside the document layout, like a
    // mermaid label. Those painters scale by the zoom themselves, so
    // this font stays unzoomed.
    FontSpec DiagramFontFor(const Node& node, float baseSize, bool bold,
                             bool italic, bool monospace) const;

    // Applies the Windows line spacing to measured lines: every line is
    // as tall as heightMult times the em, and its baseline sits
    // baselineMult times the em below the top of the line.
    void ApplyLineSpacing(std::vector<TextLine>* lines, float ptSize,
                          float heightMult, float baselineMult) const;

    // The runs of a table cell, cut at the boundaries of its inline
    // spans, so a cell carries the same styles the Windows renderer
    // gives it.
    std::vector<GlyphRun> BuildCellRuns(const TableCell& cell, bool bold,
                                         const Color& color) const;

    // Draws the lines of a block, with their underlines and their
    // strike-throughs.
    void PaintLines(const std::vector<TextLine>& lines, float x,
                    float top) const;

    // Draws the runs of one line.
    void PaintLineRuns(const TextLine& line, float x, float y) const;

    // Draws the background of the inline code spans of a block, behind
    // its text.
    void PaintInlineCodeBg(const LaidOutBlock& block,
                           float scrollOffset) const;

    // Draws a laid out block. The marker is the ordered list marker for
    // a list block, and empty for every other kind.
    void PaintBlock(const LaidOutBlock& block, float scrollOffset,
                    const std::string& marker);

    // Draws a table.
    void PaintTable(const LaidOutBlock& block, float scrollOffset);

    TextEngine* engine_ = nullptr;
    // The engine the mermaid diagrams are measured with. It belongs to
    // the renderer, which deletes it.
    TextEngine* diagramEngine_ = nullptr;
    RenderTarget* target_ = nullptr;

    // The theme metrics as given, and the ones the renderer works with
    // (scaled by the zoom, with the width cap of the mode applied).
    LayoutMetrics baseMetrics_{};
    LayoutMetrics metrics_{};
    Palette palette_{};

    std::vector<LaidOutBlock> blocks_;
    float contentWidth_ = 0.0f;
    // The x of the left edge of the text column, which centers the
    // column when it is narrower than the view.
    float originX_ = 0.0f;
    float totalHeight_ = 0.0f;
    // True when an image of the document is still being fetched.
    bool needsLayout_ = false;
    // Whether the text is wrapped.
    bool wrap_ = true;
    // The width of the text column.
    int contentWidthMode_ = 0;
    // The image cache, which holds the images of the document.
    ImageCache images_;
    // The ranges the find bar matched, and the one that is current.
    std::vector<TextMatch> searchMatches_;
    std::vector<TextMatch> markers_;

    // The parsed SVG documents, one for each block that holds one.
    std::vector<std::pair<uint32_t, SvgDoc>> svgDocs_;

    // The SVG document of a block, which is read the first time the
    // block is drawn.
    const SvgDoc* GetSvgDoc(const Node& node);

    // Whether the document is a standalone SVG file.
    bool standaloneSvg_ = false;
    int searchCurrent_ = -1;
    // The zoom of the document.
    float zoom_ = 1.0f;
    // The source text, which the caret normalization reads to skip the
    // markers that follow a link. The App owns the string.
    const std::string* sourceText_ = nullptr;
};

}  // namespace mac
