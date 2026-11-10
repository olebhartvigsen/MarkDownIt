// The DirectWrite engine behind the text seam, plan task 2.2. The
// macOS twin is CoreTextEngine in src/macos/text_engine_mac.cpp; the
// pair carries the same promises to their callers, see
// tests/seam_conformance_test.cpp for the grid every engine must pass.

#ifndef MARKDOWNIT_SRC_WINDOWS_TEXT_ENGINE_WIN_H_
#define MARKDOWNIT_SRC_WINDOWS_TEXT_ENGINE_WIN_H_

#include "platform/seams.h"

#include <dwrite.h>
#include <map>
#include <string>

namespace platform {

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
    mutable std::map<std::pair<std::wstring, float>,
                     IDWriteTextFormat*> formats_;
};

}  // namespace platform

#endif  // MARKDOWNIT_SRC_WINDOWS_TEXT_ENGINE_WIN_H_
