#pragma once

// The Core Text implementation of the mac TextEngine seam.

#include <string>
#include <vector>

#include <CoreText/CoreText.h>

#include "seams.h"

namespace mac {

// The font the engine takes for a spec, which the drawing side shares,
// so the measured metrics and the drawn glyphs come from one font.
CTFontRef CoreTextFontFor(const FontSpec& spec);

// A Core Text backed text engine.
//
// The engine measures with CTFramesetter and draws with CTLine, which is
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
    uint32_t HitTestOffset(const TextLine& line, float x,
                           bool* outTrailing) const override;
    Rect OffsetToRect(const TextLine& line, uint32_t offset) const override;
    float LineHeight(const FontSpec& font) const override;

    // The line's text as UTF-8, which the hit tester measures against.
    static std::string LineTextUtf8(const TextLine& line);

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace mac
