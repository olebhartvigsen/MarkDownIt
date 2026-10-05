#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "searchreplace.h"

// Find & Replace state, kept apart from the UI so the search logic can be
// unit tested without a window (find-replace guide sections 61 and 62).
//
// Everything here works in DOCUMENT byte offsets. Nothing in this file knows
// about screens, zoom, scroll or the viewport, so a repaint, a zoom change or
// a window resize can never change a logical search result (guide 64).

// A range takes part in the search only when the filter accepts all of it.
// The filter is how the caller restricts searching to what is really there:
// rendered document text (never hidden Markdown syntax) and, when Find is
// scoped to the selection, only the bytes inside that selection (guide 4,
// 48 and 51).
using FindScopeFilter = bool (*)(uint32_t start, uint32_t length,
                                 void* context);

struct FindStateOptions {
    SearchOptions search;            // caseSensitive, wholeWord
    bool withinSelection = false;    // scope: the selection, not the document
};

enum class FindStatus {
    Empty,       // no search text: no matches, nothing to replace
    NoMatches,   // search text is set, the document has no match
    Found,       // at least one match
};

class FindReplaceState {
public:
    // ── Search state (what the user is looking for) ──────────────────
    void SetSearchText(const std::string& text);
    const std::string& SearchText() const { return searchText_; }

    // Does not invalidate the results: the replacement text cannot change
    // which ranges match.
    void SetReplaceText(const std::string& text);
    const std::string& ReplaceText() const { return replaceText_; }

    void SetOptions(const FindStateOptions& options);
    const FindStateOptions& Options() const { return options_; }

    // The selection Find is scoped to, used when withinSelection is true.
    // Normalized on the way in, so an inverted pair behaves like a forward one.
    void SetSelectionScope(uint32_t start, uint32_t end);
    uint32_t SelectionScopeStart() const { return selectionStart_; }
    uint32_t SelectionScopeEnd() const { return selectionEnd_; }

    // ── Document state (where that text currently exists) ────────────
    // Recompute every match against the current document text. Matches never
    // overlap and never span a line break, so a query can never join the end
    // of one block with the start of the next (guide 19 and 23).
    // `scope` may be null, which accepts every range.
    void Refresh(const std::string& document, FindScopeFilter scope,
                 void* scopeContext);

    // The document changed. Every stored offset is stale until the next
    // Refresh, so no caller can act on an offset that no longer exists
    // (guide 31).
    void Invalidate();
    bool IsStale() const { return stale_; }

    // ── Results ──────────────────────────────────────────────────────
    const std::vector<TextMatch>& Matches() const { return matches_; }
    size_t MatchCount() const { return matches_.size(); }
    int CurrentIndex() const { return currentIndex_; }
    bool HasQuery() const { return !searchText_.empty(); }
    bool HasCurrentMatch() const;
    FindStatus Status() const;
    // The current match, or false when there is none (empty query, no match).
    bool CurrentMatch(TextMatch* out) const;
    // One-based index for the counter the UI shows, 0 when there is no match.
    size_t CurrentOrdinal() const;

    // ── Navigation (visual state follows from this, never the reverse) ──
    // Move to the next or previous match from a document offset, wrapping at
    // both ends of the document (guide 8). Returns false when there is no
    // match to move to.
    bool MoveNext(uint32_t from);
    bool MovePrevious(uint32_t from);
    // Make the match at or after `offset` current, wrapping when the document
    // has no match there. Used after a replacement and after an edit, so the
    // session keeps a sensible current match instead of a stale one.
    void AdoptMatchAt(uint32_t offset);

    // ── Replacement ──────────────────────────────────────────────────
    // The whole document with every current match replaced, built in a
    // single pass over the original match list, so newly inserted text is
    // never searched again (guide 37 and 38).
    //
    // *out is always written, including when there is nothing to replace, so
    // it is always safe to read. The RETURN VALUE is what says whether the
    // document changed: 0 means the caller must not apply it, which is what
    // keeps Find, and a Replace All with no match, from marking the document
    // dirty (guide 59).
    size_t BuildReplacedDocument(const std::string& document,
                                 std::string* out) const;

private:
    std::string searchText_;
    std::string replaceText_;
    FindStateOptions options_;
    uint32_t selectionStart_ = 0;
    uint32_t selectionEnd_ = 0;
    std::vector<TextMatch> matches_;
    int currentIndex_ = -1;
    bool stale_ = true;
};