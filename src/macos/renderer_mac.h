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
    // The images of the block, drawn under its text.
    std::vector<std::string> images;
};

// The mac renderer.
class MacRenderer {
public:
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
    // The mermaid diagrams follow the zoom, the way they do on Windows.
    // The rest of the document follows the theme metrics, so the caller
    // has to scale those as well.
    void SetZoom(float zoom) { zoom_ = zoom; }

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

    // The theme. The mac theme variant supplies macOS font names, the
    // shared one supplies the spacing.
    void SetTheme(const LayoutMetrics& metrics, const Palette& palette) {
        metrics_ = metrics;
        palette_ = palette;
    }

    // Lays the document out at the given width. Returns the total height.
    float Measure(const Document& doc, float widthDip);

    // Draws the laid out document.
    void Render(const Document& doc, float scrollOffset);

    // The laid out blocks, which the content view uses for hit testing.
    const std::vector<LaidOutBlock>& blocks() const { return blocks_; }

    // The document offset at a point, in document coordinates.
    uint32_t HitTestPoint(float x, float y) const;

    // The rectangle a document offset occupies, in document coordinates.
    // Returns false when the offset is not in a text block.
    bool OffsetToRect(uint32_t offset, float* outX, float* outY,
                      float* outWidth, float* outHeight) const;

    // Marks the ranges the find bar matched.
    //
    // The matches are document byte offsets, the same unit the document
    // uses, so a zoom or a scroll cannot change which text is marked.
    // A null pointer clears the marks.
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
    void ChargeForImages(LaidOutBlock* block);

    // The width the text column is capped at, in points.
    float ContentWidthLimit() const;

    // Draws the images of a block under its text.
    void PaintImages(const LaidOutBlock& block, float top, float width);

    // Paints the marks of the find bar under the text.
    void PaintSearchHighlights(float scrollOffset);

    // The rectangles a range of offsets covers.
    std::vector<Rect> RectsForRange(uint32_t start, uint32_t end) const;

    // The runs of a block's inline children, with the styles applied.
    std::vector<GlyphRun> BuildRuns(const Node& node, float baseSize,
                                     bool bold, bool italic,
                                     const Color& color,
                                     bool monospace) const;

    // The font for a run.
    FontSpec FontFor(const Node& node, float baseSize, bool bold,
                      bool italic, bool monospace) const;

    // Draws a laid out block.
    void PaintBlock(const LaidOutBlock& block, float scrollOffset);

    // Draws a table.
    void PaintTable(const LaidOutBlock& block, float scrollOffset);

    TextEngine* engine_ = nullptr;
    // The engine the mermaid diagrams are measured with. It belongs to
    // the renderer, which deletes it.
    TextEngine* diagramEngine_ = nullptr;
    RenderTarget* target_ = nullptr;

    LayoutMetrics metrics_{};
    Palette palette_{};

    std::vector<LaidOutBlock> blocks_;
    float contentWidth_ = 0.0f;
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
};

}  // namespace mac
