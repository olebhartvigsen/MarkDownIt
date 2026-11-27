#pragma once

// The Core Text implementation of the mac TextEngine seam.

#include <memory>
#include <string>
#include <vector>

#include <CoreText/CoreText.h>

#include "platform/seams.h"

namespace platform {

// The Core Text layout a painter draws: the block's lines, each one the
// CTLine the engine built (or measured) for it, with the offset the
// alignment gives the line inside the column and where the line's box
// sits in the block. CGContext draws it through CTLineDraw, so the words
// on the page are the words this engine laid out.
class CgTextLayout final : public TextLayout {
public:
    struct Line {
        // The line itself, owned by this layout.
        CTLineRef line = nullptr;
        // The left edge of the line inside the column.
        float x = 0.0f;
        // The top of the line's box, from the top of the block.
        float top = 0.0f;
        // The distance from the line's top to its baseline.
        float ascent = 0.0f;
        float height = 0.0f;
    };

    CgTextLayout(std::vector<Line> lines, float width, float height);
    ~CgTextLayout() override;

    CgTextLayout(const CgTextLayout&) = delete;
    CgTextLayout& operator=(const CgTextLayout&) = delete;

    float Width() const override { return width_; }
    float Height() const override { return height_; }

    // The caret's questions, answered from the block's own CTLines: a
    // native block keeps no seam line records, so the base class's
    // portable answer has nothing to read here.
    uint32_t LineCount() const override;
    float LineHeightAt(uint32_t lineIndex) const override;
    bool LineTextRange(uint32_t lineIndex, float layoutWidth,
                       uint32_t* outStart, uint32_t* outEnd) const override;
    TextHit HitTestPoint(float x, float y) const override;
    TextHit CaretAtOffset(uint32_t textPosition) const override;

    // The lines, for the target that draws them.
    const std::vector<Line>& DrawLines() const { return lines_; }

private:
    std::vector<Line> lines_;
    float width_ = 0.0f;
    float height_ = 0.0f;
};

// The font the engine takes for a spec, which the drawing side shares,
// so the measured metrics and the drawn glyphs come from one font.
CTFontRef CoreTextFontFor(const FontSpec& spec);

// The family the engine maps a theme family name onto ("Segoe UI" to
// the bundled Selawik, and so on). Exposed for the font tests.
std::string ResolvedFamily(const std::string& family, bool monospace);

// A Core Text backed text engine.
//
// The engine measures with CTTypesetter and draws with CTLine, which is
// the same pair the Windows side uses through DirectWrite. The mapping
// between the two is one to one: a DWRITE_LINE_METRICS line is a CTLine,
// and a DWRITE_HIT_TEST_METRICS position is a CTLineGetStringIndexForPosition
// result.
//
// The engine is stateless apart from a font cache, so one instance serves
// the whole app.
class CoreTextEngine : public TextEngine {
public:
    CoreTextEngine();
    ~CoreTextEngine() override;

    // The shared seam.
    std::vector<TextLine> MeasureWrapped(const std::vector<GlyphRun>& runs,
                                          float maxWidth,
                                          float lineSpacing) override;

    // The lines of the pass before this one are dropped, so the hit
    // tester works against the layout the renderer just made.
    void BeginLayout() override;
    uint32_t HitTestOffset(const TextLine& line, float x,
                           bool* outTrailing) const override;
    Rect OffsetToRect(const TextLine& line, uint32_t offset) const override;
    float LineHeight(const FontSpec& font) const override;

    // The block a painter draws: the lines MeasureWrapped cut, each one
    // kept as the CTLine the engine already holds for it, so the drawn
    // lines are the measured ones, with the column's alignment applied.
    TextLayoutHandle BuildLayout(const std::vector<GlyphRun>& runs,
                                 float maxWidth, TextAlign align) override;

    // The line's text as UTF-8, which the hit tester measures against.
    static std::string LineTextUtf8(const TextLine& line);

private:
    struct Impl;
    // The entry a line is measured by: its own key when it carries one,
    // and the width and the text for a line a renderer built by hand.
    // The index is the size of the entry list when nothing matches.
    std::size_t EntryIndexFor(const TextLine& line) const;
    std::unique_ptr<Impl> impl_;
};

}  // namespace platform
