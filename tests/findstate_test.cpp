// FindReplaceState: the search session state model and its navigation
// semantics, tested without a window. Everything here works in document byte
// offsets, so nothing in this file depends on a viewport (guide 64).

#include "gtest_lite.h"
#include "findstate.h"

#include <string>
#include <vector>

namespace {

// A scope filter that only accepts candidate ranges inside [lo, hi), the
// shape a caller uses for "rendered document text only" or "the current
// selection only" (guide 4 and 51).
struct ByteRange {
    uint32_t lo = 0;
    uint32_t hi = 0;
};

bool AcceptRange(uint32_t start, uint32_t length, void* context) {
    const ByteRange* range = static_cast<const ByteRange*>(context);
    if (start < range->lo) return false;
    return start + length <= range->hi;
}

// Accepts only the first range, so a rejected candidate must be skipped rather
// than consumed: a later match that overlaps it still has to be found
// (guide 23).
bool RejectFirstOnly(uint32_t start, uint32_t, void*) {
    return start != 0;
}

FindStateOptions Options(bool caseSensitive, bool wholeWord) {
    FindStateOptions options;
    options.search.caseSensitive = caseSensitive;
    options.search.wholeWord = wholeWord;
    return options;
}

}  // namespace

// ── Empty query and no matches (guide 20 and 21) ────────────────────────

TEST(FindState, EmptyQueryIsEmptyAndFindsNothing) {
    FindReplaceState state;
    state.SetSearchText("");
    state.SetReplaceText("x");
    state.Refresh("anything at all", nullptr, nullptr);

    EXPECT_EQ(state.Status(), FindStatus::Empty);
    EXPECT_EQ(state.MatchCount(), 0u);
    EXPECT_FALSE(state.HasQuery());
    EXPECT_FALSE(state.HasCurrentMatch());
    EXPECT_EQ(state.CurrentOrdinal(), 0u);
    EXPECT_EQ(state.CurrentIndex(), -1);
    TextMatch match;
    EXPECT_FALSE(state.CurrentMatch(&match));
    EXPECT_FALSE(state.MoveNext(0));
    EXPECT_FALSE(state.MovePrevious(0));
}

TEST(FindState, NoMatchesStatusWhenTheDocumentHasNoHit) {
    FindReplaceState state;
    state.SetSearchText("zebra");
    state.Refresh("the quick brown fox", nullptr, nullptr);

    EXPECT_EQ(state.Status(), FindStatus::NoMatches);
    EXPECT_EQ(state.MatchCount(), 0u);
    EXPECT_EQ(state.CurrentOrdinal(), 0u);
    EXPECT_FALSE(state.HasCurrentMatch());
    EXPECT_FALSE(state.MoveNext(0));
    EXPECT_FALSE(state.MovePrevious(4));

    // Nothing to replace: the count is zero and the output is left alone.
    std::string out;
    EXPECT_EQ(state.BuildReplacedDocument("the quick brown fox", &out), 0u);
}

TEST(FindState, FoundStatusWithAtLeastOneMatch) {
    FindReplaceState state;
    state.SetSearchText("two");
    state.Refresh("one two one", nullptr, nullptr);
    EXPECT_EQ(state.Status(), FindStatus::Found);
    EXPECT_EQ(state.MatchCount(), 1u);
}

// ── Match counter semantics (guide 52) ───────────────────────────────────

TEST(FindState, CurrentOrdinalIsOneBasedAndZeroWithoutACurrentMatch) {
    FindReplaceState state;
    state.SetSearchText("one");
    state.Refresh("one two one two one", nullptr, nullptr);
    EXPECT_EQ(state.MatchCount(), 3u);

    // Nothing is current until the caller navigates or adopts a match.
    EXPECT_EQ(state.CurrentOrdinal(), 0u);
    EXPECT_FALSE(state.HasCurrentMatch());

    EXPECT_TRUE(state.MoveNext(1));
    EXPECT_EQ(state.CurrentOrdinal(), 2u);
    EXPECT_TRUE(state.MoveNext(9));
    EXPECT_EQ(state.CurrentOrdinal(), 3u);
    EXPECT_TRUE(state.MoveNext(17));
    EXPECT_EQ(state.CurrentOrdinal(), 1u);

    TextMatch match;
    ASSERT_TRUE(state.CurrentMatch(&match));
    EXPECT_EQ(match.start, 0u);
    EXPECT_EQ(match.length, 3u);
}

// ── Navigation wraps at both ends (guide 8) ─────────────────────────────

TEST(FindState, MoveNextWalksForwardAndWrapsPastTheEnd) {
    FindReplaceState state;
    state.SetSearchText("one");
    state.Refresh("one two one two one", nullptr, nullptr);

    // Offsets that are deliberately not the start of a match, so the test does
    // not depend on whether a match exactly at `from` counts as next or current.
    ASSERT_TRUE(state.MoveNext(1));
    EXPECT_EQ(state.CurrentOrdinal(), 2u);
    ASSERT_TRUE(state.MoveNext(9));
    EXPECT_EQ(state.CurrentOrdinal(), 3u);
    // Past the last match: wrap to the first one.
    ASSERT_TRUE(state.MoveNext(17));
    EXPECT_EQ(state.CurrentOrdinal(), 1u);
}

TEST(FindState, MovePreviousWalksBackwardsAndWrapsBeforeTheStart) {
    FindReplaceState state;
    state.SetSearchText("one");
    state.Refresh("xx one two one two one", nullptr, nullptr);

    ASSERT_TRUE(state.MovePrevious(18));
    EXPECT_EQ(state.CurrentOrdinal(), 2u);
    ASSERT_TRUE(state.MovePrevious(10));
    EXPECT_EQ(state.CurrentOrdinal(), 1u);
    // Before the first match: wrap to the last one.
    ASSERT_TRUE(state.MovePrevious(0));
    EXPECT_EQ(state.CurrentOrdinal(), 3u);
}

TEST(FindState, NavigationOnAnEmptyMatchListFailsAndLeavesNoCurrentMatch) {
    FindReplaceState state;
    state.SetSearchText("zebra");
    state.Refresh("the quick brown fox", nullptr, nullptr);

    EXPECT_FALSE(state.MoveNext(0));
    EXPECT_EQ(state.CurrentIndex(), -1);
    EXPECT_FALSE(state.HasCurrentMatch());
    EXPECT_EQ(state.CurrentOrdinal(), 0u);

    EXPECT_FALSE(state.MovePrevious(19));
    EXPECT_EQ(state.CurrentIndex(), -1);
    EXPECT_FALSE(state.HasCurrentMatch());
    EXPECT_EQ(state.CurrentOrdinal(), 0u);
}

// ── AdoptMatchAt keeps a sensible current match after an edit ────────────

TEST(FindState, AdoptMatchAtSelectsTheFirstMatchAtOrAfterTheOffset) {
    FindReplaceState state;
    state.SetSearchText("one");
    state.Refresh("xx one two one two one", nullptr, nullptr);

    state.AdoptMatchAt(11);
    EXPECT_EQ(state.CurrentOrdinal(), 2u);
    TextMatch match;
    ASSERT_TRUE(state.CurrentMatch(&match));
    EXPECT_EQ(match.start, 11u);

    // Before the first match, that match becomes current.
    state.AdoptMatchAt(0);
    EXPECT_EQ(state.CurrentOrdinal(), 1u);
    ASSERT_TRUE(state.CurrentMatch(&match));
    EXPECT_EQ(match.start, 3u);
}

TEST(FindState, AdoptMatchAtWrapsWhenNothingIsLeftAfterTheOffset) {
    FindReplaceState state;
    state.SetSearchText("one");
    state.Refresh("xx one two one two one", nullptr, nullptr);

    state.AdoptMatchAt(20);
    EXPECT_EQ(state.CurrentOrdinal(), 1u);
    TextMatch match;
    ASSERT_TRUE(state.CurrentMatch(&match));
    EXPECT_EQ(match.start, 3u);
}

TEST(FindState, AdoptMatchAtOnAnEmptyMatchListLeavesNoCurrentMatch) {
    FindReplaceState state;
    state.SetSearchText("zebra");
    state.Refresh("the quick brown fox", nullptr, nullptr);

    state.AdoptMatchAt(4);
    EXPECT_EQ(state.CurrentIndex(), -1);
    EXPECT_FALSE(state.HasCurrentMatch());
}

// ── Invalidation (guide 31) ─────────────────────────────────────────────

TEST(FindState, InvalidateClearsResultsSoNoStaleOffsetSurvives) {
    FindReplaceState state;
    state.SetSearchText("cat");
    state.Refresh("cat here cat", nullptr, nullptr);
    ASSERT_TRUE(state.MoveNext(1));
    ASSERT_EQ(state.MatchCount(), 2u);
    EXPECT_FALSE(state.IsStale());

    state.Invalidate();
    EXPECT_TRUE(state.IsStale());
    EXPECT_EQ(state.MatchCount(), 0u);
    EXPECT_TRUE(state.Matches().empty());
    EXPECT_EQ(state.CurrentIndex(), -1);
    EXPECT_FALSE(state.HasCurrentMatch());
    EXPECT_EQ(state.CurrentOrdinal(), 0u);
    EXPECT_EQ(state.Status(), FindStatus::NoMatches);
    // A stale offset can never be navigated to, and nothing can be replaced.
    EXPECT_FALSE(state.MoveNext(4));
    EXPECT_FALSE(state.MovePrevious(9));
    std::string out;
    EXPECT_EQ(state.BuildReplacedDocument("cat here cat", &out), 0u);

    // Refresh brings the state back, and the search text survived.
    state.Refresh("cat here cat", nullptr, nullptr);
    EXPECT_FALSE(state.IsStale());
    EXPECT_EQ(state.MatchCount(), 2u);
}

TEST(FindState, RefreshAfterAnEditReportsNewOffsets) {
    FindReplaceState state;
    state.SetSearchText("cat");
    state.Refresh("cat here cat", nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 2u);

    state.Invalidate();
    state.Refresh("a cat and a cat", nullptr, nullptr);
    EXPECT_EQ(state.MatchCount(), 2u);
    EXPECT_EQ(state.Matches()[0].start, 2u);
    EXPECT_EQ(state.Matches()[1].start, 12u);
}

// ── Scope filter restricts matches to what can be shown or edited ───────

TEST(FindState, ScopeFilterDropsMatchesOutsideTheRenderedRange) {
    // The bytes above 8 are hidden Markdown syntax, as far as Find is
    // concerned: the search must behave as if they are not there (guide 4).
    const std::string document = "cat cat [link](cat)";
    ByteRange rendered;
    rendered.lo = 0;
    rendered.hi = 8;

    FindReplaceState state;
    state.SetSearchText("cat");
    state.Refresh(document, AcceptRange, &rendered);

    EXPECT_EQ(state.MatchCount(), 2u);
    EXPECT_EQ(state.Matches()[0].start, 0u);
    EXPECT_EQ(state.Matches()[1].start, 4u);
    ASSERT_TRUE(state.MoveNext(1));
    EXPECT_EQ(state.CurrentOrdinal(), 2u);
}

TEST(FindState, ScopeFilterStillFindsALaterOverlappingMatch) {
    // A rejected candidate is skipped, not consumed, so the overlapping match
    // that starts one byte later is still found (guide 23).
    FindReplaceState state;
    state.SetSearchText("aaa");
    state.Refresh("aaaa", RejectFirstOnly, nullptr);

    ASSERT_EQ(state.MatchCount(), 1u);
    EXPECT_EQ(state.Matches()[0].start, 1u);
    EXPECT_EQ(state.Matches()[0].length, 3u);
}

// ── A match never spans a line break (guide 19) ─────────────────────────

TEST(FindState, MatchNeverSpansALineBreak) {
    FindReplaceState state;
    state.SetSearchText("e\nA");
    state.Refresh("note\nAbove", nullptr, nullptr);
    EXPECT_EQ(state.MatchCount(), 0u);
    EXPECT_EQ(state.Status(), FindStatus::NoMatches);

    state.SetSearchText("b\nc");
    state.Refresh("ab\ncd", nullptr, nullptr);
    EXPECT_EQ(state.MatchCount(), 0u);

    // The same text on one line does match, so the rule is the line break and
    // not the letters.
    state.SetSearchText("bcd");
    state.Refresh("abcd", nullptr, nullptr);
    EXPECT_EQ(state.MatchCount(), 1u);
}

// ── Matches never overlap (guide 23) ────────────────────────────────────

TEST(FindState, MatchesNeverOverlap) {
    FindReplaceState state;
    state.SetSearchText("aa");
    state.Refresh("aaa", nullptr, nullptr);

    ASSERT_EQ(state.MatchCount(), 1u);
    EXPECT_EQ(state.Matches()[0].start, 0u);
    EXPECT_EQ(state.Matches()[0].length, 2u);
}

// ── Search options (guide 10 and 11) ─────────────────────────────────────

TEST(FindState, SearchOptionsControlCaseSensitivityAndWholeWord) {
    const std::string document = "Cat scatter CAT";

    FindReplaceState insensitive;
    insensitive.SetSearchText("cat");
    insensitive.Refresh(document, nullptr, nullptr);
    EXPECT_EQ(insensitive.MatchCount(), 3u);

    FindReplaceState sensitive;
    sensitive.SetSearchText("cat");
    sensitive.SetOptions(Options(true, false));
    sensitive.Refresh(document, nullptr, nullptr);
    // Match case finds only the lowercase "cat" inside "scatter".
    ASSERT_EQ(sensitive.MatchCount(), 1u);
    EXPECT_EQ(sensitive.Matches()[0].start, 5u);
    EXPECT_EQ(sensitive.Status(), FindStatus::Found);

    FindReplaceState wholeWord;
    wholeWord.SetSearchText("cat");
    wholeWord.SetOptions(Options(false, true));
    wholeWord.Refresh(document, nullptr, nullptr);
    ASSERT_EQ(wholeWord.MatchCount(), 2u);
    EXPECT_EQ(wholeWord.Matches()[0].start, 0u);
    EXPECT_EQ(wholeWord.Matches()[1].start, 12u);

    // The counter follows the active options, never a stale total.
    FindReplaceState narrow;
    narrow.SetSearchText("cat");
    narrow.Refresh(document, nullptr, nullptr);
    ASSERT_EQ(narrow.MatchCount(), 3u);
    narrow.SetOptions(Options(false, true));
    narrow.Refresh(document, nullptr, nullptr);
    EXPECT_EQ(narrow.MatchCount(), 2u);
    EXPECT_EQ(narrow.Status(), FindStatus::Found);
}

TEST(FindState, SearchAndReplaceTextRoundTrip) {
    FindReplaceState state;
    state.SetSearchText("alpha");
    state.SetReplaceText("beta");
    EXPECT_EQ(state.SearchText(), "alpha");
    EXPECT_EQ(state.ReplaceText(), "beta");

    state.Refresh("alpha and alpha", nullptr, nullptr);
    std::string out;
    EXPECT_EQ(state.BuildReplacedDocument("alpha and alpha", &out), 2u);
    EXPECT_EQ(out, "beta and beta");
}

// ── Replace All (guide 15, 17, 36, 37 and 38) ───────────────────────────

TEST(FindState, BuildReplacedDocumentReplacesEveryMatchInOnePass) {
    FindReplaceState state;
    state.SetSearchText("cat");
    state.SetReplaceText("dog");
    state.Refresh("cat cat cat", nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 3u);

    std::string out;
    EXPECT_EQ(state.BuildReplacedDocument("cat cat cat", &out), 3u);
    EXPECT_EQ(out, "dog dog dog");
    // Replacing the same match twice would show up as a second pass here.
    EXPECT_EQ(state.BuildReplacedDocument("cat cat cat", &out), 3u);
    EXPECT_EQ(out, "dog dog dog");
}

TEST(FindState, BuildReplacedDocumentDoesNotRescanInsertedText) {
    // The replacement contains the search string, so a matcher that keeps
    // searching would never terminate or would replace its own output
    // (guide 37 and 38).
    FindReplaceState state;
    state.SetSearchText("cat");
    state.SetReplaceText("catfish");
    state.Refresh("cat cat", nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 2u);

    std::string out;
    EXPECT_EQ(state.BuildReplacedDocument("cat cat", &out), 2u);
    EXPECT_EQ(out, "catfish catfish");

    // Each replacement starts where the original match was, so no part of the
    // inserted text is eaten a second time.
    EXPECT_EQ(out.size(), std::string("catfish catfish").size());
}

TEST(FindState, BuildReplacedDocumentWithoutMatchesReplacesNothing) {
    FindReplaceState state;
    state.SetSearchText("cat");
    state.SetReplaceText("dog");
    state.Refresh("nothing here", nullptr, nullptr);

    std::string out;
    EXPECT_EQ(state.BuildReplacedDocument("nothing here", &out), 0u);
    // *out is written even with nothing to replace, so a caller can always
    // read it. The zero return is the signal not to apply it (guide 59).
    EXPECT_EQ(out, "nothing here");
}

TEST(FindState, SettingReplaceTextKeepsTheMatchesFoundSoFar) {
    // The replacement text cannot change which ranges match, so a caller that
    // searches first and types the replacement afterwards must still be able
    // to replace. Invalidating here silently turned Replace All into a no-op.
    FindReplaceState state;
    state.SetSearchText("cat");
    state.Refresh("cat and cat", nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 2u);

    state.SetReplaceText("dog");
    EXPECT_FALSE(state.IsStale());
    EXPECT_EQ(state.MatchCount(), 2u);

    std::string out;
    EXPECT_EQ(state.BuildReplacedDocument("cat and cat", &out), 2u);
    EXPECT_EQ(out, "dog and dog");
}

TEST(FindState, SettingSearchTextOrOptionsInvalidatesTheMatches) {
    // The counterpart to the test above: a changed query or a changed option
    // describes a different search, so the old offsets must not survive
    // (guide 31).
    FindReplaceState state;
    state.SetSearchText("cat");
    state.Refresh("cat and cat", nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 2u);

    state.SetSearchText("dog");
    EXPECT_TRUE(state.IsStale());
    EXPECT_EQ(state.MatchCount(), 0u);

    state.SetSearchText("cat");
    state.Refresh("cat and cat", nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 2u);

    state.SetOptions(Options(true, true));
    EXPECT_TRUE(state.IsStale());
    EXPECT_EQ(state.MatchCount(), 0u);
}

TEST(FindState, SelectionScopeIsNormalizedOnTheWayIn) {
    // A right-to-left selection stores a forward range, so a filter built
    // from the accessors covers the same bytes either way (guide 51).
    FindReplaceState state;

    state.SetSelectionScope(11, 4);
    EXPECT_EQ(state.SelectionScopeStart(), 4u);
    EXPECT_EQ(state.SelectionScopeEnd(), 11u);

    state.SetSelectionScope(2, 8);
    EXPECT_EQ(state.SelectionScopeStart(), 2u);
    EXPECT_EQ(state.SelectionScopeEnd(), 8u);
}

TEST(FindState, BuildReplacedDocumentWithAnEmptyReplacementDeletesMatches) {
    FindReplaceState state;
    state.SetSearchText("cat");
    state.SetReplaceText("");
    state.Refresh("a cat b cat", nullptr, nullptr);

    std::string out;
    EXPECT_EQ(state.BuildReplacedDocument("a cat b cat", &out), 2u);
    EXPECT_EQ(out, "a  b ");
}

// ── Find in selection (guide 51) ────────────────────────────────────────

TEST(FindState, ReplacementInsideASelectionScopeLeavesOutsideBytesAlone) {
    const std::string document = "cat dog cat";
    ByteRange selection;
    selection.lo = 4;
    selection.hi = 11;

    FindStateOptions options;
    options.withinSelection = true;
    FindReplaceState state;
    state.SetSearchText("cat");
    state.SetReplaceText("bird");
    state.SetOptions(options);
    state.SetSelectionScope(4, 11);
    state.Refresh(document, AcceptRange, &selection);

    // "cat dog cat": the scope [4, 11) covers "dog cat", so the match at 0 is
    // outside it and the one at 8 is the only live match.
    ASSERT_EQ(state.MatchCount(), 1u);
    EXPECT_EQ(state.Matches()[0].start, 8u);

    std::string out;
    EXPECT_EQ(state.BuildReplacedDocument(document, &out), 1u);
    EXPECT_EQ(out, "cat dog bird");
}

TEST(FindState, InvertedSelectionScopeBehavesLikeAForwardOne) {
    const std::string document = "cat dog cat";
    ByteRange selection;
    selection.lo = 4;
    selection.hi = 11;

    FindStateOptions options;
    options.withinSelection = true;
    FindReplaceState state;
    state.SetSearchText("cat");
    state.SetOptions(options);
    // A selection can be captured from the anchor to the caret, so the pair
    // may arrive inverted.
    state.SetSelectionScope(11, 4);
    state.Refresh(document, AcceptRange, &selection);

    ASSERT_EQ(state.MatchCount(), 1u);
    EXPECT_EQ(state.Matches()[0].start, 8u);
}

// ── Danish characters (guide 25) ────────────────────────────────────────

// Every setter marks the stored offsets stale until the next Refresh, so the
// replace text is set before the search runs here (guide 31).
TEST(FindState, CaseInsensitiveSearchTreatsAUppercaseAndLowercaseRingAAsEqual) {
    // A-ring is U+00C5 / U+00E5, so C3 85 in UTF-8 and C3 A5.
    const std::string document = "\xC3\x85" "r og \xC3\xA5";

    FindReplaceState insensitive;
    insensitive.SetSearchText("\xC3\xA5");
    insensitive.Refresh(document, nullptr, nullptr);
    ASSERT_EQ(insensitive.MatchCount(), 2u);
    EXPECT_EQ(insensitive.Matches()[0].start, 0u);
    EXPECT_EQ(insensitive.Matches()[1].start, 7u);
    EXPECT_EQ(insensitive.Matches()[1].length, 2u);

    // The uppercase query finds the lowercase occurrence just as well.
    FindReplaceState upper;
    upper.SetSearchText("\xC3\x85");
    upper.Refresh(document, nullptr, nullptr);
    EXPECT_EQ(upper.MatchCount(), 2u);

    // Match case keeps them apart.
    FindReplaceState sensitive;
    sensitive.SetSearchText("\xC3\xA5");
    sensitive.SetOptions(Options(true, false));
    sensitive.SetReplaceText("\xC3\x86");
    sensitive.Refresh(document, nullptr, nullptr);
    ASSERT_EQ(sensitive.MatchCount(), 1u);
    EXPECT_EQ(sensitive.Matches()[0].start, 7u);

    // Replacement writes the bytes the caller asked for, unchanged.
    std::string out;
    EXPECT_EQ(sensitive.BuildReplacedDocument(document, &out), 1u);
    EXPECT_EQ(out, "\xC3\x85" "r og \xC3\x86");
}

TEST(FindState, DanishLettersAreSearchableAsThemselves) {
    // ae (C3 A6), o-stroke (C3 B8), A-ring (C3 85), then the uppercase
    // AE (C3 86) and OE (C3 98). Byte offsets in the literal below:
    //   ae 0, o-stroke 6, A-ring 10, AE 14, OE 18.
    const std::string document =
        "\xC3\xA6" "ble \xC3\xB8" "r \xC3\x85" "r \xC3\x86" "r \xC3\x98" "r";

    FindReplaceState state;
    state.SetOptions(Options(true, false));

    state.SetSearchText("\xC3\xA6");
    state.Refresh(document, nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 1u);
    EXPECT_EQ(state.Matches()[0].start, 0u);
    EXPECT_EQ(state.Matches()[0].length, 2u);

    state.SetSearchText("\xC3\xB8");
    state.Refresh(document, nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 1u);
    EXPECT_EQ(state.Matches()[0].start, 6u);

    state.SetSearchText("\xC3\x98");
    state.Refresh(document, nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 1u);
    EXPECT_EQ(state.Matches()[0].start, 18u);

    state.SetSearchText("\xC3\x85");
    state.Refresh(document, nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 1u);
    EXPECT_EQ(state.Matches()[0].start, 10u);

    // Case insensitive: the uppercase query finds both the uppercase and the
    // lowercase spelling, which are 14 bytes apart here.
    state.SetOptions(Options(false, false));
    state.SetSearchText("\xC3\x86");
    state.Refresh(document, nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 2u);
    EXPECT_EQ(state.Matches()[0].start, 0u);
    EXPECT_EQ(state.Matches()[1].start, 14u);

    state.SetSearchText("\xC3\x98");
    state.Refresh(document, nullptr, nullptr);
    ASSERT_EQ(state.MatchCount(), 2u);
    EXPECT_EQ(state.Matches()[0].start, 6u);
    EXPECT_EQ(state.Matches()[1].start, 18u);
}

// NOT TESTED: Danish digraph folding, that is searching "ae" and matching
// the single letter ae (U+00E6), or "oe" matching o-stroke (U+00F8). That is
// deliberately NOT wanted here. Guide 39 requires the characters in the Find
// field to be literal search characters unless a Regex mode is enabled, and
// folding "ae" into a one-character span would match text the user never
// typed. Guide 25 only requires the letters themselves to be searchable, which
// DanishLettersAreSearchableAsThemselves and
// CaseInsensitiveSearchTreatsAUppercaseAndLowercaseRingAAsEqual cover.

TEST(FindState, WholeWordSearchOnDanishLettersUsesWordBoundaries) {
    // The letters are word characters, so a whole-word query must not match
    // inside a longer word.
    const std::string document = "\xC3\xA6" "rlig bl\xC3\xA6" "nd";

    FindReplaceState loose;
    loose.SetSearchText("\xC3\xA6");
    loose.Refresh(document, nullptr, nullptr);
    EXPECT_EQ(loose.MatchCount(), 2u);

    FindReplaceState wholeWord;
    wholeWord.SetSearchText("\xC3\xA6");
    wholeWord.SetOptions(Options(false, true));
    wholeWord.Refresh(document, nullptr, nullptr);
    EXPECT_EQ(wholeWord.MatchCount(), 0u);
}

// No RUN_ALL_TESTS() here: tests/test_main.cpp owns main, and a second one
// makes the test binary fail to link with LNK2005.
