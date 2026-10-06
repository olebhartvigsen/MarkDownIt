#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "findstate.h"

// Modeless Find & Replace bar.
//
// A floating, owned popup built from REAL Win32 child controls (EDIT,
// BUTTON, STATIC), never a dialog and never a hand painted surface. Tab
// order, focus, Enter, Shift+Enter, Esc, mouse clicks and screen reader
// names therefore all come from the controls themselves (guide 44 and 45),
// which is far more robust than reimplementing them.
//
// MODELESS on purpose: the document stays visible and editable while the bar
// is open (guide 31). The bar is created with CreateWindowEx and rides the
// owner's existing message loop. DialogBox is never used, because a modal
// loop would block the document.
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
    // bar asks for this every time instead of remembering a position, so
    // the answer is always against the live document.
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
    // so a bare trailing enumerator would be 113 and every use of this as
    // an array size or a loop bound would be off by a hundred. Deriving it
    // from the first and last id keeps the two facts separate, so they
    // cannot drift apart again.
    kFindBarControlCount = kFindBarStatus - kFindBarFindEdit + 1
};

// The two functions the one bar serves. The mode decides enablement, not
// layout or geometry: both rows stay on screen in both modes, and the
// replace row greys out in Find mode.
enum class FindBarMode { Find, FindReplace };

class FindBar {
public:
    FindBar();
    ~FindBar();

    FindBar(const FindBar&) = delete;
    FindBar& operator=(const FindBar&) = delete;

    // Replacing the listener is safe at any time, including while the bar
    // is open.
    void SetListener(FindBarListener listener);

    // Creates the bar on first use and reuses it afterwards. Returns false
    // only when the window could not be created. The bar keeps its text and
    // options across a close and reopen (guide 54).
    //
    // mode selects the function: FindReplace turns the replace half on
    // (Ctrl+H, guide 2.2), Find greys it out (Ctrl+F, guide 2.1 and 27).
    // Both modes keep the same layout, so nothing moves on a mode switch.
    //
    // Focus moves to the Find field and its text is selected, so typing
    // immediately replaces it (guide 28). Show does NOT change
    // SetReplaceEnabled: the caller states the edit mode every time, because
    // it is the only place that knows which view is on screen (guide 4).
    bool Show(HWND owner, FindBarMode mode);

    void Hide();
    // Hide plus onClose. This is what Esc and the Close button call.
    void Close();

    // Switches the function while the bar stays open. Ctrl+F drops back to
    // Find mode, Ctrl+H turns the replace half on. Reuses the same window,
    // and keeps text, options and history (guide 54).
    void SetMode(FindBarMode mode);
    FindBarMode Mode() const { return mode_; }

    bool IsVisible() const;
    bool IsCreated() const { return hwnd_ != nullptr; }

    // Tears the windows down. Idempotent, and safe to call after the owner
    // window is gone. The destructor calls it.
    void Destroy();

    // Docks the bar along the bottom of the owner's client area, spanning
    // the full width. The owner calls this from its layout, after it has
    // reclaimed or reserved the strip height.
    void Dock();

    // The strip height in pixels at the current DPI, so the owner can
    // reserve the space before docking.
    int HeightPx() const;

    // Ctrl+F while the bar is already open: focus the Find field and select
    // its text so a new term can be typed straight away (guide 2.1).
    void FocusAndSelectSearchText();

    // Setting the text does not fire onSearchChanged: the caller is already
    // running the search it wants (guide 9). The bar also resets its counter
    // so no stale count survives a new query (guide 53).
    void SetSearchText(const std::wstring& text);
    std::wstring GetSearchText() const;

    // Changing the replacement cannot change which ranges match, so this
    // does not fire onSearchChanged and needs no re-search.
    void SetReplaceText(const std::wstring& text);
    std::wstring GetReplaceText() const;

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
    // forgets cannot mutate a document that must not change. The gate is
    // enforced inside the bar as well, so a click, a key or a programmatic
    // call cannot bypass it.
    void SetReplaceEnabled(bool enabled);
    bool IsReplaceEnabled() const { return replaceEnabled_; }

    // The message line under the fields: "14 replacements made." or an
    // error. Empty text clears it.
    void SetStatusText(const std::wstring& text);
    std::wstring GetStatusText() const;

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

    // The bar window, for the owner that needs to place it or filter input.
    HWND Handle() const { return hwnd_; }

private:
    // Window procedures. Members, so the template stays free of globals and
    // can reach the private state.
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp,
                                    LPARAM lp);
    static LRESULT CALLBACK ChildSubclass(HWND child, UINT msg, WPARAM wp,
                                          LPARAM lp);
    static bool RegisterFindBarClass();

    bool EnsureCreated();
    bool CreateControls();
    // Abandons a half built bar: the window and every child created so far
    // go away together, with their window procedures restored first.
    void AbandonPartial();
    void DetachChildren();
    void ResetHandles();
    void DockWith(int dpi);
    // Push the current state into the controls that exist, so text and
    // options set before Show survive into the window.
    void SyncControlsFromState();

    bool HandleCommand(WPARAM wp, LPARAM lp);
    bool HandleKeyDown(HWND child, WPARAM wp);
    void OnFindTextChanged();
    void OnReplaceTextChanged();
    void OnOptionToggled();
    void RequestReplaceCurrent();
    void RequestReplaceAll();

    void MoveTabFocus(HWND from, bool forward);
    void UpdateControls();
    void LayoutControls();
    // `id` is a FindBarControlId, not a child index.
    void PlaceChild(int id, int x, int y, int w, int h,
                    bool visible) const;

    HWND Control(int id) const;
    void EnableControl(int id, bool enabled);
    void SetControlText(int id, const std::wstring& text);
    std::wstring ReadControlText(int id) const;
    std::wstring FormatCounter() const;
    // The status line: the owner's text when there is any, otherwise the
    // read-only note while the replace half is on screen and unavailable.
    std::wstring FormatStatusText() const;
    uint32_t AskCaretOffset() const;

    struct SubclassRecord {
        WNDPROC proc = nullptr;
    };

    static bool IsEditId(int id);
    static bool IsButtonId(int id);
    static int IndexOfId(int id);

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    HFONT font_ = nullptr;
    int fontDpi_ = 0;
    int dpi_ = 96;

    FindReplaceState state_;
    FindBarListener listener_;

    HWND controls_[kFindBarControlCount] = {};
    SubclassRecord subclasses_[kFindBarControlCount] = {};

    // Counters as last reported by the owner. The bar never computes a match
    // count itself, so it can never show a wrong one (guide 53).
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
    // Set while the bar writes its own controls, so the notifications those
    // writes generate cannot feed back into a new search.
    bool applying_ = false;
};