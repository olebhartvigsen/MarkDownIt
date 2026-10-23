// Unicode folding in the search (find spec: "case folding and
// normalisation of equivalent representations"). The primitives come
// from unicode_fold.h; the behaviour comes from the full matcher.
//
// The byte escapes keep the test independent of the source encoding:
// "\xC3\x85" is A with a ring, "\xCC\x8A" is the combining ring,
// "\xC3\x9F" is the sharp s and "\xC3\xA9" is e with an acute.
#include "gtest_lite.h"

#include <cstdint>
#include <string>
#include <vector>

#include "searchreplace.h"
#include "unicode_fold.h"

namespace {

std::size_t Decompose(std::uint32_t code, std::vector<std::uint32_t>& out) {
    std::uint32_t buffer[8];
    const std::size_t count = unicode::DecomposeInto(code, buffer, 8);
    out.assign(buffer, buffer + count);
    return count;
}

std::size_t Fold(std::uint32_t code, std::vector<std::uint32_t>& out) {
    std::uint32_t buffer[4];
    const std::size_t count = unicode::FoldInto(code, buffer, 4);
    out.assign(buffer, buffer + count);
    return count;
}

}  // namespace

TEST(UnicodeFold, CanonicalDecomposition) {
    std::vector<std::uint32_t> out;
    ASSERT_EQ(Decompose(0xC5, out), 2u);          // A with a ring
    EXPECT_EQ(out[0], 0x41u);
    EXPECT_EQ(out[1], 0x30Au);
    ASSERT_EQ(Decompose(0x61, out), 1u);          // a plain letter maps to itself
    EXPECT_EQ(out[0], 0x61u);
    ASSERT_EQ(Decompose(0xDF, out), 1u);          // the sharp s survives NFD
    EXPECT_EQ(out[0], 0xDFu);
}

TEST(UnicodeFold, CaseFoldingIncludingExpansions) {
    // The fold runs on decomposed code points, so the primitives are
    // tested on those; a precomposed character folds through its
    // decomposition first.
    std::vector<std::uint32_t> out;
    ASSERT_EQ(Fold(0x41, out), 1u);
    EXPECT_EQ(out[0], 0x61u);
    ASSERT_EQ(Fold(0xDF, out), 2u);               // the sharp s folds to ss
    EXPECT_EQ(out[0], static_cast<std::uint32_t>('s'));
    EXPECT_EQ(out[1], static_cast<std::uint32_t>('s'));
    // I-with-a-dot has a canonical decomposition, so the fold never sees
    // it; the micro sign survives NFD and folds to the Greek small mu.
    ASSERT_EQ(Fold(0x130, out), 1u);
    EXPECT_EQ(out[0], 0x130u);
    ASSERT_EQ(Fold(0xB5, out), 1u);
    EXPECT_EQ(out[0], 0x3BCu);
    ASSERT_EQ(Fold(0x17F, out), 1u);              // the long s folds to s
    EXPECT_EQ(out[0], static_cast<std::uint32_t>('s'));
}

TEST(UnicodeFold, EquivalentRepresentationsMatch) {
    const std::string composed = "\xC3\x85";       // A with a ring, composed
    const std::string decomposed = "A\xCC\x8A";    // A plus a combining ring
    SearchOptions options;
    EXPECT_EQ(FindTextMatches(decomposed, composed, options).size(), 1u);
    EXPECT_EQ(FindTextMatches(composed, decomposed, options).size(), 1u);

    // Case sensitivity turns the folding off, but the normalisation
    // stays: both spellings remain the same text (find spec).
    SearchOptions sensitive;
    sensitive.caseSensitive = true;
    EXPECT_EQ(FindTextMatches(decomposed, composed, sensitive).size(), 1u);
}

TEST(UnicodeFold, AMatchCoversWholeCharacters) {
    // The single character A-with-a-ring folds to two units; a search
    // for the bare A must not match half of it.
    EXPECT_TRUE(FindTextMatches("\xC3\x85", "A").empty());
}

TEST(UnicodeFold, TheSharpSFoldsToSS) {
    const std::vector<TextMatch> matches =
        FindTextMatches("Stra\xC3\x9F" "e", "strasse");
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].start, 0u);
    EXPECT_EQ(matches[0].length, 7u);   // the whole word, sharp s included
}

TEST(UnicodeFold, CaseSensitiveStaysSensitive) {
    SearchOptions options;
    options.caseSensitive = true;
    EXPECT_TRUE(FindTextMatches("Stra\xC3\x9F" "e", "strasse", options).empty());
    EXPECT_TRUE(FindTextMatches("\xC3\x85", "\xC3\xA5", options).empty());
    EXPECT_EQ(FindTextMatches("\xC3\xA5", "\xC3\xA5", options).size(), 1u);
}

TEST(UnicodeFold, WholeWordUsesTheFoldedUnits) {
    SearchOptions options;
    options.wholeWord = true;
    // A whole-word search for "e" must not match a decomposed e-acute,
    // because the match would cover half of a character.
    EXPECT_TRUE(FindTextMatches("e\xCC\x81", "e", options).empty());
    // The composed and the decomposed form match each other as words.
    EXPECT_EQ(FindTextMatches("e\xCC\x81", "\xC3\xA9", options).size(), 1u);
}
