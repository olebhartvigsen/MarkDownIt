// Markers: the marker model, anchor relocation and the JSON sidecar of one
// document, tested without a window and without any Windows API. The local
// gate builds this file together with the shared test main:
//   g++ -std=c++17 -Wall -Wextra -I tests -I src
//       tests/markers_test.cpp tests/test_main.cpp src/markers.cpp -o /tmp/markers_test

#include "gtest_lite.h"
#include "markers.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

// One scratch root per test process, inside the OS temp directory. The tests
// never delete anything; the OS owns its temp tree.
const std::string& RunScratchRoot() {
    static const std::string root = [] {
        std::error_code ec;
        const std::filesystem::path base =
            std::filesystem::temp_directory_path() / "markdownit-marker-tests" /
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(base, ec);
        return base.string();
    }();
    return root;
}

std::string MakeScratchDir(const std::string& name) {
    const std::filesystem::path dir =
        std::filesystem::path(RunScratchRoot()) / name;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

void WriteTextFile(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void ExpectSameMarker(const Marker& actual, const Marker& expected) {
    EXPECT_EQ(actual.id, expected.id);
    EXPECT_EQ(actual.start, expected.start);
    EXPECT_EQ(actual.end, expected.end);
    EXPECT_EQ(actual.exact, expected.exact);
    EXPECT_EQ(actual.prefix, expected.prefix);
    EXPECT_EQ(actual.suffix, expected.suffix);
    EXPECT_EQ(actual.created, expected.created);
}

}  // namespace

// ---- Fingerprint ----

TEST(Markers, FingerprintKnownVectorsStabilityAndDifference) {
    // FNV-1a 64: the offset basis for the empty input, and the standard
    // vector for "abc".
    EXPECT_EQ(MarkerFingerprint(""), "cbf29ce484222325");
    EXPECT_EQ(MarkerFingerprint("abc"), "e71fa2190541574b");
    EXPECT_EQ(MarkerFingerprint("abc").size(), 16u);

    // Stable for the same bytes, different for different bytes.
    EXPECT_EQ(MarkerFingerprint("abc"), MarkerFingerprint("abc"));
    EXPECT_EQ(MarkerFingerprint(std::string("abc")), MarkerFingerprint("abc"));
    EXPECT_NE(MarkerFingerprint(""), MarkerFingerprint("abc"));
    EXPECT_NE(MarkerFingerprint("abc"), MarkerFingerprint("abd"));

    // The hash covers bytes, so the same UTF-8 text hashes the same.
    EXPECT_EQ(MarkerFingerprint("\xC3\xA6\xC3\xB8\xC3\xA5"),
              MarkerFingerprint("\xC3\xA6\xC3\xB8\xC3\xA5"));
}

// ---- JSON sidecar round trip ----

TEST(Markers, JsonRoundTripPreservesEveryField) {
    const std::string scratch = MakeScratchDir("roundtrip");
    MarkerStore::SetSidecarDirectoryForTests(scratch);

    const std::string document =
        "Intro plain.\n"
        "\"quoted\" and \\backslash\\ appear here.\n"
        "one\n"
        "two and tab\there.\n"
        "\xC3\xA6\xC3\xB8\xC3\xA5" " Danish and \xE4\xB8\xAD\xE6\x96\x87" " CJK.\n"
        "tail end";

    MarkerStore store;
    const auto addAt = [&store, &document](const std::string& needle) {
        const size_t pos = document.find(needle);
        EXPECT_TRUE(pos != std::string::npos);
        return store.Add(document, static_cast<uint32_t>(pos),
                         static_cast<uint32_t>(pos + needle.size()));
    };
    EXPECT_TRUE(addAt("Intro"));                 // prefix is empty
    EXPECT_TRUE(addAt("\"quoted\""));            // quotes in the anchor
    EXPECT_TRUE(addAt("\\backslash\\"));         // backslashes in the anchor
    EXPECT_TRUE(addAt("one\ntwo"));              // a newline in the anchor
    EXPECT_TRUE(addAt("tab\there"));             // a tab in the anchor
    EXPECT_TRUE(addAt("\xC3\xA6\xC3\xB8\xC3\xA5"));   // Danish UTF-8 bytes
    EXPECT_TRUE(addAt("\xE4\xB8\xAD\xE6\x96\x87"));   // CJK UTF-8 bytes
    const size_t tail = document.find("tail end");
    EXPECT_TRUE(tail != std::string::npos);
    EXPECT_TRUE(store.Add(document, static_cast<uint32_t>(tail),
                          static_cast<uint32_t>(document.size())));  // no suffix
    EXPECT_EQ(store.Count(), 8u);

    EXPECT_TRUE(store.Save("roundtrip.md", document));

    MarkerStore loaded;
    EXPECT_TRUE(loaded.LoadFor("roundtrip.md", document));
    ASSERT_EQ(loaded.Count(), store.Count());
    for (size_t i = 0; i < loaded.Count(); ++i) {
        ExpectSameMarker(loaded.Markers()[i], store.Markers()[i]);
    }
    // Empty contexts survive as empty strings, not as invented ones.
    EXPECT_EQ(loaded.Markers()[0].prefix, "");
    EXPECT_EQ(loaded.Markers()[7].suffix, "");

    MarkerStore::SetSidecarDirectoryForTests("");
}

TEST(Markers, CorruptSidecarYieldsEmptySetWithoutCrashing) {
    const std::string scratch = MakeScratchDir("corrupt");
    MarkerStore::SetSidecarDirectoryForTests(scratch);

    const std::string document = "cat and cat";
    const std::string sidecar = MarkerStore::SidecarPathFor("corrupt.md");

    // Outright garbage.
    WriteTextFile(sidecar, "this is not json at all {{{");
    MarkerStore garbage;
    EXPECT_FALSE(garbage.LoadFor("corrupt.md", document));
    EXPECT_EQ(garbage.Count(), 0u);

    // A valid prefix that stops halfway through an object.
    WriteTextFile(sidecar, "{\"version\":1,\"markers\":[{\"id\":\"m-0001\",");
    MarkerStore truncated;
    EXPECT_FALSE(truncated.LoadFor("corrupt.md", document));
    EXPECT_EQ(truncated.Count(), 0u);

    MarkerStore::SetSidecarDirectoryForTests("");
}

TEST(Markers, LoadForSkipsMarkerEntriesWithoutAnchors) {
    const std::string scratch = MakeScratchDir("anchors");
    MarkerStore::SetSidecarDirectoryForTests(scratch);

    // Tolerates unknown keys (here a nested object), and skips entries that
    // have no anchor text, while the anchored entry survives.
    WriteTextFile(MarkerStore::SidecarPathFor("anchors.md"),
                  "{\"version\":1,\"path\":\"elsewhere.md\","
                  "\"fingerprint\":\"unused\","
                  "\"markers\":["
                  "{\"id\":\"m-0001\",\"start\":0,\"end\":3},"
                  "{\"id\":\"m-0002\",\"start\":4,\"end\":7,\"exact\":\"cat\","
                  "\"prefix\":\"\",\"suffix\":\"\"}"
                  "],\"unknown\":{\"a\":[1,2]}}");

    MarkerStore store;
    EXPECT_TRUE(store.LoadFor("anchors.md", "cat and cat"));
    ASSERT_EQ(store.Count(), 1u);
    EXPECT_EQ(store.Markers()[0].id, "m-0002");
    EXPECT_EQ(store.Markers()[0].exact, "cat");
    EXPECT_EQ(store.Markers()[0].start, 4u);
    EXPECT_EQ(store.Markers()[0].end, 7u);

    MarkerStore::SetSidecarDirectoryForTests("");
}

// ---- Resolve: anchor relocation ----

TEST(Markers, ResolveRelocatesASingleOccurrence) {
    MarkerStore store;
    ASSERT_TRUE(store.Add("hello cat world", 6, 9));
    ASSERT_TRUE(store.Markers()[0].resolved);

    store.Resolve("XX hello cat world");
    EXPECT_TRUE(store.Markers()[0].resolved);
    EXPECT_EQ(store.Markers()[0].start, 9u);
    EXPECT_EQ(store.Markers()[0].end, 12u);
    EXPECT_EQ(store.Markers()[0].exact, "cat");
}

TEST(Markers, ResolveUsesPrefixAndSuffixToPickTheRightOccurrence) {
    MarkerStore store;
    ASSERT_TRUE(store.Add("aaa one cat two bbb", 8, 11));
    // The stored full-window context is "aaa one " before and " two bbb"
    // after. In the new revision the occurrence that reproduces both wins
    // with score 8, even though the occurrence at 0 is closer to the stored
    // start (8): distance alone would have picked it.
    store.Resolve("cat qq qq aaa one cat two bbb qq cat");
    EXPECT_TRUE(store.Markers()[0].resolved);
    EXPECT_EQ(store.Markers()[0].start, 18u);
    EXPECT_EQ(store.Markers()[0].end, 21u);
}

TEST(Markers, ResolveUsesPrefixAloneWhenTheSuffixMatchesNowhere) {
    MarkerStore store;
    ASSERT_TRUE(store.Add("one cat QQQ", 4, 7));
    // Two occurrences and the wrong one is nearer to the stored start (4);
    // only the prefix "one " identifies the right one, because the stored
    // suffix " QQQ" is gone from the document.
    store.Resolve("zz cat zz one cat zz");
    EXPECT_TRUE(store.Markers()[0].resolved);
    EXPECT_EQ(store.Markers()[0].start, 14u);
    EXPECT_EQ(store.Markers()[0].end, 17u);
}

TEST(Markers, ResolveFallsBackToNearestPreviousStartWhenNoContextMatches) {
    MarkerStore store;
    ASSERT_TRUE(store.Add("123456789catXYZ", 9, 12));
    // Neither the prefix "123456789" nor the suffix "XYZ" matches anywhere;
    // the occurrence nearest to the stored start (9) wins: 10, not 2 or 17.
    store.Resolve("q cat qqq cat qq cat q");
    EXPECT_TRUE(store.Markers()[0].resolved);
    EXPECT_EQ(store.Markers()[0].start, 10u);
    EXPECT_EQ(store.Markers()[0].end, 13u);
}

TEST(Markers, ResolveMarksDeletedTextUnresolvedAndKeepsTheAnchor) {
    MarkerStore store;
    ASSERT_TRUE(store.Add("keep this deleted part end", 10, 22));

    store.Resolve("keep this end");
    EXPECT_FALSE(store.Markers()[0].resolved);
    EXPECT_EQ(store.Markers()[0].start, 10u);   // offsets and anchor stay
    EXPECT_EQ(store.Markers()[0].end, 22u);
    EXPECT_EQ(store.Markers()[0].exact, "deleted part");
    EXPECT_EQ(store.UnresolvedCount(), 1u);

    // When the text comes back, the marker resolves again.
    store.Resolve("keep this deleted part end");
    EXPECT_TRUE(store.Markers()[0].resolved);
    EXPECT_EQ(store.Markers()[0].start, 10u);
    EXPECT_EQ(store.UnresolvedCount(), 0u);
}

// ---- Add: refusals, contexts and ids ----

TEST(Markers, AddRefusesEmptyOutOfBoundsAndDuplicateRanges) {
    MarkerStore store;
    const std::string document = "cat and cat";   // 11 bytes

    EXPECT_FALSE(store.Add(document, 5, 5));      // empty range
    EXPECT_FALSE(store.Add(document, 7, 3));      // inverted, hence empty
    EXPECT_FALSE(store.Add(document, 0, 100));    // past the end
    EXPECT_FALSE(store.Add(document, 12, 14));    // starts past the end
    EXPECT_EQ(store.Count(), 0u);

    EXPECT_TRUE(store.Add(document, 0, 3));
    EXPECT_TRUE(store.Add(document, 8, 11));
    EXPECT_FALSE(store.Add(document, 0, 3));      // exact duplicate range
    EXPECT_TRUE(store.Add(document, 0, 8));       // overlap is fine, range differs
    EXPECT_EQ(store.Count(), 3u);

    EXPECT_EQ(store.Markers()[0].id, "m-0001");
    EXPECT_EQ(store.Markers()[1].id, "m-0002");
    EXPECT_EQ(store.Markers()[2].id, "m-0003");
    EXPECT_EQ(store.Markers()[0].exact, "cat");
    EXPECT_EQ(store.Markers()[0].prefix, "");
    EXPECT_EQ(store.Markers()[2].suffix, "cat");
    EXPECT_TRUE(store.Markers()[2].resolved);
}

TEST(Markers, AddBuildsPrefixAndSuffixWithoutSplittingUtf8) {
    // 32 bytes before the anchor would start inside the 3 byte CJK
    // character, so the window backs off to the character boundary: 30
    // bytes, not 32, and never a partial code point.
    const std::string prefixDoc =
        std::string(10, 'a') + "\xE4\xB8\xAD" + std::string(30, 'b') + "cat" + "z";
    MarkerStore prefixStore;
    ASSERT_TRUE(prefixStore.Add(prefixDoc, 43, 46));
    EXPECT_EQ(prefixStore.Markers()[0].exact, "cat");
    EXPECT_EQ(prefixStore.Markers()[0].prefix, std::string(30, 'b'));
    EXPECT_EQ(prefixStore.Markers()[0].prefix.size(), 30u);

    // 32 bytes after the anchor lands inside a 3 byte CJK character, so the
    // suffix drops the straddling character whole: 31 bytes, not 32.
    const std::string suffixDoc =
        std::string(16, 'x') + "cat" + std::string(31, 'c') + "\xE4\xB8\xAD" + "zz";
    MarkerStore suffixStore;
    ASSERT_TRUE(suffixStore.Add(suffixDoc, 16, 19));
    EXPECT_EQ(suffixStore.Markers()[0].suffix, std::string(31, 'c'));
    EXPECT_EQ(suffixStore.Markers()[0].suffix.size(), 31u);
    EXPECT_EQ(suffixStore.Markers()[0].prefix, std::string(16, 'x'));
}

// ---- RemoveIntersecting ----

TEST(Markers, RemoveIntersectingRemovesOnlyIntersectingMarkers) {
    MarkerStore store;
    const std::string document = "abcdefghijklmnopqrstuv";   // 22 bytes
    ASSERT_TRUE(store.Add(document, 2, 5));
    ASSERT_TRUE(store.Add(document, 10, 13));
    ASSERT_TRUE(store.Add(document, 18, 21));

    // [4, 11) overlaps the first two markers; the third is untouched.
    EXPECT_EQ(store.RemoveIntersecting(4, 11), 2u);
    ASSERT_EQ(store.Count(), 1u);
    EXPECT_EQ(store.Markers()[0].id, "m-0003");
    EXPECT_EQ(store.Markers()[0].start, 18u);
    EXPECT_TRUE(store.Markers()[0].resolved);   // untouched, still resolved

    // New markers continue the id sequence after the highest survivor.
    ASSERT_TRUE(store.Add(document, 0, 2));
    EXPECT_EQ(store.Markers()[1].id, "m-0004");

    // Touching a marker at its end is not an intersection ([18,21) against
    // [21,25)), and an empty range removes nothing.
    EXPECT_EQ(store.RemoveIntersecting(21, 25), 0u);
    EXPECT_EQ(store.RemoveIntersecting(18, 18), 0u);
    EXPECT_EQ(store.Count(), 2u);

    // A range that swallows the remaining old marker removes exactly it.
    EXPECT_EQ(store.RemoveIntersecting(17, 19), 1u);
    EXPECT_EQ(store.Count(), 1u);
    EXPECT_EQ(store.Markers()[0].id, "m-0004");
}

// ---- Sidecar adoption after a move or rename ----

TEST(Markers, LoadForAdoptsTheSidecarOfAMovedDocument) {
    const std::string scratch = MakeScratchDir("adopt");
    MarkerStore::SetSidecarDirectoryForTests(scratch);

    const std::string document = "cat and cat and cat";
    const std::string oldPath =
        (std::filesystem::path(scratch) / "moved-away.md").string();
    const std::string newPath =
        (std::filesystem::path(scratch) / "arrived-here.md").string();

    MarkerStore original;
    ASSERT_TRUE(original.Add(document, 8, 11));
    ASSERT_TRUE(original.Save(oldPath, document));
    const std::vector<Marker> saved = original.Markers();

    // The document was moved or renamed: its old path is gone from disk, the
    // same bytes now live under a new path, and the sidecar is still filed
    // under the old path hash, so the store adopts it.
    MarkerStore moved;
    EXPECT_TRUE(moved.LoadFor(newPath, document));
    ASSERT_EQ(moved.Count(), 1u);
    ExpectSameMarker(moved.Markers()[0], saved[0]);

    // When the recorded path still exists on disk the sidecar belongs to a
    // live document, so nothing is adopted for the other path.
    WriteTextFile(oldPath, document);
    MarkerStore separate;
    EXPECT_FALSE(separate.LoadFor(newPath, document));
    EXPECT_EQ(separate.Count(), 0u);

    MarkerStore::SetSidecarDirectoryForTests("");
}

TEST(Markers, SaveFailsSilentlyWhenTheDirectoryIsBlocked) {
    const std::string scratch = MakeScratchDir("blocked");
    // A regular file where the sidecar directory should be: the write must
    // fail quietly and nothing else must happen.
    const std::string blocked =
        (std::filesystem::path(scratch) / "blocked").string();
    WriteTextFile(blocked, "x");
    MarkerStore::SetSidecarDirectoryForTests(blocked);

    MarkerStore store;
    ASSERT_TRUE(store.Add("cat and cat", 0, 3));
    EXPECT_FALSE(store.Save("blocked.md", "cat and cat"));

    MarkerStore::SetSidecarDirectoryForTests("");
}

// ---- MarkerNowIso8601 ----

TEST(Markers, NowIso8601HasTheExpectedShape) {
    const std::string now = MarkerNowIso8601();
    EXPECT_EQ(now.size(), 20u);
    EXPECT_EQ(now[4], '-');
    EXPECT_EQ(now[7], '-');
    EXPECT_EQ(now[10], 'T');
    EXPECT_EQ(now[13], ':');
    EXPECT_EQ(now[16], ':');
    EXPECT_EQ(now[19], 'Z');

    size_t digits = 0;
    for (size_t i = 0; i < now.size(); ++i) {
        if (i == 4u || i == 7u || i == 10u || i == 13u || i == 16u || i == 19u) {
            continue;
        }
        if (now[i] >= '0' && now[i] <= '9') ++digits;
    }
    EXPECT_EQ(digits, 14u);

    // Two calls in a row never go backwards (ISO 8601 of this shape sorts
    // as text at seconds precision).
    const std::string again = MarkerNowIso8601();
    EXPECT_TRUE(again >= now);
}

// No RUN_ALL_TESTS() here: tests/test_main.cpp owns main, and a second one
// in this file would fail the link with a duplicate symbol.
