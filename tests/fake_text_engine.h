// The fake text engine of plan task 2.3: a deterministic measurer for
// cross runner goldens. Every glyph is 8 DIP wide, whatever it is, and a
// line is as tall as the theme's line height ratio says (1.5 of the
// size, like bodyLineHeight in theme.h); no font, no OS, no rounding
// mode can move a golden.
//
// Offsets count UTF-8 code points, so a hit test or a caret rect is the
// same number on both runners. Offsets count UTF-8 bytes, the
// documented hit test unit of the seam: one position per byte, a
// multibyte character one glyph that advances the offset by its
// byte length.

#ifndef MARKDOWNIT_TESTS_FAKE_TEXT_ENGINE_H_
#define MARKDOWNIT_TESTS_FAKE_TEXT_ENGINE_H_

#include "platform/seams.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fake {

inline size_t CodePointCount(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

class FakeTextEngine final : public platform::TextEngine {
public:
    static constexpr float kGlyphWidth = 8.0f;

    void BeginLayout() override {}

    std::vector<platform::TextLine> MeasureWrapped(
            const std::vector<platform::GlyphRun>& runs,
            float maxWidth, float lineSpacing) override {
        (void)lineSpacing;  // spacing rides LayoutMetrics, not the engine
        std::vector<platform::TextLine> lines;
        platform::TextLine line;
        line.height = LineHeightOfWidest(runs);
        float x = 0.0f;
        for (const platform::GlyphRun& run : runs) {
            const float w = static_cast<float>(CodePointCount(run.text)) *
                            kGlyphWidth;
            // A run never breaks inside itself; a full run moves to the
            // next line when it no longer fits, and a first run stays on
            // the line even when it is too wide (no infinite loop).
            if (maxWidth > 0.0f && x > 0.0f && x + w > maxWidth) {
                line.width = x;
                lines.push_back(std::move(line));
                line = platform::TextLine();
                line.height = LineHeightOfWidest(runs);
                x = 0.0f;
            }
            platform::GlyphRun onLine = run;
            onLine.x = x;
            x += w;
            line.runs.push_back(std::move(onLine));
        }
        line.width = x;
        // An empty run list still draws one empty line, like text areas
        // do, so callers can place a caret somewhere.
        if (line.runs.empty() || x > 0.0f) lines.push_back(std::move(line));
        if (lines.empty()) {
            line = platform::TextLine();
            line.height = LineHeightOfWidest(runs);
            lines.push_back(std::move(line));
        }
        return lines;
    }

    uint32_t HitTestOffset(const platform::TextLine& line, float x,
                           bool* outTrailing) const override {
        // Bytes are the unit. A multibyte character is one glyph; the
        // hit answers the byte offset where the glyph it lands in
        // starts, and the walk never stops inside a character.
        if (outTrailing) *outTrailing = false;
        size_t i = 0;
        float acc = 0.0f;
        for (const platform::GlyphRun& run : line.runs) {
            for (unsigned char c : run.text) {
                const bool lead = (c & 0xC0) != 0x80;
                if (lead) {
                    if (acc + kGlyphWidth > x) {
                        if (outTrailing)
                            *outTrailing =
                                (x - acc) > kGlyphWidth * 0.5f;
                        return (uint32_t)i;
                    }
                    acc += kGlyphWidth;
                }
                ++i;
            }
        }
        return (uint32_t)i;
    }

    platform::Rect OffsetToRect(const platform::TextLine& line,
                                uint32_t offset) const override {
        // Walk to the byte offset across runs and take the glyph it
        // lands in. A continuation byte belongs to its character's
        // glyph, so it answers the same rect as its lead byte.
        size_t i = 0;
        float x = 0.0f;
        platform::Rect rect;
        rect.height = line.height;
        for (const platform::GlyphRun& run : line.runs) {
            for (unsigned char c : run.text) {
                const bool lead = (c & 0xC0) != 0x80;
                if (i == offset) {
                    rect.x = x;
                    rect.width = kGlyphWidth;
                    return rect;
                }
                ++i;
                if (lead) x += kGlyphWidth;
            }
        }
        rect.x = x;  // at or past the end: the trailing edge
        rect.width = kGlyphWidth;
        return rect;
    }

    float LineHeight(const platform::FontSpec& font) const override {
        return LineHeightOf(font);
    }

    static float LineHeightOf(const platform::FontSpec& font) {
        return font.size * 1.5f;
    }

private:
    static float LineHeightOfWidest(
            const std::vector<platform::GlyphRun>& runs) {
        float h = 0.0f;
        for (const platform::GlyphRun& run : runs)
            h = (h < LineHeightOf(run.font)) ? LineHeightOf(run.font) : h;
        return h > 0.0f ? h : 12.0f;
    }
};

}  // namespace fake

#endif  // MARKDOWNIT_TESTS_FAKE_TEXT_ENGINE_H_
