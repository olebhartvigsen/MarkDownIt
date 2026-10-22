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
};

// The mac renderer.
class MacRenderer {
public:
    MacRenderer();
    ~MacRenderer();

    // Sets the engine and the target the renderer draws with.
    void SetTextEngine(TextEngine* engine) { engine_ = engine; }
    void SetRenderTarget(RenderTarget* target) { target_ = target; }

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

    // The content width, the width of the text column.
    float contentWidth() const { return contentWidth_; }

private:
    // Lays one block out and appends it.
    void LayoutBlock(const Node& node, float x, float width, float* y);

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
    RenderTarget* target_ = nullptr;

    LayoutMetrics metrics_{};
    Palette palette_{};

    std::vector<LaidOutBlock> blocks_;
    float contentWidth_ = 0.0f;
    float totalHeight_ = 0.0f;
};

}  // namespace mac
