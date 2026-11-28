// The per run source range of the display list (plan 2.2, slice 10's
// display list step). A Text item names the span every run was cut
// from, so the one pill pass can tell a span that continues on the
// next line from two spans that meet on a line.
//
// The fake engine of tests/fake_text_engine.h never breaks inside a
// run, which is the wrong shape for this suite: the question is what
// happens when an engine slices a span at the wrap. SlicingEngine is
// that engine, fixed width and deterministic, so the assertions hold
// on both runners.

#include "fake_text_engine.h"
#include "gtest_lite.h"
#include "layout/document_layout.h"
#include "theme.h"

#include <variant>
#include <vector>

namespace {

struct SlicingEngine : platform::TextEngine {
    fake::FakeTextEngine base;

    void BeginLayout() override { base.BeginLayout(); }
    uint32_t HitTestOffset(const platform::TextLine& line, float x,
                           bool* outTrailing) const override {
        return base.HitTestOffset(line, x, outTrailing);
    }
    platform::Rect OffsetToRect(const platform::TextLine& line,
                                uint32_t offset) const override {
        return base.OffsetToRect(line, offset);
    }
    float LineHeight(const platform::FontSpec& font) const override {
        return base.LineHeight(font);
    }

    std::vector<platform::TextLine> MeasureWrapped(
            const std::vector<platform::GlyphRun>& runs, float maxWidth,
            float) override {
        std::string text;
        for (const platform::GlyphRun& run : runs) text += run.text;
        if (text.empty()) return {};
        size_t perLine = maxWidth > 0.0f
                             ? static_cast<size_t>(maxWidth / fake::FakeTextEngine::kGlyphWidth)
                             : text.size();
        if (perLine < 1) perLine = 1;
        std::vector<platform::TextLine> lines;
        size_t start = 0;
        while (start < text.size()) {
            const size_t lineEnd =
                start + perLine < text.size() ? start + perLine : text.size();
            platform::TextLine line;
            line.height = 24.0f;
            line.ascent = 18.0f;
            size_t offset = 0;
            for (const platform::GlyphRun& run : runs) {
                const size_t runEnd = offset + run.text.size();
                if (runEnd > start && offset < lineEnd) {
                    const size_t from = start > offset ? start - offset : 0;
                    const size_t to = lineEnd < runEnd ? lineEnd - offset
                                                       : run.text.size();
                    platform::GlyphRun sliced = run;
                    sliced.text = run.text.substr(from, to - from);
                    sliced.x = static_cast<float>(offset + from - start) *
                               fake::FakeTextEngine::kGlyphWidth;
                    line.runs.push_back(sliced);
                }
                offset = runEnd;
            }
            float width = 0.0f;
            for (const platform::GlyphRun& run : line.runs) {
                width = run.x + static_cast<float>(run.text.size()) * fake::FakeTextEngine::kGlyphWidth;
            }
            line.width = width;
            lines.push_back(std::move(line));
            start = lineEnd;
        }
        return lines;
    }
};

InlineBlock Child(const char* text, bool code, uint32_t offset) {
    InlineBlock child;
    child.kind = InlineKind::Text;
    child.code = code;
    child.srcOffset = offset;
    child.srcLength = static_cast<uint32_t>(std::string(text).size());
    for (unsigned char c : std::string(text)) child.text.push_back(c);
    return child;
}

std::vector<const display::Text*> Texts(const display::DisplayList& list) {
    std::vector<const display::Text*> out;
    for (const display::Item& item : list.items) {
        if (const auto* t = std::get_if<display::Text>(&item)) out.push_back(t);
    }
    return out;
}

// One span, 40 bytes, wider than the column, so the engine cuts it.
struct Fixture {
    SlicingEngine engine;
    LayoutMetrics metrics = BaseMetrics();
    Palette palette = BasePalette();
    Document doc;
    display::DisplayList list;

    explicit Fixture(std::vector<InlineBlock> children, float width) {
        Node para;
        para.block = BlockKind::Paragraph;
        para.children = std::move(children);
        para.contentOffset = 0;
        para.contentLength = 200;
        doc.nodes.push_back(std::move(para));
        layout::LayoutInput in;
        in.doc = &doc;
        in.metrics = &metrics;
        in.palette = &palette;
        in.contentWidth = width;
        in.zoom = 1.0f;
        in.engine = &engine;
        list = layout::LayoutDocument(in).list;
    }
};

}  // namespace

TEST(RunSpans, AWrappedSpanKeepsOneRange) {
    const std::string code(40, 'x');
    Fixture f({Child("before ", false, 0),
               Child(code.c_str(), true, 7)},
              80.0f);  // ten glyphs per line, the span spans four
    const auto texts = Texts(f.list);
    ASSERT_TRUE(texts.size() >= 3);

    size_t fragments = 0;
    display::SourceRange first;
    for (const display::Text* text : texts) {
        ASSERT_EQ(text->runSpans.size(), text->line.runs.size());
        for (size_t i = 0; i < text->runSpans.size(); ++i) {
            if (!text->runSpans[i].inlineCode) continue;
            if (fragments == 0) {
                first = text->runSpans[i];
                EXPECT_EQ(first.offset, static_cast<uint32_t>(7));
                EXPECT_EQ(first.length, static_cast<uint32_t>(code.size()));
            } else {
                EXPECT_EQ(text->runSpans[i].offset, first.offset);
                EXPECT_EQ(text->runSpans[i].length, first.length);
            }
            ++fragments;
        }
    }
    // Every fragment of the one span, on every line it was cut to.
    EXPECT_EQ(fragments, static_cast<size_t>(5));
}

TEST(RunSpans, AdjacentSpansKeepTheirOwnRanges) {
    Fixture f({Child("aa", true, 0), Child(" ", false, 2),
               Child("bbb", true, 3)},
              400.0f);
    const auto texts = Texts(f.list);
    ASSERT_EQ(texts.size(), static_cast<size_t>(1));
    const display::Text& text = *texts.front();
    ASSERT_EQ(text.runSpans.size(), static_cast<size_t>(3));
    EXPECT_TRUE(text.runSpans[0].inlineCode);
    EXPECT_EQ(text.runSpans[0].offset, static_cast<uint32_t>(0));
    EXPECT_EQ(text.runSpans[0].length, static_cast<uint32_t>(2));
    EXPECT_FALSE(text.runSpans[1].inlineCode);
    EXPECT_TRUE(text.runSpans[2].inlineCode);
    EXPECT_EQ(text.runSpans[2].offset, static_cast<uint32_t>(3));
    EXPECT_EQ(text.runSpans[2].length, static_cast<uint32_t>(3));
}

TEST(RunSpans, ACodeBlockCarriesNoInlineSpan) {
    SlicingEngine engine;
    const LayoutMetrics metrics = BaseMetrics();
    const Palette palette = BasePalette();
    Document doc;
    Node code;
    code.block = BlockKind::CodeBlock;
    code.raw = U"alpha bravo charlie delta echo foxtrot golf hotel";
    code.contentOffset = 0;
    code.contentLength = code.raw.size();
    doc.nodes.push_back(code);
    layout::LayoutInput in;
    in.doc = &doc;
    in.metrics = &metrics;
    in.palette = &palette;
    in.contentWidth = 80.0f;
    in.zoom = 1.0f;
    in.engine = &engine;
    const display::DisplayList list = layout::LayoutDocument(in).list;
    const auto texts = Texts(list);
    ASSERT_TRUE(!texts.empty());
    for (const display::Text* text : texts) {
        EXPECT_TRUE(text->runSpans.empty());
    }
}
