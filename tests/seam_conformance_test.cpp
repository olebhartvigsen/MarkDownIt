// The seam conformance suite of plan task 2.2. One set of assertions,
// every engine that implements the seam. The fake engine of task 2.3 and
// the Core Text engine run it on macOS, the DirectWrite engine on Windows,
// and each future engine joins with one line.
//
// The assertions are structural invariants, not exact numbers: real
// engines answer with real fonts, so the suite checks what every
// conforming engine owes its callers, whatever glyph widths it picks.
//
// The second half of the grid covers the seam's laid out block: every
// engine builds one, and it is drawn through a real platform target into
// an offscreen surface whose ink is read back.

#include "fake_text_engine.h"
#include "gtest_lite.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

// The concrete adapters below the seam read the seam, and windows.h
// redefines DrawText as a macro before the seam is read, so the target
// header comes first, exactly the way renderer.h orders it on Windows.
#if defined(_WIN32)
#include "../src/windows/render_target_win.h"
#include "../src/windows/text_engine_win.h"
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#endif

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include "../src/macos/text_engine_mac.h"
#include "../src/macos/cg_target_mac.h"
#endif

using platform::FontSpec;
using platform::GlyphRun;
using platform::TextEngine;
using platform::TextLine;

namespace {

GlyphRun MakeRun(const std::string& text, float size = 14.0f) {
    GlyphRun run;
    run.text = text;
    run.font.size = size;
    run.font.family = "Body";
    return run;
}

FontSpec MakeFont(float size = 14.0f) {
    FontSpec font;
    font.size = size;
    font.family = "Body";
    return font;
}

// What every conforming engine owes its callers. label only names the
// engine in the failure output.
void CheckTextEngine(TextEngine& engine) {
    // Line height is deterministic and follows the size.
    const float h1 = engine.LineHeight(MakeFont(14.0f));
    const float h1again = engine.LineHeight(MakeFont(14.0f));
    EXPECT_NEAR(h1, h1again, 0.001f);
    const float h2 = engine.LineHeight(MakeFont(24.0f));
    EXPECT_TRUE(h2 >= h1);

    // The font's own box: a natural line height and an ascent inside it,
    // the same pair pass to pass. Every engine, native or not, answers
    // the caret's question with a box no smaller than the glyphs it sets.
    // The probe names the theme's body face, which both ports resolve to
    // a real family, so a native engine can answer with its own metrics.
    FontSpec probe;
    probe.family = "Segoe UI";
    probe.size = 14.0f;
    float natural = 0.0f;
    float ascent = 0.0f;
    EXPECT_TRUE(engine.FontBox(probe, &natural, &ascent));
    EXPECT_GT(natural, 0.0f);
    EXPECT_GT(ascent, 0.0f);
    EXPECT_LE(ascent, natural);
    EXPECT_TRUE(natural >= 14.0f * 0.9f);
    float naturalAgain = 0.0f;
    EXPECT_TRUE(engine.FontBox(probe, &naturalAgain, nullptr));
    EXPECT_NEAR(naturalAgain, natural, 0.001f);
    // A bigger size asks a bigger box.
    probe.size = 28.0f;
    float bigBox = 0.0f;
    EXPECT_TRUE(engine.FontBox(probe, &bigBox, nullptr));
    EXPECT_GT(bigBox, natural);

    // The platform's family list, the answer that replaced the renderer's
    // own collection walk. A portable engine may hold none, so the grid
    // pins the shape: the same list twice, unique, and every name real.
    const std::vector<std::string> families = engine.FontFamilies();
    const std::vector<std::string> familiesAgain = engine.FontFamilies();
    EXPECT_EQ(families.size(), familiesAgain.size());
    for (size_t i = 0; i < families.size(); ++i) {
        EXPECT_TRUE(!families[i].empty());
        if (i > 0) EXPECT_LT(families[i - 1], families[i]);
    }

    // The measure a caller lays out itself asks for: a width and a height
    // for the block, deterministic, and a taller answer when the caller
    // asks for a bigger line box. The exact numbers are the engine's.
    const platform::TextMeasure naturalBox =
        engine.MeasureText("label", probe, 500.0f, platform::LineBox());
    EXPECT_GT(naturalBox.width, 0.0f);
    EXPECT_GT(naturalBox.height, 0.0f);
    const platform::TextMeasure measureAgain =
        engine.MeasureText("label", probe, 500.0f, platform::LineBox());
    EXPECT_NEAR(measureAgain.width, naturalBox.width, 0.001f);
    EXPECT_NEAR(measureAgain.height, naturalBox.height, 0.001f);
    const platform::TextMeasure tallBox =
        engine.MeasureText("label", probe, 500.0f,
                           platform::LineBox{2.0f, 1.6f});
    EXPECT_GT(tallBox.height, naturalBox.height);

    // The block a role asks for: the column it named, a real height, and
    // one line box of the style's height per line it broke into.
    platform::BlockStyle role;
    role.font = probe;
    role.maxWidth = 200.0f;
    role.lineHeightEm = 2.0f;
    const std::vector<platform::TextRangeStyle> noRanges;
    const platform::TextLayoutHandle roleBlock =
        engine.BuildBlock(u"one two", noRanges, role);
    ASSERT_TRUE(roleBlock != nullptr);
    if (roleBlock) {
        EXPECT_NEAR(roleBlock->Width(), 200.0f, 0.5f);
        EXPECT_GT(roleBlock->Height(), 0.0f);
        EXPECT_NEAR(roleBlock->Height(),
                    2.0f * probe.size *
                        static_cast<float>(roleBlock->LineCount()),
                    1.5f);
    }

    std::vector<GlyphRun> runs = {MakeRun("first words", 14.0f),
                                  MakeRun("second", 14.0f)};

    // A maxWidth of zero means one unbounded line, and every run stays
    // on it in order, at nondecreasing x.
    engine.BeginLayout();
    std::vector<TextLine> unbounded = engine.MeasureWrapped(runs, 0.0f, 0.0f);
    EXPECT_EQ(unbounded.size(), static_cast<size_t>(1));
    EXPECT_EQ(unbounded[0].runs.size(), static_cast<size_t>(2));
    size_t glyphs = 0;
    for (const TextLine& line : unbounded) {
        float x = -1.0f;
        for (const GlyphRun& run : line.runs) {
            EXPECT_TRUE(run.x >= x);
            x = run.x;
            for (unsigned char c : run.text) {
                if ((c & 0xC0) != 0x80) ++glyphs;
            }
        }
    }
    EXPECT_TRUE(glyphs >= 11);  // first words alone carries it
    EXPECT_TRUE(unbounded[0].width >= 11 * 2);

    // Measuring is deterministic pass to pass.
    engine.BeginLayout();
    std::vector<TextLine> again = engine.MeasureWrapped(runs, 0.0f, 0.0f);
    EXPECT_EQ(again.size(), unbounded.size());
    EXPECT_NEAR(again[0].width, unbounded[0].width, 0.001f);

    // A bounded width keeps every character, in order, and wrapping
    // never makes the text wider than the unbounded line was. Engines
    // may merge runs that share a style or split a run at a line break,
    // so the grid counts code points, not run entries: whatever the
    // engine did to the runs, the reading order and the ink must
    // survive the wrap.
    std::vector<GlyphRun> many = {MakeRun("aaaa"), MakeRun("bbbb"),
                                  MakeRun("cccc")};
    const float bound = 0.75f * unbounded[0].width;
    ASSERT_TRUE(bound > 0.0f);
    engine.BeginLayout();
    std::vector<TextLine> wrapped = engine.MeasureWrapped(many, bound, 0.0f);
    std::string wrappedText;
    float wrappedWidth = 0.0f;
    for (const TextLine& line : wrapped) {
        float x = -1.0f;
        for (const GlyphRun& run : line.runs) {
            wrappedText += run.text;
            EXPECT_TRUE(run.x >= x);
            x = run.x;
        }
        wrappedWidth += line.width;
        EXPECT_GT(line.height, 0.0f);
    }
    std::string sourceText;
    for (const GlyphRun& run : many) sourceText += run.text;
    EXPECT_EQ(wrappedText, sourceText);
    EXPECT_LE(wrappedWidth, unbounded[0].width + 0.001f);

    // A caret inside a wrapped line sits inside that line. The engines
    // that own their layouts answer positions in the paragraph's index
    // space, so a caller that speaks line-relative units gets that line's
    // first byte back for every offset, and the caret never moves into the
    // line. The second line is the case: the first one starts at zero and
    // hides the mistake. The bound is a fraction of this text's own
    // unbounded width, so the wrap happens whatever glyph widths the
    // engine picked.
    std::vector<GlyphRun> wrapping = {MakeRun("aaaa"), MakeRun("bbbb"),
                                      MakeRun("cccc")};
    engine.BeginLayout();
    std::vector<TextLine> whole = engine.MeasureWrapped(wrapping, 0.0f, 0.0f);
    ASSERT_TRUE(whole.size() == static_cast<size_t>(1));
    ASSERT_TRUE(whole[0].width > 3.0f);
    engine.BeginLayout();
    std::vector<TextLine> wrappedTwice =
        engine.MeasureWrapped(wrapping, whole[0].width * 0.6f, 0.0f);
    ASSERT_TRUE(wrappedTwice.size() >= static_cast<size_t>(2));
    const TextLine& secondLine = wrappedTwice[1];
    const platform::Rect secondStart = engine.OffsetToRect(secondLine, 0);
    const platform::Rect secondInside = engine.OffsetToRect(secondLine, 2);
    EXPECT_GT(secondInside.x, secondStart.x + 0.5f);

    // Word wrapping breaks lines at spaces, and the space that carries
    // the break still has to advance the ink after it: a line laid
    // run by run at trimmed widths drops every space at a run's end,
    // and the words come out glued. The grid pins the gap between two
    // runs sharing a wrapped line, with the bound measured off the
    // engine's own unbounded answer so metric guesses stay out. Runs
    // one and two fit the bound together, run three does not.
    std::vector<GlyphRun> parted = {MakeRun("aa ", 14.0f),
                                   MakeRun("bb ", 14.0f),
                                   MakeRun("ccc", 14.0f)};
    std::vector<GlyphRun> pair = {parted[0], parted[1]};
    engine.BeginLayout();
    std::vector<TextLine> pairLines = engine.MeasureWrapped(pair, 0.0f, 0.0f);
    ASSERT_TRUE(pairLines.size() >= 1);
    ASSERT_TRUE(pairLines[0].width > 1.0f);
    const float pairBound = pairLines[0].width + 0.5f;
    engine.BeginLayout();
    std::vector<TextLine> partedLines =
        engine.MeasureWrapped(parted, pairBound, 0.0f);
    ASSERT_TRUE(partedLines.size() >= 2);  // the third run did not fit
    std::string partedText;
    for (const TextLine& line : partedLines) {
        const GlyphRun* previous = nullptr;
        for (const GlyphRun& run : line.runs) {
            if (previous) {
                // The space between the runs still advances: a gap a
                // reader can see, not the trimmed-width overlap.
                EXPECT_GT(run.x, previous->x + 1.0f);
            }
            partedText += run.text;
            previous = &run;
        }
    }
    std::string partedSource;
    for (const GlyphRun& run : parted) partedSource += run.text;
    EXPECT_EQ(partedText, partedSource);

    // Offsets are UTF-8 bytes: the multibyte line draws four
    // characters in eight bytes, and the trailing hit answers the
    // byte count, not the character count.
    std::vector<GlyphRun> wide = {MakeRun("a\xC3\xA9\xF0\x9F\x8E\x89"
                                         "b")};
    engine.BeginLayout();
    std::vector<TextLine> wideLines =
        engine.MeasureWrapped(wide, 0.0f, 0.0f);
    const platform::Rect wideEnd = engine.OffsetToRect(wideLines[0], 8);
    bool wideTrailing = false;
    const uint32_t wideHit = engine.HitTestOffset(wideLines[0],
                                                  wideEnd.x + 999.0f,
                                                  &wideTrailing);
    EXPECT_EQ(wideHit, static_cast<uint32_t>(8));

    // A measured line carries the engine's own handle on it: set, unique
    // within a pass, and kept by a copy, which is how every reader holds
    // the line. Matching a line by its width and its text is not an
    // identity, and a stale entry that shares the width alone answers the
    // line's first byte for every offset, so the caret sits at the start
    // of the line no matter where the click went.
    engine.BeginLayout();
    std::vector<TextLine> keyed = engine.MeasureWrapped(parted, 0.0f, 0.0f);
    ASSERT_TRUE(keyed.size() >= 1);
    for (size_t i = 0; i < keyed.size(); ++i) {
        EXPECT_TRUE(keyed[i].measuredKey != 0u);
        const TextLine copy = keyed[i];
        EXPECT_EQ(copy.measuredKey, keyed[i].measuredKey);
        for (size_t j = i + 1; j < keyed.size(); ++j) {
            EXPECT_TRUE(keyed[i].measuredKey != keyed[j].measuredKey);
        }
    }

    // A line of several runs, the shape every paragraph with a styled
    // span has. A caret offset is a byte inside the line, so its rect only
    // moves right as the offset grows, across a run boundary as well as
    // inside a run. A paragraph map that skips a run's worth of bytes
    // makes every offset past that run answer its last byte, and the
    // caret stops at the end of the styled span.
    std::vector<GlyphRun> styled = {MakeRun("plain ", 14.0f),
                                    MakeRun("code", 12.0f),
                                    MakeRun(" after", 14.0f)};
    engine.BeginLayout();
    std::vector<TextLine> styledLines =
        engine.MeasureWrapped(styled, 0.0f, 0.0f);
    ASSERT_TRUE(styledLines.size() == static_cast<size_t>(1));
    const TextLine& styledLine = styledLines[0];
    float previousX = -1.0f;
    for (uint32_t byte = 0; byte <= 16; ++byte) {
        const platform::Rect at = engine.OffsetToRect(styledLine, byte);
        EXPECT_TRUE(at.x >= previousX - 0.01f);
        previousX = at.x;
    }
    // The caret at the line's last byte sits at the line's end, and the
    // caret at the last run's first byte sits a code span's width right of
    // the code span's start. A map that skips or repeats a run's bytes
    // parks both of them on an earlier run, which the plain sweep above
    // cannot see: a stall is still non-decreasing.
    const platform::Rect styledEnd = engine.OffsetToRect(styledLine, 16);
    EXPECT_TRUE(styledEnd.x >= styledLine.width - 1.0f);
    const platform::Rect codeStart = engine.OffsetToRect(styledLine, 6);
    const platform::Rect afterStart = engine.OffsetToRect(styledLine, 10);
    EXPECT_TRUE(afterStart.x > codeStart.x + 1.0f);
    bool styledTrailing = false;
    EXPECT_EQ(engine.HitTestOffset(styledLine, styledEnd.x + 500.0f,
                                   &styledTrailing),
              static_cast<uint32_t>(16));

    // Hit test and caret rect agree about position and direction.
    const TextLine& line = unbounded[0];
    const platform::Rect atZero = engine.OffsetToRect(line, 0);
    const platform::Rect atEnd = engine.OffsetToRect(line, 40);
    EXPECT_TRUE(atEnd.x >= atZero.x);
    bool trailing = true;
    const uint32_t hitZero = engine.HitTestOffset(line, atZero.x, &trailing);
    EXPECT_TRUE(hitZero <= 1u);
    const uint32_t hitEnd =
        engine.HitTestOffset(line, atEnd.x + 999.0f, &trailing);
    EXPECT_TRUE(hitEnd >= hitZero);
}

// ---------------------------------------------------------------------------
// The ink a target actually puts down
// ---------------------------------------------------------------------------
//
// Drawing a laid out block cannot be checked from the parts the seam
// exposes: the point of the method is that the platform's own text object
// draws it. So the assertions below draw into an offscreen surface and
// read the paper back. On Windows that is a Direct2D DC target over a
// DIB section, on macOS a Core Graphics bitmap context; both are the real
// adapter, bound to memory instead of a window.
//
// Every pixel is 32 bit with four channels and the alpha last, on both
// platforms, so one scan finds the ink in either byte order: a black
// glyph is dark in all three colour channels, whichever one leads.

constexpr int kSurfaceW = 320;
constexpr int kSurfaceH = 120;

platform::Rect ScanInk(const unsigned char* pixels, int width, int height) {
    int left = width;
    int top = height;
    int right = 0;
    int bottom = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char* p = pixels + (static_cast<size_t>(y) *
                                                   static_cast<size_t>(width) +
                                               static_cast<size_t>(x)) *
                                                  4;
            if (p[0] < 128 && p[1] < 128 && p[2] < 128) {
                if (x < left) left = x;
                if (x + 1 > right) right = x + 1;
                if (y < top) top = y;
                if (y + 1 > bottom) bottom = y + 1;
            }
        }
    }
    platform::Rect ink;
    if (right <= left || bottom <= top) return ink;  // nothing drawn
    ink.x = static_cast<float>(left);
    ink.y = static_cast<float>(top);
    ink.width = static_cast<float>(right - left);
    ink.height = static_cast<float>(bottom - top);
    return ink;
}

// The same sweep, restricted to the pixels one hue owns. Green is the
// second byte in both pixel layouts the surfaces use; red and blue swap
// places between them, so the caller names the two indices. A red glyph
// reads redder than blue down to its faint antialiased edge, and the
// white paper reads neither.
platform::Rect ScanInkHue(const unsigned char* pixels, int width, int height,
                          int redAt, int blueAt, bool red) {
    int left = width;
    int top = height;
    int right = 0;
    int bottom = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char* p = pixels + (static_cast<size_t>(y) *
                                                   static_cast<size_t>(width) +
                                               static_cast<size_t>(x)) *
                                                  4;
            const int r = p[redAt];
            const int b = p[blueAt];
            const bool hit = red ? r > b + 24 : b > r + 24;
            if (!hit) continue;
            if (x < left) left = x;
            if (x + 1 > right) right = x + 1;
            if (y < top) top = y;
            if (y + 1 > bottom) bottom = y + 1;
        }
    }
    platform::Rect ink;
    if (right <= left || bottom <= top) return ink;  // nothing drawn
    ink.x = static_cast<float>(left);
    ink.y = static_cast<float>(top);
    ink.width = static_cast<float>(right - left);
    ink.height = static_cast<float>(bottom - top);
    return ink;
}

#if defined(_WIN32)

// A Direct2D DC target over a top down DIB section: the software path the
// printer uses, with the pixels readable straight from memory.
class InkSurface {
public:
    bool Ready() const { return ready_; }
    platform::RenderTarget& Target() { return *target_; }

    void Begin() {
        dcTarget_->BeginDraw();
        dcTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
        dcTarget_->Clear(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    }
    void End() { dcTarget_->EndDraw(); }

    platform::Rect Ink() { return ScanInk(bits_, kSurfaceW, kSurfaceH); }
    // The two inks the per run colour test lays down. The DIB is
    // B8G8R8A8, so blue leads and red trails.
    platform::Rect RedInk() {
        return ScanInkHue(bits_, kSurfaceW, kSurfaceH, 2, 0, true);
    }
    platform::Rect BlueInk() {
        return ScanInkHue(bits_, kSurfaceW, kSurfaceH, 2, 0, false);
    }

    ~InkSurface() {
        target_.reset();  // its formats go before the factory they came from
        engine_.reset();
        if (dcTarget_) dcTarget_->Release();
        if (factory_) factory_->Release();
        if (old_) SelectObject(dc_, old_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
    }

    InkSurface() {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = kSurfaceW;
        info.bmiHeader.biHeight = -kSurfaceH;  // top down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc_ = CreateCompatibleDC(nullptr);
        if (!dc_) return;
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS,
                                   reinterpret_cast<void**>(&bits_), nullptr, 0);
        if (!bitmap_ || !bits_) return;
        old_ = SelectObject(dc_, bitmap_);
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     &factory_)) ||
            !factory_) {
            return;
        }
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_IGNORE),
            96.0f, 96.0f);
        if (FAILED(factory_->CreateDCRenderTarget(&props, &dcTarget_)) ||
            !dcTarget_) {
            return;
        }
        RECT area = {0, 0, kSurfaceW, kSurfaceH};
        if (FAILED(dcTarget_->BindDC(dc_, &area))) return;
        // Grayscale, never ClearType: the paper has no subpixel stripes.
        dcTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        engine_.reset(new platform::DWriteEngine());
        target_.reset(
            new platform::D2DTarget(dcTarget_, engine_->Factory()));
        ready_ = true;
    }

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
    unsigned char* bits_ = nullptr;
    ID2D1Factory* factory_ = nullptr;
    ID2D1DCRenderTarget* dcTarget_ = nullptr;
    std::unique_ptr<platform::DWriteEngine> engine_;
    std::unique_ptr<platform::D2DTarget> target_;
    bool ready_ = false;
};

#elif defined(__APPLE__)

// A Core Graphics bitmap context with the renderer's own flip: origin top
// left, y down, so a block placed at (x, top) lands at those pixels.
class InkSurface {
public:
    bool Ready() const { return ready_; }
    platform::RenderTarget& Target() { return *target_; }

    void Begin() {
        CGContextSaveGState(context_);
        CGContextSetRGBFillColor(context_, 1.0, 1.0, 1.0, 1.0);
        CGContextFillRect(context_, CGRectMake(0, 0, kSurfaceW, kSurfaceH));
    }
    void End() { CGContextRestoreGState(context_); }

    platform::Rect Ink() {
        return ScanInk(pixels_.data(), kSurfaceW, kSurfaceH);
    }
    // The two inks the per run colour test lays down. The bitmap is
    // RGBA, so red leads and blue trails.
    platform::Rect RedInk() {
        return ScanInkHue(pixels_.data(), kSurfaceW, kSurfaceH, 0, 2, true);
    }
    platform::Rect BlueInk() {
        return ScanInkHue(pixels_.data(), kSurfaceW, kSurfaceH, 0, 2, false);
    }

    ~InkSurface() {
        target_.reset();
        engine_.reset();
        if (context_) CGContextRelease(context_);
        if (space_) CGColorSpaceRelease(space_);
    }

    InkSurface() {
        pixels_.assign(static_cast<size_t>(kSurfaceW) *
                           static_cast<size_t>(kSurfaceH) * 4,
                       255);
        space_ = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        if (!space_) return;
        context_ = CGBitmapContextCreate(
            pixels_.data(), kSurfaceW, kSurfaceH, 8,
            static_cast<size_t>(kSurfaceW) * 4, space_,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        if (!context_) return;
        CGContextTranslateCTM(context_, 0, kSurfaceH);
        CGContextScaleCTM(context_, 1, -1);
        engine_.reset(new platform::CoreTextEngine());
        target_.reset(new platform::CoreGraphicsTarget(context_));
        ready_ = true;
    }

private:
    std::vector<unsigned char> pixels_;
    CGColorSpaceRef space_ = nullptr;
    CGContextRef context_ = nullptr;
    std::unique_ptr<platform::CoreTextEngine> engine_;
    std::unique_ptr<platform::CoreGraphicsTarget> target_;
    bool ready_ = false;
};

#endif  // _WIN32 / __APPLE__

// What every conforming engine owes a painter that draws a block: a block
// of the column's width and a real height, the ink inside the column it
// was given, a second placement that moves the ink, a narrower column
// that wraps to more lines, and an alignment that shifts the words to the
// column's far edge. None of the numbers is a font metric: every engine
// picks its own, so the grid pins the relations.
void CheckDrawLayout(TextEngine& engine, InkSurface& surface) {
    const platform::Color black{0.0f, 0.0f, 0.0f, 1.0f};

    std::vector<GlyphRun> runs = {MakeRun("table ", 14.0f),
                                  MakeRun("cell", 14.0f)};
    runs[1].font.bold = true;

    // The block reports the column it was laid out in and the height of
    // its lines.
    std::vector<TextLine> whole = engine.MeasureWrapped(runs, 0.0f, 0.0f);
    ASSERT_TRUE(!whole.empty());
    ASSERT_TRUE(whole[0].width > 20.0f);
    const float column = whole[0].width + 120.0f;

    platform::TextLayoutHandle block =
        engine.BuildLayout(runs, column, platform::TextAlign::Left);
    ASSERT_TRUE(block != nullptr);
    if (!block) return;
    EXPECT_NEAR(block->Width(), column, 0.5f);
    const float oneLine = block->Height();
    EXPECT_GT(oneLine, 0.0f);

    // The ink lands inside the column the block was placed in.
    surface.Begin();
    surface.Target().DrawLayout(*block, 20.0f, 15.0f, black);
    surface.End();
    const platform::Rect ink = surface.Ink();
    EXPECT_GT(ink.width, 0.0f);
    EXPECT_GT(ink.height, 0.0f);
    EXPECT_GE(ink.x, 19.0f);
    EXPECT_LE(ink.x + ink.width, 21.0f + column);
    // The block's own top is where its first baseline's ascent starts, and
    // a line is drawn at its top plus the ascent the engine reported. The
    // generic engine reports none, so its glyphs may sit above the top;
    // what every engine owes is that the ink stays inside the block's box.
    EXPECT_LT(ink.y, 60.0f);
    EXPECT_LE(ink.y + ink.height, 16.0f + block->Height());

    // Placing the same block further right moves its ink with it: the draw
    // is placed by the caller, not by a position baked into the layout.
    surface.Begin();
    surface.Target().DrawLayout(*block, 180.0f, 15.0f, black);
    surface.End();
    const platform::Rect moved = surface.Ink();
    EXPECT_GT(moved.x, ink.x + 100.0f);

    // A narrower column breaks the same words onto more lines, and the
    // block grows with them.
    platform::TextLayoutHandle narrow =
        engine.BuildLayout(runs, whole[0].width * 0.6f,
                           platform::TextAlign::Left);
    ASSERT_TRUE(narrow != nullptr);
    if (narrow) EXPECT_GT(narrow->Height(), oneLine * 1.5f);

    // Right alignment puts the words at the column's far edge, so the ink
    // starts a column's worth further right than the same words left
    // aligned.
    platform::TextLayoutHandle left =
        engine.BuildLayout(runs, column, platform::TextAlign::Left);
    platform::TextLayoutHandle right =
        engine.BuildLayout(runs, column, platform::TextAlign::Right);
    ASSERT_TRUE(left != nullptr);
    ASSERT_TRUE(right != nullptr);
    if (left && right) {
        surface.Begin();
        surface.Target().DrawLayout(*left, 10.0f, 15.0f, black);
        surface.End();
        const platform::Rect leftInk = surface.Ink();
        surface.Begin();
        surface.Target().DrawLayout(*right, 10.0f, 15.0f, black);
        surface.End();
        const platform::Rect rightInk = surface.Ink();
        EXPECT_GT(rightInk.x, leftInk.x + 40.0f);
    }

    // The caret's questions, which the layout cache asks of the block
    // instead of holding the platform's text object: the block names its
    // lines, a line knows its text range and its height, a point answers
    // a position, and a position answers a caret. Relations only: every
    // engine picks its own glyph widths.
    ASSERT_TRUE(block->LineCount() >= 1u);
    float stacked = 0.0f;
    for (uint32_t i = 0; i < block->LineCount(); ++i) {
        EXPECT_GT(block->LineHeightAt(i), 0.0f);
        stacked += block->LineHeightAt(i);
    }
    EXPECT_NEAR(stacked, block->Height(), 1.0f);

    uint32_t caretLineStart = 99u;
    uint32_t caretLineEnd = 0u;
    ASSERT_TRUE(block->LineTextRange(0, block->Width(), &caretLineStart,
                                     &caretLineEnd));
    EXPECT_EQ(caretLineStart, 0u);
    EXPECT_TRUE(caretLineEnd > 0u);

    const platform::TextHit caretAtStart = block->CaretAtOffset(0);
    EXPECT_NEAR(caretAtStart.x, 0.0f, 0.5f);
    EXPECT_NEAR(caretAtStart.y, 0.0f, 0.5f);
    EXPECT_GT(caretAtStart.height, 0.0f);

    const platform::TextHit caretAtEnd = block->CaretAtOffset(caretLineEnd);
    EXPECT_GT(caretAtEnd.x, caretAtStart.x + 1.0f);
    EXPECT_NEAR(caretAtEnd.y, caretAtStart.y, 0.5f);
    EXPECT_NEAR(caretAtEnd.height, caretAtStart.height, 0.5f);

    // A point halfway along the first line answers a position inside it,
    // on the same line the caret does.
    const platform::TextHit midHit = block->HitTestPoint(
        (caretAtStart.x + caretAtEnd.x) * 0.5f,
        caretAtStart.y + caretAtStart.height * 0.5f);
    EXPECT_TRUE(midHit.inside);
    EXPECT_TRUE(midHit.textPosition > 0u);
    EXPECT_TRUE(midHit.textPosition < caretLineEnd);
    EXPECT_NEAR(midHit.height, caretAtStart.height, 0.5f);

    // Runs that carry their own ink: one block, two colours, and each
    // colour has to land on the paper where its own run sits. The scan
    // reads the two hues apart, so a target that paints the whole block in
    // the fallback colour fails here, and so does one that paints the
    // colours swapped or both on the same run.
    const platform::Color red{0.85f, 0.05f, 0.05f, 1.0f};
    const platform::Color blue{0.05f, 0.05f, 0.85f, 1.0f};
    std::vector<GlyphRun> inked = {MakeRun("iiii", 24.0f),
                                   MakeRun("llll", 24.0f)};
    inked[0].color = red;
    inked[1].color = blue;
    engine.BeginLayout();
    std::vector<TextLine> inkedLines =
        engine.MeasureWrapped(inked, 0.0f, 0.0f);
    ASSERT_TRUE(!inkedLines.empty());
    if (!inkedLines.empty()) {
        const TextLine& inkedLine = inkedLines[0];
        ASSERT_TRUE(inkedLine.runs.size() >= 2);
        if (inkedLine.runs.size() >= 2) {
            // Where the red run ends and the blue one starts, measured by
            // the engine that will draw them.
            const float split = inkedLine.runs[1].x;
            platform::TextLayoutHandle inkedBlock = engine.BuildLayout(
                inked, inkedLine.width + 60.0f, platform::TextAlign::Left);
            ASSERT_TRUE(inkedBlock != nullptr);
            if (inkedBlock) {
                const float placeX = 10.0f;
                surface.Begin();
                surface.Target().DrawLayout(*inkedBlock, placeX, 15.0f,
                                            black);
                surface.End();
                const platform::Rect redInk = surface.RedInk();
                const platform::Rect blueInk = surface.BlueInk();
                EXPECT_GT(redInk.width, 0.0f);
                EXPECT_GT(blueInk.width, 0.0f);
                EXPECT_GT(blueInk.x, redInk.x + 1.0f);
                // Each colour stays inside its own run's x range, with a
                // pixel of slack for the antialiased edge.
                EXPECT_GE(redInk.x, placeX - 1.0f);
                EXPECT_LE(redInk.x + redInk.width, placeX + split + 2.0f);
                EXPECT_GE(blueInk.x, placeX + split - 2.0f);
                EXPECT_LE(blueInk.x + blueInk.width,
                          placeX + inkedLine.width + 2.0f);
            }
        }
    }

    // A run with no colour of its own still draws in the block's colour:
    // the draw parameter is the fallback, so a one colour block is what it
    // always was.
    surface.Begin();
    surface.Target().DrawLayout(*block, 20.0f, 15.0f, red);
    surface.End();
    const platform::Rect fallbackInk = surface.RedInk();
    EXPECT_GT(fallbackInk.width, 0.0f);
    EXPECT_GE(fallbackInk.x, 19.0f);
    EXPECT_LE(fallbackInk.x + fallbackInk.width, 21.0f + column);
}

}  // namespace

TEST(SeamConformance, FakeEngineFollowsTheSeam) {
    fake::FakeTextEngine engine;
    CheckTextEngine(engine);
}

// The portable block on a real target: the seam's own line layout, drawn
// line by line through DrawText, which is the path a target that does not
// recognise a block takes.
TEST(SeamConformance, FakeEngineDrawsALaidOutBlock) {
    fake::FakeTextEngine engine;
    InkSurface surface;
    ASSERT_TRUE(surface.Ready());
    if (!surface.Ready()) return;
    CheckDrawLayout(engine, surface);
}

#ifdef __APPLE__
TEST(SeamConformance, CoreTextEngineFollowsTheSeam) {
    platform::CoreTextEngine engine;
    CheckTextEngine(engine);
}

// The Core Text block on the Core Graphics target: the measured CTLines,
// drawn with CTLineDraw, the mac painter's own text path.
TEST(SeamConformance, CoreTextEngineDrawsALaidOutBlock) {
    platform::CoreTextEngine engine;
    InkSurface surface;
    ASSERT_TRUE(surface.Ready());
    if (!surface.Ready()) return;
    CheckDrawLayout(engine, surface);
}
#endif

#ifdef _WIN32
TEST(SeamConformance, DWriteEngineFollowsTheSeam) {
    platform::DWriteEngine engine;
    CheckTextEngine(engine);
}

// The DirectWrite block on the Direct2D target: one DrawTextLayout, the
// call the table used to make for itself.
TEST(SeamConformance, DWriteEngineDrawsALaidOutBlock) {
    platform::DWriteEngine engine;
    InkSurface surface;
    ASSERT_TRUE(surface.Ready());
    if (!surface.Ready()) return;
    CheckDrawLayout(engine, surface);
}
#endif
