// One caret record shape, filled by both ports.
//
// The class answers every caret question from the shared LayoutCache, whose
// entry is a BlockLayout in UTF-16. The Windows renderer already fills that
// entry during its paint pass (node range, content offset, the parser's
// map); the class fills it from the walk's own lines and maps. These cases
// pin that both fillers produce entries the cache reads the same way, and
// that the shared builder in src/caretrecords.h does the UTF-16 conversion
// one way only.

#include "gtest_lite.h"

#include "../src/caretrecords.h"
#include "../src/layoutcache.h"

#include <string>
#include <vector>

namespace {

// One line of fixed-width text: half an em per UTF-8 byte, one em tall.
// The caret geometry is not what these cases check, the addressing is.
platform::TextLine MakeLine(const std::string& text, float top) {
    platform::TextLine line;
    platform::GlyphRun run;
    run.text = text;
    run.x = 0.0f;
    run.font.size = 10.0f;
    line.runs.push_back(run);
    line.width = static_cast<float>(text.size()) * 5.0f;
    line.height = 10.0f;
    line.ascent = 8.0f;
    (void)top;
    return line;
}

// A rendered-byte to source map for text whose every byte maps one to one
// onto a source range starting at `start`.
std::vector<uint32_t> IdentityMap(size_t bytes, uint32_t start) {
    std::vector<uint32_t> map;
    for (size_t i = 0; i < bytes; ++i) {
        map.push_back(start + static_cast<uint32_t>(i));
    }
    return map;
}

std::vector<uint32_t> IdentityEnd(size_t bytes, uint32_t start) {
    std::vector<uint32_t> map;
    for (size_t i = 0; i < bytes; ++i) {
        map.push_back(start + static_cast<uint32_t>(i) + 1);
    }
    return map;
}

}  // namespace

// The builder gives one UTF-16 unit per ASCII byte and its own source
// byte, which is what the caret seam indexes.
TEST(CaretRecord, AsciiTextMapsOneUnitPerByte) {
    const std::string text = "abc";
    std::vector<platform::TextLine> lines{MakeLine(text, 0.0f)};
    const auto map = IdentityMap(text.size(), 100);
    const auto end = IdentityEnd(text.size(), 100);

    const BlockLayout bl = caret::Record(lines, 0.0f, 0.0f, 30.0f, 10.0f,
                                        100, 3, 100, 103, false,
                                        ColumnAlign::Default, 0.0f, 0, 0.0f,
                                        map, end);
    EXPECT_EQ(3u, bl.u16ToSrc.size());
    EXPECT_EQ(100u, bl.u16ToSrc[0]);
    EXPECT_EQ(102u, bl.u16ToSrc[2]);
    EXPECT_EQ(103u, bl.u16ToSrcEnd[2]);
    EXPECT_TRUE(bl.layout != nullptr);
    EXPECT_NEAR(10.0f, bl.height, 0.001f);
}

// A four byte code point takes two UTF-16 units, both carrying the same
// source span, the conversion the seam's index space expects.
TEST(CaretRecord, SurrogatePairTakesTwoUnits) {
    const std::string text = "a\xF0\x9F\x98\x80";  // 'a' and U+1F600
    std::vector<platform::TextLine> lines{MakeLine(text, 0.0f)};
    const auto map = IdentityMap(text.size(), 0);
    const auto end = IdentityEnd(text.size(), 0);

    const BlockLayout bl = caret::Record(lines, 0.0f, 0.0f, 30.0f, 10.0f,
                                        0, 5, 0, 5, false,
                                        ColumnAlign::Default, 0.0f, 0, 0.0f,
                                        map, end);
    EXPECT_EQ(3u, bl.u16ToSrc.size());
    EXPECT_EQ(1u, bl.u16ToSrc[1]);
    EXPECT_EQ(1u, bl.u16ToSrc[2]);
    // Both units carry the map entry of the character's first byte, and the
    // end entry is whatever the region's own map says for that byte.
    EXPECT_EQ(2u, bl.u16ToSrcEnd[2]);
}

// A region that draws nothing keeps one addressable position, the sentinel
// an empty table cell parks its caret at.
TEST(CaretRecord, EmptyRegionKeepsOneSentinel) {
    const std::vector<platform::TextLine> noLines;
    const std::vector<uint32_t> noMap;
    const BlockLayout bl = caret::Record(noLines, 5.0f, 6.0f, 20.0f, 14.0f,
                                         42, 0, 42, 42, true,
                                         ColumnAlign::Center, -1.0f, 3, 11.0f,
                                         noMap, noMap);
    EXPECT_EQ(1u, bl.u16ToSrc.size());
    EXPECT_EQ(42u, bl.u16ToSrc[0]);
    EXPECT_TRUE(bl.layout == nullptr);
    EXPECT_NEAR(14.0f, bl.height, 0.001f);
    EXPECT_TRUE(bl.isTableCell);
    EXPECT_EQ(42u, bl.srcCellStart);
}

// The two fillers put the same text in the same place: a record built with
// the class's drawn span (the first drawn byte and the drawn length) and one
// built with the Windows paint pass's node range and content offset answer
// the cache identically inside the drawn text, where both agree by design.
TEST(CaretRecord, BothFillersAnswerTheSameInsideTheDrawnText) {
    const std::string text = "hello";
    std::vector<platform::TextLine> lines{MakeLine(text, 0.0f)};

    // The source is "**hello**": the drawn text starts at byte 2.
    const std::string source = "**hello**";
    const uint32_t drawnStart = 2;
    const auto map = IdentityMap(text.size(), drawnStart);
    const auto end = IdentityEnd(text.size(), drawnStart);

    // The class's filler: the drawn span.
    BlockLayout bySpan = caret::Record(lines, 0.0f, 0.0f, 40.0f, 10.0f,
                                       drawnStart, 5, drawnStart,
                                       drawnStart + 5, false,
                                       ColumnAlign::Default, 0.0f, 0, 0.0f,
                                       map, end);
    bySpan.height = 10.0f;

    // The Windows paint pass's filler: the node range, the content
    // offset, and the same map. Only the fields differ; the text does not.
    BlockLayout byNode = bySpan;
    byNode.srcOffset = 0;
    byNode.srcLength = static_cast<uint32_t>(source.size());
    byNode.textStartOffset = drawnStart;

    LayoutCache spanCache;
    spanCache.SetSourceText(&source);
    spanCache.Add(bySpan);

    LayoutCache nodeCache;
    nodeCache.SetSourceText(&source);
    nodeCache.Add(byNode);

    for (uint32_t offset = drawnStart; offset <= drawnStart + 5; ++offset) {
        float sx = 0.0f, sy = 0.0f, sh = 0.0f;
        float nx = 0.0f, ny = 0.0f, nh = 0.0f;
        const bool spanFound = spanCache.OffsetToCaretRect(offset, &sx, &sy, &sh);
        const bool nodeFound = nodeCache.OffsetToCaretRect(offset, &nx, &ny, &nh);
        EXPECT_TRUE(spanFound);
        EXPECT_TRUE(nodeFound);
        EXPECT_NEAR(sx, nx, 0.001f);
        EXPECT_NEAR(sy, ny, 0.001f);
        // Both agree that the hidden marker before the text is not a caret
        // position: the nearest drawn byte is the first one.
        EXPECT_EQ(drawnStart,
                  spanCache.NormalizeToRenderedCaret(0));
        EXPECT_EQ(drawnStart,
                  nodeCache.NormalizeToRenderedCaret(0));
    }
}
