// The DirectWrite engine behind the text seam, plan task 2.2. The
// macOS twin is CoreTextEngine in src/macos/text_engine_mac.cpp; the
// pair carries the same promises to their callers, see
// tests/seam_conformance_test.cpp for the grid every engine must pass.

#ifndef MARKDOWNIT_SRC_WINDOWS_TEXT_ENGINE_WIN_H_
#define MARKDOWNIT_SRC_WINDOWS_TEXT_ENGINE_WIN_H_

#include "platform/seams.h"

#include <dwrite.h>
#include <map>
#include <tuple>
#include <string>

namespace platform {

// The DirectWrite layout a painter draws: the IDWriteTextLayout the engine
// built for one block, kept alive for as long as the seam's handle is.
// D2DTarget draws it through DrawTextLayout, so the words on the page are
// the words this engine measured and broke.
class DWriteTextLayout final : public TextLayout {
public:
    // One run's UTF-16 range and the ink it asked for. A range with a
    // transparent colour drew no colour of its own; the target leaves it
    // to the brush it hands DrawTextLayout.
    struct ColorSpan {
        UINT32 start = 0;
        UINT32 length = 0;
        Color color;
    };

    DWriteTextLayout(IDWriteTextLayout* layout, float width, float height,
                     std::vector<ColorSpan> colorSpans);
    ~DWriteTextLayout() override;

    DWriteTextLayout(const DWriteTextLayout&) = delete;
    DWriteTextLayout& operator=(const DWriteTextLayout&) = delete;

    float Width() const override { return width_; }
    float Height() const override { return height_; }

    // The caret's questions, answered by the DirectWrite layout itself:
    // the block keeps no seam line records, so the base class's portable
    // answer has nothing to read here.
    uint32_t LineCount() const override;
    float LineHeightAt(uint32_t lineIndex) const override;
    bool LineTextRange(uint32_t lineIndex, float layoutWidth,
                       uint32_t* outStart, uint32_t* outEnd) const override;
    TextHit HitTestPoint(float x, float y) const override;
    TextHit CaretAtOffset(uint32_t textPosition) const override;

    // The object itself, lent to the target that draws it. Never null for
    // a layout this engine answered.
    IDWriteTextLayout* Layout() const { return layout_; }

    // The run ranges and their inks, in the order the runs were given.
    const std::vector<ColorSpan>& ColorSpans() const { return colorSpans_; }

private:
    IDWriteTextLayout* layout_ = nullptr;
    float width_ = 0.0f;
    float height_ = 0.0f;
    std::vector<ColorSpan> colorSpans_;
};

class DWriteEngine final : public TextEngine {
public:
    DWriteEngine();
    ~DWriteEngine() override;

    DWriteEngine(const DWriteEngine&) = delete;
    DWriteEngine& operator=(const DWriteEngine&) = delete;

    void BeginLayout() override;

    std::vector<TextLine> MeasureWrapped(
            const std::vector<GlyphRun>& runs,
            float maxWidth, float lineSpacing) override;

    uint32_t HitTestOffset(const TextLine& line, float x,
                           bool* outTrailing) const override;

    Rect OffsetToRect(const TextLine& line,
                      uint32_t offset) const override;

    float LineHeight(const FontSpec& font) const override;

    // The font's own vertical metrics, from the system collection the
    // engine's factory owns: the family the spec names is resolved here
    // instead of in a renderer that used to walk the collection itself.
    bool FontBox(const FontSpec& font, float* natural,
                 float* ascent) const override;

    // The block the table and the paragraph painters draw: one DirectWrite
    // layout over the runs, word wrapped at the column width, aligned
    // inside it and dressed run by run with the faces and decorations the
    // runs name. A run of DrawText calls cannot say any of that.
    TextLayoutHandle BuildLayout(const std::vector<GlyphRun>& runs,
                                 float maxWidth, TextAlign align) override;

    // The factory this engine measured with, lent to a painter that
    // must draw exactly what the engine measured: the table cells
    // paint their record lines from the same faces that sized
    // them, so no second measure can disagree with the record.
    IDWriteFactory* Factory() const { return factory_; }

private:
    // A format per family and size, kept for the lifetime of the
    // engine: creating one is the expensive call in this file.
    IDWriteTextFormat* FormatFor(const FontSpec& font) const;

    // Measures one run. Returns the advance width of its text.
    float MeasureRun(const GlyphRun& run, float maxWidth) const;

    // The advance one run costs a composed line, trailing spaces
    // included: the trimmed width would glue the next run onto the
    // spaces' ground.
    float MeasureRunTrailing(const GlyphRun& run, float maxWidth) const;

    // The exact x of a UTF-16 offset inside one run's text, or the
    // trailing edge past the end when the offset is out of range.
    float PositionInRun(const GlyphRun& run, float maxWidth,
                        uint32_t utf16Offset) const;

    IDWriteFactory* factory_ = nullptr;
    // The key the next measured line carries, the seam's identity for a
    // line a reader holds a copy of.
    uint64_t nextKey_ = 0;
    using FormatKey = std::tuple<std::wstring, float, int, int>;
    mutable std::map<FormatKey, IDWriteTextFormat*> formats_;
};

// Wraps a DirectWrite layout the caller already built in the seam's
// handle, so a cache can hold the block without naming its type. The
// handle takes its own reference; the caller releases the one
// CreateTextLayout gave it.
TextLayoutHandle AdoptTextLayout(IDWriteTextLayout* layout, float width,
                                 float height);

// Builds the block the caret cache keeps for one plain text in a format
// the renderer owns: the text wrapped at `maxWidth`, dressed with the
// inline styles the ranges name, and aligned inside the column. Null when
// DirectWrite refuses the layout. The caller keeps the text and the
// format, the handle keeps the layout.
TextLayoutHandle MakeTextLayout(IDWriteFactory* factory,
                                const std::u16string& text,
                                IDWriteTextFormat* format, float maxWidth,
                                const std::vector<TextRangeStyle>& styles,
                                TextAlign align);

}  // namespace platform

#endif  // MARKDOWNIT_SRC_WINDOWS_TEXT_ENGINE_WIN_H_
