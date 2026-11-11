#ifndef MARKDOWNIT_SRC_LAYOUT_TEXT_RUNS_H_
#define MARKDOWNIT_SRC_LAYOUT_TEXT_RUNS_H_

// The shared text helpers of the block layouts: run building, font
// conversion and the CSS line box. Everything here is portable on
// purpose, no platform include anywhere.

#include "displaylist.h"
#include "dom.h"
#include "platform/seams.h"
#include "theme.h"

#include <string>
#include <vector>

namespace layout {

// Points to device pixels, the factor both renderers apply.
constexpr float kPtToDip = 96.0f / 72.0f;

// The theme names fonts by role; both engines resolve these families,
// the way the theme macros spell them for each platform.
constexpr const char* kBodyFont = "Segoe UI";
constexpr const char* kSemiboldBodyFont = "Segoe UI Semibold";
constexpr const char* kCodeFont = "Cascadia Mono";

platform::Color ToColor(const ThemeColor& c);
std::string Utf32ToUtf8(const std::u32string& text);

// The font of one run: the theme's point size becomes device pixels
// at the live zoom.
platform::FontSpec fontSpecific(float baseSize, bool bold, bool italic,
                               bool monospace, const char* family);

// The CSS line box: the line is lineHeightEm times the font size,
// the glyphs centred in it with half leading, the ascent kept from
// the engine.
void ApplyLineSpacing(std::vector<platform::TextLine>* lines,
                      float ptSize, float lineHeightEm, float zoom,
                      platform::TextEngine* engine);

// One run per inline span: a link is a colour change and an
// underline, an inline code span takes the code face at the code
// size, a strong span takes the semibold face. boldIn and italicIn
// carry the block's inherited weight and slant.
std::vector<platform::GlyphRun> BuildRuns(const Node& node,
                                          float baseSize, bool boldIn,
                                          bool italicIn,
                                          const platform::Color& color,
                                          bool monospace,
                                          const char* family,
                                          float codeFontSize,
                                          const ThemeColor& linkColor,
                                          bool underlineLinks = true);

// An information box: a quote that starts with a label word and a
// colon, the way DocC writes its asides ("Note: ...", "Warning: ...").
// The label stays in the text. Synonyms map to the four kinds.
struct CalloutMatch {
    bool found = false;
    CalloutKind kind = CalloutKind::Info;
    size_t labelLength = 0;  // characters, colon included
};

inline CalloutMatch DetectCallout(const Node& node) {
    CalloutMatch out;
    if (node.children.empty()) return out;
    const InlineBlock& first = node.children[0];
    if (first.kind != InlineKind::Text || first.code) return out;
    static const struct { const char* word; CalloutKind kind; } kWords[] = {
        {"info", CalloutKind::Info},       {"note", CalloutKind::Info},
        {"warning", CalloutKind::Warning}, {"caution", CalloutKind::Warning},
        {"important", CalloutKind::Warning},
        {"danger", CalloutKind::Danger},   {"error", CalloutKind::Danger},
        {"success", CalloutKind::Success}, {"tip", CalloutKind::Success},
    };
    for (const auto& w : kWords) {
        size_t n = 0;
        while (w.word[n]) ++n;
        if (first.text.size() < n + 1 || first.text[n] != U':') continue;
        bool ok = true;
        for (size_t i = 0; i < n && ok; ++i) {
            char32_t c = first.text[i];
            if (c >= U'A' && c <= U'Z') c = c - U'A' + U'a';
            ok = c == static_cast<char32_t>(w.word[i]);
        }
        if (ok) {
            out.found = true;
            out.kind = w.kind;
            out.labelLength = n + 1;
            return out;
        }
    }
    return out;
}

// The alignment of one table cell: the column's own mark from the
// source, and where the column has none, the style's header rule (the
// Wikipedia wikitable centers its header cells). Every painter and
// every selection rect asks here, so the ports cannot disagree.
inline ColumnAlign CellAlign(const Node& table, const TableRow& row,
                             size_t col, const LayoutMetrics& m) {
    ColumnAlign a = col < table.aligns.size() ? table.aligns[col]
                                              : ColumnAlign::Default;
    if (a == ColumnAlign::Default && m.tableHeaderCenter &&
        col < row.cells.size() && row.cells[col].isHeader)
        a = ColumnAlign::Center;
    return a;
}

// The run text of a heading: the semibold face at the level's size.
std::vector<platform::GlyphRun> BuildHeadingRuns(const Node& node,
                                                 float size,
                                                 const ThemeColor& color,
                                                 const LayoutMetrics& m,
                                                 const Palette& pal);

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_TEXT_RUNS_H_
