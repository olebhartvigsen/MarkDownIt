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

// The run text of a heading: the semibold face at the level's size.
std::vector<platform::GlyphRun> BuildHeadingRuns(const Node& node,
                                                 float size,
                                                 const ThemeColor& color,
                                                 const LayoutMetrics& m,
                                                 const Palette& pal);

}  // namespace layout

#endif  // MARKDOWNIT_SRC_LAYOUT_TEXT_RUNS_H_
