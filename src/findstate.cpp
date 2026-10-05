#include "findstate.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// First index whose start is at or after `from`, or matches.size() when every
// match starts before it. Shared by MoveNext and AdoptMatchAt so the two can
// never disagree about where a match begins.
size_t FirstAtOrAfter(const std::vector<TextMatch>& matches, uint32_t from) {
    const auto it = std::lower_bound(
        matches.begin(), matches.end(), from,
        [](const TextMatch& match, uint32_t offset) {
            return match.start < offset;
        });
    return static_cast<size_t>(it - matches.begin());
}

}  // namespace

void FindReplaceState::SetSearchText(const std::string& text) {
    searchText_ = text;
    // Every stored offset was computed for the old query, so none of it can
    // be shown until the next Refresh (guide 31).
    Invalidate();
}

void FindReplaceState::SetReplaceText(const std::string& text) {
    // Deliberately does NOT invalidate. Which ranges match depends on the
    // search text, the options and the document, never on the replacement.
    // Invalidating here meant the natural order Refresh then SetReplaceText
    // silently wiped every match, so Replace All replaced nothing.
    replaceText_ = text;
}

void FindReplaceState::SetOptions(const FindStateOptions& options) {
    options_ = options;
    // Case sensitivity and whole-word matching change which ranges match, so
    // the old offsets describe a different search than the new options.
    Invalidate();
}

void FindReplaceState::SetSelectionScope(uint32_t start, uint32_t end) {
    // Normalized on the way in, so the stored pair is always a forward range.
    // A caller that selected right to left still gets the range it meant.
    if (start <= end) {
        selectionStart_ = start;
        selectionEnd_ = end;
    } else {
        selectionStart_ = end;
        selectionEnd_ = start;
    }
}

void FindReplaceState::Refresh(const std::string& document,
                               FindScopeFilter scope, void* scopeContext) {
    // A null filter is valid and accepts every range, which is exactly what
    // the matcher expects. The filter stays the caller's: rendered document
    // text for a document scope, the selection range for withinSelection.
    matches_ = FindTextMatchesScoped(document, searchText_, options_.search,
                                     scope, scopeContext);

    // Document order is the order every navigation and counter assumes.
    std::stable_sort(matches_.begin(), matches_.end(),
                     [](const TextMatch& a, const TextMatch& b) {
                         return a.start < b.start;
                     });

    // The list was rebuilt, so any index into the old one is meaningless.
    // Refresh reports what exists; the caller decides which match becomes
    // current with MoveNext or AdoptMatchAt, which is what keeps a wrapped
    // "first match is current" (guide 63) in the caller's hands.
    currentIndex_ = -1;
    stale_ = false;
}

void FindReplaceState::Invalidate() {
    // Clearing the list as well as the flag is deliberate: a caller that
    // ignores IsStale() then gets zero matches instead of offsets that point
    // at text which no longer exists.
    stale_ = true;
    matches_.clear();
    currentIndex_ = -1;
}

bool FindReplaceState::HasCurrentMatch() const {
    return currentIndex_ >= 0 &&
           static_cast<size_t>(currentIndex_) < matches_.size();
}

FindStatus FindReplaceState::Status() const {
    if (!HasQuery()) return FindStatus::Empty;
    if (matches_.empty()) return FindStatus::NoMatches;
    return FindStatus::Found;
}

bool FindReplaceState::CurrentMatch(TextMatch* out) const {
    if (!out || !HasCurrentMatch()) return false;
    *out = matches_[static_cast<size_t>(currentIndex_)];
    return true;
}

size_t FindReplaceState::CurrentOrdinal() const {
    if (!HasCurrentMatch()) return 0;
    return static_cast<size_t>(currentIndex_) + 1;
}

bool FindReplaceState::MoveNext(uint32_t from) {
    if (matches_.empty()) {
        currentIndex_ = -1;
        return false;
    }
    const size_t index = FirstAtOrAfter(matches_, from);
    // Past the last match: wrap to the top of the document (guide 8).
    currentIndex_ = static_cast<int>(index == matches_.size() ? 0 : index);
    return true;
}

bool FindReplaceState::MovePrevious(uint32_t from) {
    if (matches_.empty()) {
        currentIndex_ = -1;
        return false;
    }
    // Strictly before `from`, so Previous from the first match still moves.
    const size_t index = FirstAtOrAfter(matches_, from);
    if (index == 0) {
        // Before the first match: wrap to the last one (guide 8).
        currentIndex_ = static_cast<int>(matches_.size() - 1);
    } else {
        currentIndex_ = static_cast<int>(index - 1);
    }
    return true;
}

void FindReplaceState::AdoptMatchAt(uint32_t offset) {
    if (matches_.empty()) {
        currentIndex_ = -1;
        return;
    }
    const size_t index = FirstAtOrAfter(matches_, offset);
    currentIndex_ = static_cast<int>(index == matches_.size() ? 0 : index);
}

size_t FindReplaceState::BuildReplacedDocument(const std::string& document,
                                               std::string* out) const {
    if (!out) return 0;
    // *out is always written, even with nothing to replace, so a caller never
    // has to check it was touched before reading. The return value stays the
    // signal for "the document changed": 0 means the caller must not apply it,
    // which is what keeps Find and an empty Replace All from marking the
    // document dirty (guide 59).
    if (matches_.empty()) {
        *out = document;
        return 0;
    }
    // One pass over the original match list. Text the replacement inserts is
    // never searched again, so replacing "a" with "aa" still terminates and
    // replaces each original occurrence exactly once (guide 37 and 38).
    *out = ReplaceTextMatches(document, matches_, replaceText_);
    return matches_.size();
}