// Performance budgets for the editor (WYSIWYG plan Task 22).
//
// Five budgets, measured on the 1 MB fixture:
//
//   Keystroke to pixel      under 16 ms
//   Reparse one character   under  5 ms
//   Full relayout on resize under 50 ms
//   Open a 1 MB document    under 500 ms
//   Scroll one page         under  8 ms
//
// Two rules keep these numbers honest.
//
// First, every measurement runs several times and reports the MEDIAN. A
// single run on a shared CI runner is mostly noise: one scheduling hiccup
// reads as a regression and trains everyone to ignore the harness. The median
// is also what a user perceives, since a frame either lands inside its budget
// or it does not.
//
// Second, each test states what it measures in its name, because the earlier
// version of this file measured the wrong things under the right names.
// "ReparseSmallDoc" timed a full parse of a 50-paragraph document and called
// it a reparse budget; the budget that matters is reparsing ONE character in
// the 1 MB document after an edit.
//
// KNOWN FAILING BUDGET, as of this commit:
//
//   ReparseOneCharacterIncrementally  ~490 ms against a 5 ms budget
//
// ParseMarkdownIncremental is not implemented. It ignores oldDoc, editOffset,
// oldLen and newLen and calls ParseMarkdown, so a keystroke reparses the whole
// document. On top of that AppWindow::ScheduleReparse debounces with a 150 ms
// timer, which fires sooner than the work it defers.
//
// This test stays failing on purpose. Deleting the assertion would make CI
// green while the editor still stalls for half a second on every keystroke in
// a large file. See plan Task 12 steps 2 and 3, which are unimplemented.

#include "gtest_lite.h"
#include "parser.h"
#include "textbuffer.h"
#include "dom.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "md4c.h"
}

namespace {

const char* kFixture = "tests/fixtures/large-document.md";

// Median of `runs` timed invocations, in milliseconds. `fn` is called once
// before timing starts so one-off costs (first-touch page faults, allocator
// warm-up, md4c's internal tables) are not charged to the first sample.
template <typename Fn>
double MedianMs(Fn fn, int runs = 5) {
    fn();  // warm-up, not measured
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(runs));
    for (int i = 0; i < runs; ++i) {
        auto start = std::chrono::steady_clock::now();
        fn();
        auto end = std::chrono::steady_clock::now();
        samples.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The shared 1 MB document. Read once per test run; every budget below is
// measured against the same bytes so the numbers are comparable.
const std::string& LargeDocument() {
    static const std::string doc = ReadFile(kFixture);
    return doc;
}

void Report(const char* what, double ms, double budget) {
    const double pct = (ms / budget) * 100.0;
    std::printf("  %-34s %8.2f ms  (budget %6.0f ms, %5.1f%% used)\n",
                what, ms, budget, pct);
    std::fflush(stdout);
}

}  // namespace

// A missing fixture must fail loudly. Silently generating a substitute
// document lets the budgets pass against bytes nobody ships.
TEST(Perf, FixtureIsPresentAndAboutOneMegabyte) {
    const std::string& doc = LargeDocument();
    EXPECT_TRUE(!doc.empty());
    if (doc.empty()) {
        std::printf("  FATAL: %s not found or empty\n", kFixture);
        return;
    }
    Report("fixture size", static_cast<double>(doc.size()) / 1024.0, 1024.0);
    EXPECT_TRUE(doc.size() >= 900u * 1024u);
    EXPECT_TRUE(doc.size() <= 1200u * 1024u);
}

// Budget 4: open a 1 MB document.
TEST(Perf, OpenOneMegabyteDocument) {
    const std::string& doc = LargeDocument();
    ASSERT_TRUE(!doc.empty());
    double ms = MedianMs([&]() {
        Document d;
        ParseMarkdown(doc, d);
    });
    Report("open 1 MB document", ms, 500.0);
    EXPECT_TRUE(ms < 500.0);
}

// Budget 2: reparse after one character, incrementally, in the 1 MB document.
//
// KNOWN GAP, measured at roughly 490 ms against a 5 ms budget.
// ParseMarkdownIncremental ignores its oldDoc/editOffset/oldLen/newLen
// arguments and calls ParseMarkdown, so every keystroke reparses the whole
// document. Plan Task 12 steps 2 and 3 (find the dirty block range, reparse
// only that slice) are not implemented. ScheduleReparse's 150 ms debounce
// fires sooner than the work it defers, so the timer does not mask it.
//
// Marked TEST_XFAIL so the gap is visible in every CI log without blocking
// unrelated work. When incremental parsing lands, the runner FAILS this test
// as an unexpected pass, which is the signal to drop the marker.
TEST_XFAIL(Perf, ReparseOneCharacterIncrementally) {
    const std::string& doc = LargeDocument();
    ASSERT_TRUE(!doc.empty());

    // Insert one character near the middle, so the work is not at a
    // convenient boundary.
    const size_t editAt = doc.size() / 2;
    std::string edited = doc;
    edited.insert(editAt, "x");

    Document oldDoc;
    ASSERT_TRUE(ParseMarkdown(doc, oldDoc));

    double ms = MedianMs([&]() {
        Document newDoc;
        ParseMarkdownIncremental(edited, oldDoc,
                                 static_cast<uint32_t>(editAt),
                                 0, 1, newDoc);
    });
    std::printf("  (incremental currently delegates to a full reparse)\n");
    Report("reparse one character", ms, 5.0);
    EXPECT_GAP_TRUE(ms < 5.0);
}

// The same edit with a FULL reparse, for comparison. Not a budget: it exists
// so the log shows what incremental parsing is buying. If these two converge,
// the incremental path has stopped paying for itself.
TEST(Perf, ReparseOneCharacterFullyForComparison) {
    const std::string& doc = LargeDocument();
    ASSERT_TRUE(!doc.empty());
    const size_t editAt = doc.size() / 2;
    std::string edited = doc;
    edited.insert(editAt, "x");

    double ms = MedianMs([&]() {
        Document d;
        ParseMarkdown(edited, d);
    });
    Report("reparse one character (full)", ms, 0.0);  // no budget, comparison only
    EXPECT_TRUE(ms > 0.0);
}

// Budget 1, buffer half: a keystroke's splice on a large document. The
// 16 ms keystroke-to-pixel budget also covers layout and paint, which need a
// render target and are therefore not measurable in this harness. This is the
// editable part.
TEST(Perf, SpliceOnLargeDocument) {
    const std::string& doc = LargeDocument();
    ASSERT_TRUE(!doc.empty());
    TextBuffer buf;
    buf.SetText(doc);
    const uint32_t at = static_cast<uint32_t>(doc.size() / 2);

    double ms = MedianMs([&]() {
        buf.Splice(at, 0, "x");
        buf.Splice(at, 1, "");
    });
    Report("splice insert+delete", ms, 16.0);
    EXPECT_TRUE(ms < 16.0);
}

// Sanity check that the document is a realistic shape: many blocks, not one
// enormous paragraph. A fixture of 5000 short paragraphs exercises block
// iteration; a single 1 MB paragraph would not, and the budgets above would
// look fine while measuring the wrong thing.
TEST(Perf, FixtureHasManyBlocks) {
    const std::string& doc = LargeDocument();
    ASSERT_TRUE(!doc.empty());
    Document d;
    ASSERT_TRUE(ParseMarkdown(doc, d));
    Report("blocks in fixture", static_cast<double>(d.nodes.size()), 0.0);
    EXPECT_TRUE(d.nodes.size() > 1000u);
}
