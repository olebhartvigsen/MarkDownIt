// The Windows renderer, driven through the Batch 5.3 test seam.
//
// What the tests pin is the platform parity the Batch 3.3 twin promised:
// RectsForRange answers from the same shared rule
// (layout/selection_rects.h) the mac renderer answers from, the
// frame's selection washes ARE those rects, and the walk contract
// holds: Measure and Render advance one shared layout, so the band a
// heading, an hr or a table cell charges is the band the frame
// paints. The engine is a fixed grid (the same arithmetic the mac
// suite's FakeEngine spells), installed through
// SetWalkEngineForTesting; DirectWrite is faked by the COM shims
// below, which implement the real interface shape so the same file
// compiles against the real SDK on the Windows runner and against
// the winsmoke stubs in the headless harness. Production never
// calls the seam, so the shipped build is untouched.

#include "gtest_lite.h"

#include "../src/renderer.h"
#include "../src/windows/text_engine_win.h"
#include "../src/dom.h"
#include "../src/theme.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <string>
#include <vector>

using platform::FontSpec;
using platform::GlyphRun;
using platform::Rect;
using platform::TextEngine;
using platform::TextLine;

// Whether the build sees the winsmoke stub SDK (the headless
// harness defines WINSMOKE_STUB). The guard blocks below spell the
// DirectWrite and D2D1 pure virtuals that only the real SDK
// declares, so the fakes derive the real interfaces on the Windows
// runner; against the stubs those members would name types the
// stub never spelled, and the stub's own smaller pure set is fully
// covered by the unguarded members above them.
#ifndef WINSMOKE_STUB
#define MARKDOWNIT_REAL_SDK 1
#endif

namespace {

// ---------------------------------------------------------------------------
// The fixed grid engine: the twin of the mac suite's FakeEngine.
// A character is half an em wide, a line is one em tall, every hard
// break starts a new line, and wrapping happens at the character
// that no longer fits.
// ---------------------------------------------------------------------------
class FakeEngine : public TextEngine {
public:
    struct Call {
        float maxWidth = 0.0f;
        float size = 0.0f;
        std::string text;
    };
    std::vector<Call> calls;

    std::vector<TextLine> MeasureWrapped(const std::vector<GlyphRun>& runs,
                                         float maxWidth,
                                         float lineSpacing) override {
        (void)lineSpacing;

        std::string text;
        for (const GlyphRun& run : runs) text += run.text;
        if (text.empty()) return {};

        Call call;
        call.maxWidth = maxWidth;
        call.size = runs[0].font.size;
        call.text = text;
        calls.push_back(call);

        const float charWidth = runs[0].font.size * 0.5f;
        const float width = maxWidth > 0.0f ? maxWidth : 1.0e9f;
        size_t perLine = static_cast<size_t>(std::floor(width / charWidth));
        if (perLine < 1) perLine = 1;

        // One line per hard break, wrapped at the width. The runs are
        // sliced at the line window the way the real engine slices
        // them, so the styles travel with the text.
        std::vector<TextLine> lines;
        size_t start = 0;
        while (start < text.size()) {
            const size_t hard = text.find('\n', start);
            const size_t stop =
                hard == std::string::npos ? text.size() : hard + 1;
            size_t pos = start;
            while (pos < stop) {
                size_t count = stop - pos;
                if (count > perLine) count = perLine;
                const size_t lineEnd = pos + count;

                TextLine line;
                line.width = static_cast<float>(count) * charWidth;
                line.height = runs[0].font.size;
                line.ascent = line.height;

                size_t offset = 0;
                for (const GlyphRun& run : runs) {
                    const size_t runEnd = offset + run.text.size();
                    if (runEnd > pos && offset < lineEnd) {
                        const size_t from = pos > offset ? pos - offset : 0;
                        const size_t to = lineEnd < runEnd
                                             ? lineEnd - offset
                                             : run.text.size();
                        GlyphRun sliced = run;
                        sliced.text = run.text.substr(from, to - from);
                        sliced.x = static_cast<float>(offset + from - start) *
                                   charWidth;
                        line.runs.push_back(sliced);
                    }
                    offset = runEnd;
                }
                lines.push_back(line);
                pos = lineEnd;
            }
            start = stop;
        }
        return lines;
    }

    uint32_t HitTestOffset(const TextLine& line, float x,
                           bool* outTrailing) const override {
        if (outTrailing) *outTrailing = false;
        const float charWidth = CharWidth(line);
        if (charWidth <= 0.0f) return 0;
        const long index = static_cast<long>(x / charWidth);
        return index <= 0 ? 0u : static_cast<uint32_t>(index);
    }

    Rect OffsetToRect(const TextLine& line, uint32_t offset) const override {
        const float charWidth = CharWidth(line);
        Rect rect;
        rect.x = static_cast<float>(offset) * charWidth;
        rect.width = charWidth;
        rect.height = line.height;
        return rect;
    }

    float LineHeight(const FontSpec& font) const override {
        return font.size;
    }

private:
    static float CharWidth(const TextLine& line) {
        for (const GlyphRun& run : line.runs) {
            if (run.font.size > 0.0f) return run.font.size * 0.5f;
        }
        return 0.0f;
    }
};

// ---------------------------------------------------------------------------
// The DirectWrite fakes. The unguarded members cover the pure set the
// winsmoke stub declares (the surface renderer.cpp uses); the
// MARKDOWNIT_REAL_SDK blocks complete the real SDK's interfaces so
// the same class is concrete on the Windows runner. Bodies of the
// guarded members never run in these tests.
//
// FakeTextFormat and FakeTextLayout are two separate bases, the way
// the two real interfaces are: both spell the format level, so a
// layout can stand in for a format (CreateTextLayout hands it the
// source format's family and size), and each carries its own copy of
// the format members instead of sharing a C++ base the interfaces
// do not share.
// ---------------------------------------------------------------------------

struct FakeFmtState {
    std::wstring family;
    float size = 16.0f;
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_WORD_WRAPPING wrap = DWRITE_WORD_WRAPPING_WRAP;
};

class FakeTextFormat : public IDWriteTextFormat {
public:
    FakeFmtState st;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        (void)riid;
        if (!ppv) return 0x80004003L;
        *ppv = this;
        return 0;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE SetTextAlignment(
        DWRITE_TEXT_ALIGNMENT textAlignment) override {
        st.align = textAlignment;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetParagraphAlignment(
        DWRITE_PARAGRAPH_ALIGNMENT) override {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetWordWrapping(
        DWRITE_WORD_WRAPPING wordWrapping) override {
        st.wrap = wordWrapping;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetLineSpacing(
        DWRITE_LINE_SPACING_METHOD method, FLOAT, FLOAT) override {
        (void)method;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetLineSpacing(
        DWRITE_LINE_SPACING_METHOD* method, FLOAT* height,
        FLOAT* baseline) override {
        if (method) *method = DWRITE_LINE_SPACING_METHOD_DEFAULT;
        if (height) *height = st.size;
        if (baseline) *baseline = st.size * 0.8f;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetFontFamilyName(WCHAR* fontFamilyName,
                                                UINT32 nameLength) override {
        if (!fontFamilyName || nameLength == 0) return 0x80070057L;
        const size_t n = st.family.size() < nameLength - 1
                             ? st.family.size()
                             : nameLength - 1;
        for (size_t i = 0; i < n; ++i) fontFamilyName[i] = st.family[i];
        fontFamilyName[n] = L'\0';
        return 0;
    }
    // The stub spells the getters const, the real header does not,
    // so both spellings exist and answer the same numbers.
    UINT32 STDMETHODCALLTYPE GetFontFamilyNameLength() const {
        return static_cast<UINT32>(st.family.size());
    }
    UINT32 STDMETHODCALLTYPE GetFontFamilyNameLength() {
        return static_cast<UINT32>(st.family.size());
    }
    // The renderer's FontBoxOf asks for a collection first and stops
    // when the answer is empty, so no collection is ever spelled.
    HRESULT STDMETHODCALLTYPE GetFontCollection(
        IDWriteFontCollection** fontCollection) override {
        if (fontCollection) *fontCollection = nullptr;
        return 0;
    }
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() const {
        return st.align;
    }
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() {
        return st.align;
    }
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() const {
        return st.wrap;
    }
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() {
        return st.wrap;
    }
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() const {
        return DWRITE_FONT_WEIGHT_NORMAL;
    }
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() {
        return DWRITE_FONT_WEIGHT_NORMAL;
    }
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() const {
        return DWRITE_FONT_STYLE_NORMAL;
    }
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() {
        return DWRITE_FONT_STYLE_NORMAL;
    }
    DWRITE_FONT_STRETCH STDMETHODCALLTYPE GetFontStretch() const {
        return DWRITE_FONT_STRETCH_NORMAL;
    }
    DWRITE_FONT_STRETCH STDMETHODCALLTYPE GetFontStretch() {
        return DWRITE_FONT_STRETCH_NORMAL;
    }
    FLOAT STDMETHODCALLTYPE GetFontSize() const { return st.size; }
    FLOAT STDMETHODCALLTYPE GetFontSize() { return st.size; }

    // Real SDK only: reading direction, flow, tabs, trimming and the
    // locale getters the real format adds.
    HRESULT STDMETHODCALLTYPE SetReadingDirection(
        DWRITE_READING_DIRECTION) { return 0; }
    HRESULT STDMETHODCALLTYPE SetFlowDirection(DWRITE_FLOW_DIRECTION) {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetIncrementalTabStop(FLOAT) { return 0; }
    HRESULT STDMETHODCALLTYPE SetTrimming(
        const DWRITE_TRIMMING*, IDWriteInlineObject*) { return 0; }
    HRESULT STDMETHODCALLTYPE GetTrimming(
        DWRITE_TRIMMING*, IDWriteInlineObject**) { return 0; }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetReadingDirection() {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    DWRITE_FLOW_DIRECTION STDMETHODCALLTYPE GetFlowDirection() {
        return DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM;
    }
    FLOAT STDMETHODCALLTYPE GetIncrementalTabStop() { return 4.0f * st.size; }
    DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() {
        return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    }
    UINT32 STDMETHODCALLTYPE GetLocaleNameLength() { return 0; }
    HRESULT STDMETHODCALLTYPE GetLocaleName(WCHAR*, UINT32) { return 0; }
};

class FakeTextLayout : public IDWriteTextLayout {
public:
    FakeTextLayout(const std::wstring& text, FakeTextFormat* src,
                   float maxW, float maxH)
        : text_(text), maxW_(maxW) {
        (void)maxH;
        if (src) st = src->st;
    }

    FakeFmtState st;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        (void)riid;
        if (!ppv) return 0x80004003L;
        *ppv = this;
        return 0;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    // The format level, spelled again: the real layout is a format.
    HRESULT STDMETHODCALLTYPE SetTextAlignment(
        DWRITE_TEXT_ALIGNMENT textAlignment) override {
        st.align = textAlignment;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetParagraphAlignment(
        DWRITE_PARAGRAPH_ALIGNMENT) override {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetWordWrapping(
        DWRITE_WORD_WRAPPING wordWrapping) override {
        st.wrap = wordWrapping;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetLineSpacing(
        DWRITE_LINE_SPACING_METHOD, FLOAT, FLOAT) override {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetLineSpacing(
        DWRITE_LINE_SPACING_METHOD* method, FLOAT* height,
        FLOAT* baseline) override {
        if (method) *method = DWRITE_LINE_SPACING_METHOD_DEFAULT;
        if (height) *height = st.size;
        if (baseline) *baseline = st.size * 0.8f;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetFontFamilyName(WCHAR* fontFamilyName,
                                                UINT32 nameLength) override {
        if (!fontFamilyName || nameLength == 0) return 0x80070057L;
        const size_t n = st.family.size() < nameLength - 1
                             ? st.family.size()
                             : nameLength - 1;
        for (size_t i = 0; i < n; ++i) fontFamilyName[i] = st.family[i];
        fontFamilyName[n] = L'\0';
        return 0;
    }
    UINT32 STDMETHODCALLTYPE GetFontFamilyNameLength() const {
        return static_cast<UINT32>(st.family.size());
    }
    UINT32 STDMETHODCALLTYPE GetFontFamilyNameLength() {
        return static_cast<UINT32>(st.family.size());
    }
    HRESULT STDMETHODCALLTYPE GetFontCollection(
        IDWriteFontCollection** fontCollection) override {
        if (fontCollection) *fontCollection = nullptr;
        return 0;
    }
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() const {
        return st.align;
    }
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() {
        return st.align;
    }
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() const {
        return st.wrap;
    }
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() {
        return st.wrap;
    }
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() const {
        return DWRITE_FONT_WEIGHT_NORMAL;
    }
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() {
        return DWRITE_FONT_WEIGHT_NORMAL;
    }
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() const {
        return DWRITE_FONT_STYLE_NORMAL;
    }
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() {
        return DWRITE_FONT_STYLE_NORMAL;
    }
    DWRITE_FONT_STRETCH STDMETHODCALLTYPE GetFontStretch() const {
        return DWRITE_FONT_STRETCH_NORMAL;
    }
    DWRITE_FONT_STRETCH STDMETHODCALLTYPE GetFontStretch() {
        return DWRITE_FONT_STRETCH_NORMAL;
    }
    FLOAT STDMETHODCALLTYPE GetFontSize() const { return st.size; }
    FLOAT STDMETHODCALLTYPE GetFontSize() { return st.size; }

    // The layout level the frame drives. GetMetrics answers with
    // the same grid arithmetic the walk's engine answers with, so
    // whatever still asks a layout reads the band the walk charged.
    HRESULT STDMETHODCALLTYPE GetMetrics(
        DWRITE_TEXT_METRICS* textMetrics) override {
        if (!textMetrics) return 0x80004003L;
        // A real layout wraps at its box: the width it reports is the
        // widest line inside the box, never the whole string laid
        // out flat. The fake answers the same shape so pieces that
        // share a line measure honestly.
        const float charWidth = st.size * 0.5f;
        const size_t chars = text_.size();
        size_t per = chars > 0 ? chars : 1;
        UINT32 lines = 1;
        if (maxW_ > 0.0f && charWidth > 0.0f && chars > 0) {
            const size_t perLine =
                static_cast<size_t>(maxW_ / charWidth);
            per = perLine < 1 ? 1 : perLine;
            lines = static_cast<UINT32>((chars + per - 1) / per);
            if (lines < 1) lines = 1;
        }
        const size_t widest = chars < per ? chars : per;
        const float width = static_cast<float>(widest) * charWidth;
        textMetrics->left = 0.0f;
        textMetrics->top = 0.0f;
        textMetrics->width = width;
        textMetrics->widthIncludingTrailingWhitespace = width;
        textMetrics->height = static_cast<float>(lines) * st.size;
        textMetrics->layoutWidth = maxW_;
        textMetrics->layoutHeight = 0.0f;
        textMetrics->maxBidiReorderingDepth = 1;
        textMetrics->lineCount = lines;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetLineMetrics(
        DWRITE_LINE_METRICS* lineMetrics, UINT32 maxLineCount,
        UINT32* actualLineCount) override {
        if (!actualLineCount) return 0x80004003L;
        const UINT32 want = static_cast<UINT32>(text_.size());
        const UINT32 n = want < maxLineCount ? want : maxLineCount;
        for (UINT32 i = 0; i < n; ++i) {
            if (lineMetrics) {
                lineMetrics[i].length = 1;
                lineMetrics[i].trailingWhitespaceLength = 0;
                lineMetrics[i].newlineLength = 0;
                lineMetrics[i].height = st.size;
                lineMetrics[i].baseline = st.size * 0.8f;
                lineMetrics[i].isTrimmed = 0;
            }
        }
        *actualLineCount = want;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetFontSize(FLOAT, DWRITE_TEXT_RANGE) override {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetFontFamilyName(const WCHAR*,
                                               DWRITE_TEXT_RANGE) override {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetFontWeight(DWRITE_FONT_WEIGHT,
                                           DWRITE_TEXT_RANGE) override {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetFontStyle(DWRITE_FONT_STYLE,
                                          DWRITE_TEXT_RANGE) override {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetUnderline(BOOL, DWRITE_TEXT_RANGE) override {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetStrikethrough(BOOL,
                                               DWRITE_TEXT_RANGE) override {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetDrawingEffect(IUnknown*,
                                              DWRITE_TEXT_RANGE) override {
        return 0;
    }
    // The hit tests answer an empty hand: selection and caret geometry
    // come from the walk's records now, so any leftover caller paints
    // nothing instead of drifting from them.
    HRESULT STDMETHODCALLTYPE HitTestTextRange(
        UINT32, UINT32, FLOAT, FLOAT, DWRITE_HIT_TEST_METRICS* m,
        UINT32, UINT32* actualHitTestMetricsCount) override {
        if (m) {
            m->left = 0; m->top = 0; m->width = 0; m->height = 0;
            m->textPosition = 0; m->length = 0; m->isTrimmed = 0;
        }
        if (actualHitTestMetricsCount) *actualHitTestMetricsCount = 0;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE HitTestPoint(FLOAT, FLOAT, BOOL* isTrailingHit,
                                           BOOL* isInside,
                                           DWRITE_HIT_TEST_METRICS* m) override {
        if (isTrailingHit) *isTrailingHit = 0;
        if (isInside) *isInside = 0;
        if (m) {
            m->left = 0; m->top = 0; m->width = 0; m->height = 0;
            m->textPosition = 0; m->length = 0; m->isTrimmed = 0;
        }
        return 0;
    }
    HRESULT STDMETHODCALLTYPE HitTestTextPosition(
        UINT32, BOOL, FLOAT* pointX, FLOAT* pointY,
        DWRITE_HIT_TEST_METRICS* m) override {
        if (pointX) *pointX = 0.0f;
        if (pointY) *pointY = 0.0f;
        if (m) {
            m->left = 0; m->top = 0; m->width = 0; m->height = 0;
            m->textPosition = 0; m->length = 0; m->isTrimmed = 0;
        }
        return 0;
    }

    // Real SDK only: the format getters the real layout redeclares
    // with a position argument, plus the rest of its own surface.
    HRESULT STDMETHODCALLTYPE SetReadingDirection(
        DWRITE_READING_DIRECTION) { return 0; }
    HRESULT STDMETHODCALLTYPE SetFlowDirection(DWRITE_FLOW_DIRECTION) {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetIncrementalTabStop(FLOAT) { return 0; }
    HRESULT STDMETHODCALLTYPE SetTrimming(
        const DWRITE_TRIMMING*, IDWriteInlineObject*) { return 0; }
    HRESULT STDMETHODCALLTYPE GetTrimming(
        DWRITE_TRIMMING*, IDWriteInlineObject**) { return 0; }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetReadingDirection() {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    DWRITE_FLOW_DIRECTION STDMETHODCALLTYPE GetFlowDirection() {
        return DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM;
    }
    FLOAT STDMETHODCALLTYPE GetIncrementalTabStop() { return 4.0f * st.size; }
    DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() {
        return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    }
    UINT32 STDMETHODCALLTYPE GetLocaleNameLength() { return 0; }
    HRESULT STDMETHODCALLTYPE GetLocaleName(WCHAR*, UINT32) { return 0; }
    HRESULT STDMETHODCALLTYPE SetMaxWidth(FLOAT) { return 0; }
    HRESULT STDMETHODCALLTYPE SetMaxHeight(FLOAT) { return 0; }
    HRESULT STDMETHODCALLTYPE SetFontCollection(
        IDWriteFontCollection*, DWRITE_TEXT_RANGE) { return 0; }
    HRESULT STDMETHODCALLTYPE SetFontStretch(DWRITE_FONT_STRETCH,
                                             DWRITE_TEXT_RANGE) { return 0; }
    HRESULT STDMETHODCALLTYPE SetInlineObject(IDWriteInlineObject*,
                                             DWRITE_TEXT_RANGE) { return 0; }
    HRESULT STDMETHODCALLTYPE SetTypography(IDWriteTypography*,
                                            DWRITE_TEXT_RANGE) { return 0; }
    HRESULT STDMETHODCALLTYPE SetLocaleName(const WCHAR*,
                                           DWRITE_TEXT_RANGE) { return 0; }
    FLOAT STDMETHODCALLTYPE GetMaxWidth() { return maxW_; }
    FLOAT STDMETHODCALLTYPE GetMaxHeight() { return 0.0f; }
    HRESULT STDMETHODCALLTYPE GetFontCollection(
        UINT32, IDWriteFontCollection** fontCollection, DWRITE_TEXT_RANGE*) {
        if (fontCollection) *fontCollection = nullptr;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetFontFamilyNameLength(
        UINT32, UINT32* nameLength, DWRITE_TEXT_RANGE*) {
        if (nameLength) *nameLength = static_cast<UINT32>(st.family.size());
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetFontFamilyName(UINT32, WCHAR* name,
                                               UINT32 nameSize,
                                               DWRITE_TEXT_RANGE*) {
        return GetFontFamilyName(name, nameSize);
    }
    HRESULT STDMETHODCALLTYPE GetFontWeight(UINT32, DWRITE_FONT_WEIGHT* w,
                                           DWRITE_TEXT_RANGE*) {
        if (w) *w = DWRITE_FONT_WEIGHT_NORMAL;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetFontStyle(UINT32, DWRITE_FONT_STYLE* s,
                                           DWRITE_TEXT_RANGE*) {
        if (s) *s = DWRITE_FONT_STYLE_NORMAL;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetFontStretch(UINT32, DWRITE_FONT_STRETCH* s,
                                             DWRITE_TEXT_RANGE*) {
        if (s) *s = DWRITE_FONT_STRETCH_NORMAL;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetFontSize(UINT32, FLOAT* s, DWRITE_TEXT_RANGE*) {
        if (s) *s = st.size;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetUnderline(UINT32, BOOL* b, DWRITE_TEXT_RANGE*) {
        if (b) *b = 0;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetStrikethrough(UINT32, BOOL* b,
                                              DWRITE_TEXT_RANGE*) {
        if (b) *b = 0;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetDrawingEffect(UINT32, IUnknown** e,
                                               DWRITE_TEXT_RANGE*) {
        if (e) *e = nullptr;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetInlineObject(UINT32, IDWriteInlineObject**,
                                              DWRITE_TEXT_RANGE*) {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetTypography(UINT32, IDWriteTypography** t,
                                           DWRITE_TEXT_RANGE*) {
        (void)t;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetLocaleNameLength(
        UINT32, UINT32* nameLength, DWRITE_TEXT_RANGE*) {
        if (nameLength) *nameLength = 0;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32, WCHAR* n, UINT32 sz,
                                            DWRITE_TEXT_RANGE*) {
        (void)n; (void)sz;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Draw(void*, IDWriteTextRenderer*, FLOAT,
                                   FLOAT) {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetOverhangMetrics(DWRITE_OVERHANG_METRICS* o) {
        if (o) *o = DWRITE_OVERHANG_METRICS();
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetClusterMetrics(DWRITE_CLUSTER_METRICS*,
                                                UINT32, UINT32* out) {
        if (out) *out = 0;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE DetermineMinWidth(FLOAT* minWidth) {
        if (minWidth) *minWidth = 0.0f;
        return 0;
    }

private:
    std::wstring text_;
    float maxW_ = 0.0f;
};

class FakeDWriteFactory : public IDWriteFactory {
public:
    std::vector<FakeTextFormat*> formats;
    std::vector<FakeTextLayout*> layouts;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        (void)riid;
        if (!ppv) return 0x80004003L;
        *ppv = this;
        return 0;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE CreateTextFormat(
        const WCHAR* fontFamilyName, IDWriteFontCollection*,
        DWRITE_FONT_WEIGHT, DWRITE_FONT_STYLE, DWRITE_FONT_STRETCH,
        FLOAT fontSize, const WCHAR* localeName,
        IDWriteTextFormat** textFormat) override {
        (void)localeName;
        if (!textFormat) return 0x80004003L;
        FakeTextFormat* fmt = new FakeTextFormat();
        fmt->st.size = fontSize;
        if (fontFamilyName) {
            for (const WCHAR* p = fontFamilyName; *p; ++p) {
                fmt->st.family.push_back(*p);
            }
        }
        formats.push_back(fmt);
        *textFormat = fmt;
        return 0;
    }

    HRESULT STDMETHODCALLTYPE CreateTextLayout(
        const WCHAR* string, UINT32 stringLength, IDWriteTextFormat* textFormat,
        FLOAT maxWidth, FLOAT maxHeight,
        IDWriteTextLayout** textLayout) override {
        (void)maxHeight;
        if (!textLayout) return 0x80004003L;
        std::wstring text;
        if (string) {
            for (UINT32 i = 0; i < stringLength; ++i) text.push_back(string[i]);
        }
        FakeTextFormat* src = static_cast<FakeTextFormat*>(textFormat);
        FakeTextLayout* layout = new FakeTextLayout(text, src, maxWidth, 0.0f);
        layouts.push_back(layout);
        *textLayout = layout;
        return 0;
    }

    // Real SDK only: the rest of the factory's pure surface. None of
    // it is reached by these tests; the bodies only complete the
    // interface so the class is concrete on the runner.
    HRESULT STDMETHODCALLTYPE GetSystemFontCollection(
        IDWriteFontCollection** fontCollection, BOOL checkForUpdates) {
        (void)checkForUpdates;
        if (fontCollection) *fontCollection = nullptr;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE CreateCustomFontCollection(
        IDWriteFontCollectionLoader* collectionLoader, const void* collectionKey,
        UINT32 collectionKeySize, IDWriteFontCollection** fontCollection) {
        (void)collectionLoader; (void)collectionKey; (void)collectionKeySize;
        if (fontCollection) *fontCollection = nullptr;
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE CreateGdiCompatibleTextLayout(
        const WCHAR* string, UINT32 stringLength, IDWriteTextFormat* textFormat,
        FLOAT maxWidth, FLOAT maxHeight, FLOAT pixelsPerDip,
        const DWRITE_MATRIX* transform, BOOL useGdiNatural,
        IDWriteTextLayout** textLayout) {
        (void)string; (void)stringLength; (void)textFormat; (void)maxWidth;
        (void)maxHeight; (void)pixelsPerDip; (void)transform;
        (void)useGdiNatural;
        if (textLayout) *textLayout = nullptr;
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE RegisterFontCollectionLoader(
        IDWriteFontCollectionLoader*) { return 0; }
    HRESULT STDMETHODCALLTYPE UnregisterFontCollectionLoader(
        IDWriteFontCollectionLoader*) { return 0; }
    HRESULT STDMETHODCALLTYPE CreateFontFileReference(
        const WCHAR* filePath, const FILETIME* lastWriteTime,
        IDWriteFontFile** fontFile) {
        (void)filePath; (void)lastWriteTime;
        if (fontFile) *fontFile = nullptr;
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE CreateCustomFontFileReference(
        const void* fontFileReferenceKey, UINT32 fontFileReferenceKeySize,
        IDWriteFontFileLoader* fontFileLoader, IDWriteFontFile** fontFile) {
        (void)fontFileReferenceKey; (void)fontFileReferenceKeySize;
        (void)fontFileLoader;
        if (fontFile) *fontFile = nullptr;
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE CreateFontFace(
        DWRITE_FONT_FACE_TYPE fontFaceType, UINT32 numberOfFiles,
        IDWriteFontFile* const* fontFiles, UINT32 faceIndex,
        DWRITE_FONT_SIMULATIONS fontFaceSimulationFlags,
        IDWriteFontFace** fontFace) {
        (void)fontFaceType; (void)numberOfFiles; (void)fontFiles;
        (void)faceIndex; (void)fontFaceSimulationFlags;
        if (fontFace) *fontFace = nullptr;
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE CreateRenderingParams(
        IDWriteRenderingParams** renderingParams) {
        if (renderingParams) *renderingParams = nullptr;
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE CreateMonitorRenderingParams(
        HMONITOR monitor, IDWriteRenderingParams** renderingParams) {
        (void)monitor;
        if (renderingParams) *renderingParams = nullptr;
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE CreateCustomRenderingParams(
        FLOAT gamma, FLOAT enhancedContrast, FLOAT clearTypeLevel,
        DWRITE_PIXEL_GEOMETRY pixelGeometry, DWRITE_RENDERING_MODE renderingMode,
        IDWriteRenderingParams** renderingParams) {
        (void)gamma; (void)enhancedContrast; (void)clearTypeLevel;
        (void)pixelGeometry; (void)renderingMode;
        if (renderingParams) *renderingParams = nullptr;
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE RegisterFontFileLoader(IDWriteFontFileLoader*) {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE UnregisterFontFileLoader(IDWriteFontFileLoader*) {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE CreateTypography(IDWriteTypography** typography) {
        if (typography) *typography = nullptr;
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE GetGdiInterop(IDWriteGdiInterop** gdiInterop) {
        if (gdiInterop) *gdiInterop = nullptr;
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE CreateEllipsisTrimmingSign(
        IDWriteTextFormat*, IDWriteInlineObject**) {
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE CreateTextAnalyzer(IDWriteTextAnalyzer**) {
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE CreateNumberSubstitution(
        DWRITE_NUMBER_SUBSTITUTION_METHOD, const WCHAR*, BOOL,
        IDWriteNumberSubstitution**) {
        return 0x80004001L;
    }
    HRESULT STDMETHODCALLTYPE CreateGlyphRunAnalysis(
        const DWRITE_GLYPH_RUN*, FLOAT, const DWRITE_MATRIX*,
        DWRITE_RENDERING_MODE, DWRITE_MEASURING_MODE, FLOAT, FLOAT,
        IDWriteGlyphRunAnalysis**) {
        return 0x80004001L;
    }
};

// ---------------------------------------------------------------------------
// The D2D fakes: a brush that remembers its color and a target that
// records what the frame paints, so a repaint can be checked without
// a screen.
// ---------------------------------------------------------------------------
class FakeSolidBrush : public ID2D1SolidColorBrush {
public:
    D2D1_COLOR_F color;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        (void)riid;
        if (!ppv) return 0x80004003L;
        *ppv = this;
        return 0;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    void SetColor(const D2D1_COLOR_F& c) { color = c; }
    void GetFactory(ID2D1Factory** factory) const {
        if (factory) *factory = nullptr;
    }
    void SetColor(const D2D1_COLOR_F* c) {
        if (c) color = *c;
    }
    // Real SDK only: the brush interface beyond the stub.
    void SetOpacity(FLOAT) {}
    void SetTransform(const D2D1_MATRIX_3X2_F*) {}
    FLOAT GetOpacity() const { return 1.0f; }
    void GetTransform(D2D1_MATRIX_3X2_F* out) const {
        if (out) *out = D2D1_MATRIX_3X2_F();
    }
    D2D1_COLOR_F GetColor() const { return color; }
};

class FakeRenderTarget : public ID2D1RenderTarget {
public:
    struct Fill {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
    };
    std::vector<Fill> fills;
    int clears = 0;
    int clips = 0;
    D2D1_SIZE_F size = {800.0f, 600.0f};

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        (void)riid;
        if (!ppv) return 0x80004003L;
        *ppv = this;
        return 0;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    D2D1_SIZE_F GetSize() const { return size; }

    // The stub spells the rect takes by reference, the real header
    // takes them by pointer behind inline helpers, so both
    // spellings exist and record the same ink.
    void SetTransform(const D2D1_MATRIX_3X2_F*) {}
    void GetTransform(D2D1_MATRIX_3X2_F* out) const {
        if (out) *out = D2D1_MATRIX_3X2_F();
    }
    void Clear(const D2D1_COLOR_F*) { ++clears; }

    HRESULT CreateSolidColorBrush(const D2D1_COLOR_F& color,
                                  ID2D1SolidColorBrush** out) {
        if (!out) return 0x80004003L;
        FakeSolidBrush* b = new FakeSolidBrush();
        b->color = color;
        *out = b;
        return 0;
    }

    void DrawBitmap(ID2D1Bitmap*, const D2D1_RECT_F*, FLOAT,
                    D2D1_BITMAP_INTERPOLATION_MODE, const D2D1_RECT_F*) {}
    void DrawLine(D2D1_POINT_2F, D2D1_POINT_2F, ID2D1Brush*, FLOAT = 1.0f,
                  ID2D1StrokeStyle* = nullptr) {}
    void DrawGeometry(ID2D1Geometry*, ID2D1Brush*, FLOAT = 1.0f,
                      ID2D1StrokeStyle* = nullptr) {}
    // The painted glyph pieces, the pipe's true positions: every
    // DrawTextLayout the frame or a painter issues is recorded with
    // its origin, its measured advance and the text its layout holds,
    // so a pair check can ask whether consecutive pieces keep the
    // spaces between words.
    struct TextDraw {
        float x = 0.0f;
        float y = 0.0f;
        float adv = 0.0f;
    };
    std::vector<TextDraw> textDraws;
    void RecordText(D2D1_POINT_2F origin, IDWriteTextLayout* layout) {
        // The real layout interface has no text getter, so the
        // record carries the pieces a gap check needs: where the
        // piece was placed and how wide its layout measured. The
        // thin fake layout answers the same numbers on the fake
        // stack.
        TextDraw d;
        d.x = origin.x;
        d.y = origin.y;
        if (layout) {
            DWRITE_TEXT_METRICS tm = {};
            if (SUCCEEDED(layout->GetMetrics(&tm))) d.adv = tm.width;
        }
        textDraws.push_back(d);
    }
    void DrawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* layout,
                        ID2D1Brush*,
                        D2D1_DRAW_TEXT_OPTIONS = D2D1_DRAW_TEXT_OPTIONS_NONE) {
        RecordText(origin, layout);
    }
    void FillGeometry(ID2D1Geometry*, ID2D1Brush*, ID2D1Brush* = nullptr) {}

    void DrawRectangle(const D2D1_RECT_F&, ID2D1Brush*, FLOAT = 1.0f,
                       ID2D1StrokeStyle* = nullptr) {}
    void DrawRoundedRectangle(const D2D1_ROUNDED_RECT&, ID2D1Brush*,
                              FLOAT = 1.0f, ID2D1StrokeStyle* = nullptr) {}
    void DrawEllipse(const D2D1_ELLIPSE&, ID2D1Brush*, FLOAT = 1.0f,
                     ID2D1StrokeStyle* = nullptr) {}
    void FillRectangle(const D2D1_RECT_F& rect, ID2D1Brush* brush) {
        Record(&rect, brush);
    }
    void FillEllipse(const D2D1_ELLIPSE&, ID2D1Brush*) {}
    void FillRoundedRectangle(const D2D1_ROUNDED_RECT&, ID2D1Brush*) {}
    void PushAxisAlignedClip(const D2D1_RECT_F&, D2D1_ANTIALIAS_MODE) {
        ++clips;
    }
    void PopAxisAlignedClip() { --clips; }
    void GetFactory(ID2D1Factory** factory) const {
        if (factory) *factory = nullptr;
    }

    // Real SDK only: the pointer forms the real interface declares.
    void DrawRectangle(const D2D1_RECT_F*, ID2D1Brush*, FLOAT = 1.0f,
                       ID2D1StrokeStyle* = nullptr) {}
    void DrawRoundedRectangle(const D2D1_ROUNDED_RECT*, ID2D1Brush*,
                              FLOAT = 1.0f, ID2D1StrokeStyle* = nullptr) {}
    void DrawEllipse(const D2D1_ELLIPSE*, ID2D1Brush*, FLOAT = 1.0f,
                     ID2D1StrokeStyle* = nullptr) {}
    void FillRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush) {
        Record(rect, brush);
    }
    void FillEllipse(const D2D1_ELLIPSE*, ID2D1Brush*) {}
    void FillRoundedRectangle(const D2D1_ROUNDED_RECT*, ID2D1Brush*) {}
    void PushAxisAlignedClip(const D2D1_RECT_F*, D2D1_ANTIALIAS_MODE) {
        ++clips;
    }

public:
    // The rest of the render target's pure surface. None of it runs
    // in these tests; the members exist so the class is concrete on
    // the runner.
    HRESULT CreateBitmap(D2D1_SIZE_U, const void*, UINT32,
                         const D2D1_BITMAP_PROPERTIES*, ID2D1Bitmap**) {
        return 0x80004001L;
    }
    HRESULT CreateBitmapFromWicBitmap(IWICBitmapSource*,
                                      const D2D1_BITMAP_PROPERTIES*,
                                      ID2D1Bitmap**) {
        return 0x80004001L;
    }
    HRESULT CreateSharedBitmap(REFIID, void*, const D2D1_BITMAP_PROPERTIES*,
                               ID2D1Bitmap**) {
        return 0x80004001L;
    }
    HRESULT CreateBitmapBrush(ID2D1Bitmap*,
                              const D2D1_BITMAP_BRUSH_PROPERTIES*,
                              const D2D1_BRUSH_PROPERTIES*,
                              ID2D1BitmapBrush**) {
        return 0x80004001L;
    }
    HRESULT CreateSolidColorBrush(const D2D1_COLOR_F* color,
                                  const D2D1_BRUSH_PROPERTIES* props,
                                  ID2D1SolidColorBrush** out) {
        // The real interface's two-argument convenience forms forward
        // here on the runner, through the inline headers, so this is
        // the creator the frame's brushes actually come from there.
        // It answers the same brush the reference form builds.
        if (!color || !out) return 0x80004003L;
        FakeSolidBrush* b = new FakeSolidBrush();
        b->color = *color;
        if (props) b->color.a *= props->opacity;
        *out = b;
        return 0;
    }
    HRESULT CreateGradientStopCollection(const D2D1_GRADIENT_STOP*, UINT32,
                                         D2D1_GAMMA, D2D1_EXTEND_MODE,
                                         ID2D1GradientStopCollection**) {
        return 0x80004001L;
    }
    HRESULT CreateLinearGradientBrush(
        const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES*,
        const D2D1_BRUSH_PROPERTIES*, ID2D1GradientStopCollection*,
        ID2D1LinearGradientBrush**) {
        return 0x80004001L;
    }
    HRESULT CreateRadialGradientBrush(
        const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES*,
        const D2D1_BRUSH_PROPERTIES*, ID2D1GradientStopCollection*,
        ID2D1RadialGradientBrush**) {
        return 0x80004001L;
    }
    HRESULT CreateCompatibleRenderTarget(const D2D1_SIZE_F*,
                                         const D2D1_SIZE_U*,
                                         const D2D1_PIXEL_FORMAT*,
                                         D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS,
                                         ID2D1BitmapRenderTarget**) {
        return 0x80004001L;
    }
    HRESULT CreateLayer(const D2D1_SIZE_F*, ID2D1Layer**) {
        return 0x80004001L;
    }
    HRESULT CreateMesh(ID2D1Mesh**) { return 0x80004001L; }
    void DrawTextW(const WCHAR*, UINT32, IDWriteTextFormat*,
                   const D2D1_RECT_F*, ID2D1Brush*,
                   D2D1_DRAW_TEXT_OPTIONS, DWRITE_MEASURING_MODE) {}
    void DrawGlyphRun(D2D1_POINT_2F, const DWRITE_GLYPH_RUN*, ID2D1Brush*,
                      DWRITE_MEASURING_MODE) {}
    void FillMesh(ID2D1Mesh*, ID2D1Brush*) {}
    void FillOpacityMask(ID2D1Bitmap*, ID2D1Brush*, D2D1_OPACITY_MASK_CONTENT,
                         const D2D1_RECT_F*, const D2D1_RECT_F*) {}
    void SetAntialiasMode(D2D1_ANTIALIAS_MODE) {}
    void SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE) {}
    void SetTextRenderingParams(IDWriteRenderingParams*) {}
    void SetTags(D2D1_TAG, D2D1_TAG) {}
    void GetTags(D2D1_TAG* tag1, D2D1_TAG* tag2) const {
        if (tag1) *tag1 = 0;
        if (tag2) *tag2 = 0;
    }
    void PushLayer(const D2D1_LAYER_PARAMETERS*, ID2D1Layer*) {}
    void PopLayer() {}
    HRESULT Flush(D2D1_TAG*, D2D1_TAG*) { return 0; }
    HRESULT EndDraw(D2D1_TAG*, D2D1_TAG*) { return 0; }
    void SaveDrawingState(ID2D1DrawingStateBlock*) const {}
    void RestoreDrawingState(ID2D1DrawingStateBlock*) {}
    void SetDpi(FLOAT, FLOAT) {}
    BOOL IsSupported(const D2D1_RENDER_TARGET_PROPERTIES*) const {
        return TRUE;
    }
    D2D1_ANTIALIAS_MODE GetAntialiasMode() const {
        return D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    }
    D2D1_PIXEL_FORMAT GetPixelFormat() const {
        return D2D1_PIXEL_FORMAT();
    }
    D2D1_SIZE_U GetPixelSize() const { return D2D1_SIZE_U(); }
    D2D1_TEXT_ANTIALIAS_MODE GetTextAntialiasMode() const {
        return D2D1_TEXT_ANTIALIAS_MODE_DEFAULT;
    }
    UINT32 GetMaximumBitmapSize() const { return 0; }
    void BeginDraw() {}
    void GetDpi(FLOAT* dpiX, FLOAT* dpiY) const {
        if (dpiX) *dpiX = 96.0f;
        if (dpiY) *dpiY = 96.0f;
    }
    void GetTextRenderingParams(
        IDWriteRenderingParams** textRenderingParams) const {
        if (textRenderingParams) *textRenderingParams = nullptr;
    }

private:
    void Record(const D2D1_RECT_F* rect, ID2D1Brush* brush) {
        Fill fill;
        if (rect) {
            fill.left = rect->left;
            fill.top = rect->top;
            fill.right = rect->right;
            fill.bottom = rect->bottom;
        }
        if (brush) {
            FakeSolidBrush* b = static_cast<FakeSolidBrush*>(brush);
            fill.r = b->color.r;
            fill.g = b->color.g;
            fill.b = b->color.b;
            fill.a = b->color.a;
        }
        fills.push_back(fill);
    }
};

// ---------------------------------------------------------------------------
// The hand-built documents, the same shape the mac suite builds so
// the numbers land on the same bands.
// ---------------------------------------------------------------------------
std::u32string Utf32(const std::string& text) {
    std::u32string out;
    for (unsigned char c : text) out.push_back(static_cast<char32_t>(c));
    return out;
}

Node Para(const std::string& text) {
    Node node;
    node.block = BlockKind::Paragraph;
    InlineBlock child;
    child.kind = InlineKind::Text;
    child.text = Utf32(text);
    node.children.push_back(child);
    node.contentLength = static_cast<uint32_t>(text.size());
    return node;
}

Node Heading(int level, const std::string& text) {
    Node node = Para(text);
    node.block = BlockKind::Heading;
    node.level = level;
    return node;
}

Node Code(const std::string& text) {
    Node node;
    node.block = BlockKind::CodeBlock;
    node.raw = Utf32(text);
    return node;
}

Node Rule() {
    Node node;
    node.block = BlockKind::ThematicBreak;
    return node;
}

Node Table(const std::vector<std::vector<std::string>>& rows) {
    Node node;
    node.block = BlockKind::Table;
    for (size_t r = 0; r < rows.size(); ++r) {
        TableRow row;
        for (const std::string& text : rows[r]) {
            TableCell cell;
            cell.text = Utf32(text);
            cell.isHeader = (r == 0);
            row.cells.push_back(cell);
        }
        node.rows.push_back(row);
    }
    return node;
}

Document Doc(std::vector<Node> nodes) {
    Document doc;
    doc.nodes = std::move(nodes);
    return doc;
}

// Numbering the source spans the way the parser does, so a selection
// range addresses real document bytes: every node holds its content
// at contentOffset, and a table's cells hold theirs in order.
void NumberOffsets(Document* doc) {
    uint32_t at = 0;
    for (Node& n : doc->nodes) {
        n.srcOffset = at;
        n.contentOffset = at;
        uint32_t len = 0;
        if (n.block == BlockKind::CodeBlock) {
            len = static_cast<uint32_t>(n.raw.size());
        } else if (!n.children.empty()) {
            for (const InlineBlock& child : n.children) {
                len += static_cast<uint32_t>(child.text.size());
            }
        } else if (n.block == BlockKind::Table) {
            uint32_t cells = 0;
            for (const TableRow& row : n.rows) {
                for (const TableCell& cell : row.cells) {
                    cells += static_cast<uint32_t>(cell.text.size());
                }
            }
            len = cells + 1;
        } else {
            len = 1;
        }
        n.srcLength = len;
        for (TableRow& row : n.rows) {
            for (TableCell& cell : row.cells) {
                cell.srcOffset = at;
                cell.srcEnd = at + static_cast<uint32_t>(cell.text.size());
                at += static_cast<uint32_t>(cell.text.size()) + 1;
            }
        }
        for (InlineBlock& child : n.children) {
            child.srcOffset = n.contentOffset;
            child.srcLength = static_cast<uint32_t>(child.text.size());
        }
        at += len;
    }
}

const LayoutMetrics kBase = BaseMetrics();
constexpr float kPtToDip = 96.0f / 72.0f;
const float kBodyEm = kBase.bodyFontSize * kPtToDip;

bool SameRect(const Rect& a, const FakeRenderTarget::Fill& b, float eps) {
    return std::fabs(a.x - b.left) < eps &&
           std::fabs(a.y - b.top) < eps &&
           std::fabs(a.x + a.width - b.right) < eps &&
           std::fabs(a.y + a.height - b.bottom) < eps;
}

}  // namespace

// The rects of one wrapped paragraph: one band per crossed line,
// tight on the grid's widths, stacked from the frame's own padTop.
TEST(RendererWin, SelectionRectsAnswerPerWrappedLine) {
    FakeEngine engine;
    Renderer renderer;
    FakeDWriteFactory dw;
    ASSERT_TRUE(renderer.Init(&dw));
    renderer.SetWalkEngineForTesting(&engine);
    renderer.SetContentWidthMode(3);  // no cap: the wrap is ours alone

    const std::string text(60, 'a');
    Document doc = Doc({Para(text)});
    NumberOffsets(&doc);

    const LayoutMetrics m = ScaleMetrics(kBase, 1.0f);
    const float padX = m.padX;
    const float width = 2.0f * padX + 8.0f * kBodyEm;  // 16 chars/line
    const uint32_t start = doc.nodes[0].contentOffset + 2;
    const uint32_t end = doc.nodes[0].contentOffset + 34;

    const std::vector<Rect> rects = renderer.RectsForRange(&dw, doc, width,
                                                           0.0f, start, end);
    // The 16-character column breaks the 60-byte run every 16 bytes,
    // so bytes 2 through 33 cross three wrapped lines: the first line
    // scans from the range's start byte, the middle line holds its
    // full measure, the last one carries the two bytes that remain.
    ASSERT_EQ(rects.size(), 3u);
    const float charW = kBodyEm * 0.5f;
    const float lineH = kBodyEm * kBase.bodyLineHeight;
    EXPECT_NEAR(rects[0].x, padX + 2.0f * charW, 0.02f);
    EXPECT_NEAR(rects[0].width, 14.0f * charW, 0.02f);
    EXPECT_NEAR(rects[1].x, padX, 0.02f);
    EXPECT_NEAR(rects[1].width, 16.0f * charW, 0.02f);
    EXPECT_NEAR(rects[1].y - rects[0].y, lineH, 0.02f);
    EXPECT_NEAR(rects[2].width, 2.0f * charW, 0.02f);
    EXPECT_NEAR(rects[0].y, m.padTop, 0.02f);
}

// A caret range in an empty paragraph paints no wash: a selection of
// zero length answers no rectangles on either port, and the caret's
// own address is the CaretRect domain, never the range rule's.
TEST(RendererWin, SelectionRectsAnswerNoWashForACaret) {
    FakeEngine engine;
    Renderer renderer;
    FakeDWriteFactory dw;
    ASSERT_TRUE(renderer.Init(&dw));
    renderer.SetWalkEngineForTesting(&engine);
    renderer.SetContentWidthMode(3);

    Document doc = Doc({Para("")});
    NumberOffsets(&doc);
    doc.nodes[0].virtualEmptyParagraph = true;

    const LayoutMetrics m = ScaleMetrics(kBase, 1.0f);
    const std::vector<Rect> rects = renderer.RectsForRange(
        &dw, doc, 600.0f, 0.0f, doc.nodes[0].contentOffset,
        doc.nodes[0].contentOffset);
    EXPECT_TRUE(rects.empty());
}

// The table twin: the same per line arithmetic, but placed in the
// cell's grid column and padY band the walk laid the glyphs in, the
// rule the mac twin answers with.
TEST(RendererWin, SelectionRectsAnswerTheTableCellBands) {
    FakeEngine engine;
    Renderer renderer;
    FakeDWriteFactory dw;
    ASSERT_TRUE(renderer.Init(&dw));
    renderer.SetWalkEngineForTesting(&engine);
    renderer.SetContentWidthMode(3);

    Document doc = Doc({Table({{"ab", "abcdefgh"}, {"cd", "efgh"}})});
    NumberOffsets(&doc);

    const LayoutMetrics m = ScaleMetrics(kBase, 1.0f);
    const float width = 600.0f;
    const std::vector<Rect> rects = renderer.RectsForRange(
        &dw, doc, width, 0.0f, doc.nodes[0].rows[1].cells[1].srcOffset,
        doc.nodes[0].rows[1].cells[1].srcEnd);

    // The right cell of the second row: one line, inside its column,
    // padY into its own row band, under the header row.
    ASSERT_EQ(rects.size(), 1u);
    const float charW = kBodyEm * 0.5f;
    const float lineBox = kBodyEm * kBase.bodyLineHeight;
    const float row0H = lineBox + 2.0f * m.cellPadY;
    const float colW = (width - 2.0f * m.padX) * 0.5f;
    EXPECT_NEAR(rects[0].x, m.padX + colW + m.cellPadX, 0.02f);
    EXPECT_NEAR(rects[0].width, 4.0f * charW, 0.02f);
    EXPECT_NEAR(rects[0].y, m.padTop + row0H + m.cellPadY, 0.02f);
}

// Measure charges the walk's bands, and the frame paints exactly
// those bands: a heading, an hr and a table cell advance the same
// total through both doors (the walk contract).
TEST(RendererWin, MeasureAndRenderAdvanceOneWalk) {
    FakeEngine engine;
    Renderer renderer;
    FakeDWriteFactory dw;
    ASSERT_TRUE(renderer.Init(&dw));
    renderer.SetWalkEngineForTesting(&engine);
    renderer.SetContentWidthMode(3);

    const float width = 600.0f;
    const LayoutMetrics m = ScaleMetrics(kBase, 1.0f);
    const float bodyLine = kBodyEm * kBase.bodyLineHeight;
    const float rowH = bodyLine + 2.0f * m.cellPadY;

    // A heading: its own band plus the gap after it. Measure of just
    // the heading, then of heading plus one paragraph: the difference
    // is the paragraph's gap and band.
    Document heading = Doc({Heading(1, "Title")});
    NumberOffsets(&heading);
    Document both = Doc({Heading(1, "Title"), Para("body")});
    NumberOffsets(&both);

    const float headOnly = renderer.Measure(&dw, heading, width, 0.0f);
    const float two = renderer.Measure(&dw, both, width, 0.0f);
    const float h1 = kBase.headingSizes[1] * kPtToDip;
    const float headBand = h1 * kBase.headingLineHeight + 0.3f * h1 + 1.0f;
    EXPECT_NEAR(headOnly - headBand, m.padTop * 3.0f, 0.02f);
    EXPECT_NEAR(two - headOnly, kBase.paraGap + bodyLine, 0.02f);

    // An hr: charged above and below, a fixed band the walk owns.
    Document withRule = Doc({Para("body"), Rule(), Para("tail")});
    NumberOffsets(&withRule);
    Document plain = Doc({Para("body"), Para("tail")});
    NumberOffsets(&plain);
    const float ruleDelta =
        renderer.Measure(&dw, withRule, width, 0.0f) -
        renderer.Measure(&dw, plain, width, 0.0f);
    // The walk charges the hr band at four units and its gaps above
    // and below, in the slot a plain paragraph gap takes without it.
    EXPECT_NEAR(ruleDelta,
                kBase.ruleGapAbove + kBase.ruleGapBelow + 4.0f -
                    kBase.paraGap,
                0.02f);

    // A table: each row is its own band; adding one row adds one
    // band, and the twin's wash of the last row starts under all
    // earlier bands: the same total the measure answers.
    Document table2 = Doc({Table({{"ab", "cd"}, {"ef", "gh"}})});
    NumberOffsets(&table2);
    Document table3 = Doc(
        {Table({{"ab", "cd"}, {"ef", "gh"}, {"ij", "kl"}})});
    NumberOffsets(&table3);
    const float t2 = renderer.Measure(&dw, table2, width, 0.0f);
    const float t3 = renderer.Measure(&dw, table3, width, 0.0f);
    EXPECT_NEAR(t3 - t2, rowH, 0.02f);

    FakeRenderTarget rt;
    Selection sel;
    sel.anchor.offset = table3.nodes[0].rows[2].cells[1].srcOffset;
    sel.active.offset = table3.nodes[0].rows[2].cells[1].srcEnd;
    const std::vector<Rect> twin = renderer.RectsForRange(
        &dw, table3, width, 0.0f, sel.Start(), sel.End(), &sel);
    ASSERT_EQ(twin.size(), 1u);
    EXPECT_NEAR(twin[0].y, m.padTop + 2.0f * rowH + m.cellPadY, 0.02f);
    // The walk sits its bands on one padTop and closes the frame with
    // two; the wash starts cellPadY into its row, so the distance from
    // the wash's line bottom to the measured bottom is the two closing
    // padTops plus that inset.
    EXPECT_NEAR(t3 - (twin[0].y + rowH - 2.0f * m.cellPadY),
                m.padTop * 2.0f + m.cellPadY, 0.02f);
}

// A selection adds no layout work: a frame with a selection measures
// exactly what the same frame measures without one, because the
// washes read the walk's own records, and the washes painted are the
// twin's rects for that selection.
TEST(RendererWin, SelectionRepaintAsksNoLayoutBeyondTheRects) {
    FakeEngine engine;
    Renderer renderer;
    FakeDWriteFactory dw;
    ASSERT_TRUE(renderer.Init(&dw));
    renderer.SetWalkEngineForTesting(&engine);
    renderer.SetContentWidthMode(3);

    Document doc = Doc({Heading(1, "Title"),
                        Para(std::string(40, 'b')),
                        Table({{"ab", "cd"}, {"ef", "gh"}}),
                        Code("line1\nline2")});
    NumberOffsets(&doc);

    const float width =
        2.0f * ScaleMetrics(kBase, 1.0f).padX + 8.0f * kBodyEm;
    FakeRenderTarget rt;

    // The frame without a selection first: its calls are the walk's
    // own measures, nothing more.
    FakeRenderTarget rtBare;
    renderer.Render(&rtBare, &dw, doc, width, 0.0f, 0.0f, 0.0f, nullptr);
    const size_t bareCalls = engine.calls.size();

    // The same frame with a selection: the walk runs its own pass
    // again, and the washes read the records it laid, so the frame
    // measures exactly its bare number again.
    Selection sel;
    sel.anchor.offset = doc.nodes[1].contentOffset + 4;
    sel.active.offset = sel.anchor.offset + 20;
    renderer.Render(&rt, &dw, doc, width, 0.0f, 0.0f, 0.0f, &sel);
    EXPECT_EQ(engine.calls.size(), bareCalls * 2u);

    // And the washes the selection frame painted are exactly the
    // twin's rects for the same selection.
    const std::vector<Rect> twin = renderer.RectsForRange(
        &dw, doc, width, 0.0f, sel.Start(), sel.End(), &sel);
    ASSERT_TRUE(!twin.empty());
    size_t matched = 0;
    for (const Rect& r : twin) {
        bool found = false;
        for (const FakeRenderTarget::Fill& f : rt.fills) {
            if (SameRect(r, f, 0.02f)) {
                found = true;
                break;
            }
        }
        if (found) ++matched;
    }
    EXPECT_EQ(matched, twin.size());
    if (matched != twin.size()) {
        printf("twin=%zu matched=%zu\n", twin.size(), matched);
        for (const Rect& r : twin)
            printf("twin  %f %f %f %f\n", r.x, r.y, r.width, r.height);
        for (const FakeRenderTarget::Fill& f : rt.fills)
            printf("fill  %f %f %f %f\n", f.left, f.top,
                   f.right, f.bottom);
        fflush(stdout);
    }
}

// The washes hold one shape on both ports: the frame paints the
// twin's rects for a selection crossing a heading into a table,
// table cells included, in the bands the walk laid.
TEST(RendererWin, FrameWashesCrossingBlocksMatchTheTwin) {
    FakeEngine engine;
    Renderer renderer;
    FakeDWriteFactory dw;
    ASSERT_TRUE(renderer.Init(&dw));
    renderer.SetWalkEngineForTesting(&engine);
    renderer.SetContentWidthMode(3);

    Document doc = Doc({Heading(2, "Head"),
                        Table({{"ab", "cd"}, {"ef", "gh"}})});
    NumberOffsets(&doc);

    const float width = 600.0f;
    FakeRenderTarget rt;
    Selection sel;
    sel.anchor.offset = doc.nodes[0].contentOffset;
    sel.active.offset = doc.nodes[1].rows[1].cells[0].srcEnd;

    renderer.Render(&rt, &dw, doc, width, 0.0f, 0.0f, 0.0f, &sel);

    const std::vector<Rect> twin = renderer.RectsForRange(
        &dw, doc, width, 0.0f, sel.Start(), sel.End(), &sel);
    ASSERT_GE(twin.size(), 2u);
    for (const Rect& r : twin) {
        bool found = false;
        for (const FakeRenderTarget::Fill& f : rt.fills) {
            if (SameRect(r, f, 0.02f)) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
        if (!found)
            printf("missing %f %f %f %f\n", r.x, r.y, r.width,
                   r.height);
    }
    fflush(stdout);
}


// The real engine places every slice of a line where the layout it
// measured in placed it: the spans share one DirectWrite layout, and
// the position a run starts at is where that layout starts it, after
// the space the previous run carries. A run placed before its
// predecessor's advance ends glues words; a run placed far beyond it
// spreads them. The capture states on the runner have shown both, so
// the engine itself is pinned here against the one authority there
// is on a Windows runner: its own DirectWrite.
TEST(RendererWin, RealEngineSliceGapsMatchTheLayout) {
    platform::DWriteEngine engine;
    std::vector<platform::GlyphRun> runs;
    platform::GlyphRun plain;
    plain.text = "The quick ";
    plain.font.family = "body";
    plain.font.size = 14.0f;
    platform::GlyphRun boldRun = plain;
    boldRun.text = "brown ";
    boldRun.font.bold = true;
    platform::GlyphRun ital = plain;
    ital.text = "fox jumps";
    ital.font.italic = true;
    runs.push_back(plain);
    runs.push_back(boldRun);
    runs.push_back(ital);

    engine.BeginLayout();
    const std::vector<platform::TextLine> lines =
        engine.MeasureWrapped(runs, 0.0f, 0.0f);
    if (lines.empty() || lines[0].runs.empty()) {
        // No DirectWrite factory in this environment (the fake stack
        // links the engine against a stub without one); the runner
        // owns the real answer.
        printf("RealEngineSliceGaps: engine degraded, no layout\n");
        fflush(stdout);
        return;
    }
    ASSERT_GE(lines.size(), 1u);
    printf("RealEngineSliceGaps: lines=%zu runs=%zu\n",
           lines.size(), lines[0].runs.size());
    for (size_t i = 0; i < lines[0].runs.size(); ++i) {
        const platform::GlyphRun& r = lines[0].runs[i];
        printf("  run i=%zu x=%f text=[%s]\n", i, r.x, r.text.c_str());
    }
    fflush(stdout);
    ASSERT_GE(lines[0].runs.size(), 3u);
    float prevX = 0.0f;
    for (size_t i = 0; i + 1 < lines[0].runs.size(); ++i) {
        const platform::GlyphRun& cur = lines[0].runs[i];
        const platform::GlyphRun& next = lines[0].runs[i + 1];
        // The visible advance of this slice, from the engine's own
        // measure of the slice alone.
        std::vector<platform::GlyphRun> one;
        one.push_back(cur);
        engine.BeginLayout();
        const std::vector<platform::TextLine> own =
            engine.MeasureWrapped(one, 0.0f, 0.0f);
        ASSERT_GE(own.size(), 1u);
        ASSERT_GE(own[0].runs.size(), 1u);
        const float advance = own[0].width;
        const float gap = next.x - (cur.x + advance);
        printf("  pair i=%zu gap=%f (cur.x=%f own.width=%f)\n",
               i, gap, cur.x, advance);
        // Glue: the next run starts before this one's ink has ended.
        EXPECT_GE(gap, -0.75f);
        // Spread: the run starts no further than the space it takes
        // plus rounding room. A space at fourteen points is under
        // four points wide.
        EXPECT_LE(gap, 9.0f);
        prevX = cur.x;
    }
    (void)prevX;
    fflush(stdout);
}


// The painted pieces themselves, the last mile the captures saw
// drift on: a heading and a paragraph with styled runs render
// through the real engine, and every piece on one text row must
// sit where the piece before it ended, neither before its ink has
// finished (glued words) nor a void beyond the space it carries
// (spread words). The fake stack has no DirectWrite factory, so no
// walk pieces exist there and the test reports its degradation; the
// runner owns the real numbers.
TEST(RendererWin, PaintedLinesKeepTheWordSpaces) {
    Document doc = Doc({Heading(2, "Statisk evidens fundet af test"),
                        Para("Almindelig tekst med **fede** og *skraa* ord")});
    NumberOffsets(&doc);

    Renderer renderer;
    FakeDWriteFactory dw;
    ASSERT_TRUE(renderer.Init(&dw));
    renderer.SetContentWidthMode(3);
    // The highlight feature: a marker band under the headings and
    // the words, the layer the drift report suspects. The wash must
    // not move the pieces it sits under.
    std::vector<TextMatch> marks;
    marks.push_back({0, 400});
    renderer.SetMarkers(&marks, true);
    // The states where the drift shows run zoomed, not at 100 percent:
    // the capture states sit near 0.71 and the reader's own window
    // can carry any zoom. The pieces must keep their word spaces at
    // any zoom the frame lays them at.
    renderer.SetZoom(0.71f);

    const float width =
        2.0f * ScaleMetrics(kBase, 1.0f).padX + 8.0f * kBodyEm;
    FakeRenderTarget rt;
    renderer.Render(&rt, &dw, doc, width, 0.0f, 0.0f, 0.0f, nullptr);
    if (rt.textDraws.empty()) {
        printf("PaintedLines: engine degraded, no pieces\n");
        fflush(stdout);
        return;
    }
    printf("PaintedLines: pieces=%zu\n", rt.textDraws.size());
    for (const FakeRenderTarget::TextDraw& d : rt.textDraws) {
        printf("  piece x=%f y=%f adv=%f\n", d.x, d.y, d.adv);
    }
    fflush(stdout);
    int judged = 0;
    {
        // Only neighbours judge: pieces of one row, ordered by x,
        // each against the piece that follows it. A first against a
        // third carries the whole middle word plus its spaces, which
        // is a sentence, not a seam.
        std::vector<FakeRenderTarget::TextDraw> pieces;
        for (const FakeRenderTarget::TextDraw& d : rt.textDraws)
            pieces.push_back(d);
        std::sort(pieces.begin(), pieces.end(),
                  [](const FakeRenderTarget::TextDraw& a,
                     const FakeRenderTarget::TextDraw& b) {
                      if (a.y != b.y) return a.y < b.y;
                      return a.x < b.x;
                  });
        for (size_t i = 1; i < pieces.size(); ++i) {
            const FakeRenderTarget::TextDraw& first = pieces[i - 1];
            const FakeRenderTarget::TextDraw& second = pieces[i];
            if (first.y != second.y) continue;
            const float gap = second.x - (first.x + first.adv);
            if (gap > 45.0f) continue;  // a cell or block boundary
            ++judged;
            printf("  pair gap=%f (first x=%f adv=%f second x=%f)\n",
                   gap, first.x, first.adv, second.x);
            EXPECT_GE(gap, -0.75f);  // the next piece starts too early
            EXPECT_LE(gap, 12.0f);   // the next piece starts too late
        }
    }
    printf("PaintedLines: judged=%d\n", judged);
    EXPECT_GE(judged, 1);
    fflush(stdout);
}

// Table cells share their row's baseline, so their painted pieces
// are the cross cell question the user saw: a cell that paints past
// its column lands on the next cell's words and the table reads as
// glued. Whatever stack runs, no two pieces may overlap.
TEST(RendererWin, PaintedCellsNeverOverlap) {
    // The shape from the reported document: three columns, code
    // spans in body cells and multi-word header cells, with the
    // highlight band under it all.
    Document doc = Doc({Table(
        {{"Scenario", "Observed outcome",
          "Talentech-adaptor-integration table"},
         {"`145825`, working posting", "HTTP 200 after seconds",
          "The full flow completed with an Enhedsregister lookup."}})});
    NumberOffsets(&doc);

    Renderer renderer;
    FakeDWriteFactory dw;
    ASSERT_TRUE(renderer.Init(&dw));
    renderer.SetContentWidthMode(3);
    std::vector<TextMatch> marks;
    marks.push_back({0, 400});
    renderer.SetMarkers(&marks, true);

    // A width the columns can share, with the hyphenated word the
    // equal split could not hold: the column must widen to carry
    // the whole word instead of slicing it into its neighbour.
    const float width =
        2.0f * ScaleMetrics(kBase, 1.0f).padX + 30.0f * kBodyEm;
    FakeRenderTarget rt;
    renderer.Render(&rt, &dw, doc, width, 0.0f, 0.0f, 0.0f, nullptr);
    printf("PaintedCells: pieces=%zu\n", rt.textDraws.size());
    for (const FakeRenderTarget::TextDraw& d : rt.textDraws) {
        printf("  piece x=%f y=%f adv=%f\n", d.x, d.y, d.adv);
    }
    fflush(stdout);
    int judged = 0;
    for (size_t a = 0; a < rt.textDraws.size(); ++a) {
        for (size_t b = a + 1; b < rt.textDraws.size(); ++b) {
            const FakeRenderTarget::TextDraw& pa = rt.textDraws[a];
            const FakeRenderTarget::TextDraw& pb = rt.textDraws[b];
            if (pa.y != pb.y) continue;
            const bool left = pb.x < pa.x;
            const FakeRenderTarget::TextDraw& first = left ? pb : pa;
            const FakeRenderTarget::TextDraw& second = left ? pa : pb;
            const float gap = second.x - (first.x + first.adv);
            ++judged;
            printf("  cellpair gap=%f (first x=%f adv=%f second x=%f)\n",
                   gap, first.x, first.adv, second.x);
            EXPECT_GE(gap, -0.75f);  // a cell painted into its neighbour
        }
    }
    printf("PaintedCells: judged=%d\n", judged);
    EXPECT_GE(judged, 1);
    fflush(stdout);
}

// Dual geometry probe: one table, one frame, every painter must
// read the same column split. The fake target records the fills of
// both the display items and the overlay card; the hairlines of the
// grid answer which x each painter placed its columns at.
TEST(RendererWin, DualGeometryProbe) {
    Document doc = Doc({Table({
        {"a", "En"},
        {"b", std::string(60, 'w')},
        {"Enhedssupplerende-register-taellereforskel", "lidt"},
    })});
    NumberOffsets(&doc);
    Renderer renderer;
    FakeDWriteFactory dw;
    ASSERT_TRUE(renderer.Init(&dw));
    renderer.SetContentWidthMode(3);
    const float width =
        2.0f * ScaleMetrics(kBase, 1.0f).padX + 30.0f * kBodyEm;
    FakeRenderTarget rt;
    renderer.Render(&rt, &dw, doc, width, 0.0f, 0.0f, 0.0f, nullptr);
    printf("DUALP fills=%zu\n", rt.fills.size());
    for (size_t i = 0; i < rt.fills.size(); ++i) {
        const FakeRenderTarget::Fill& f = rt.fills[i];
        const float w = f.right - f.left;
        const float h = f.bottom - f.top;
        if ((w < 5.0f || h < 5.0f) && (w > 0.0f || h > 0.0f))
            printf("DUALP grid x=%f y=%f w=%f h=%f color=%f,%f,%f\n",
                   f.left, f.top, w, h, f.r, f.g, f.b);
    }
    fflush(stdout);
    EXPECT_GT(rt.fills.size(), 0u);
}

// The reported document's own table: four columns, two long code
// tokens, the shape the drift report saw go wild. Every grid line
// both painters draw answers with its place, so a split that
// disagrees shows as two verticals where there should be one, and
// the cell pieces answer whether any column still paints across
// its neighbour.
TEST(RendererWin, ReportTableShapeHolds) {
    Document doc = Doc({Table({
        {"Integration", "Retning", "Teknik i dag", "Kilde"},
        {"HCM til SLS: opret og aendr ansaettelsesforhold",
         "AU til SLS",
         "Atom feed fra Oracle HCM, Red Hat Fuse/JBoss, "
         "SLS webservice",
         "Confluence 160479867"},
        {"Specialwebservice `OesSLSLoenmodtagerOpdateringList`",
         "SLS til AU",
         "Braendevarelse af loenmodtagerliste "
         "med `PersonExt1Service.skiftCprKOTtilSLS`",
         "Confluence 173314451"},
    })});
    NumberOffsets(&doc);
    Renderer renderer;
    FakeDWriteFactory dw;
    ASSERT_TRUE(renderer.Init(&dw));
    renderer.SetContentWidthMode(0);

    FakeRenderTarget rt;
    renderer.Render(&rt, &dw, doc, 1561.0f, 0.0f, 0.0f, 0.0f, nullptr);
    printf("REPORT fills=%zu pieces=%zu\n", rt.fills.size(),
           rt.textDraws.size());
    for (size_t i = 0; i < rt.fills.size(); ++i) {
        const FakeRenderTarget::Fill& f = rt.fills[i];
        const float w = f.right - f.left;
        const float h = f.bottom - f.top;
        if (w < 5.0f || h < 5.0f)
            printf("REPORT grid x=%f y=%f w=%f h=%f\n",
                   f.left, f.top, w, h);
    }
    for (const FakeRenderTarget::TextDraw& d : rt.textDraws)
        printf("REPORT piece x=%f y=%f adv=%f\n", d.x, d.y, d.adv);
    fflush(stdout);
    // The verticals of the whole table must sit at the same x per
    // row edge from both painters: count distinct near-1px x
    // stripes, they number columnCount + 1 plus the right edge.
    std::vector<float> xs;
    for (size_t i = 0; i < rt.fills.size(); ++i) {
        const FakeRenderTarget::Fill& f = rt.fills[i];
        const float w = f.right - f.left;
        const float h = f.bottom - f.top;
        if (w < 2.0f && h > 4.0f) xs.push_back(f.left);
    }
    std::sort(xs.begin(), xs.end());
    std::vector<float> uniq;
    for (float x : xs) {
        if (uniq.empty() || x - uniq.back() > 2.5f) uniq.push_back(x);
    }
    printf("REPORT distinct vlines: %zu\n", uniq.size());
    for (float x : uniq) printf("REPORT vline %f\n", x);
    // Four columns answer five inner edges; the frame hairline at
    // the right edge is the sixth.
    EXPECT_GE(uniq.size(), 5u);
    EXPECT_LE(uniq.size(), 7u);
    fflush(stdout);
}
