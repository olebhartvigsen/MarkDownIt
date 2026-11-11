// The shared text helpers, ported from the two renderers.

#include "layout/text_runs.h"

namespace layout {

platform::Color ToColor(const ThemeColor& c) {
    platform::Color out;
    out.r = c.r;
    out.g = c.g;
    out.b = c.b;
    out.a = c.a;
    return out;
}

std::string Utf32ToUtf8(const std::u32string& text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        if (c < 0x80) {
            out.push_back((char)c);
        } else if (c < 0x800) {
            out.push_back((char)(0xC0 | (c >> 6)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back((char)(0xE0 | (c >> 12)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (c >> 18)));
            out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

platform::FontSpec fontSpecific(float baseSize, bool bold, bool italic,
                               bool monospace, const char* family) {
    platform::FontSpec font;
    font.family = family ? family : kBodyFont;
    font.size = baseSize * kPtToDip;
    font.bold = bold;
    font.italic = italic;
    font.monospace = monospace;
    return font;
}

void ApplyLineSpacing(std::vector<platform::TextLine>* lines,
                      float ptSize, float lineHeightEm, float zoom,
                      platform::TextEngine* engine) {
    const float em = ptSize * kPtToDip * zoom;
    float natural = em;
    float fontAscent = em;
    if (engine && !lines->empty() && !lines->front().runs.empty()) {
        const float measured =
            engine->LineHeight(lines->front().runs.front().font);
        if (measured > 0.0f) natural = measured;
        if (lines->front().ascent > 0.0f) fontAscent =
            lines->front().ascent;
    }
    const float height = lineHeightEm * em;
    const float ascent = (height - natural) * 0.5f + fontAscent;
    for (platform::TextLine& line : *lines) {
        line.height = height;
        line.ascent = ascent;
    }
}

std::vector<platform::GlyphRun> BuildRuns(const Node& node,
                                          float baseSize, bool boldIn,
                                          bool italicIn,
                                          const platform::Color& color,
                                          bool monospace,
                                          const char* family,
                                          float codeFontSize,
                                          const ThemeColor& linkColor,
                                          bool underlineLinks) {
    std::vector<platform::GlyphRun> runs;
    for (const InlineBlock& child : node.children) {
        const std::string runText = Utf32ToUtf8(child.text);
        if (runText.empty()) continue;

        platform::GlyphRun run;
        run.text = runText;
        const bool childCode = child.code;
        const float runSize = childCode ? codeFontSize : baseSize;
        const char* runFamily = family;
        bool runBold = boldIn;
        if (child.strong && runFamily == nullptr) {
            runFamily = kSemiboldBodyFont;
            runBold = false;
        }
        run.font = fontSpecific(runSize, runBold, italicIn || child.em,
                               monospace || childCode, runFamily);
        const bool link = child.kind == InlineKind::Link;
        const platform::Color runColor =
            link ? ToColor(linkColor) : color;
        run.r = runColor.r;
        run.g = runColor.g;
        run.b = runColor.b;
        run.a = runColor.a;
        run.underline = link && underlineLinks;
        run.strike = child.strike;
        runs.push_back(run);
    }
    return runs;
}

std::vector<platform::GlyphRun> BuildHeadingRuns(const Node& node,
                                                 float size,
                                                 const ThemeColor& color,
                                                 const LayoutMetrics& m,
                                                 const Palette& pal) {
    return BuildRuns(node, size, false, false, ToColor(color), false,
                     kSemiboldBodyFont, m.codeFontSize, pal.link,
                     m.linkUnderline);
}

}  // namespace layout
