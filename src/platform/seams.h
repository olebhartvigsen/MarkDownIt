#pragma once

#include <cassert>

// The trace beside the default DrawLayout below. crash_trace.h reaches
// windows.h on Windows, and that header maps DrawText to DrawTextW, which
// would rename the seam's own method for every translation unit this file
// reaches. The guard is the same removal renderer.h and
// render_target_win.h perform after their platform headers.
#include "crash_trace.h"
#ifdef DrawText
#undef DrawText
#endif

// The narrow interfaces both painters are written against.
//
// The seams are deliberately small: they cover exactly what the markdown
// renderer needs and nothing else. That keeps the drawing code honest,
// because there is no way to reach for a platform API that is not in the
// seam.
//
// The macOS renderer has always drawn through them. The Windows renderer is
// being moved onto them one method at a time, so a paint pass can become one
// shared file instead of two bodies that have to be kept alike by hand. The
// Direct2D and DirectWrite halves live in src/windows/; the Core Graphics and
// Core Text halves in src/macos/. tools/check_painter_seam.py counts the raw
// platform names left in each painter and only lets the number fall.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace platform {

// ---------------------------------------------------------------------------
// TextEngine: measure and place text
// ---------------------------------------------------------------------------

// A color as 0..1 floats in RGBA order.
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

// A font request, in the terms the shared theme uses.
struct FontSpec {
    // Font family name, for example "Helvetica Neue" or "Menlo".
    std::string family;
    // Size in points.
    float size = 14.0f;
    // Bold.
    bool bold = false;
    // Semibold, which the theme's heading levels wear when they name no
    // family of their own. Bold wins when both are set.
    bool semibold = false;
    // Italic.
    bool italic = false;
    // Monospace, which the code theme needs.
    bool monospace = false;
};

// A run of text with one style, which is what Core Text draws.
struct GlyphRun {
    // The text of the run, UTF-8.
    std::string text;
    // The style of the run.
    FontSpec font;
    // The ink of the run, as a color. A run that names no colour of its
    // own leaves the alpha at zero, and the block that draws it decides
    // the ink instead: DrawLayout's color argument on the portable path,
    // the same argument as the default brush on a native one. Every
    // builder that means a colour sets one, so a token run and the run
    // beside it can be drawn in one block and still differ in ink.
    Color color{0.0f, 0.0f, 0.0f, 0.0f};
    // The horizontal offset of the run inside its line, in points.
    float x = 0.0f;
    // The run is a link, drawn with an underline under the glyphs.
    bool underline = false;
    // The run is struck through.
    bool strike = false;
};

// The ink a run is drawn in: its own colour, or `fallback` when the run
// carries none (alpha zero, the default). One rule for every reader, so
// the portable path and the native ones cannot disagree about which runs
// own their ink.
inline Color RunInk(const GlyphRun& run, const Color& fallback) {
    return run.color.a > 0.0f ? run.color : fallback;
}

// The ink of a run that has no block colour to fall back on. The seam's
// historical default, black, which is what a builder that sets no ink has
// always drawn.
inline Color RunInk(const GlyphRun& run) {
    return RunInk(run, Color{0.0f, 0.0f, 0.0f, 1.0f});
}

// A UTF-16 range of one block's text and the inline styles the range
// carries, so a platform can dress its own text object with them.
//
// The ranges are applied in the order they are given. A whole-cell range
// (a bold header row) therefore comes before the spans inside it that may
// override it, which is the order the table's own dressing has always
// used.
struct TextRangeStyle {
    // The range, in UTF-16 code units of the block's text.
    uint32_t start = 0;
    uint32_t length = 0;
    // The weight the range asks for, 100 to 900, or zero to keep the
    // format's own. 600 is the semibold a table header wears and 700 the
    // bold of a strong span.
    int weight = 0;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    // A code span is drawn in this face at this size, in points. An empty
    // family leaves the block's own face in place.
    std::string family;
    float size = 0.0f;
};

// One laid out line of text.
struct TextLine {
    // The width of the line, in points.
    float width = 0.0f;
    // The height of the line, in points.
    float height = 0.0f;
    // The distance from the top of the line to the baseline, in points.
    float ascent = 0.0f;
    // The runs of the line, in order.
    std::vector<GlyphRun> runs;
    // The engine's own handle on the line it measured, so a hit test or a
    // caret rect asks for that line by name instead of matching a width
    // and a text again. A width and a text are not an identity: two lines
    // can share both, and a stale entry can share the width alone. Zero
    // for a line that was built by hand, which the check falls back on.
    uint64_t measuredKey = 0;
};

// A rectangle in points.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// One answer to a caret question about a laid out block, in the block's
// own coordinates: x from the column's left edge, y from the block's top.
struct TextHit {
    // UTF-16 code-unit index into the block's text.
    uint32_t textPosition = 0;
    // Where the position sits: the glyph edge for an offset. A hit test
    // answers a position, not a caret, so it leaves this at zero.
    float x = 0.0f;
    // The top of the line the position is on, from the top of the block.
    float y = 0.0f;
    // The height of that line, in points.
    float height = 0.0f;
    // True when the point landed on the trailing half of a glyph.
    bool trailing = false;
    // True when the point is inside the block's box.
    bool inside = false;
};

// The horizontal alignment of a laid out block inside the column it was
// laid out for.
enum class TextAlign {
    Left,    // flush with the column's left edge
    Center,  // centred on the column
    Right,   // flush with the column's right edge
};

// One laid out block of text: the runs of a paragraph or a table cell
// already broken into lines, measured and aligned, as the text engine
// keeps them. A target draws it without measuring anything again, which
// is what a wrapped, aligned block needs and what a row of DrawText calls
// cannot express: those place one run at one baseline and know nothing
// of the line the run belongs to.
//
// The handle is opaque to shared code. Windows keeps the DirectWrite
// layout the engine built, macOS keeps the block's Core Text lines, and
// the generic engine keeps the seam's own line records. A block is drawn
// by the target that shares its engine; shared code only passes it on.
class TextLayout {
public:
    virtual ~TextLayout() = default;

    // The width of the column the block was laid out in, in points.
    virtual float Width() const = 0;

    // The height of the block, in points: the top of its first line to
    // the bottom of its last.
    virtual float Height() const = 0;

    // The lines the block was broken into, in seam terms, when the layout
    // keeps them that way. The generic engine's layout does. A native one
    // answers null, because its lines live inside the platform's own text
    // object, and its target draws that object instead.
    virtual const std::vector<TextLine>* Lines() const { return nullptr; }

    // The left edge of one line inside the block's column, from the
    // alignment the block was laid out with. A native layout aligns its
    // own lines and answers zero; the portable one answers the offset.
    virtual float LineX(const TextLine& line) const {
        (void)line;
        return 0.0f;
    }

    // The caret's questions about the block, in the block's own
    // coordinates. A caret cache and a selection wash ask these instead of
    // holding the platform's text object, so one seam answers where the
    // glyphs ended up on both platforms.
    //
    // The base answers them from Lines(), the seam's own line records,
    // which the portable block keeps. A native block keeps no such
    // records: its lines live inside the platform's text object, so
    // DWriteTextLayout and CgTextLayout answer these themselves.
    //
    // The positions are UTF-16 code units, the unit a block speaks, not
    // the UTF-8 bytes TextEngine's per line answers take.
    virtual uint32_t LineCount() const;
    virtual float LineHeightAt(uint32_t lineIndex) const;
    // The UTF-16 range one visual line covers. `layoutWidth` is the width
    // of the whole block: DirectWrite has no direct query for a line's
    // text range and finds one by hit testing the line's edges, the right
    // one probed at the block's width.
    virtual bool LineTextRange(uint32_t lineIndex, float layoutWidth,
                               uint32_t* outStart, uint32_t* outEnd) const;
    // Hit test a point in the block.
    virtual TextHit HitTestPoint(float x, float y) const;
    // Where a UTF-16 position sits in the block: the caret's own answer.
    virtual TextHit CaretAtOffset(uint32_t textPosition) const;
};

// The seam's handle on a laid out block. Shared ownership, because a
// painter may hold a block across frames while the engine's own copies
// come and go with the layout pass.
using TextLayoutHandle = std::shared_ptr<const TextLayout>;

namespace detail {

// One UTF-8 code point: how many bytes it takes and how many UTF-16 code
// units it becomes. A stray byte counts as one unit, the replacement a
// real text object would draw for it.
inline void Utf8Char(const std::string& text, size_t at, size_t* bytes,
                     uint32_t* units) {
    const unsigned char c = static_cast<unsigned char>(text[at]);
    if (c < 0x80) {
        *bytes = 1;
        *units = 1;
    } else if ((c & 0xE0) == 0xC0) {
        *bytes = 2;
        *units = 1;
    } else if ((c & 0xF0) == 0xE0) {
        *bytes = 3;
        *units = 1;
    } else if ((c & 0xF8) == 0xF0) {
        *bytes = 4;
        *units = 2;
    } else {
        *bytes = 1;
        *units = 1;
    }
    if (*bytes > text.size() - at) *bytes = text.size() - at;
}

// The UTF-16 code units of a UTF-8 string.
inline uint32_t Utf16Length(const std::string& text) {
    uint32_t units = 0;
    for (size_t at = 0; at < text.size();) {
        size_t bytes = 1;
        uint32_t width = 1;
        Utf8Char(text, at, &bytes, &width);
        units += width;
        at += bytes;
    }
    return units;
}

// The UTF-16 range one line of a portable block covers.
inline bool LineRangeOf(const std::vector<TextLine>& lines,
                        uint32_t lineIndex, uint32_t* outStart,
                        uint32_t* outEnd) {
    if (lineIndex >= lines.size()) return false;
    uint32_t start = 0;
    for (uint32_t i = 0; i < lineIndex; ++i) {
        for (const GlyphRun& run : lines[i].runs) start += Utf16Length(run.text);
    }
    uint32_t length = 0;
    for (const GlyphRun& run : lines[lineIndex].runs) {
        length += Utf16Length(run.text);
    }
    if (outStart) *outStart = start;
    if (outEnd) *outEnd = start + length;
    return true;
}

// The top of one line of a portable block, from the top of the block.
inline float LineTopOf(const std::vector<TextLine>& lines,
                       uint32_t lineIndex) {
    float top = 0.0f;
    for (uint32_t i = 0; i < lineIndex && i < lines.size(); ++i) {
        top += lines[i].height;
    }
    return top;
}

// The right edge of one run's share of a line: the next run's start, or
// the line's own end for the last run.
inline float RunRight(const TextLine& line, size_t runIndex) {
    return runIndex + 1 < line.runs.size() ? line.runs[runIndex + 1].x
                                           : line.width;
}

}  // namespace detail

// The base answers the caret's questions from the seam's own line
// records, and stays a no-op for a block that keeps none and overrides
// nothing.

inline uint32_t TextLayout::LineCount() const {
    const std::vector<TextLine>* lines = Lines();
    return lines ? static_cast<uint32_t>(lines->size()) : 0u;
}

inline float TextLayout::LineHeightAt(uint32_t lineIndex) const {
    const std::vector<TextLine>* lines = Lines();
    if (!lines || lineIndex >= lines->size()) return 0.0f;
    return (*lines)[lineIndex].height;
}

inline bool TextLayout::LineTextRange(uint32_t lineIndex, float layoutWidth,
                                      uint32_t* outStart,
                                      uint32_t* outEnd) const {
    (void)layoutWidth;  // a line record knows its own range
    const std::vector<TextLine>* lines = Lines();
    if (!lines) return false;
    return detail::LineRangeOf(*lines, lineIndex, outStart, outEnd);
}

inline TextHit TextLayout::HitTestPoint(float x, float y) const {
    TextHit hit;
    const std::vector<TextLine>* lines = Lines();
    if (!lines || lines->empty()) return hit;

    uint32_t index = 0;
    float top = 0.0f;
    for (uint32_t i = 0; i < lines->size(); ++i) {
        const float height = (*lines)[i].height;
        if (y < top + height || i + 1 == lines->size()) {
            index = i;
            break;
        }
        top += height;
    }
    const TextLine& line = (*lines)[index];
    uint32_t start = 0;
    uint32_t end = 0;
    detail::LineRangeOf(*lines, index, &start, &end);

    // The run the point landed on, split evenly across its characters:
    // the line records carry each run's left edge and the line's width,
    // not a glyph advance per character.
    uint32_t offset = start;
    bool trailing = false;
    const float into = x - LineX(line);
    if (!line.runs.empty() && into > 0.0f) {
        uint32_t runStart = start;
        for (size_t r = 0; r < line.runs.size(); ++r) {
            const GlyphRun& run = line.runs[r];
            const float right = detail::RunRight(line, r);
            const uint32_t units = detail::Utf16Length(run.text);
            const bool last = r + 1 == line.runs.size();
            if (into < right || last) {
                const float span = right - run.x;
                const float intoRun = into - run.x;
                if (units == 0 || span <= 0.0f) {
                    offset = runStart + units;
                } else {
                    const float fraction = intoRun / span;
                    const uint32_t at =
                        fraction <= 0.0f
                            ? 0u
                            : (fraction >= 1.0f
                                   ? units
                                   : static_cast<uint32_t>(fraction * units));
                    offset = runStart + at;
                    const float cell = span / static_cast<float>(units);
                    trailing =
                        intoRun - static_cast<float>(at) * cell > cell * 0.5f;
                }
                break;
            }
            runStart += units;
        }
    }
    if (offset > end) offset = end;

    hit.textPosition = offset;
    hit.y = top;
    hit.height = line.height;
    hit.trailing = trailing;
    hit.inside = true;
    return hit;
}

inline TextHit TextLayout::CaretAtOffset(uint32_t textPosition) const {
    TextHit hit;
    hit.textPosition = textPosition;
    const std::vector<TextLine>* lines = Lines();
    if (!lines || lines->empty()) return hit;

    uint32_t index = 0;
    for (uint32_t i = 0; i < lines->size(); ++i) {
        uint32_t start = 0;
        uint32_t end = 0;
        detail::LineRangeOf(*lines, i, &start, &end);
        index = i;
        if (textPosition < end || i + 1 == lines->size()) break;
    }
    const TextLine& line = (*lines)[index];
    uint32_t start = 0;
    uint32_t end = 0;
    detail::LineRangeOf(*lines, index, &start, &end);
    uint32_t offset = textPosition > end ? end : textPosition;
    if (offset < start) offset = start;

    float x = 0.0f;
    uint32_t runStart = start;
    for (size_t r = 0; r < line.runs.size(); ++r) {
        const GlyphRun& run = line.runs[r];
        const float right = detail::RunRight(line, r);
        const uint32_t units = detail::Utf16Length(run.text);
        if (offset <= runStart + units || r + 1 == line.runs.size()) {
            const float span = right - run.x;
            const uint32_t into = offset > runStart ? offset - runStart : 0;
            x = units == 0
                    ? run.x
                    : run.x + span * static_cast<float>(into) /
                                  static_cast<float>(units);
            break;
        }
        runStart += units;
    }

    hit.x = LineX(line) + x;
    hit.y = detail::LineTopOf(*lines, index);
    hit.height = line.height;
    hit.inside = true;
    return hit;
}

namespace detail {

// The portable layout: the lines an engine measured, kept exactly as they
// came back, with the column width and the alignment they were laid out
// for. Every engine that owns no text object of its own answers with this
// one, through the TextEngine default below, and it is what the generic
// engine has.
class LineTextLayout final : public TextLayout {
public:
    LineTextLayout(std::vector<TextLine> lines, float width, TextAlign align)
        : lines_(std::move(lines)), width_(width), align_(align) {
        for (const TextLine& line : lines_) height_ += line.height;
    }

    float Width() const override { return width_; }
    float Height() const override { return height_; }
    const std::vector<TextLine>* Lines() const override { return &lines_; }

    float LineX(const TextLine& line) const override {
        if (!(width_ > 0.0f)) return 0.0f;
        if (align_ == TextAlign::Center) return (width_ - line.width) * 0.5f;
        if (align_ == TextAlign::Right) return width_ - line.width;
        return 0.0f;
    }

private:
    std::vector<TextLine> lines_;
    float width_ = 0.0f;
    float height_ = 0.0f;
    TextAlign align_ = TextAlign::Left;
};

}  // namespace detail

// Wraps measured lines in the seam's handle, aligned inside the column
// width they were measured at.
inline TextLayoutHandle MakeLineLayout(std::vector<TextLine> lines,
                                       float width, TextAlign align) {
    return std::make_shared<detail::LineTextLayout>(std::move(lines), width,
                                                    align);
}

// Measures and places text.
//
// MeasureWrapped lays the runs out in a column of the given width and
// returns the lines. The runs keep their order, so a caller can map a
// character offset back to the run that holds it.
//
// HitTestOffset answers which character offset a horizontal position on a
// line belongs to, which is what the caret and the selection need.
//
// OffsetToRect answers where a character offset sits on a line, which is
// what the caret and the selection rectangles need.
//
// The offsets are UTF-8 byte offsets into the concatenated run text of the
// line, which is the same unit the document uses.
class TextEngine {
public:
    virtual ~TextEngine() = default;

    // Called before a full layout pass, so the engine can drop the
    // measurements of the pass before it. The default keeps everything;
    // an engine that keeps its lines alive for hit testing overrides
    // it.
    virtual void BeginLayout() {}

    // Lays the runs out. The runs are measured and broken into lines that
    // fit maxWidth. A maxWidth of zero means one unbounded line.
    virtual std::vector<TextLine> MeasureWrapped(
        const std::vector<GlyphRun>& runs,
        float maxWidth,
        float lineSpacing) = 0;

    // The character offset on the line at the given x. The x is relative
    // to the start of the line. Returns the offset of the closest
    // character boundary, and sets outTrailing to true when the position
    // is in the trailing half of a character.
    virtual uint32_t HitTestOffset(const TextLine& line, float x,
                                   bool* outTrailing) const = 0;

    // The rectangle a character offset occupies on the line.
    virtual Rect OffsetToRect(const TextLine& line, uint32_t offset) const = 0;

    // The height of a line of the given font, which the editor needs for
    // the caret and for line spacing.
    virtual float LineHeight(const FontSpec& font) const = 0;

    // The font's own vertical metrics at the spec's size: the natural
    // line box it asks for (ascent + descent + line gap) and its ascent,
    // both in points. False when the platform cannot resolve the family.
    //
    // This is the number the cursor guide calls the current font's line
    // height, the one the caret is drawn with. It is not the rendered
    // line box: that one carries the theme's line-height multiplier and
    // is a different question, asked by the caller that sets a block's
    // line box.
    //
    // The base answers from LineHeight, which every engine already has,
    // with the ascent as the usual share of it. The native engines
    // override it with the font's own metrics, which is what the caret
    // has always measured.
    virtual bool FontBox(const FontSpec& font, float* natural,
                         float* ascent) const {
        const float height = LineHeight(font);
        if (natural) *natural = height;
        if (ascent) *ascent = height * 0.8f;
        return height > 0.0f;
    }

    // The family names the platform can set, sorted and unique. Shared
    // code asks for a name here instead of walking a platform collection
    // of its own; an engine with no collection answers an empty list.
    virtual std::vector<std::string> FontFamilies() const { return {}; }

    // Lays the runs out into one block that a target draws without
    // measuring again: the same breaks MeasureWrapped takes at the same
    // width, kept with the column width and the alignment they were laid
    // out for. The default builds the portable line block on top of
    // MeasureWrapped, which every engine already answers; a native engine
    // overrides it with its own text object.
    virtual TextLayoutHandle BuildLayout(const std::vector<GlyphRun>& runs,
                                         float maxWidth, TextAlign align) {
        return MakeLineLayout(MeasureWrapped(runs, maxWidth, 0.0f), maxWidth,
                              align);
    }
};

// ---------------------------------------------------------------------------
// RenderTarget: draw
// ---------------------------------------------------------------------------

// A point in points.
struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

// An image the target can draw.
//
// The handle is opaque to the seam: a decoded bitmap on one platform, a
// vector document on another. The target owns the drawing, so the
// renderer never has to know what is behind the handle.
using BitmapHandle = void*;

// The kinds of image a target can be handed.
enum class ImageKind {
    Raster,  // a decoded bitmap, drawn as it is
    Vector,  // a vector document, scaled into the rectangle
};

// What a BitmapHandle points at.
//
// The seam passes the handle around as an opaque pointer, so the mac
// layer keeps the concrete type here and both sides agree on it.
struct Image {
    ImageKind kind = ImageKind::Raster;
    // The platform image: a CGImageRef for a raster image, an NSImage
    // for a vector one.
    void* handle = nullptr;
    // The natural size of the image in points.
    float width = 0.0f;
    float height = 0.0f;
};

// Draws the shapes and text the markdown renderer needs.
//
// Every method works in points. The caller scales the context once, so
// the renderer never deals with device pixels.
class RenderTarget {
public:
    virtual ~RenderTarget() = default;

    // Fills a rectangle.
    virtual void FillRect(const Rect& rect, const Color& color) = 0;

    // Fills several rectangles with one colour.
    //
    // A search or a marker layer fills a rectangle per wrapped line, so one
    // block of text with hundreds of matches is hundreds of fills in two
    // colours. The default fills them one at a time; a target that can keep
    // a brush alive across the batch overrides it and pays for one brush.
    virtual void FillRects(const std::vector<Rect>& rects, const Color& color) {
        for (const Rect& rect : rects) FillRect(rect, color);
    }

    // Fills a rectangle with rounded corners. The radius is in points.
    virtual void FillRoundedRect(const Rect& rect, float radius,
                                 const Color& color) = 0;

    // Fills a rectangle with only the named ends rounded. A wrapped
    // inline code span uses it: the first fragment keeps the left cap,
    // the last keeps the right, and the fragments between them are
    // square, so the background reads as one run and not as a row of
    // pills with a gap at every line break.
    virtual void FillRoundedRectSides(const Rect& rect, float radius,
                                      const Color& color, bool roundLeft,
                                      bool roundRight) {
        (void)roundLeft;
        (void)roundRight;
        FillRoundedRect(rect, radius, color);
    }

    // Strokes a rectangle with a one point line.
    virtual void StrokeRect(const Rect& rect, const Color& color,
                            float lineWidth) = 0;

    // Strokes a rounded rectangle, the outline of the geometry
    // FillRoundedRect fills. The radius is in points.
    virtual void StrokeRoundedRect(const Rect& rect, float radius,
                                   const Color& color, float lineWidth) = 0;

    // Strokes a straight line.
    virtual void StrokeLine(float x0, float y0, float x1, float y1,
                            const Color& color, float lineWidth) = 0;

    // Draws a line of text at the given position. The position is the
    // left edge of the baseline.
    virtual void DrawText(const std::string& utf8, const FontSpec& font,
                          float x, float baselineY, const Color& color) = 0;

    // Draws a laid out block of text with its top left at (x, topY), its
    // lines already broken, measured and aligned by the engine that built
    // it. Each run is drawn in its own colour; `color` is the ink for the
    // runs that carry none (the default, a transparent run colour), so a
    // block whose runs are all unset draws in one colour exactly as it
    // did before runs could carry their own.
    //
    // A json fence is the case that needs it: four token inks across one
    // block, on the same lines, at the same positions.
    //
    // A block from this platform's engine goes through the platform's own
    // text object, which is the only way a wrapped, justified block can be
    // drawn. Any other block (the generic engine's, on a real target) is
    // drawn line by line through DrawText by this default, so a target
    // that has no native path for a block still draws it.
    virtual void DrawLayout(const TextLayout& layout, float x, float topY,
                            const Color& color) {
        const std::vector<TextLine>* lines = layout.Lines();
        if (!lines) {
            // A native block handed to a target that does not own it. This is
            // a bug in the caller, not a state to absorb: drawing nothing
            // would leave a blank run in the document that no test notices.
            assert(false && "native text layout given to a target that cannot draw it");
            diag::TraceFmt("SEAMLAYOUT: target cannot draw a native block (native=1)");
            return;
        }
        float lineTop = topY;
        for (const TextLine& line : *lines) {
            const float lineX = x + layout.LineX(line);
            const float baseline = lineTop + line.ascent;
            for (size_t k = 0; k < line.runs.size(); ++k) {
                const GlyphRun& run = line.runs[k];
                // A run's share of the line runs to the next run's start,
                // and the last one to the line's own end.
                const float end = k + 1 < line.runs.size()
                                      ? line.runs[k + 1].x
                                      : line.width;
                const float width = end - run.x;
                const float runX = lineX + run.x;
                const Color ink = RunInk(run, color);
                DrawText(run.text, run.font, runX, baseline, ink);
                if (run.underline)
                    StrokeLine(runX, baseline + 2.0f, runX + width,
                               baseline + 2.0f, ink, 1.0f);
                if (run.strike)
                    StrokeLine(runX,
                               baseline - 0.30f * run.font.size,
                               runX + width,
                               baseline - 0.30f * run.font.size,
                               ink, 1.0f);
            }
            lineTop += line.height;
        }
    }

    // Draws an image into the rectangle, scaled to fit it. The image is
    // scaled to fit, so the aspect ratio is kept and the image is
    // centred in the rectangle.
    virtual void DrawBitmap(BitmapHandle image, const Rect& rect) = 0;

    // Fills an ellipse that fits the rectangle.
    virtual void FillEllipse(const Rect& rect, const Color& color) = 0;

    // Strokes an ellipse that fits the rectangle.
    virtual void StrokeEllipse(const Rect& rect, const Color& color,
                               float lineWidth) = 0;

    // Fills a closed polygon. The points are in the order they are
    // given, and the last point is joined back to the first.
    virtual void FillPolygon(const std::vector<Point>& points,
                             const Color& color) = 0;

    // Draws a disclosure mark for a subtree: a chevron, pointing down
    // while the subtree is open and right while it is folded. The
    // base draws the small filled triangle the desktop pane uses; the
    // mac target overrides it with the system's own chevron symbol.
    virtual void DrawChevron(const Rect& rect, bool expanded,
                             const Color& color) {
        const float half = rect.height * 0.28f;
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        std::vector<Point> triangle;
        if (expanded) {
            triangle = {Point{cx - half, cy - half * 0.6f},
                        Point{cx + half, cy - half * 0.6f},
                        Point{cx, cy + half * 0.6f}};
        } else {
            triangle = {Point{cx - half * 0.6f, cy - half},
                        Point{cx + half * 0.6f, cy},
                        Point{cx - half * 0.6f, cy + half}};
        }
        FillPolygon(triangle, color);
    }

    // Strokes a polygon. The points are in the order they are given,
    // and the last point is joined back to the first when the shape is
    // closed.
    virtual void StrokePolygon(const std::vector<Point>& points,
                               const Color& color, float lineWidth,
                               bool closed) = 0;

    // Fills a pie slice of the ellipse that fits the rectangle.
    //
    // The angles are in degrees, measured clockwise from the three
    // o'clock position, the same way the pie chart data names them.
    virtual void FillPieSlice(const Rect& rect, float startAngle,
                              float endAngle, const Color& color) = 0;

    // Pushes a clip rectangle. Every PushClip must be matched by a
    // PopClip.
    virtual void PushClip(const Rect& rect) = 0;
    virtual void PopClip() = 0;
};

// ---------------------------------------------------------------------------
// TextRangeSource: where a character range sits on the page
// ---------------------------------------------------------------------------

// Answers the rectangles a character range of one laid out block covers.
//
// The split is what lets a highlight pass be shared code. The renderer owns
// the policy: which ranges are drawn, in which colour, in which order. The
// platform owns the geometry: where the glyphs of a range ended up after
// wrapping, zoom and scroll. The platform that answers the geometry is the
// same one that measured the block, so a range can be located in the units
// it was laid out in.
class TextRangeSource {
public:
    virtual ~TextRangeSource() = default;

    // Appends the rectangle of every line the range covers to `out`, in
    // target coordinates. The range is in UTF-16 units of the block's text,
    // which is the unit both platforms lay out in, and is clamped to the
    // block. Lines whose band lies outside [cullTop, cullBottom) are skipped
    // without a hit test, so a range scrolled out of view costs nothing.
    virtual void AppendRangeRects(uint32_t start, uint32_t end,
                                  float cullTop, float cullBottom,
                                  std::vector<Rect>* out) = 0;
};

// ---------------------------------------------------------------------------
// TelemetrySink: the one outbound request the app makes
// ---------------------------------------------------------------------------

// Posts an analytics payload and answers whether the service accepted it.
//
// The payload is built by shared code (src/analytics.h); this interface is
// only the transport, so the decision about what leaves the machine stays in
// one place and can be tested with a fake. Implementations are fire and
// forget: a failure is written to the crash trace and never shown to the
// reader, because the app works without a network.
class TelemetrySink {
public:
    virtual ~TelemetrySink() = default;

    // POST `body` to `url` with the app key in the Aptabase header. Returns
    // true when the service answered 2xx. Implementations must not throw and
    // must not block the caller for longer than a couple of seconds.
    virtual bool Post(const std::string& url, const std::string& appKey,
                      const std::string& body) = 0;
};

// The transport for this platform: WinHTTP in src/windows/telemetry_win.cpp,
// NSURLSession in src/macos/telemetry_mac.mm. The shell asks for one here and
// never names a platform header to get it. Never returns null, never throws.
std::unique_ptr<TelemetrySink> CreateTelemetrySink();

// ---------------------------------------------------------------------------
// AppShell: the window and the platform
// ---------------------------------------------------------------------------

// The window and the services the app needs from the system.
//
// The mac implementation wraps NSWindow, NSApplication, NSPasteboard and
// the rest. The interface exists so the app logic can ask for a service
// without knowing it is on macOS, and so the behaviour can be tested.
class AppShell {
public:
    virtual ~AppShell() = default;

    // The size of the window's content area, in points.
    virtual void GetContentSize(float* outWidth, float* outHeight) const = 0;

    // The scale factor of the backing store, which is 1.0 on a non Retina
    // display and 2.0 on a Retina one.
    virtual float GetScaleFactor() const = 0;

    // Shows the window and makes it key.
    virtual void ShowWindow() = 0;

    // Requests a redraw of the content area.
    virtual void InvalidateContent() = 0;

    // Starts a repeating timer. The callback runs on the main thread.
    // Returns a handle that StopTimer takes.
    virtual uint32_t StartTimer(double intervalSeconds,
                                std::function<void()> callback) = 0;
    virtual void StopTimer(uint32_t handle) = 0;

    // Shows the system context menu at a point in the content area.
    virtual void ShowContextMenu(const std::vector<std::string>& items,
                                 float x, float y) = 0;

    // The text on the clipboard, and the ability to set it.
    virtual std::string GetClipboardText() = 0;
    virtual void SetClipboardText(const std::string& text) = 0;

    // Opens a URL in the user's browser.
    virtual void OpenInBrowser(const std::string& url) = 0;

    // The user's home directory, where the settings and the logs live.
    virtual std::string GetHomeDirectory() const = 0;

    // The path of the file the user opened, empty when nothing is open.
    virtual std::string GetOpenFilePath() const = 0;
};

// Opens a URL in the user's browser. The caller has checked the
// scheme; this only performs the handoff and reports whether the
// system took it.
bool OpenExternalUrl(const std::string& url);

}  // namespace platform
