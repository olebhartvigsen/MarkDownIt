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
// KNOWN BUDGET, now met:
//
//   ReparseOneCharacterIncrementally  ~4 ms against a 5 ms budget
//
// ParseMarkdownIncremental used to ignore its arguments and call ParseMarkdown,
// so every keystroke reparsed the whole document (~490 ms). Plan Task 12 is
// implemented: the dirty block range is found, only that slice is parsed, and
// the path stands down for the edits a slice cannot judge (a line break, a
// code block, the title, a link reference definition, a change that crosses a
// block boundary). The edit point below is inside a paragraph on purpose: that
// is the case the budget describes, and the test asserts the fast path was
// actually taken so it cannot quietly time the fallback.

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
    if (MD_TESTS_UNDER_SANITIZERS) return;
    EXPECT_TRUE(ms < 500.0);
}

// The budget is about typing inside a paragraph. The fixture's midpoint lands
// on the blank line after a fenced block, where the incremental path stands
// down by design (a slice cannot judge a fence), and an edit there would time
// a full parse and report it as a reparse.
size_t ParagraphEditPoint(const std::string& doc) {
    const char* marker = "This is paragraph number";
    size_t at = doc.find(marker, doc.size() / 2);
    if (at == std::string::npos) at = doc.find(marker);
    if (at == std::string::npos) return doc.empty() ? 0 : doc.size() / 2;
    return at + 12;   // inside the line, clear of both of its edges
}

// Budget 2: reparse after one character, incrementally, in the 1 MB document.
TEST(Perf, ReparseOneCharacterIncrementally) {
    const std::string& doc = LargeDocument();
    ASSERT_TRUE(!doc.empty());

    const size_t editAt = ParagraphEditPoint(doc);
    std::string edited = doc;
    edited.insert(editAt, "x");

    Document oldDoc;
    ASSERT_TRUE(ParseMarkdown(doc, oldDoc));

    // The app reparses in place, so the measurement does too. A deep copy of
    // this fixture's document costs about 10 ms on its own, more than twice the
    // budget; timing the convenience overload that copies would report the
    // harness's bookkeeping as the editor's cost. One untouched copy per run.
    std::vector<Document> pool(8, oldDoc);
    size_t next = 0;
    bool wentIncremental = true;
    double ms = MedianMs([&]() {
        Document& target = pool[next++ % pool.size()];
        const bool incremental = ParseMarkdownIncremental(
            edited, target, static_cast<uint32_t>(editAt), 0, 1);
        wentIncremental = wentIncremental && incremental;
    });
    // Without this the test would pass by timing the full-parse fallback, which
    // is the exact mistake the older version of this harness made.
    EXPECT_TRUE(wentIncremental);
    Report("reparse one character", ms, 5.0);
    EXPECT_TRUE(ms < 5.0);
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
    if (MD_TESTS_UNDER_SANITIZERS) return;
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
