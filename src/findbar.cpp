#include "findbar.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Modeless Find & Replace bar. See references/find-replace.md.
//
// Design notes
// ------------
// The window is created with CreateWindowExW and the owner's existing
// message loop dispatches it. It is NOT a dialog: DialogBox spins a modal
// loop, and a modal loop is exactly what guide 31 forbids, because the
// document must stay visible AND editable while Find is open.
//
// That choice also rules out an in-memory DLGTEMPLATE, even though the
// dialog template helpers in this codebase are the natural building block
// for a dialog. A control in a template only becomes a real, focusable,
// tabbable, screen reader addressable child window once a dialog manager
// instantiates it. Building the children with CreateWindowExW instead gives
// the same real EDIT, BUTTON and STATIC controls, so tab order, focus,
// Enter, Shift+Enter, Esc, mouse clicks and accessible names all come from
// the platform rather than from hand rolled emulation.
//
// Because there is no dialog manager, the two dialog conveniences a real
// dialog would have given us for free are implemented explicitly here:
//   * Tab navigation: HandleKeyDown moves focus with MoveTabFocus over the
//     WS_TABSTOP children in creation order (guide 45).
//   * Enter as the default action: Enter is Next, Shift+Enter is Previous,
//     and Enter on a focused button activates it (guide 2.3).
//
// Nothing in this file reads or writes the document buffer, the undo stack
// or the document selection, and no document offset is ever stored here.
// Every change is requested through FindBarListener (guide 62 and 64).
// Searching therefore works identically in the read-only rendered view and
// in the raw source view, because the owner supplies the same document text
// and the same scope filter in both (guide 4).

namespace {

// ---------------------------------------------------------------------------
// Localization table (guide 46)
// ---------------------------------------------------------------------------
// Every user-visible string in this component lives here, and this is the
// only place any of them is defined. Everything else refers to them through
// S() below, which is the single read site for the table, so a translator
// edits one block and nothing else.
enum StringId {
    kStrFind,
    kStrFindAndReplace,
    kStrFindLabel,
    kStrReplaceLabel,
    kStrPrevious,
    kStrNext,
    kStrReplace,
    kStrReplaceAll,
    kStrClose,
    kStrMatchCase,
    kStrWholeWord,
    kStrNoMatches,
    kStrReadOnlyHint,
    kStrCount
};

const wchar_t* const kStrings[kStrCount] = {
    L"Find",                       // kStrFind
    L"Find and Replace",           // kStrFindAndReplace
    L"Find:",                      // kStrFindLabel
    L"Replace with:",              // kStrReplaceLabel
    L"Previous",                   // kStrPrevious
    L"Next",                       // kStrNext
    L"Replace",                    // kStrReplace
    L"Replace All",                // kStrReplaceAll
    L"Close",                      // kStrClose
    L"Match case",                 // kStrMatchCase
    L"Whole word",                 // kStrWholeWord
    L"No matches",                 // kStrNoMatches
    L"Replacing is not available in this view.",  // kStrReadOnlyHint
    // No sentinel: kStrCount is exactly the number of strings above.
};

const wchar_t* S(StringId id) {
    const int index = static_cast<int>(id);
    if (index < 0 || index >= static_cast<int>(kStrCount)) return L"";
    if (!kStrings[index]) return L"";
    return kStrings[index];
}

// Window class names are identifiers, not user-visible text, so they are
// deliberately outside the localization table.
const wchar_t* const kWindowClass = L"MarkDownItFindBar";

// The "3 of 17" separator is user-visible, so it sits with the other
// strings a translator has to reach.
const wchar_t* const kCounterSeparator = L" of ";

// Logical layout, in 96 DPI pixels. Two modes: the compact Find bar and the
// expanded Find and Replace bar (guide 26 and 27).
const int kWidth = 420;
const int kHeightFind = 80;
const int kHeightReplace = 118;

// UTF-8 conversion. The state speaks UTF-8 bytes because that is the
// document encoding, while the controls speak UTF-16.
std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
        nullptr);
    if (needed <= 0) {
        needed = WideCharToMultiByte(CP_UTF8, 0, text.data(),
            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    }
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

UINT DpiOf(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 && hwnd) {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        auto fn = reinterpret_cast<PFN_GetDpiForWindow>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (fn) {
            const UINT dpi = fn(hwnd);
            if (dpi > 0) return dpi;
        }
    }
    return 96;
}

}  // namespace

bool FindBar::RegisterFindBarClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = &FindBar::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (RegisterClassW(&wc) == 0) return false;
    registered = true;
    return true;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

FindBar::FindBar() {
    // Both options default OFF (guide 10). Whole word is a request to the
    // search engine, never a filter applied here (guide 11).
    FindStateOptions options;
    options.search.caseSensitive = false;
    options.search.wholeWord = false;
    state_.SetOptions(options);
}

FindBar::~FindBar() {
    Destroy();
}

// ---------------------------------------------------------------------------
// Window and controls
// ---------------------------------------------------------------------------

bool FindBar::Show(HWND owner, bool expandForReplace) {
    if (!owner || !IsWindow(owner)) return false;
    // The owner is needed BEFORE creation: it is the parent of the popup and
    // the source of the DPI the bar is laid out in.
    owner_ = owner;
    if (!EnsureCreated()) return false;

    SetExpanded(expandForReplace);
    // A reused window may have been left with stale field text by an edit
    // that happened while the bar was hidden, so the controls are re-synced
    // from the state every time the bar is shown.
    SyncControlsFromState();
    UpdateControls();
    Reposition();

    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);


    // Guide 28: focus moves to the Find field and the text is selected, so
    // typing replaces it immediately.
    SetActiveWindow(hwnd_);
    FocusAndSelectSearchText();
    return true;
}

void FindBar::Hide() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    ShowWindow(hwnd_, SW_HIDE);
    // Guide 28: focus returns to the document. The document selection is
    // untouched, so a match the user navigated to is still there (guide 2.4).
    HWND focus = GetFocus();
    if ((focus == hwnd_ || IsChild(hwnd_, focus)) && owner_ &&
        IsWindow(owner_)) {
        SetFocus(owner_);
    }
}

void FindBar::Close() {
    const bool wasVisible = IsVisible();
    Hide();
    if (wasVisible && listener_.onClose) listener_.onClose();
}

bool FindBar::IsVisible() const {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

void FindBar::Destroy() {
    if (hwnd_ && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
    ResetHandles();
}

void FindBar::Reposition() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    Reposition(DpiOf(owner_ ? owner_ : hwnd_));
}

void FindBar::FocusAndSelectSearchText() {
    HWND edit = Control(kFindBarFindEdit);
    if (!edit || !IsWindowVisible(hwnd_)) return;
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);
}

// ---------------------------------------------------------------------------
// Text and options
// ---------------------------------------------------------------------------

void FindBar::SetSearchText(const std::wstring& text) {
    searchText_ = text;
    state_.SetSearchText(WideToUtf8(text));
    HWND edit = Control(kFindBarFindEdit);
    if (edit) {
        applying_ = true;
        SetWindowTextW(edit, text.c_str());
        applying_ = false;
    }
    // The query changed, so the old counts describe a different search. Clear
    // them instead of leaving a wrong number on screen (guide 53); the owner
    // reports the real ones from onSearchChanged.
    countCurrent_ = 0;
    countTotal_ = 0;
    status_ = FindStatus::Empty;
    UpdateControls();
}

std::wstring FindBar::GetSearchText() const {
    if (!IsVisible()) return searchText_;
    return ReadControlText(kFindBarFindEdit);
}

void FindBar::SetReplaceText(const std::wstring& text) {
    replaceText_ = text;
    // Which ranges match cannot depend on the replacement text, so the state
    // takes this without invalidating its matches.
    state_.SetReplaceText(WideToUtf8(text));
    HWND edit = Control(kFindBarReplaceEdit);
    if (edit) {
        applying_ = true;
        SetWindowTextW(edit, text.c_str());
        applying_ = false;
    }
}

std::wstring FindBar::GetReplaceText() const {
    if (!IsVisible()) return replaceText_;
    return ReadControlText(kFindBarReplaceEdit);
}

void FindBar::SetCaseSensitive(bool enabled) {
    HWND check = Control(kFindBarMatchCase);
    if (check) {
        applying_ = true;
        SendMessageW(check, BM_SETCHECK,
                     enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        applying_ = false;
    }
    FindStateOptions options = state_.Options();
    options.search.caseSensitive = enabled;
    // Options change which ranges match, so the stored offsets are stale
    // until the owner refreshes (guide 31).
    state_.SetOptions(options);
    countCurrent_ = 0;
    countTotal_ = 0;
    status_ = FindStatus::Empty;
    UpdateControls();
    if (listener_.onSearchChanged) listener_.onSearchChanged();
}

bool FindBar::GetCaseSensitive() const {
    return state_.Options().search.caseSensitive;
}

void FindBar::SetWholeWord(bool enabled) {
    HWND check = Control(kFindBarWholeWord);
    if (check) {
        applying_ = true;
        SendMessageW(check, BM_SETCHECK,
                     enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        applying_ = false;
    }
    FindStateOptions options = state_.Options();
    options.search.wholeWord = enabled;
    state_.SetOptions(options);
    countCurrent_ = 0;
    countTotal_ = 0;
    status_ = FindStatus::Empty;
    UpdateControls();
    if (listener_.onSearchChanged) listener_.onSearchChanged();
}

bool FindBar::GetWholeWord() const {
    return state_.Options().search.wholeWord;
}

void FindBar::SetMatchCount(size_t currentOrdinal, size_t total,
                            FindStatus status) {
    countCurrent_ = currentOrdinal;
    countTotal_ = total;
    status_ = status;
    UpdateControls();
}

void FindBar::SyncFromState() {
    SetMatchCount(state_.CurrentOrdinal(), state_.MatchCount(),
                  state_.Status());
}

void FindBar::SetReplaceEnabled(bool enabled) {
    replaceEnabled_ = enabled;
    // Guide 4 asks the UI to make the state visible rather than silently
    // ignoring a click, so the status line explains it. The owner's own
    // status text, for example a replacement count, always wins.
    SetControlText(kFindBarStatus, FormatStatusText());
    UpdateControls();
}

void FindBar::SetStatusText(const std::wstring& text) {
    statusText_ = text;
    SetControlText(kFindBarStatus, FormatStatusText());
}

std::wstring FindBar::GetStatusText() const {
    return statusText_;
}

// ---------------------------------------------------------------------------
// Navigation. The bar moves the state and reports the match; it never
// selects anything in the document itself (guide 7 and 62).
// ---------------------------------------------------------------------------

bool FindBar::NavigateNext(uint32_t from) {
    if (!state_.MoveNext(from)) return false;
    TextMatch match;
    if (!state_.CurrentMatch(&match)) return false;
    if (listener_.onSelectionChanged) {
        listener_.onSelectionChanged(match.start, match.length);
    }
    return true;
}

bool FindBar::NavigatePrevious(uint32_t from) {
    if (!state_.MovePrevious(from)) return false;
    TextMatch match;
    if (!state_.CurrentMatch(&match)) return false;
    if (listener_.onSelectionChanged) {
        listener_.onSelectionChanged(match.start, match.length);
    }
    return true;
}

bool FindBar::SelectMatchAt(uint32_t offset) {
    state_.AdoptMatchAt(offset);
    TextMatch match;
    if (!state_.CurrentMatch(&match)) return false;
    if (listener_.onSelectionChanged) {
        listener_.onSelectionChanged(match.start, match.length);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Commands from the controls
// ---------------------------------------------------------------------------

bool FindBar::HandleCommand(WPARAM wp, LPARAM lp) {
    (void)lp;
    const int id = LOWORD(wp);
    const int code = HIWORD(wp);
    switch (id) {
        case kFindBarFindEdit:
            if (code == EN_CHANGE) OnFindTextChanged();
            return true;
        case kFindBarReplaceEdit:
            if (code == EN_CHANGE) OnReplaceTextChanged();
            return true;
        case kFindBarMatchCase:
        case kFindBarWholeWord:
            if (code == BN_CLICKED) OnOptionToggled();
            return true;
        case kFindBarPrevious:
            if (code == BN_CLICKED) NavigatePrevious(AskCaretOffset());
            return true;
        case kFindBarNext:
            if (code == BN_CLICKED) NavigateNext(AskCaretOffset());
            return true;
        case kFindBarReplace:
            if (code == BN_CLICKED) RequestReplaceCurrent();
            return true;
        case kFindBarReplaceAll:
            if (code == BN_CLICKED) RequestReplaceAll();
            return true;
        case kFindBarClose:
            if (code == BN_CLICKED) Close();
            return true;
        default:
            break;
    }
    return false;
}

void FindBar::OnFindTextChanged() {
    if (applying_) return;
    const std::wstring text = ReadControlText(kFindBarFindEdit);
    searchText_ = text;
    const std::string query = WideToUtf8(text);
    // Typing the same text back, for example through undo in the edit, must
    // not start a new search.
    if (query == state_.SearchText()) return;
    state_.SetSearchText(query);
    // Guide 5: search is incremental, no Enter needed, and guide 53: show
    // nothing rather than a count that belongs to the previous query.
    countCurrent_ = 0;
    countTotal_ = 0;
    status_ = FindStatus::Empty;
    UpdateControls();
    if (listener_.onSearchChanged) listener_.onSearchChanged();
}

void FindBar::OnReplaceTextChanged() {
    if (applying_) return;
    replaceText_ = ReadControlText(kFindBarReplaceEdit);
    // The replacement never changes which ranges match, so no re-search and
    // no counter reset here.
    state_.SetReplaceText(WideToUtf8(replaceText_));
}

void FindBar::OnOptionToggled() {
    if (applying_) return;
    FindStateOptions options = state_.Options();
    options.search.caseSensitive =
        SendDlgItemMessageW(hwnd_, kFindBarMatchCase, BM_GETCHECK, 0, 0) ==
        BST_CHECKED;
    options.search.wholeWord =
        SendDlgItemMessageW(hwnd_, kFindBarWholeWord, BM_GETCHECK, 0, 0) ==
        BST_CHECKED;
    state_.SetOptions(options);
    countCurrent_ = 0;
    countTotal_ = 0;
    status_ = FindStatus::Empty;
    UpdateControls();
    if (listener_.onSearchChanged) listener_.onSearchChanged();
}

void FindBar::RequestReplaceCurrent() {
    // The gate lives here, not only in the enabled state of the buttons, so
    // that no caller, click or key can mutate a document the owner considers
    // read-only (guide 4 and 63).
    if (!replaceEnabled_) return;
    if (!state_.HasQuery()) return;
    TextMatch match;
    if (!state_.CurrentMatch(&match)) return;
    if (listener_.onReplaceCurrent) {
        listener_.onReplaceCurrent(match.start, match.length);
    }
}

void FindBar::RequestReplaceAll() {
    if (!replaceEnabled_) return;
    if (!state_.HasQuery()) return;
    if (state_.MatchCount() == 0) return;
    if (listener_.onReplaceAll) {
        listener_.onReplaceAll(GetSearchText(), GetReplaceText(),
                               state_.Options());
    }
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

bool FindBar::HandleKeyDown(HWND child, WPARAM wp) {
    const WPARAM key = wp;
    if (key == VK_TAB) {
        const bool backward = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        MoveTabFocus(child, !backward);
        return true;
    }
    if (key == VK_ESCAPE) {
        // Guide 2.4: closing touches no document state at all, because this
        // component cannot reach the document.
        Close();
        return true;
    }
    if (key == VK_RETURN) {
        const bool backward = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (IsButtonId(GetDlgCtrlID(child))) {
            // Without a dialog manager Enter would not reach a pushbutton,
            // so the button is activated explicitly.
            SendMessageW(child, BM_CLICK, 0, 0);
            return true;
        }
        if (IsEditId(GetDlgCtrlID(child))) {
            // Guide 2.3.
            if (backward) {
                NavigatePrevious(AskCaretOffset());
            } else {
                NavigateNext(AskCaretOffset());
            }
            return true;
        }
        return true;
    }
    return false;
}

void FindBar::MoveTabFocus(HWND from, bool forward) {
    // Walk the child chain in creation order. Creation order is the
    // declaration order in the header enum, which is exactly the order
    // guide 45 asks for: Find, Replace, Previous, Next, Match case,
    // Whole word, Replace, Replace All, Close.
    std::vector<HWND> stops;
    HWND child = GetWindow(hwnd_, GW_HWNDFIRST);
    for (; child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!IsWindowVisible(child) || !IsWindowEnabled(child)) continue;
        const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
        if (style & WS_TABSTOP) stops.push_back(child);
    }
    if (stops.empty()) return;

    size_t index = stops.size();
    for (size_t i = 0; i < stops.size(); ++i) {
        if (stops[i] == from) {
            index = i;
            break;
        }
    }
    if (index == stops.size()) {
        // Focus was not in the list, for example it was in a hidden control.
        // Start at whichever end matches the direction of travel.
        SetFocus(forward ? stops.front() : stops.back());
        return;
    }
    size_t next = forward ? index + 1 : (index + stops.size() - 1);
    next %= stops.size();
    SetFocus(stops[next]);
}

// ---------------------------------------------------------------------------
// Control state
// ---------------------------------------------------------------------------

void FindBar::UpdateControls() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    const bool hasQuery = state_.HasQuery();
    // Guide 20: an empty field is never "match everything". Guide 21: no
    // matches disables navigation and replacement too.
    const bool navigable = hasQuery && status_ == FindStatus::Found &&
                           countTotal_ > 0;
    const bool replaceAvailable = replaceEnabled_ && navigable;

    EnableControl(kFindBarPrevious, navigable);
    EnableControl(kFindBarNext, navigable);
    EnableControl(kFindBarReplace, replaceAvailable);
    EnableControl(kFindBarReplaceAll, replaceAvailable);
    EnableControl(kFindBarMatchCase, true);
    EnableControl(kFindBarWholeWord, true);
    EnableControl(kFindBarClose, true);

    // A real STATIC, so a screen reader announces the count as it changes
    // (guide 44).
    SetControlText(kFindBarCounter, FormatCounter());
}

std::wstring FindBar::FormatCounter() const {
    if (status_ == FindStatus::Empty) return std::wstring();
    if (status_ == FindStatus::NoMatches) return S(kStrNoMatches);
    // Guide 3 and 52: "3 of 17". Built by concatenation rather than through
    // a printf-style format, so the pattern stays in the localization table
    // and MSVC has no non-literal format string to complain about.
    const std::wstring ordinal = std::to_wstring(countCurrent_);
    const std::wstring total = std::to_wstring(countTotal_);
    std::wstring text;
    text.reserve(ordinal.size() + total.size() + 8);
    text += ordinal;
    text += kCounterSeparator;
    text += total;
    return text;
}

std::wstring FindBar::FormatStatusText() const {
    if (!statusText_.empty()) return statusText_;
    // Guide 4: rather than silently ignoring Replace in a read-only view,
    // say why. Only while the replace half is on screen.
    if (expanded_ && !replaceEnabled_) return S(kStrReadOnlyHint);
    return std::wstring();
}

// ---------------------------------------------------------------------------
// Window procedures
// ---------------------------------------------------------------------------

LRESULT CALLBACK FindBar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        // The instance is known before the first message that can reach the
        // window, so no lookup can ever fail.
        //
        // GWLP_USERDATA is the right slot: a pointer-sized value that exists
        // on every window. The dialog-only DWLP_USER is not. On this plain
        // window a store to it fails with ERROR_INVALID_INDEX, silently, and
        // then every lookup returns null, the whole procedure routes to
        // DefWindowProc, and the bar opens as an empty rectangle.
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    FindBar* self = reinterpret_cast<FindBar*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_COMMAND:
            if (self->HandleCommand(wp, lp)) return 0;
            return DefWindowProcW(hwnd, msg, wp, lp);
        case WM_KEYDOWN:
            if (self->HandleKeyDown(hwnd, wp)) return 0;
            return DefWindowProcW(hwnd, msg, wp, lp);
        case WM_GETDLGCODE:
            // Take every key, including Enter and Tab, so the subclass and
            // this procedure both see them.
            return DLGC_WANTALLKEYS;
        case WM_CLOSE:
            self->Close();
            return 0;
        case WM_DESTROY:
            // Children are still alive here: the parent is destroyed before
            // them, so their window procedures can still be restored.
            self->DetachChildren();
            self->hwnd_ = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK FindBar::ChildSubclass(HWND child, UINT msg, WPARAM wp,
                                        LPARAM lp) {
    FindBar* self = reinterpret_cast<FindBar*>(
        GetWindowLongPtrW(child, GWLP_USERDATA));
    WNDPROC next = nullptr;
    // Only a live control of THIS bar has a stored procedure to chain to. A
    // foreign window carrying a stale GWLP_USERDATA falls through to the default
    // procedure instead of indexing the subclass table out of range.
    if (self && self->hwnd_ && GetParent(child) == self->hwnd_) {
        const int id = GetDlgCtrlID(child);
        const int index = id - static_cast<int>(kFindBarFindEdit);
        if (index >= 0 && index < static_cast<int>(kFindBarControlCount)) {
            next = self->subclasses_[index].proc;
        }
    }
    if (!next) return DefWindowProcW(child, msg, wp, lp);

    if (msg == WM_KEYDOWN && self->HandleKeyDown(child, wp)) return 0;
    if (msg == WM_CHAR) {
        // Swallow the character form of Enter and Escape so they produce no
        // beep and no stray insertion.
        if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
    }
    return CallWindowProcW(next, child, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Creation, layout, teardown
// ---------------------------------------------------------------------------

bool FindBar::EnsureCreated() {
    if (hwnd_ && IsWindow(hwnd_)) return true;
    if (!RegisterFindBarClass()) return false;

    dpi_ = static_cast<int>(DpiOf(owner_));
    font_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const int height = MulDiv(expanded_ ? kHeightReplace : kHeightFind,
                              dpi_, 96);
    // An owned WS_POPUP window. It is destroyed with its owner, and it never
    // activates or steals the document's place in the Z-order.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, S(kStrFind),
                            WS_POPUP | WS_BORDER, 0, 0,
                            MulDiv(kWidth, dpi_, 96), height,
                            owner_, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_ || !IsWindow(hwnd_)) {
        if (font_) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        hwnd_ = nullptr;
        return false;
    }
    if (font_) {
        SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(font_),
                     TRUE);
    }
    if (!CreateControls()) {
        Destroy();
        return false;
    }
    return true;
}

bool FindBar::CreateControls() {
    if (!hwnd_) return false;
    HINSTANCE instance = GetModuleHandleW(nullptr);

    struct ChildSpec {
        int id;
        const wchar_t* className;
        DWORD style;
        DWORD exStyle;
        const wchar_t* text;
    };
    // Created in the header's enum order, which is the tab order (guide 45).
    // The first control carries WS_GROUP, so the focus group starts there.
    // WS_EX_CLIENTEDGE is an EXTENDED style, so it is passed as its own
    // argument rather than being mixed into the window style.
    const DWORD childBase = WS_CHILD | WS_VISIBLE;
    const DWORD labelStyle = childBase | SS_LEFT;
    const DWORD editStyle =
        childBase | WS_GROUP | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
    const DWORD buttonStyle = childBase | WS_TABSTOP | BS_PUSHBUTTON;
    const DWORD checkStyle = childBase | WS_TABSTOP | BS_AUTOCHECKBOX;

    const ChildSpec specs[] = {
        { kFindBarFindEdit, L"EDIT", editStyle, WS_EX_CLIENTEDGE, L"" },
        { kFindBarReplaceEdit, L"EDIT", editStyle, WS_EX_CLIENTEDGE, L"" },
        { kFindBarFindLabel, L"STATIC", labelStyle, 0, S(kStrFindLabel) },
        { kFindBarReplaceLabel, L"STATIC", labelStyle, 0,
          S(kStrReplaceLabel) },
        { kFindBarPrevious, L"BUTTON", buttonStyle, 0, S(kStrPrevious) },
        { kFindBarNext, L"BUTTON", buttonStyle, 0, S(kStrNext) },
        { kFindBarMatchCase, L"BUTTON", checkStyle, 0, S(kStrMatchCase) },
        { kFindBarWholeWord, L"BUTTON", checkStyle, 0, S(kStrWholeWord) },
        { kFindBarReplace, L"BUTTON", buttonStyle, 0, S(kStrReplace) },
        { kFindBarReplaceAll, L"BUTTON", buttonStyle, 0, S(kStrReplaceAll) },
        { kFindBarClose, L"BUTTON", buttonStyle, 0, S(kStrClose) },
        { kFindBarCounter, L"STATIC", labelStyle, 0, L"" },
        { kFindBarStatus, L"STATIC", labelStyle, 0, L"" },
    };
    const int specCount = static_cast<int>(sizeof(specs) / sizeof(specs[0]));
    if (specCount != static_cast<int>(kFindBarControlCount)) return false;

    for (int i = 0; i < specCount; ++i) {
        const ChildSpec& spec = specs[i];
        HWND child = CreateWindowExW(
            spec.exStyle, spec.className, spec.text, spec.style, 0, 0, 1, 1,
            hwnd_, reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(spec.id)),
            instance, nullptr);
        // A null or dead child handle would be handed out later by GetDlgItem
        // and walked by MoveTabFocus, so the whole bar is abandoned here
        // instead of being left half built.
        if (!child || !IsWindow(child)) {
            AbandonPartial();
            return false;
        }
        const int index = IndexOfId(spec.id);
        if (index != i) {
            // The header enum order and this table must agree, or the tab
            // order would not be the order written down in guide 45.
            AbandonPartial();
            return false;
        }
        controls_[index] = child;
        // The bar pointer first, so the child procedure can reach it as soon
        // as the subclass below is installed. GWLP_USERDATA for the reason
        // given on the bar's own store in WndProc: a store to the dialog-only
        // DWLP_USER fails silently on a control, the subclass then cannot
        // find the bar, and DefWindowProc owns the control and paints nothing.
        SetWindowLongPtrW(child, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        if (font_) {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_),
                         TRUE);
        }
        // Subclass last, so no message can arrive before the record is
        // complete.
        SubclassRecord& record = subclasses_[index];
        record.proc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(child, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(
                                                   &ChildSubclass)));
        if (!record.proc) {
            AbandonPartial();
            return false;
        }
    }

    LayoutControls();
    SyncControlsFromState();
    UpdateControls();
    return true;
}

void FindBar::SyncControlsFromState() {
    // Text and options may have been set before the window existed, so the
    // controls are filled from the state rather than the other way round.
    applying_ = true;
    const FindStateOptions& options = state_.Options();
    SendMessageW(Control(kFindBarMatchCase), BM_SETCHECK,
                 options.search.caseSensitive ? BST_CHECKED : BST_UNCHECKED,
                 0);
    SendMessageW(Control(kFindBarWholeWord), BM_SETCHECK,
                 options.search.wholeWord ? BST_CHECKED : BST_UNCHECKED, 0);
    SetControlText(kFindBarFindEdit, searchText_);
    SetControlText(kFindBarReplaceEdit, replaceText_);
    SetControlText(kFindBarCounter, FormatCounter());
    SetControlText(kFindBarStatus, FormatStatusText());
    applying_ = false;
}

void FindBar::AbandonPartial() {
    // DetachChildren restores each child's original window procedure and
    // clears the handles, so nothing is left pointing at this bar.
    if (hwnd_ && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
    ResetHandles();
}

void FindBar::SetExpanded(bool expand) {
    expanded_ = expand;
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    SetWindowTextW(hwnd_, S(expand ? kStrFindAndReplace : kStrFind));
    const int showOrHide = expand ? SW_SHOW : SW_HIDE;
    ShowWindow(Control(kFindBarReplaceLabel), showOrHide);
    ShowWindow(Control(kFindBarReplaceEdit), showOrHide);
    ShowWindow(Control(kFindBarReplace), showOrHide);
    ShowWindow(Control(kFindBarReplaceAll), showOrHide);
    LayoutControls();
    // The status line may have just gained or lost the read-only note.
    SetControlText(kFindBarStatus, FormatStatusText());
}

void FindBar::LayoutControls() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    const bool expand = expanded_;

    // Find row: label, field, then the two navigation buttons.
    PlaceChild(kFindBarFindLabel, 8, 8, 40, 14, true);
    PlaceChild(kFindBarFindEdit, 50, 6, 210, 20, true);
    PlaceChild(kFindBarPrevious, 266, 5, 72, 22, true);
    PlaceChild(kFindBarNext, 342, 5, 64, 22, true);

    // Option row, with the counter on its right in the compact bar.
    PlaceChild(kFindBarMatchCase, 8, expand ? 62 : 32, 92, 18, true);
    PlaceChild(kFindBarWholeWord, 106, expand ? 62 : 32, 92, 18, true);

    // Replace row, only when expanded.
    PlaceChild(kFindBarReplaceLabel, 8, 36, 70, 14, expand);
    PlaceChild(kFindBarReplaceEdit, 80, 34, 180, 20, expand);
    PlaceChild(kFindBarReplace, 206, 60, 72, 22, expand);
    PlaceChild(kFindBarReplaceAll, 282, 60, 84, 22, expand);

    // Counter and message line.
    if (expand) {
        PlaceChild(kFindBarCounter, 8, 92, 120, 18, true);
        PlaceChild(kFindBarStatus, 134, 92, 190, 18, true);
        PlaceChild(kFindBarClose, 330, 90, 82, 22, true);
    } else {
        PlaceChild(kFindBarCounter, 206, 33, 118, 18, true);
        PlaceChild(kFindBarStatus, 8, 54, 300, 16, true);
        PlaceChild(kFindBarClose, 330, 50, 82, 22, true);
    }

    const int width = MulDiv(kWidth, dpi_, 96);
    const int height = MulDiv(expand ? kHeightReplace : kHeightFind, dpi_, 96);
    SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FindBar::PlaceChild(int id, int x, int y, int w, int h,
                         bool visible) const {
    const int index = IndexOfId(id);
    HWND child = controls_[index];
    if (!child || !IsWindow(child)) return;
    const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE |
                       (visible ? 0 : SWP_HIDEWINDOW);
    SetWindowPos(child, nullptr, MulDiv(x, dpi_, 96), MulDiv(y, dpi_, 96),
                 MulDiv(w, dpi_, 96), MulDiv(h, dpi_, 96), flags);
}

void FindBar::Reposition(int dpi) {
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    if (dpi <= 0) dpi = 96;
    dpi_ = dpi;
    // Children are laid out too, so a monitor change with a different DPI
    // scales the whole bar rather than only its window.
    LayoutControls();

    // Top right of the owner's window, which is where a floating search bar
    // belongs (guide 26 and 27), then clamped onto the monitor so it can
    // never open off screen or under the taskbar.
    RECT owner = {};
    const HWND reference = owner_ && IsWindow(owner_) ? owner_ : hwnd_;
    if (!GetWindowRect(reference, &owner)) {
        owner.left = 0;
        owner.top = 0;
        owner.right = MulDiv(kWidth, dpi, 96);
        owner.bottom = MulDiv(expanded_ ? kHeightReplace : kHeightFind, dpi,
                              96);
    }
    int x = owner.right - MulDiv(kWidth, dpi, 96) - MulDiv(16, dpi, 96);
    int y = owner.top + MulDiv(16, dpi, 96);

    POINT probe = {x + MulDiv(kWidth, dpi, 96) / 2,
                   y + MulDiv(expanded_ ? kHeightReplace : kHeightFind, dpi,
                              96) / 2};
    HMONITOR monitor = MonitorFromPoint(probe, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        if (x + MulDiv(kWidth, dpi, 96) > info.rcWork.right) {
            x = info.rcWork.right - MulDiv(kWidth, dpi, 96);
        }
        if (y + MulDiv(expanded_ ? kHeightReplace : kHeightFind, dpi, 96) >
            info.rcWork.bottom) {
            y = info.rcWork.bottom -
                MulDiv(expanded_ ? kHeightReplace : kHeightFind, dpi, 96);
        }
        if (x < info.rcWork.left) x = info.rcWork.left;
        if (y < info.rcWork.top) y = info.rcWork.top;
    }

    // No SWP_SHOWWINDOW here: Reposition must never resurrect a bar the owner
    // has closed. Show is what puts it on screen.
    SetWindowPos(hwnd_, HWND_TOP, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
}

void FindBar::DetachChildren() {
    for (int i = 0; i < kFindBarControlCount; ++i) {
        SubclassRecord& record = subclasses_[i];
        HWND child = controls_[i];
        if (child && IsWindow(child) && record.proc) {
            SetWindowLongPtrW(child, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(record.proc));
        }
        record.proc = nullptr;
        controls_[i] = nullptr;
    }
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
}

void FindBar::ResetHandles() {
    hwnd_ = nullptr;
    owner_ = nullptr;
    DetachChildren();
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

int FindBar::IndexOfId(int id) {
    const int index = id - static_cast<int>(kFindBarFindEdit);
    if (index < 0 || index >= static_cast<int>(kFindBarControlCount)) {
        return 0;
    }
    return index;
}

bool FindBar::IsEditId(int id) {
    return id == kFindBarFindEdit || id == kFindBarReplaceEdit;
}

bool FindBar::IsButtonId(int id) {
    switch (id) {
        case kFindBarPrevious:
        case kFindBarNext:
        case kFindBarReplace:
        case kFindBarReplaceAll:
        case kFindBarClose:
        case kFindBarMatchCase:
        case kFindBarWholeWord:
            return true;
        default:
            break;
    }
    return false;
}

HWND FindBar::Control(int id) const {
    return controls_[IndexOfId(id)];
}

void FindBar::EnableControl(int id, bool enabled) {
    HWND child = Control(id);
    if (!child || !IsWindow(child)) return;
    EnableWindow(child, enabled ? TRUE : FALSE);
}

void FindBar::SetControlText(int id, const std::wstring& text) {
    HWND child = Control(id);
    if (!child || !IsWindow(child)) return;
    SetWindowTextW(child, text.c_str());
}

std::wstring FindBar::ReadControlText(int id) const {
    HWND child = Control(id);
    if (!child || !IsWindow(child)) return std::wstring();
    const int length = GetWindowTextLengthW(child);
    if (length <= 0) return std::wstring();
    std::wstring text;
    text.resize(static_cast<size_t>(length) + 1);
    const int copied = GetWindowTextW(child, &text[0], length + 1);
    if (copied <= 0) return std::wstring();
    text.resize(static_cast<size_t>(copied));
    return text;
}

uint32_t FindBar::AskCaretOffset() const {
    if (!listener_.caretOffset) return 0;
    return listener_.caretOffset();
}

void FindBar::SetListener(FindBarListener listener) {
    listener_ = std::move(listener);
}