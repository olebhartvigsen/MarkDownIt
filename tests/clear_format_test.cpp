#include "gtest_lite.h"
#include "formatting.h"
#include "textbuffer.h"
#include "caret.h"
#include "undostack.h"
#include "dom.h"

#include <string>
#include <vector>

// The inline markers of a document, taken away from a selection.
//
// The shared module does this work now, so the Windows build and the
// mac build clear the same markers.

namespace {

// A document with one paragraph, whose spans are given.
Document MakeDoc(const std::vector<InlineBlock>& spans,
                 uint32_t srcOffset, uint32_t srcLength) {
    Document doc;
    Node node;
    node.block = BlockKind::Paragraph;
    node.srcOffset = srcOffset;
    node.srcLength = srcLength;
    node.children = spans;
    doc.nodes.push_back(node);
    return doc;
}

// A span with the marker flags and the place in the source.
InlineBlock Span(bool strong, bool em, bool code, bool strike,
                 uint32_t offset, uint32_t length) {
    InlineBlock span;
    span.strong = strong;
    span.em = em;
    span.code = code;
    span.strike = strike;
    span.srcOffset = offset;
    span.srcLength = length;
    return span;
}

}  // namespace

TEST(InlineMarkerRuns, FindsTheMarkersOfABoldSpan) {
    // "**text**": the content is at 2, the markers run from 0 to 8.
    const Document doc = MakeDoc({Span(true, false, false, false, 2, 4)}, 0, 8);
    const std::string text = "**text**";

    const std::vector<InlineMarkerRun> runs =
        CollectInlineMarkerRuns(doc, text, 0, static_cast<uint32_t>(text.size()));

    EXPECT_EQ(runs.size(), size_t(1));
    if (runs.empty()) return;
    EXPECT_EQ(runs[0].markerStart, uint32_t(0));
    EXPECT_EQ(runs[0].markerEnd, uint32_t(8));
    EXPECT_EQ(runs[0].contentStart, uint32_t(2));
    EXPECT_EQ(runs[0].contentEnd, uint32_t(6));
}

TEST(InlineMarkerRuns, FindsTheMarkersOfEveryKind) {
    // "**a** *b* `c` ~~d~~"
    const std::string text = "**a** *b* `c` ~~d~~";
    const Document doc = MakeDoc(
        {Span(true, false, false, false, 2, 1),
         Span(false, true, false, false, 7, 1),
         Span(false, false, true, false, 11, 1),
         Span(false, false, false, true, 16, 1)},
        0, static_cast<uint32_t>(text.size()));

    const std::vector<InlineMarkerRun> runs =
        CollectInlineMarkerRuns(doc, text, 0, static_cast<uint32_t>(text.size()));

    EXPECT_EQ(runs.size(), size_t(4));
}

TEST(InlineMarkerRuns, LeavesASpanOutsideTheSelectionAlone) {
    const Document doc = MakeDoc({Span(true, false, false, false, 2, 4)}, 0, 8);
    const std::string text = "**text**";

    // The selection is before the span.
    const std::vector<InlineMarkerRun> runs =
        CollectInlineMarkerRuns(doc, text, 0, 1);

    EXPECT_EQ(runs.size(), size_t(0));
}

TEST(InlineMarkerRuns, MergesTwoSpansThatShareTheirMarkers) {
    // "***text***": the bold and the italic share the markers.
    const std::string text = "***text***";
    const Document doc = MakeDoc(
        {Span(true, false, false, false, 3, 4),
         Span(false, true, false, false, 3, 4)},
        0, static_cast<uint32_t>(text.size()));

    const std::vector<InlineMarkerRun> runs =
        CollectInlineMarkerRuns(doc, text, 0, static_cast<uint32_t>(text.size()));

    // The two spans are one run, because the markers are shared.
    EXPECT_EQ(runs.size(), size_t(1));
    if (runs.empty()) return;
    EXPECT_EQ(runs[0].markerStart, uint32_t(0));
    EXPECT_EQ(runs[0].markerEnd, uint32_t(10));
}

TEST(ClearInlineFormatting, TakesTheMarkersAwayAndKeepsTheText) {
    TextBuffer buffer;
    buffer.SetText("**text**");

    Selection sel;
    sel.anchor = {0};
    sel.active = {8};

    const Document doc = MakeDoc({Span(true, false, false, false, 2, 4)}, 0, 8);
    ClearInlineFormatting(doc, &buffer, &sel);

    EXPECT_EQ(buffer.Text(), std::string("text"));
    // The selection covers the same text, now without the markers.
    EXPECT_EQ(sel.Start(), uint32_t(0));
    EXPECT_EQ(sel.End(), uint32_t(4));
}

TEST(ClearInlineFormatting, DoesNothingWithoutASelection) {
    TextBuffer buffer;
    buffer.SetText("**text**");

    Selection sel;
    sel.Collapse({2});

    const Document doc = MakeDoc({Span(true, false, false, false, 2, 4)}, 0, 8);
    ClearInlineFormatting(doc, &buffer, &sel);

    EXPECT_EQ(buffer.Text(), std::string("**text**"));
}

TEST(ClearInlineFormatting, RecordsAnUndoStep) {
    TextBuffer buffer;
    buffer.SetText("**text**");
    UndoStack undo;

    Selection sel;
    sel.anchor = {0};
    sel.active = {8};

    const Document doc = MakeDoc({Span(true, false, false, false, 2, 4)}, 0, 8);
    ClearInlineFormatting(doc, &buffer, &sel, &undo);

    EXPECT_TRUE(undo.CanUndo());
    UndoEntry entry;
    EXPECT_TRUE(undo.Undo(entry));
    // The entry carries the marked text back, so the change can be
    // taken back.
    EXPECT_EQ(entry.removed, std::string("**text**"));
    EXPECT_EQ(entry.inserted, std::string("text"));
    EXPECT_EQ(entry.offset, uint32_t(0));
}
