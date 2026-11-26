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
    DWriteTextLayout(IDWriteTextLayout* layout, float width, float height);
    ~DWriteTextLayout() override;

    DWriteTextLayout(const DWriteTextLayout&) = delete;
    DWriteTextLayout& operator=(const DWriteTextLayout&) = delete;

    float Width() const override { return width_; }
    float Height() const override { return height_; }

    // The object itself, lent to the target that draws it. Never null for
    // a layout this engine answered.
    IDWriteTextLayout* Layout() const { return layout_; }

private:
    IDWriteTextLayout* layout_ = nullptr;
    float width_ = 0.0f;
    float height_ = 0.0f;
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

}  // namespace platform

#endif  // MARKDOWNIT_SRC_WINDOWS_TEXT_ENGINE_WIN_H_
