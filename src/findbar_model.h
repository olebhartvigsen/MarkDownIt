#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "findstate.h"

// The find and replace model, shared by every platform's find bar.
//
// This header holds the part of the find bar that is about SEARCH STATE
// and the OWNER CONTRACT, and nothing about pixels. The Windows bar
// (src/findbar.h) builds real Win32 child controls on top of it; the
// macOS bar builds AppKit controls on top of the same model, so both
// platforms agree on how a search behaves.
//
// Division of work (guide 62 and 64). This component owns SEARCH STATE and
// VISUAL STATE only:
//   * It collects the query, the replacement, the options and the counter.
//   * It NEVER touches the document buffer, the undo stack or the document
//     selection, and it never stores a document offset of its own.
// Every change it wants applied is requested through FindBarListener. That
// separation is also why it stays correct while the document is edited
// underneath it: there is no cached offset an edit could invalidate.
//
// Owner contract, in order:
//   1. Ctrl+F: bar.Show(owner, false). Ctrl+H: bar.Show(owner, true).
//      SetReplaceEnabled(editing) must be called on every Show and on every
//      change of view, because Replace defaults to DISABLED.
//   2. Populate the field with SetSearchText(selection) and call
//      state().Refresh(document, scopeFilter, context),
//      state().AdoptMatchAt(caret), SyncFromState(). See guide 9.
//   3. Incremental search: the owner implements onSearchChanged by refreshing
//      bar.state() and reporting back with SetMatchCount or SyncFromState
//      (guide 5). The bar never searches the document itself.
//   4. Navigation: the bar moves bar.state() and reports the new current
//      match through onSelectionChanged, which the owner turns into a
//      document selection, a scroll into view and renderer highlighting
//      (guide 6 and 7).
//   5. Replace: the owner performs the edit and the undo transaction
//      (guide 17 and 58), then Refresh + SelectMatchAt(caretAfter) so the
//      session continues with the ORIGINAL query (guide 22).
//   6. Replace All: the owner calls state().BuildReplacedDocument and applies
//      the result only when the returned count is greater than zero (guide 59).
//   7. Report the result with SetStatusText, for example "14 replacements
//      made." (guide 15).
//
// The owner reads caretOffset() when a navigation is requested, so the bar
// never caches a position of its own (guide 31).

struct FindBarListener {
    // Where a navigation should start from, in document byte offsets. The
    // bar asks for this every time instead of remembering a position, so the
    // answer is always against the live document.
    std::function<uint32_t()> caretOffset;

    // The query, the options or the scope changed. The owner must refresh
    // bar.state() against the current document, choose the current match
    // and report the counts. Typed text is never a document undo entry and
    // never marks the document dirty (guide 58 and 59).
    std::function<void()> onSearchChanged;

    // A match became current, through Next, Previous, Enter, Shift+Enter,
    // Replace or a document edit. Scroll it into view, select it and update
    // the highlights (guide 6 and 7).
    std::function<void(uint32_t offset, uint32_t length)> onSelectionChanged;

    // The user clicked Replace. Replaces exactly this range. Never fired in
    // a read-only view (guide 4).
    std::function<void(uint32_t offset, uint32_t length)> onReplaceCurrent;

    // The user clicked Replace All. Replaces every current match, as ONE
    // undo transaction. Never fired in a read-only view (guide 4).
    std::function<void(const std::wstring& query,
                       const std::wstring& replacement,
                       const FindStateOptions& options)> onReplaceAll;

    // The bar closed, from Esc or from the Close button. The owner returns
    // focus to the document and keeps its selection (guide 2.4 and 28).
    std::function<void()> onClose;
};

// Control identifiers. They are part of the public surface so an owner can
// reach a control with GetDlgItem if it needs to, and they double as the
// declaration order that produces the tab order (guide 45).
enum FindBarControlId {
    kFindBarFindEdit = 100,
    kFindBarReplaceEdit,
    kFindBarFindLabel,
    kFindBarReplaceLabel,
    kFindBarPrevious,
    kFindBarNext,
    kFindBarMatchCase,
    kFindBarWholeWord,
    kFindBarReplace,
    kFindBarReplaceAll,
    kFindBarClose,
    kFindBarCounter,
    kFindBarStatus,

    // The NUMBER of controls, not the next id. The ids above start at 100,
    // so a bare trailing enumerator would be 113 and every use of this as an
    // array size or a loop bound would be off by a hundred. Deriving it
    // from the first and last id keeps the two facts separate, so they
    // cannot drift apart again.
    kFindBarControlCount = kFindBarStatus - kFindBarFindEdit + 1
};

// The two functions the one bar serves. The mode decides enablement, not
// layout or geometry: both rows stay on screen in both modes, and the
// replace row greys out in Find mode.
enum class FindBarMode { Find, FindReplace };

// The platform-independent half of the find bar.
//
// It owns the search state, the listener, the field text, the options and
// the match counter, and it moves the state on a navigation request. It
// does not know what a window is, so the same model drives the Windows bar
// and the macOS bar.
class FindBarModel {
public:
    FindBarModel() = default;
    FindBarModel(const FindBarModel&) = delete;
    FindBarModel& operator=(const FindBarModel&) = delete;

    // Replacing the listener is safe at any time, including while the bar
    // is open.
    void SetListener(FindBarListener listener) { listener_ = std::move(listener); }
    const FindBarListener& listener() const { return listener_; }

    // Switches the function while the bar stays open. Ctrl+F drops back to
    // Find mode, Ctrl+H turns the replace half on. Keeps text, options and
    // history (guide 54).
    void SetMode(FindBarMode mode) { mode_ = mode; }
    FindBarMode Mode() const { return mode_; }

    // Setting the text does not fire onSearchChanged: the caller is already
    // running the search it wants (guide 9). The bar also resets its counter
    // so no stale count survives a new query (guide 53).
    void SetSearchText(const std::wstring& text);
    const std::wstring& GetSearchText() const { return searchText_; }

    // Changing the replacement cannot change which ranges match, so this
    // does not fire onSearchChanged and needs no re-search.
    void SetReplaceText(const std::wstring& text);
    const std::wstring& GetReplaceText() const { return replaceText_; }

    // Match case and Whole word, both default OFF (guide 10 and 11). Whole
    // word is applied by the search engine, never by the bar. Both fire
    // onSearchChanged because they change which ranges match.
    void SetCaseSensitive(bool enabled);
    bool GetCaseSensitive() const;
    void SetWholeWord(bool enabled);
    bool GetWholeWord() const;

    // The counter the bar shows: "3 of 17", "17 matches" or "No matches"
    // (guide 3, 21 and 52). An empty Find field shows nothing at all and
    // disables every action (guide 20).
    void SetMatchCount(size_t currentOrdinal, size_t total, FindStatus status);
    // Convenience for the common flow: take the counter from the state the
    // owner has just refreshed.
    void SyncFromState();

    // Grey out Replace and Replace All where editing is not allowed, which
    // is every read-only view (guide 4). Defaults to FALSE, so a caller who
    // forgets cannot mutate a document that must not change.
    void SetReplaceEnabled(bool enabled) { replaceEnabled_ = enabled; }
    bool IsReplaceEnabled() const { return replaceEnabled_; }

    // The message line under the fields: "14 replacements made." or an
    // error. Empty text clears it.
    void SetStatusText(const std::wstring& text) { statusText_ = text; }
    const std::wstring& GetStatusText() const { return statusText_; }

    // The search state this bar owns. The owner refreshes it and reads the
    // matches from it. MoveNext, MovePrevious, AdoptMatchAt,
    // BuildReplacedDocument and the selection scope all live here, not in
    // the UI (guide 61).
    FindReplaceState& state() { return state_; }
    const FindReplaceState& state() const { return state_; }

    // Navigation helpers. Each moves the state and reports the new current
    // match through onSelectionChanged. `from` is the document offset to
    // start from, normally caretOffset(). Navigation wraps at both ends of
    // the document (guide 8). Returns false when there is no match at all.
    bool NavigateNext(uint32_t from);
    bool NavigatePrevious(uint32_t from);
    // Make the match at or after `offset` current. Used by the owner after
    // a replacement or a document edit (guide 22 and 30).
    bool SelectMatchAt(uint32_t offset);

    // The counter the model holds, for the bar to display.
    size_t countCurrent() const { return countCurrent_; }
    size_t countTotal() const { return countTotal_; }
    FindStatus status() const { return status_; }

    // The status line: the owner's text when there is any, otherwise the
    // read-only note while the replace half is on screen and unavailable.
    std::wstring FormatStatusText() const;

protected:
    // The counter and the status the model reported, so a bar can show
    // them without recomputing anything.
    size_t countCurrent_ = 0;
    size_t countTotal_ = 0;
    FindStatus status_ = FindStatus::Empty;

    // The field text as the bar last saw it, cached so text set BEFORE the
    // window exists is not lost and so GetSearchText works while hidden.
    std::wstring searchText_;
    std::wstring replaceText_;
    std::wstring statusText_;

    FindBarMode mode_ = FindBarMode::Find;
    bool replaceEnabled_ = false;

    FindReplaceState state_;
    FindBarListener listener_;
};
