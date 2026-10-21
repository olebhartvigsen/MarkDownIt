#include "findbar_model.h"

#include "textenc.h"

namespace {

// The strings the model itself needs. The rest of the bar's strings live
// with the widget, because only the widget shows them (guide 46).
const wchar_t* const kStrNoMatches = L"No matches";
const wchar_t* const kStrReadOnlyHint = L"Replacing is not available in this view.";
const wchar_t* const kCounterSeparator = L" of ";

}  // namespace

void FindBarModel::SetSearchText(const std::wstring& text) {
    searchText_ = text;
    state_.SetSearchText(textenc::WideToUtf8(text));
    // The query changed, so the old counts describe a different search. Clear
    // them instead of leaving a wrong number on screen (guide 53); the owner
    // reports the real ones from onSearchChanged.
    countCurrent_ = 0;
    countTotal_ = 0;
    status_ = FindStatus::Empty;
}

void FindBarModel::SetReplaceText(const std::wstring& text) {
    replaceText_ = text;
    // Which ranges match cannot depend on the replacement text, so the state
    // takes this without invalidating its matches.
    state_.SetReplaceText(textenc::WideToUtf8(text));
}

void FindBarModel::SetCaseSensitive(bool enabled) {
    FindStateOptions options = state_.Options();
    options.search.caseSensitive = enabled;
    // Options change which ranges match, so the stored offsets are stale
    // until the owner refreshes (guide 31).
    state_.SetOptions(options);
    countCurrent_ = 0;
    countTotal_ = 0;
    status_ = FindStatus::Empty;
    if (listener_.onSearchChanged) listener_.onSearchChanged();
}

bool FindBarModel::GetCaseSensitive() const {
    return state_.Options().search.caseSensitive;
}

void FindBarModel::SetWholeWord(bool enabled) {
    FindStateOptions options = state_.Options();
    options.search.wholeWord = enabled;
    state_.SetOptions(options);
    countCurrent_ = 0;
    countTotal_ = 0;
    status_ = FindStatus::Empty;
    if (listener_.onSearchChanged) listener_.onSearchChanged();
}

bool FindBarModel::GetWholeWord() const {
    return state_.Options().search.wholeWord;
}

void FindBarModel::SetMatchCount(size_t currentOrdinal, size_t total,
                                 FindStatus status) {
    countCurrent_ = currentOrdinal;
    countTotal_ = total;
    status_ = status;
}

void FindBarModel::SyncFromState() {
    SetMatchCount(state_.CurrentOrdinal(), state_.MatchCount(),
                  state_.Status());
}

// ---------------------------------------------------------------------------
// Navigation. The bar moves the state and reports the match; it never
// selects anything in the document itself (guide 7 and 62).
// ---------------------------------------------------------------------------

bool FindBarModel::NavigateNext(uint32_t from) {
    // The owner keeps the current match selected, so the caret normally
    // sits at the match end, where the raw offset already resolves to the
    // following match. When the caret sits at the match start instead (a
    // click right there), the raw offset would resolve back to the current
    // match and Next would look dead; anchor on the match end then.
    TextMatch current;
    if (state_.HasCurrentMatch() && state_.CurrentMatch(&current)) {
        const uint32_t matchEnd = current.start + current.length;
        if (from >= current.start && from < matchEnd) from = matchEnd;
    }
    if (!state_.MoveNext(from)) return false;
    TextMatch match;
    if (!state_.CurrentMatch(&match)) return false;
    // The counter is the bar's own display of the state the bar just
    // moved; the owner only turns the match into a document selection.
    // SyncFromState touches counter and status only, never the edit
    // texts, so this can never retrigger a search.
    SyncFromState();
    if (listener_.onSelectionChanged) {
        listener_.onSelectionChanged(match.start, match.length);
    }
    return true;
}

bool FindBarModel::NavigatePrevious(uint32_t from) {
    // Mirror of Next. With the caret at the current match's end, the
    // "match strictly before the caret" rule resolved to the current match
    // itself, so Previous looked dead on every click. Anchor on the match
    // start whenever the caret is inside the current match or at its end,
    // so Previous always steps to a different match and wraps at the first
    // one (guide 8).
    TextMatch current;
    if (state_.HasCurrentMatch() && state_.CurrentMatch(&current)) {
        const uint32_t matchEnd = current.start + current.length;
        if (from > current.start && from <= matchEnd) from = current.start;
    }
    if (!state_.MovePrevious(from)) return false;
    TextMatch match;
    if (!state_.CurrentMatch(&match)) return false;
    // Same as NavigateNext: the counter must follow the state this button
    // just moved.
    SyncFromState();
    if (listener_.onSelectionChanged) {
        listener_.onSelectionChanged(match.start, match.length);
    }
    return true;
}

bool FindBarModel::SelectMatchAt(uint32_t offset) {
    state_.AdoptMatchAt(offset);
    TextMatch match;
    if (!state_.CurrentMatch(&match)) return false;
    // Adopting moves the current ordinal too, so the counter follows here
    // as well (a replacement continues the count from where it landed).
    SyncFromState();
    if (listener_.onSelectionChanged) {
        listener_.onSelectionChanged(match.start, match.length);
    }
    return true;
}

std::wstring FindBarModel::FormatStatusText() const {
    // The owner's own status text, for example a replacement count, always
    // wins. Otherwise the read-only note is shown while the replace half is
    // on screen and unavailable (guide 4).
    if (!statusText_.empty()) return statusText_;
    if (mode_ == FindBarMode::FindReplace && !replaceEnabled_) {
        return kStrReadOnlyHint;
    }
    return std::wstring();
}
